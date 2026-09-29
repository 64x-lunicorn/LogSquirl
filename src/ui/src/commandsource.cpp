/*
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * This file is part of LogSquirl.
 *
 * LogSquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LogSquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "commandsource.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <mutex>
#include <utility>

#include "instancehandover.h"
#include "log.h"
#include "stdinpump.h"
#include "streamwriter.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <vector>
#endif

namespace {

// How long a shell may take to start before it counts as not started.
constexpr int StartTimeoutMs = 10'000;
// The length a command's tab title is elided to.
constexpr qsizetype TitleLength = 40;

#ifndef Q_OS_WIN

// Stops the process groups of commands: SIGTERM at once, SIGKILL after the
// grace period to whatever of the group is still there. The wait for it runs
// on a thread of its own, so that stopping never blocks the thread that asks
// for it. A group is forgotten as soon as it is gone: its id may be taken by
// a new group after that, which must never get a signal meant for this one.
// At exit, every group still waiting gets SIGKILL at once.
class GroupReaper {
public:
    static constexpr std::chrono::milliseconds GracePeriod{ 2000 };
    static constexpr std::chrono::milliseconds PollInterval{ 20 };

    static GroupReaper& instance()
    {
        static GroupReaper reaper;
        return reaper;
    }

    GroupReaper( const GroupReaper& ) = delete;
    GroupReaper& operator=( const GroupReaper& ) = delete;

    void stop( pid_t group )
    {
        if ( group <= 0 || ::kill( -group, SIGTERM ) != 0 ) {
            // Gone already.
            return;
        }

        const std::scoped_lock lock( mutex_ );
        pending_.push_back( { group, std::chrono::steady_clock::now() + GracePeriod } );
        if ( !worker_.joinable() ) {
            worker_ = std::thread( [ this ] { run(); } );
        }
        wakeUp_.notify_one();
    }

private:
    struct Pending {
        pid_t group;
        std::chrono::steady_clock::time_point killAt;
    };

    GroupReaper() = default;

    ~GroupReaper()
    {
        {
            const std::scoped_lock lock( mutex_ );
            exiting_ = true;
        }
        wakeUp_.notify_one();
        if ( worker_.joinable() ) {
            worker_.join();
        }
    }

    void run()
    {
        std::unique_lock lock( mutex_ );
        while ( true ) {
            wakeUp_.wait( lock, [ this ] { return exiting_ || !pending_.empty(); } );

            const auto now = std::chrono::steady_clock::now();
            std::erase_if( pending_, [ this, now ]( const Pending& pending ) {
                if ( ::kill( -pending.group, 0 ) != 0 && errno == ESRCH ) {
                    return true;
                }
                if ( exiting_ || now >= pending.killAt ) {
                    ::kill( -pending.group, SIGKILL );
                    return true;
                }
                return false;
            } );

            if ( exiting_ ) {
                return;
            }
            if ( !pending_.empty() ) {
                wakeUp_.wait_for( lock, PollInterval, [ this ] { return exiting_; } );
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wakeUp_;
    std::vector<Pending> pending_;
    bool exiting_ = false;
    std::thread worker_;
};

#endif

#ifdef Q_OS_WIN

// Ends every process of the job, and the job with them.
void closeJob( void*& job )
{
    if ( job != nullptr ) {
        TerminateJobObject( static_cast<HANDLE>( job ), 1 );
        CloseHandle( static_cast<HANDLE>( job ) );
        job = nullptr;
    }
}

#endif

// Removes a spool file with its folder, when that is left empty; whether the
// file is gone.
bool removeSpoolFile( const QString& path )
{
    if ( !QFile::remove( path ) && QFile::exists( path ) ) {
        return false;
    }
    // Only removed when empty.
    QDir().rmdir( QFileInfo( path ).absolutePath() );
    return true;
}

// The spool files that could not be removed with their Command Source: on
// Windows, one the secondary instance that writes it still has open (#623),
// or one a tab still reads when the window goes before it. They are removed
// again when the application exits.
class LeftoverSpoolFiles {
public:
    static LeftoverSpoolFiles& instance()
    {
        static LeftoverSpoolFiles leftovers;
        return leftovers;
    }

    LeftoverSpoolFiles( const LeftoverSpoolFiles& ) = delete;
    LeftoverSpoolFiles& operator=( const LeftoverSpoolFiles& ) = delete;

    void add( const QString& path )
    {
        const std::scoped_lock lock( mutex_ );
        paths_.append( path );
    }

    void removeAll()
    {
        const std::scoped_lock lock( mutex_ );
        paths_.removeIf( []( const QString& path ) { return removeSpoolFile( path ); } );
    }

private:
    LeftoverSpoolFiles() = default;

    ~LeftoverSpoolFiles()
    {
        removeAll();
    }

    std::mutex mutex_;
    QStringList paths_;
};

// The shell the user runs commands in, as the environment names it.
QString userShell()
{
#ifdef Q_OS_WIN
    return qEnvironmentVariable( "ComSpec" );
#else
    return qEnvironmentVariable( "SHELL" );
#endif
}

} // namespace

bool CommandEnd::commandNotFound() const
{
#ifdef Q_OS_WIN
    constexpr int NotFound = 9009;
#else
    constexpr int NotFound = 127;
#endif
    return kind == Kind::Exited && exitCode == NotFound;
}

ShellInvocation shellInvocation( const QString& commandLine, const QString& shell )
{
    ShellInvocation invocation;
#ifdef Q_OS_WIN
    invocation.program = shell.isEmpty() ? QStringLiteral( "cmd.exe" ) : shell;
    // /s keeps the command line as it is between the outer quotes.
    invocation.nativeArguments = QStringLiteral( "/d /s /c \"%1\"" ).arg( commandLine );
#else
    invocation.program = shell.isEmpty() ? QStringLiteral( "/bin/sh" ) : shell;
    invocation.arguments = { QStringLiteral( "-l" ), QStringLiteral( "-c" ), commandLine };
#endif
    return invocation;
}

QString commandTabTitle( const QString& commandLine )
{
    auto simplified = commandLine.simplified();
    if ( simplified.size() <= TitleLength ) {
        return simplified;
    }
    // One character for the ellipsis; the start of a command line tells a
    // little more than its end.
    const auto kept = TitleLength - 1;
    const auto head = kept - kept / 2;
    const auto tail = kept / 2;
    return simplified.left( head ) + QChar( 0x2026 ) + simplified.right( tail );
}

CommandSource::CommandSource( Kind kind )
    : kind_( kind )
{
}

std::unique_ptr<CommandSource> CommandSource::startCommand( const RecentCommand& command,
                                                            QString* error )
{
    std::unique_ptr<CommandSource> source( new CommandSource( Kind::Command ) );
    source->command_ = command;
    if ( source->command_.workingFolder.isEmpty() ) {
        source->command_.workingFolder = QDir::homePath();
    }

    const QFileInfo folder( source->command_.workingFolder );
    if ( !folder.isDir() ) {
        *error = tr( "The working folder %1 does not exist." )
                     .arg( QDir::toNativeSeparators( source->command_.workingFolder ) );
        return nullptr;
    }

    source->writer_ = std::make_unique<logsquirl::plugins::StreamWriter>(
        commandTabTitle( command.commandLine ) );
    source->spoolPath_ = source->writer_->filePath();
    if ( source->spoolPath_.isEmpty() ) {
        *error = tr( "Could not create a file for the output of the command." );
        return nullptr;
    }

    if ( const auto startError = source->startProcess(); !startError.isEmpty() ) {
        *error = startError;
        return nullptr;
    }
    return source;
}

std::unique_ptr<CommandSource> CommandSource::readStandardInput( int fd, QString* error )
{
    std::unique_ptr<CommandSource> source( new CommandSource( Kind::StandardInput ) );
    source->writer_ = std::make_unique<logsquirl::plugins::StreamWriter>( "stdin" );
    source->spoolPath_ = source->writer_->filePath();
    if ( source->spoolPath_.isEmpty() ) {
        *error = tr( "Could not create a file for the data read from standard input." );
        return nullptr;
    }

    // The pump calls back on its own thread, and is stopped before this
    // object goes: what it posts here is dropped with this object.
    auto* self = source.get();
    source->pump_
        = std::make_unique<logsquirl::plugins::StdinPump>( fd, *source->writer_, [ self ] {
              QMetaObject::invokeMethod( self, [ self ] {
                  self->finish( CommandEnd{ CommandEnd::Kind::InputClosed, 0 } );
              } );
          } );
    return source;
}

std::unique_ptr<CommandSource> CommandSource::adoptSpoolFile( const QString& path )
{
    std::unique_ptr<CommandSource> source( new CommandSource( Kind::SpoolFile ) );
    source->spoolPath_ = path;
    // The process that writes it goes on for as long as this is there.
    if ( !markSpoolAdopted( path ) ) {
        LOG_WARNING << "Could not mark the spool file " << path << " as taken over";
    }
    return source;
}

CommandSource::~CommandSource()
{
    stop();

    // The spool file goes with this object. The writer removes the one it
    // wrote.
    writer_.reset();
    if ( spoolPath_.isEmpty() ) {
        return;
    }
    if ( kind_ == Kind::SpoolFile ) {
        // First, so that the process that writes the file stops, and lets go
        // of it on Windows.
        QFile::remove( spoolAdoptionMarker( spoolPath_ ) );
    }
    // On Windows a file the tab still has open stays -- the window may go
    // before its tabs -- and so does one another process writes.
    if ( !removeSpoolFile( spoolPath_ ) ) {
        LOG_INFO << "Could not remove the spool file " << spoolPath_ << " yet, will at exit";
        LeftoverSpoolFiles::instance().add( spoolPath_ );
    }
}

void CommandSource::removeLeftoverSpoolFiles()
{
    LeftoverSpoolFiles::instance().removeAll();
}

QString CommandSource::startProcess()
{
    const auto invocation = shellInvocation( command_.commandLine, userShell() );

    process_ = new QProcess( this );
    process_->setProgram( invocation.program );
    process_->setArguments( invocation.arguments );
#ifdef Q_OS_WIN
    process_->setNativeArguments( invocation.nativeArguments );
#endif
    process_->setWorkingDirectory( command_.workingFolder );
    // A command that reads its standard input finds it closed rather than
    // waiting for the user.
    process_->setStandardInputFile( QProcess::nullDevice() );
    if ( command_.includeStandardError ) {
        process_->setProcessChannelMode( QProcess::MergedChannels );
    }
    else {
        process_->setStandardErrorFile( QProcess::nullDevice() );
    }

#ifdef Q_OS_WIN
    // Every process the command starts is in its Job Object, which kills all
    // of them when it is closed. The shell starts suspended and is put in the
    // job before it runs, so that none of its children is started outside it.
    job_ = CreateJobObjectW( nullptr, nullptr );
    if ( job_ != nullptr ) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if ( !SetInformationJobObject( static_cast<HANDLE>( job_ ),
                                       JobObjectExtendedLimitInformation, &limits,
                                       sizeof( limits ) ) ) {
            LOG_WARNING << "Could not set up the job of a command: " << GetLastError();
        }
    }
    else {
        LOG_WARNING << "Could not create a job for a command: " << GetLastError();
    }
    process_->setCreateProcessArgumentsModifier(
        [ this ]( QProcess::CreateProcessArguments* arguments ) {
            arguments->flags |= CREATE_SUSPENDED;
            creatingProcess_ = arguments->processInformation;
        } );
#else
    // Its own session, and with it its own process group, which is stopped as
    // a whole.
    process_->setChildProcessModifier( [] { ::setsid(); } );
#endif

    connect( process_, &QProcess::readyReadStandardOutput, this, &CommandSource::readOutput );
    connect( process_, &QProcess::finished, this,
             [ this ]( int exitCode, QProcess::ExitStatus status ) {
                 readOutput();
        // What the shell left running ends with it.
#ifdef Q_OS_WIN
                 closeJob( job_ );
#else
                 GroupReaper::instance().stop( static_cast<pid_t>( processGroup_ ) );
#endif
                 processGroup_ = 0;
                 finish( status == QProcess::CrashExit
                             ? CommandEnd{ CommandEnd::Kind::Stopped, exitCode }
                             : CommandEnd{ CommandEnd::Kind::Exited, exitCode } );
             } );

    process_->start();

#ifdef Q_OS_WIN
    // start() has created the process, suspended, by the time it returns.
    if ( creatingProcess_ != nullptr ) {
        auto* created = static_cast<PROCESS_INFORMATION*>( creatingProcess_ );
        creatingProcess_ = nullptr;
        if ( process_->state() != QProcess::NotRunning ) {
            if ( job_ == nullptr
                 || !AssignProcessToJobObject( static_cast<HANDLE>( job_ ), created->hProcess ) ) {
                LOG_WARNING << "Could not put a command in its job: " << GetLastError();
            }
            ResumeThread( created->hThread );
        }
    }
#endif

    if ( !process_->waitForStarted( StartTimeoutMs ) ) {
        const auto why = process_->errorString();
        disconnect( process_, nullptr, this, nullptr );
        delete process_;
        process_ = nullptr;
        return tr( "The shell %1 could not be started: %2" )
            .arg( QDir::toNativeSeparators( invocation.program ), why );
    }
    processGroup_ = process_->processId();
    LOG_INFO << "Command started, process " << processGroup_ << ": " << command_.commandLine;
    return {};
}

void CommandSource::readOutput()
{
    if ( process_ == nullptr || writer_ == nullptr ) {
        return;
    }
    const auto output = process_->readAllStandardOutput();
    if ( !output.isEmpty() ) {
        writer_->pushBytes( output.constData(), static_cast<size_t>( output.size() ) );
    }
}

void CommandSource::finish( const CommandEnd& end )
{
    if ( ended_ ) {
        return;
    }
    ended_ = true;
    if ( writer_ ) {
        writer_->signalEos();
    }
    Q_EMIT ended( end );
}

void CommandSource::stop()
{
    pump_.reset();
    stopProcess();
    if ( writer_ && !writer_->isFinished() ) {
        writer_->signalEos();
    }
}

void CommandSource::stopProcess()
{
    if ( process_ != nullptr ) {
        disconnect( process_, nullptr, this, nullptr );
    }

#ifdef Q_OS_WIN
    closeJob( job_ );
#else
    GroupReaper::instance().stop( static_cast<pid_t>( processGroup_ ) );
#endif
    processGroup_ = 0;

    if ( process_ == nullptr ) {
        return;
    }

    auto* process = std::exchange( process_, nullptr );
    if ( process->state() == QProcess::NotRunning ) {
        delete process;
        return;
    }

#ifdef Q_OS_WIN
    // Closing the job ended the shell already, unless there was no job.
    process->kill();
#endif
    // The shell has its grace period like the rest of its group: it may be
    // running a trap for SIGTERM. Deleting its QProcess now would kill it and
    // wait for it on this thread, so the QProcess is let go instead: it goes
    // once it has seen the shell end -- with SIGTERM, or with the SIGKILL the
    // Group Reaper sends to the group 2 s later -- and with the application
    // otherwise, whose destruction kills the shell first.
    process->setParent( nullptr );
    if ( auto* application = QCoreApplication::instance();
         application != nullptr && application->thread() == process->thread() ) {
        process->setParent( application );
    }
    connect( process, &QProcess::finished, process, &QObject::deleteLater );
}

QString CommandSource::endedTitle( const QString& title, const CommandEnd& end )
{
    switch ( end.kind ) {
    case CommandEnd::Kind::Exited:
        // At once, so that a title with a %1 in it is kept as it is.
        return tr( "%1 [exit %2]" ).arg( title, QString::number( end.exitCode ) );
    case CommandEnd::Kind::Stopped:
        return tr( "%1 [stopped]" ).arg( title );
    case CommandEnd::Kind::InputClosed:
        break;
    }
    return title;
}

QString CommandSource::endedToolTip( const CommandEnd& end )
{
    switch ( end.kind ) {
    case CommandEnd::Kind::Exited:
        return end.commandNotFound()
                   ? tr( "Ended with exit code %1: command not found" ).arg( end.exitCode )
                   : tr( "Ended with exit code %1" ).arg( end.exitCode );
    case CommandEnd::Kind::Stopped:
        return tr( "Was stopped" );
    case CommandEnd::Kind::InputClosed:
        break;
    }
    return {};
}

QString CommandSource::endedMessage( const QString& title, const CommandEnd& end )
{
    switch ( end.kind ) {
    case CommandEnd::Kind::Exited:
        return end.commandNotFound() ? tr( "\"%1\" ended with exit code %2: command not found" )
                                           .arg( title, QString::number( end.exitCode ) )
                                     : tr( "\"%1\" ended with exit code %2" )
                                           .arg( title, QString::number( end.exitCode ) );
    case CommandEnd::Kind::Stopped:
        return tr( "\"%1\" was stopped" ).arg( title );
    case CommandEnd::Kind::InputClosed:
        break;
    }
    return {};
}
