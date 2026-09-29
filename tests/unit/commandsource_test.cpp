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

// A Command Source runs a command line through the user's shell and writes its
// output to the spool file of a tab, or owns a spool file somebody else
// writes; destroying it stops the command with every process it started and
// removes the file (#575).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "commandsource.h"
#include "test_utils.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <optional>

#ifndef Q_OS_WIN
#include "command_process_probe.h"
#endif

namespace {

QByteArray contentOf( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return {};
    }
    return file.readAll();
}

#ifndef Q_OS_WIN

// Runs `commandLine` and waits for it to end: what it wrote and how it ended.
struct Run {
    std::unique_ptr<CommandSource> source;
    std::optional<CommandEnd> end;
};

Run runToEnd( const QString& commandLine, bool includeStandardError = true )
{
    const ShellForTests shell;
    Run run;
    QString error;
    run.source = CommandSource::startCommand(
        RecentCommand{ commandLine, QDir::tempPath(), includeStandardError }, &error );
    INFO( error.toStdString() );
    REQUIRE( run.source != nullptr );
    QObject::connect( run.source.get(), &CommandSource::ended,
                      [ &run ]( const CommandEnd& end ) { run.end = end; } );
    REQUIRE( waitUiState( [ &run ] { return run.end.has_value(); }, 10'000 ) );
    return run;
}

#endif

} // namespace

TEST_CASE( "A command line runs through the user's shell", "[commandsource]" )
{
#ifdef Q_OS_WIN
    const auto invocation = shellInvocation( "ping -t localhost", "C:\\Windows\\cmd.exe" );
    CHECK( invocation.program == "C:\\Windows\\cmd.exe" );
    CHECK( invocation.arguments.isEmpty() );
    CHECK( invocation.nativeArguments == "/d /s /c \"ping -t localhost\"" );
    CHECK( shellInvocation( "dir", {} ).program == "cmd.exe" );
#else
    // A login shell, so that PATH is the user's also when the application
    // was started from the Finder or the Dock.
    const auto invocation = shellInvocation( "kubectl logs -f api | grep ERROR", "/bin/zsh" );
    CHECK( invocation.program == "/bin/zsh" );
    CHECK( invocation.arguments == QStringList{ "-l", "-c", "kubectl logs -f api | grep ERROR" } );
    CHECK( shellInvocation( "true", {} ).program == "/bin/sh" );
#endif
}

TEST_CASE( "A command's tab is titled by its command line, elided in the middle",
           "[commandsource]" )
{
    CHECK( commandTabTitle( "journalctl -f" ) == "journalctl -f" );

    const QString longLine = "ssh build-server.example.com tail -f /var/log/nginx/access.log";
    const auto title = commandTabTitle( longLine );
    CHECK( title.size() == 40 );
    CHECK( title.startsWith( "ssh build-server.exa" ) );
    CHECK( title.endsWith( "nginx/access.log" ) );
    CHECK( title.contains( QChar( 0x2026 ) ) );
}

TEST_CASE( "The title and status of an ended command tell how it ended", "[commandsource]" )
{
    const CommandEnd exited{ CommandEnd::Kind::Exited, 3 };
    CHECK( CommandSource::endedTitle( "make", exited ) == "make [exit 3]" );
    CHECK( CommandSource::endedToolTip( exited ) == "Ended with exit code 3" );
    CHECK( CommandSource::endedMessage( "make", exited ) == "\"make\" ended with exit code 3" );

#ifdef Q_OS_WIN
    const CommandEnd notFound{ CommandEnd::Kind::Exited, 9009 };
#else
    const CommandEnd notFound{ CommandEnd::Kind::Exited, 127 };
#endif
    REQUIRE( notFound.commandNotFound() );
    CHECK( CommandSource::endedMessage( "mkae", notFound ).endsWith( ": command not found" ) );
    CHECK( CommandSource::endedToolTip( notFound ).endsWith( ": command not found" ) );

    const CommandEnd stopped{ CommandEnd::Kind::Stopped, 9 };
    CHECK_FALSE( stopped.commandNotFound() );
    CHECK( CommandSource::endedTitle( "make", stopped ) == "make [stopped]" );
    CHECK( CommandSource::endedMessage( "make", stopped ) == "\"make\" was stopped" );
}

TEST_CASE( "A command does not start in a working folder that does not exist", "[commandsource]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto missing = directory.filePath( "missing" );

    QString error;
    const auto source
        = CommandSource::startCommand( RecentCommand{ "echo hello", missing, true }, &error );
    CHECK( source == nullptr );
    CHECK( error.contains( QDir::toNativeSeparators( missing ) ) );
}

TEST_CASE( "A spool file handed over is removed with its Command Source", "[commandsource]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto folder = directory.filePath( "spool" );
    REQUIRE( QDir().mkpath( folder ) );
    const auto path = folder + "/stream.log";
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "1\n2\n" );
    file.close();

    auto source = CommandSource::adoptSpoolFile( path );
    CHECK( source->kind() == CommandSource::Kind::SpoolFile );
    CHECK( source->spoolPath() == path );
    CHECK_FALSE( source->hasEnded() );

    source.reset();
    CHECK_FALSE( QFileInfo::exists( path ) );
    CHECK_FALSE( QFileInfo::exists( folder ) );
}

#ifndef Q_OS_WIN

TEST_CASE( "A command's output goes to its spool file, which goes with the Command Source",
           "[commandsource]" )
{
    auto run = runToEnd( "printf 'one\\ntwo\\n'; exit 3" );
    REQUIRE( run.end->kind == CommandEnd::Kind::Exited );
    CHECK( run.end->exitCode == 3 );
    CHECK_FALSE( run.end->commandNotFound() );
    CHECK( run.source->hasEnded() );

    const auto spool = run.source->spoolPath();
    CHECK( contentOf( spool ) == "one\ntwo\n" );

    run.source.reset();
    CHECK_FALSE( QFileInfo::exists( spool ) );
}

TEST_CASE( "A command's standard error is in its output only when asked for", "[commandsource]" )
{
    const auto commandLine = QStringLiteral( "echo out; echo err 1>&2" );

    const auto withErrors = runToEnd( commandLine, true );
    CHECK( contentOf( withErrors.source->spoolPath() ) == "out\nerr\n" );

    const auto withoutErrors = runToEnd( commandLine, false );
    CHECK( contentOf( withoutErrors.source->spoolPath() ) == "out\n" );
}

TEST_CASE( "A command the shell does not know ends as not found", "[commandsource]" )
{
    const auto run = runToEnd( "no_such_command_for_logsquirl_575" );
    REQUIRE( run.end->kind == CommandEnd::Kind::Exited );
    CHECK( run.end->exitCode == 127 );
    CHECK( run.end->commandNotFound() );
}

TEST_CASE( "A command killed by a signal ends as stopped", "[commandsource]" )
{
    const auto run = runToEnd( "kill -9 $$" );
    CHECK( run.end->kind == CommandEnd::Kind::Stopped );
}

TEST_CASE( "A pipe works in a command line", "[commandsource]" )
{
    const auto run = runToEnd( "printf 'a INFO\\nb ERROR\\nc INFO\\n' | grep ERROR" );
    CHECK( run.end->exitCode == 0 );
    CHECK( contentOf( run.source->spoolPath() ) == "b ERROR\n" );
}

TEST_CASE( "Destroying a Command Source stops the command and every process it started",
           "[commandsource]" )
{
    const ShellForTests shell;

    // The shell starts a grandchild -- one that ignores SIGTERM, too -- and
    // tells its process id.
    const auto ignoresTerm = GENERATE( false, true );
    const auto commandLine = ignoresTerm
                                 ? QStringLiteral( "sh -c 'trap \"\" TERM; while :; do sleep 1; "
                                                   "done' & echo $!; wait" )
                                 : QStringLiteral( "sleep 60 & echo $!; wait" );
    INFO( commandLine.toStdString() );

    QString error;
    auto source = CommandSource::startCommand( RecentCommand{ commandLine, {}, true }, &error );
    INFO( error.toStdString() );
    REQUIRE( source != nullptr );

    qint64 grandchild = 0;
    REQUIRE( waitUiState(
        [ & ] {
            grandchild = contentOf( source->spoolPath() ).trimmed().toLongLong();
            return grandchild > 0;
        },
        10'000 ) );
    REQUIRE( processIsRunning( grandchild ) );

    QElapsedTimer stopping;
    stopping.start();
    source.reset();
    // Stopping does not wait for the processes to end.
    CHECK( stopping.elapsed() < 1500 );

    // SIGKILL follows SIGTERM after 2 s.
    CHECK( waitUiState( [ grandchild ] { return !processIsRunning( grandchild ); }, 8'000 ) );
}

#endif
