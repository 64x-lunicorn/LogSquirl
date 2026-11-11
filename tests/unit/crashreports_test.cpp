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

// What the crash handler does with the crash reports of the last run, and the
// issue it offers to open (#444). The Sentry and Crashpad calls, the dialogs
// and the browser stay outside: the tests hand in recording actions instead,
// so no test asks a question, touches a crash database or opens a URL.

#include <catch2/catch_test_macros.hpp>

#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <cstdarg>
#include <cstddef>
#include <vector>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <signal.h>
#endif

#include "crashreports.h"
#include "issuereporter.h"
#include "logsquirl_version.h"

namespace {

// Records every action the handling takes, and answers the question with the
// choices it is given, in order.
struct RecordingActions {
    std::vector<CrashReportChoice> answers;

    QStringList symbolized;
    QStringList asked;
    QStringList askedDumpFiles;
    std::vector<std::size_t> sent;
    std::vector<std::size_t> discarded;
    QStringList offeredIssues;

    CrashReportActions actions()
    {
        CrashReportActions result;
        result.symbolize = [ this ]( const QString& dumpFile ) {
            symbolized << dumpFile;
            return QStringLiteral( "stack of %1" ).arg( dumpFile );
        };
        result.askUser = [ this ]( const QString& formattedReport, const QString& dumpFile ) {
            asked << formattedReport;
            askedDumpFiles << dumpFile;
            REQUIRE( static_cast<std::size_t>( asked.size() ) <= answers.size() );
            return answers[ static_cast<std::size_t>( asked.size() - 1 ) ];
        };
        result.send = [ this ]( std::size_t index ) { sent.push_back( index ); };
        result.discard = [ this ]( std::size_t index ) { discarded.push_back( index ); };
        result.offerIssue = [ this ]( const QString& crashId ) { offeredIssues << crashId; };
        return result;
    }
};

// The issue body GitHub is asked to prefill.
QString issueBody( const QUrl& url )
{
    return QUrlQuery( url ).queryItemValue( "body", QUrl::FullyDecoded );
}

// Hands the message to the formatting the way Sentry's logger does, as a
// format and a va_list (#833).
QString sentryMessage( const char* message, ... ) Q_ATTRIBUTE_FORMAT_PRINTF( 1, 2 );

QString sentryMessage( const char* message, ... )
{
    va_list args;
    va_start( args, message );
    const auto text = formatSentryMessage( message, args );
    va_end( args );
    return text;
}

} // namespace

TEST_CASE( "No crash report of the last run asks nothing", "[crashreports]" )
{
    RecordingActions recording;

    CHECK_FALSE( handlePendingCrashReports( {}, recording.actions() ) );

    CHECK( recording.symbolized.isEmpty() );
    CHECK( recording.asked.isEmpty() );
    CHECK( recording.offeredIssues.isEmpty() );
}

TEST_CASE( "A crash report already uploaded is left alone", "[crashreports]" )
{
    RecordingActions recording;

    const std::vector<PendingCrashReport> reports{
        { "0f1e2d3c", "/dumps/old.dmp", true },
    };

    CHECK_FALSE( handlePendingCrashReports( reports, recording.actions() ) );

    CHECK( recording.symbolized.isEmpty() );
    CHECK( recording.asked.isEmpty() );
    CHECK( recording.sent.empty() );
    CHECK( recording.discarded.empty() );
    CHECK( recording.offeredIssues.isEmpty() );
}

TEST_CASE( "The user sees the dump file and its stack before choosing", "[crashreports]" )
{
    RecordingActions recording;
    recording.answers = { CrashReportChoice::Discard };

    const std::vector<PendingCrashReport> reports{
        { "a1b2", "/dumps/a1b2.dmp", false },
    };

    handlePendingCrashReports( reports, recording.actions() );

    CHECK( recording.symbolized == QStringList{ "/dumps/a1b2.dmp" } );
    REQUIRE( recording.asked.size() == 1 );
    CHECK( recording.asked.front() == "/dumps/a1b2.dmp\nstack of /dumps/a1b2.dmp" );
    CHECK( recording.askedDumpFiles == QStringList{ "/dumps/a1b2.dmp" } );
}

TEST_CASE( "A crash report the user sends is queued for upload", "[crashreports]" )
{
    RecordingActions recording;
    recording.answers = { CrashReportChoice::Send };

    const std::vector<PendingCrashReport> reports{
        { "a1b2", "/dumps/a1b2.dmp", false },
    };

    // The caller waits for the upload before the application goes on.
    CHECK( handlePendingCrashReports( reports, recording.actions() ) );

    CHECK( recording.sent == std::vector<std::size_t>{ 0 } );
    CHECK( recording.discarded.empty() );
    CHECK( recording.offeredIssues == QStringList{ "a1b2" } );
}

TEST_CASE( "A crash report the user discards is deleted, and the issue is still offered",
           "[crashreports]" )
{
    RecordingActions recording;
    recording.answers = { CrashReportChoice::Discard };

    const std::vector<PendingCrashReport> reports{
        { "a1b2", "/dumps/a1b2.dmp", false },
    };

    CHECK_FALSE( handlePendingCrashReports( reports, recording.actions() ) );

    CHECK( recording.sent.empty() );
    CHECK( recording.discarded == std::vector<std::size_t>{ 0 } );
    CHECK( recording.offeredIssues == QStringList{ "a1b2" } );
}

TEST_CASE( "Each crash report of the last run gets its own choice", "[crashreports]" )
{
    RecordingActions recording;
    recording.answers = { CrashReportChoice::Discard, CrashReportChoice::Send };

    const std::vector<PendingCrashReport> reports{
        { "first", "/dumps/first.dmp", false },
        { "done", "/dumps/done.dmp", true },
        { "third", "/dumps/third.dmp", false },
    };

    // One report sent is enough to wait for the upload.
    CHECK( handlePendingCrashReports( reports, recording.actions() ) );

    CHECK( recording.askedDumpFiles == QStringList{ "/dumps/first.dmp", "/dumps/third.dmp" } );
    // The indexes are those of the list handed in, so the caller finds the
    // database entry of each report; the uploaded one in between counts.
    CHECK( recording.discarded == std::vector<std::size_t>{ 0 } );
    CHECK( recording.sent == std::vector<std::size_t>{ 2 } );
    CHECK( recording.offeredIssues == QStringList{ "first", "third" } );
}

TEST_CASE( "The crash dumps live below the given data directory", "[crashreports]" )
{
    CHECK( crashDumpDirectory( "/home/user/.local/share/LogSquirl" )
           == "/home/user/.local/share/LogSquirl/logsquirl_dump" );
}

TEST_CASE( "The Crashpad tools are found next to the application", "[crashreports]" )
{
#ifdef Q_OS_WIN
    CHECK( crashpadToolPath( "C:/Program Files/LogSquirl", "logsquirl_crashpad_handler" )
           == "C:/Program Files/LogSquirl/logsquirl_crashpad_handler.exe" );
#else
    CHECK( crashpadToolPath( "/opt/logsquirl/bin", "logsquirl_crashpad_handler" )
           == "/opt/logsquirl/bin/logsquirl_crashpad_handler" );
#endif
}

TEST_CASE( "A message of Sentry's logger has its placeholders filled", "[crashreports]" )
{
    REQUIRE( sentryMessage( "using database path \"%s\"", "/tmp/logsquirl_dump" )
             == QStringLiteral( "using database path \"/tmp/logsquirl_dump\"" ) );
    REQUIRE( sentryMessage( "sending envelope %d of %d", 1, 3 )
             == QStringLiteral( "sending envelope 1 of 3" ) );
    REQUIRE( sentryMessage( "starting backend" ) == QStringLiteral( "starting backend" ) );
}

TEST_CASE( "The issue offered after a crash opens a new issue of the repository",
           "[crashreports][issuereporter]" )
{
    const auto url = IssueReporter::issueUrl( IssueTemplate::Crash, "a1b2-c3d4" );

    CHECK( url.isValid() );
    CHECK( url.scheme() == "https" );
    CHECK( url.host() == "github.com" );
    CHECK( url.path() == "/64x-lunicorn/LogSquirl/issues/new" );

    const auto body = issueBody( url );
    CHECK( body.startsWith( "Details for the issue\n" ) );
    CHECK( body.contains( "#### What did you do?" ) );
    CHECK( body.contains( "Crash id:\na1b2-c3d4\n" ) );
    CHECK_FALSE( body.contains( "Exception:" ) );
}

TEST_CASE( "The issue offered after an exception quotes the exception",
           "[crashreports][issuereporter]" )
{
    const auto body
        = issueBody( IssueReporter::issueUrl( IssueTemplate::Exception, "std::bad_alloc" ) );

    CHECK( body.contains( "Exception:\nstd::bad_alloc\n" ) );
    CHECK_FALSE( body.contains( "Crash id:" ) );
}

TEST_CASE( "A bug report asks what was expected and what happened",
           "[crashreports][issuereporter]" )
{
    const auto body = issueBody( IssueReporter::issueUrl( IssueTemplate::Bug, "ignored" ) );

    CHECK( body.contains( "#### What did you do?" ) );
    CHECK( body.contains( "#### What did you expect to see?" ) );
    CHECK( body.contains( "#### What did you see instead?" ) );
    CHECK_FALSE( body.contains( "ignored" ) );
}

TEST_CASE( "Every issue names the build it comes from", "[crashreports][issuereporter]" )
{
    const auto body = issueBody( IssueReporter::issueUrl( IssueTemplate::Bug ) );

    CHECK( body.contains(
        QStringLiteral( "> LogSquirl version %1" ).arg( QString( logsquirlVersion() ) ) ) );
    CHECK( body.contains( QString( logsquirlCommit() ) ) );
    CHECK( body.contains( QStringLiteral( "> Qt %1, tbb " ).arg( qVersion() ) ) );
}

TEST_CASE( "Characters of the issue text that mean something in a URL survive it",
           "[crashreports][issuereporter]" )
{
    const QString information = "a & b = c? #1 100% +plus/slash\nnext line ü";

    const auto url = IssueReporter::issueUrl( IssueTemplate::Exception, information );

    // Nothing of the text leaks into another query item or the fragment.
    CHECK( url.fragment().isEmpty() );
    CHECK( QUrlQuery( url ).queryItems().size() == 1 );
    CHECK( issueBody( url ).contains( "Exception:\n" + information + "\n" ) );
}

#ifdef Q_OS_MACOS

namespace {

constexpr exception_mask_t CrashpadExceptionMask
    = EXC_MASK_CRASH | EXC_MASK_RESOURCE | EXC_MASK_GUARD;

// Stands in for Crashpad's client and handler: a port of its own becomes the
// task's crash exception port, as the client makes the handler's port, and
// it hears when no process holds a send right to it any more -- the no-senders
// notification on which the handler ends. Gives the task its old exception
// ports back when it goes.
class CrashHandlerStandIn {
public:
    CrashHandlerStandIn()
    {
        const auto task = mach_task_self();
        mach_port_allocate( task, MACH_PORT_RIGHT_RECEIVE, &port_ );
        mach_port_allocate( task, MACH_PORT_RIGHT_RECEIVE, &notifyPort_ );
        mach_port_insert_right( task, port_, port_, MACH_MSG_TYPE_MAKE_SEND );
        task_swap_exception_ports(
            task, CrashpadExceptionMask, port_,
            static_cast<exception_behavior_t>( EXCEPTION_STATE_IDENTITY | MACH_EXCEPTION_CODES ),
            MACHINE_THREAD_STATE, savedMasks_, &savedCount_, savedPorts_, savedBehaviors_,
            savedFlavors_ );
        // From here on only the task's exception port holds a send right.
        mach_port_deallocate( task, port_ );
        mach_port_t previous = MACH_PORT_NULL;
        mach_port_request_notification( task, port_, MACH_NOTIFY_NO_SENDERS, 0, notifyPort_,
                                        MACH_MSG_TYPE_MAKE_SEND_ONCE, &previous );
    }

    ~CrashHandlerStandIn()
    {
        clientEnds();
        mach_port_mod_refs( mach_task_self(), port_, MACH_PORT_RIGHT_RECEIVE, -1 );
        mach_port_mod_refs( mach_task_self(), notifyPort_, MACH_PORT_RIGHT_RECEIVE, -1 );
    }

    CrashHandlerStandIn( const CrashHandlerStandIn& ) = delete;
    CrashHandlerStandIn& operator=( const CrashHandlerStandIn& ) = delete;

    bool isTaskCrashPort() const
    {
        exception_mask_t masks[ EXC_TYPES_COUNT ] = {};
        mach_msg_type_number_t count = EXC_TYPES_COUNT;
        mach_port_t ports[ EXC_TYPES_COUNT ] = {};
        exception_behavior_t behaviors[ EXC_TYPES_COUNT ] = {};
        thread_state_flavor_t flavors[ EXC_TYPES_COUNT ] = {};
        task_get_exception_ports( mach_task_self(), EXC_MASK_CRASH, masks, &count, ports, behaviors,
                                  flavors );
        bool found = false;
        for ( mach_msg_type_number_t i = 0; i < count; ++i ) {
            found = found || ports[ i ] == port_;
            if ( MACH_PORT_VALID( ports[ i ] ) ) {
                mach_port_deallocate( mach_task_self(), ports[ i ] );
            }
        }
        return found;
    }

    // As the client process ending: the task lets go of the handler's port.
    void clientEnds()
    {
        if ( clientEnded_ ) {
            return;
        }
        clientEnded_ = true;
        task_set_exception_ports( mach_task_self(), CrashpadExceptionMask, MACH_PORT_NULL,
                                  EXCEPTION_DEFAULT, THREAD_STATE_NONE );
        for ( mach_msg_type_number_t i = 0; i < savedCount_; ++i ) {
            task_set_exception_ports( mach_task_self(), savedMasks_[ i ], savedPorts_[ i ],
                                      savedBehaviors_[ i ], savedFlavors_[ i ] );
            if ( MACH_PORT_VALID( savedPorts_[ i ] ) ) {
                mach_port_deallocate( mach_task_self(), savedPorts_[ i ] );
            }
        }
    }

    // Whether the handler would end within the time: no process is left with
    // a send right to its port.
    bool wouldEndWithin( mach_msg_timeout_t milliseconds )
    {
        struct {
            mach_msg_header_t header;
            char body[ 256 ];
        } message = {};
        return mach_msg( &message.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof( message ),
                         notifyPort_, milliseconds, MACH_PORT_NULL )
                   == MACH_MSG_SUCCESS
               && message.header.msgh_id == MACH_NOTIFY_NO_SENDERS;
    }

private:
    mach_port_t port_ = MACH_PORT_NULL;
    mach_port_t notifyPort_ = MACH_PORT_NULL;
    bool clientEnded_ = false;
    exception_mask_t savedMasks_[ EXC_TYPES_COUNT ] = {};
    mach_msg_type_number_t savedCount_ = EXC_TYPES_COUNT;
    mach_port_t savedPorts_[ EXC_TYPES_COUNT ] = {};
    exception_behavior_t savedBehaviors_[ EXC_TYPES_COUNT ] = {};
    thread_state_flavor_t savedFlavors_[ EXC_TYPES_COUNT ] = {};
};

} // namespace

// A process LogSquirl starts, such as the adb server a plugin's "adb devices"
// leaves running, kept the crash handler of a LogSquirl that had quit alive
// for days (#953).
TEST_CASE( "A child process outliving the application does not keep its crash handler running",
           "[crashreports][childprocesses]" )
{
    detachChildProcessesFromCrashHandler();
    CrashHandlerStandIn handler;

    QProcess child;
    child.start( QStringLiteral( "/bin/sleep" ), { QStringLiteral( "10" ) } );
    REQUIRE( child.waitForStarted() );

    // The application itself is still watched.
    CHECK( handler.isTaskCrashPort() );

    handler.clientEnds();
    CHECK( handler.wouldEndWithin( 3000 ) );
    CHECK( child.state() == QProcess::Running );

    child.kill();
    child.waitForFinished();
}

TEST_CASE( "A detached child process does not keep the crash handler running",
           "[crashreports][childprocesses]" )
{
    detachChildProcessesFromCrashHandler();
    CrashHandlerStandIn handler;

    qint64 pid = 0;
    REQUIRE( QProcess::startDetached( QStringLiteral( "/bin/sleep" ), { QStringLiteral( "10" ) },
                                      {}, &pid ) );

    handler.clientEnds();
    CHECK( handler.wouldEndWithin( 3000 ) );

    ::kill( static_cast<pid_t>( pid ), SIGKILL );
}

#endif
