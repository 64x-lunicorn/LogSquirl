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

#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <cstddef>
#include <vector>

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
