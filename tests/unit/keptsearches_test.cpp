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

// The Kept Searches of a Log File, without the widget showing them in tabs:
// a Search is added, made current and dropped in one place, the Open Log File
// and every view follow which one is current, and a Search dropped is held by
// nothing once its Filtered View is gone (#518).

#include "filteredview.h"
#include "keptsearches.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "openlogfile.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "test_policies.h"
#include "test_utils.h"
#include "viewset.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTemporaryFile>
#include <QTest>

#include <functional>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

QString numberedLine( int line )
{
    return QStringLiteral( "this is line %1" ).arg( line, 6, 10, QChar( '0' ) );
}

// An Open Log File of 100 loaded Log Lines, its View Set and its Kept
// Searches, whose Filtered Views nothing else owns.
struct LogFile {
    // Each Log Line reads as lineText says, by default "this is line 000042".
    explicit LogFile( const std::function<QString( int )>& lineText = numberedLine )
    {
        REQUIRE( file.open() );
        for ( int line = 0; line < 100; ++line ) {
            file.write( ( lineText( line ) + '\n' ).toUtf8() );
        }
        file.flush();

        openLogFile->open( file.fileName() );
        REQUIRE( waitUiState(
            [ this ] { return openLogFile->logData()->getNbLine() == 100_lcount; }, 30000 ) );
        QCoreApplication::processEvents();
    }

    ~LogFile()
    {
        // Filtered Views dropped are deleted later: delete them before their
        // Searches go.
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        for ( auto& view : views ) {
            delete view.data();
        }
    }

    LogFile( const LogFile& ) = delete;
    LogFile& operator=( const LogFile& ) = delete;

    std::weak_ptr<LogFilteredData> currentSearch() const
    {
        return openLogFile->filteredData();
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "keptsearches_test_XXXXXX" };
    std::shared_ptr<OpenLogFile> openLogFile = std::make_shared<OpenLogFile>(
        policies.indexing, policies.search, policies.fileAccess, policies.decoding,
        policies.recognition, std::make_shared<LogFormatCatalog>(), nullptr );
    QuickFindPattern quickFindPattern;
    ViewSet viewSet;
    std::vector<QPointer<FilteredView>> views;
    KeptSearches keptSearches{ openLogFile, viewSet, [ this ]( LogFilteredData* search ) {
                                  auto* view = new FilteredView( search, &quickFindPattern, false );
                                  views.emplace_back( view );
                                  return view;
                              } };
};

} // namespace

SCENARIO( "A Search made current is current in the Open Log File and every view", "[keptsearches]" )
{
    LogFile logFile;

    GIVEN( "the first Search of the Log File shown" )
    {
        auto* first = logFile.keptSearches.showCurrentSearch();
        const auto firstSearch = logFile.currentSearch();

        THEN( "it is current in the View Set, shown in its Filtered View" )
        {
            REQUIRE( logFile.keptSearches.currentView() == first );
            REQUIRE( logFile.viewSet.currentSearch() == firstSearch.lock().get() );
            REQUIRE( logFile.keptSearches.count() == 1 );
        }

        WHEN( "another Search is started" )
        {
            auto* another = logFile.keptSearches.startAnother();
            const auto anotherSearch = logFile.currentSearch();

            THEN( "the new one is current, and the first is kept" )
            {
                REQUIRE( anotherSearch.lock() != firstSearch.lock() );
                REQUIRE_FALSE( firstSearch.expired() );
                REQUIRE( logFile.keptSearches.currentView() == another );
                REQUIRE( logFile.viewSet.currentSearch() == anotherSearch.lock().get() );
                REQUIRE( logFile.keptSearches.count() == 2 );
            }

            AND_WHEN( "the first one is made current again" )
            {
                logFile.keptSearches.makeCurrent( first );

                THEN( "the Open Log File and the View Set have it current" )
                {
                    REQUIRE( logFile.currentSearch().lock() == firstSearch.lock() );
                    REQUIRE( logFile.viewSet.currentSearch() == firstSearch.lock().get() );
                    REQUIRE( logFile.keptSearches.currentView() == first );
                }
            }

            AND_WHEN( "the first one, not current, is dropped" )
            {
                const QPointer<FilteredView> dropped{ first };
                REQUIRE( logFile.keptSearches.drop( first ) );

                THEN( "its view goes, and then its Search: nothing holds it" )
                {
                    REQUIRE_FALSE( firstSearch.expired() );
                    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
                    REQUIRE( dropped.isNull() );
                    REQUIRE( firstSearch.expired() );
                    REQUIRE( logFile.keptSearches.count() == 1 );
                    REQUIRE( logFile.viewSet.currentSearch() == anotherSearch.lock().get() );
                }
            }

            AND_WHEN( "the current one is dropped" )
            {
                REQUIRE( logFile.keptSearches.drop( another ) );
                QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );

                THEN( "the one before it is current everywhere, and the one dropped is gone" )
                {
                    REQUIRE( anotherSearch.expired() );
                    REQUIRE( logFile.currentSearch().lock() == firstSearch.lock() );
                    REQUIRE( logFile.viewSet.currentSearch() == firstSearch.lock().get() );
                    REQUIRE( logFile.keptSearches.currentView() == first );
                }

                AND_WHEN( "the last one left is dropped" )
                {
                    THEN( "it is not: a Log File keeps one Search" )
                    {
                        REQUIRE_FALSE( logFile.keptSearches.drop( first ) );
                        REQUIRE( logFile.keptSearches.count() == 1 );
                        REQUIRE( logFile.keptSearches.currentView() == first );
                    }
                }
            }
        }
    }
}

SCENARIO( "Only the current Search's progress is told", "[keptsearches]" )
{
    LogFile logFile;
    auto* first = logFile.keptSearches.showCurrentSearch();

    std::vector<SearchSession::State> told;
    QObject::connect( &logFile.keptSearches, &KeptSearches::currentSearchUpdated,
                      [ &told ]( const SearchSession::State& state ) { told.push_back( state ); } );

    GIVEN( "the current Search running" )
    {
        logFile.openLogFile->requestSearch( RegularExpressionPattern( "line 00001" ) );

        WHEN( "it is left to run" )
        {
            THEN( "its progress is told" )
            {
                REQUIRE( waitUiState(
                    [ &told ] {
                        return !told.empty() && told.back().phase == SearchSession::Phase::Complete;
                    },
                    30000 ) );
                REQUIRE( told.back().pattern.pattern == "line 00001" );
            }
        }

        WHEN( "another Search is made current before what it told arrived" )
        {
            logFile.keptSearches.startAnother();
            QTest::qWait( 300 );

            THEN( "nothing it told is told any more" )
            {
                REQUIRE( told.empty() );
            }

            AND_WHEN( "the first one is made current again and searched" )
            {
                logFile.keptSearches.makeCurrent( first );
                logFile.openLogFile->requestSearch( RegularExpressionPattern( "line 00002" ) );

                THEN( "its progress is told again" )
                {
                    REQUIRE( waitUiState(
                        [ &told ] {
                            return !told.empty()
                                   && told.back().phase == SearchSession::Phase::Complete;
                        },
                        30000 ) );
                    REQUIRE( told.back().pattern.pattern == "line 00002" );
                }
            }
        }
    }
}

SCENARIO(
    "A kept Search repeated after the Decoding Policy changed finds the new reading's Matches",
    "[keptsearches]" )
{
    // Every other Log Line has "bold end" in it, split by an ANSI color
    // sequence: it matches only once such sequences are hidden.
    LogFile logFile( []( int line ) {
        return line % 2 == 0 ? numberedLine( line ) + QStringLiteral( " \x1b[1mbold\x1b[0m end" )
                             : numberedLine( line );
    } );
    auto* first = logFile.keptSearches.showCurrentSearch();
    const auto firstSearch = logFile.currentSearch().lock();

    const RegularExpressionPattern boldEnd( "bold end" );
    const auto waitComplete = [ &firstSearch ] {
        return waitUiState(
            [ &firstSearch ] {
                return firstSearch->searchState().phase == SearchSession::Phase::Complete;
            },
            30000 );
    };

    GIVEN( "a Search that finds nothing while the ANSI color sequences show, kept" )
    {
        logFile.openLogFile->requestSearch( boldEnd );
        REQUIRE( waitComplete() );
        REQUIRE( firstSearch->getNbMatches() == 0_lcount );
        logFile.keptSearches.startAnother();

        WHEN( "they are hidden, and the kept Search is made current and repeated" )
        {
            auto decoding = logFile.policies.decoding;
            decoding.hideAnsiColorSequences = true;
            logFile.openLogFile->logData()->setDecodingPolicy( decoding );

            logFile.keptSearches.makeCurrent( first );
            logFile.openLogFile->requestSearch( boldEnd );
            const auto requested = firstSearch->searchState();
            REQUIRE( waitComplete() );

            THEN( "it runs again and finds the Log Lines as they read now" )
            {
                REQUIRE_FALSE( requested.fromCache );
                REQUIRE( firstSearch->getNbMatches() == 50_lcount );
            }
        }

        WHEN( "nothing changed, and the kept Search is made current and repeated" )
        {
            logFile.keptSearches.makeCurrent( first );
            logFile.openLogFile->requestSearch( boldEnd );

            THEN( "it is served from the cache" )
            {
                REQUIRE( firstSearch->searchState().fromCache );
                REQUIRE( firstSearch->getNbMatches() == 0_lcount );
            }
        }
    }
}
