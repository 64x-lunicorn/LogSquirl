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

#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "filewatcher.h"
#include "logformatcatalog.h"
#include "savedsearches.h"
#include "session.h"
#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"

#include "crawlerwidget.h"

// Format Recognition as the coordinator of a Log File runs it (#104): once
// per first load, manual reload or truncation, against the one Log Format
// Catalog the Session hands every view.

namespace {

constexpr int LineCount = 60;

const char* RecognitionTestFormatJson = R"({
    "recognition_test_log": {
        "title": "%1",
        "regex": {
            "basic": {
                "pattern": "^RECOGNITION_TEST (?<timestamp>\\d{6}) (?<level>[A-Z]+) \\[(?<thread>[a-z]+)\\] (?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "value": {
            "thread": { "kind": "string", "identifier": true }
        },
        "sample": [{ "line": "RECOGNITION_TEST 000001 INFO [main] hello" }]
    }
})";

void writeUserFormat( const QString& directory, const QString& title )
{
    QFile file( QDir( directory ).filePath( "recognition_test.json" ) );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) );
    file.write( QString::fromUtf8( RecognitionTestFormatJson ).arg( title ).toUtf8() );
}

void writeLogLines( QFile& file )
{
    for ( int i = 0; i < LineCount; ++i ) {
        file.write( QString( "RECOGNITION_TEST %1 INFO [main] this is line %2\n" )
                        .arg( i, 6, 10, QChar( '0' ) )
                        .arg( i )
                        .toUtf8() );
    }
    file.flush();
}

bool writeLogFile( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        return false;
    }
    writeLogLines( file );
    return true;
}

} // namespace

struct FormatRecognitionAccess {};

template <>
struct CrawlerWidget::access_by<FormatRecognitionAccess> {
    std::unique_ptr<CrawlerWidget> crawler;

    explicit access_by( ViewInterface* view )
        : crawler( static_cast<CrawlerWidget*>( view ) )
    {
    }

    bool isLoadingFinished() const
    {
        return !crawler->loadingInProgress_;
    }

    LinesCount nbLines() const
    {
        return crawler->logData_->getNbLine();
    }

    int recognitionCount() const
    {
        return crawler->formatRecognitionCount_;
    }

    const LogFormatDefinition* logFormat() const
    {
        return crawler->recognizedFormat_.get();
    }

    const LogFormatCatalog* logFormatCatalog() const
    {
        return crawler->logFormatCatalog_.get();
    }

    bool isTableViewToggled() const
    {
        return crawler->tableViewToggle_->isChecked();
    }

    void reload()
    {
        crawler->reload();
    }

    bool waitLoaded( LinesCount lines )
    {
        return waitUiState( [ this, lines ] { return nbLines() == lines && isLoadingFinished(); } );
    }
};

using CrawlerAccess = CrawlerWidget::access_by<FormatRecognitionAccess>;

namespace {

std::unique_ptr<CrawlerAccess> openLogFile( Session& session, const QString& path )
{
    return std::make_unique<CrawlerAccess>(
        session.open( path, [] { return new CrawlerWidget(); } ) );
}

SettingsPolicies recognitionEnabled()
{
    auto policies = testSettingsPolicies();
    policies.recognition.enabled = true;
    return policies;
}

} // namespace

SCENARIO( "Every Log File is recognized against the one Log Format Catalog",
          "[ui][formatrecognition]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto userFormats = directory.filePath( "formats" );
    REQUIRE( QDir().mkpath( userFormats ) );
    writeUserFormat( userFormats, "Before Edit" );

    const auto firstPath = directory.filePath( "first.log" );
    const auto secondPath = directory.filePath( "second.log" );
    REQUIRE( writeLogFile( firstPath ) );
    REQUIRE( writeLogFile( secondPath ) );

    auto catalog = std::make_shared<LogFormatCatalog>( userFormats );
    catalog->rebuild();

    const auto policies = recognitionEnabled();
    Session session{ policies, catalog };
    session.savedSearches().clear();

    GIVEN( "two Log Files opened one after the other" )
    {
        auto first = openLogFile( session, firstPath );
        REQUIRE( first->waitLoaded( LinesCount( LineCount ) ) );
        auto second = openLogFile( session, secondPath );
        REQUIRE( second->waitLoaded( LinesCount( LineCount ) ) );

        THEN( "both were handed the same Catalog instance, the Session's" )
        {
            REQUIRE( first->logFormatCatalog() == catalog.get() );
            REQUIRE( second->logFormatCatalog() == catalog.get() );
            REQUIRE( session.logFormatCatalog().get() == catalog.get() );
        }

        THEN( "each was recognized once, with the Catalog's own Log Format" )
        {
            REQUIRE( first->recognitionCount() == 1 );
            REQUIRE( second->recognitionCount() == 1 );
            REQUIRE( first->logFormat() == catalog->formatByName( "recognition_test_log" ).get() );
            REQUIRE( second->logFormat() == first->logFormat() );
        }
    }

    GIVEN( "a recognized Log File and a user Log Format edited on disk" )
    {
        auto logFile = openLogFile( session, firstPath );
        REQUIRE( logFile->waitLoaded( LinesCount( LineCount ) ) );
        REQUIRE( logFile->logFormat() != nullptr );
        REQUIRE( logFile->logFormat()->title() == "Before Edit" );

        writeUserFormat( userFormats, "After Edit" );

        WHEN( "settings are applied without any setting having changed" )
        {
            session.applyPolicies( policies );

            THEN( "the Catalog was rebuilt and holds the edited Log Format" )
            {
                REQUIRE( catalog->formatByName( "recognition_test_log" )->title() == "After Edit" );
            }

            THEN( "the open Log File keeps the Log Format it was recognized with" )
            {
                REQUIRE( logFile->logFormat()->title() == "Before Edit" );
                REQUIRE( logFile->recognitionCount() == 1 );
            }

            AND_WHEN( "the Log File is reloaded by hand" )
            {
                logFile->reload();
                REQUIRE( waitUiState( [ & ] { return logFile->recognitionCount() == 2; } ) );
                REQUIRE( logFile->waitLoaded( LinesCount( LineCount ) ) );

                THEN( "Format Recognition ran again and picked up the edited Log Format" )
                {
                    REQUIRE( logFile->recognitionCount() == 2 );
                    REQUIRE( logFile->logFormat()
                             == catalog->formatByName( "recognition_test_log" ).get() );
                    REQUIRE( logFile->logFormat()->title() == "After Edit" );
                }
            }
        }
    }
}

SCENARIO( "A changed Recognition Policy takes effect at the next Format Recognition",
          "[ui][formatrecognition]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto userFormats = directory.filePath( "formats" );
    REQUIRE( QDir().mkpath( userFormats ) );
    writeUserFormat( userFormats, "Recognition Test" );

    const auto path = directory.filePath( "file.log" );
    REQUIRE( writeLogFile( path ) );

    auto catalog = std::make_shared<LogFormatCatalog>( userFormats );
    catalog->rebuild();

    GIVEN( "a Log File opened while Format Recognition was disabled" )
    {
        Session session{ testSettingsPolicies(), catalog };
        session.savedSearches().clear();

        auto logFile = openLogFile( session, path );
        REQUIRE( logFile->waitLoaded( LinesCount( LineCount ) ) );
        REQUIRE( logFile->logFormat() == nullptr );

        WHEN( "an enabled Recognition Policy arrives" )
        {
            session.applyPolicies( recognitionEnabled() );
            QTest::qWait( 100 );

            THEN( "nothing is recognized until the next Format Recognition" )
            {
                REQUIRE( logFile->logFormat() == nullptr );
            }

            AND_WHEN( "the Log File is reloaded" )
            {
                logFile->reload();

                THEN( "its Log Format is recognized" )
                {
                    REQUIRE( waitUiState( [ & ] { return logFile->logFormat() != nullptr; } ) );
                    REQUIRE( logFile->logFormat()->name() == "recognition_test_log" );
                }
            }
        }
    }
}

SCENARIO( "A truncated Log File is recognized exactly once when it has loaded again",
          "[ui][formatrecognition]" )
{
    // Polling as well as native watching, so the truncation is noticed on
    // any platform without waiting long.
    FileWatcher::getFileWatcher().setWatchPolicy(
        WatchPolicy{ .nativeWatchEnabled = true, .pollingEnabled = true, .pollIntervalMs = 100 } );

    // Whether a recognized Log Format opens as a Table View rides the
    // Presentation Policy, handed to the Log File when it is opened: the
    // Crawler Widget reads no setting of its own for it (#185).
    auto policies = recognitionEnabled();
    policies.presentation.autoShowTableView = true;

    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto userFormats = directory.filePath( "formats" );
    REQUIRE( QDir().mkpath( userFormats ) );
    writeUserFormat( userFormats, "Recognition Test" );

    const auto path = directory.filePath( "truncated.log" );
    REQUIRE( writeLogFile( path ) );

    auto catalog = std::make_shared<LogFormatCatalog>( userFormats );
    catalog->rebuild();

    Session session{ policies, catalog };
    session.savedSearches().clear();

    GIVEN( "a recognized Log File shown as a Table View, with Auto-show Table View enabled" )
    {
        auto logFile = openLogFile( session, path );
        REQUIRE( logFile->waitLoaded( LinesCount( LineCount ) ) );
        REQUIRE( logFile->recognitionCount() == 1 );
        REQUIRE( logFile->logFormat() != nullptr );
        REQUIRE( waitUiState( [ & ] { return logFile->isTableViewToggled(); } ) );

        WHEN( "the Log File is truncated and then written again" )
        {
            {
                QFile file( path );
                REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
            }
            REQUIRE( waitUiState( [ & ] { return logFile->nbLines() == 0_lcount; } ) );
            REQUIRE( logFile->logFormat() == nullptr );

            {
                QFile file( path );
                REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Append ) );
                writeLogLines( file );
            }
            REQUIRE( logFile->waitLoaded( LinesCount( LineCount ) ) );
            REQUIRE( waitUiState( [ & ] { return logFile->recognitionCount() >= 2; } ) );

            // Anything the Table View's toggle set off would have run by now.
            QTest::qWait( 500 );

            THEN( "Format Recognition ran exactly once more" )
            {
                REQUIRE( logFile->recognitionCount() == 2 );
            }

            THEN( "the Log Format is recognized again and shown as a Table View" )
            {
                REQUIRE( logFile->logFormat()
                         == catalog->formatByName( "recognition_test_log" ).get() );
                REQUIRE( logFile->isTableViewToggled() );
            }
        }
    }

    FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{} );
}
