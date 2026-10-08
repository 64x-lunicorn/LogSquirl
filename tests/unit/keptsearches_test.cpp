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
#include "indexoperation.h"
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
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

QString numberedLine( int line )
{
    return QStringLiteral( "this is line %1" ).arg( line, 6, 10, QChar( '0' ) );
}

// An Open Log File of 100 Log Lines, its View Set and its Kept Searches,
// whose Filtered Views nothing else owns.
struct LogFile {
    enum class Load { Now, Later };

    // Each Log Line reads as lineText says, by default "this is line 000042".
    // Loaded later, the Log File is not opened until load().
    explicit LogFile( const std::function<QString( int )>& lineText = numberedLine,
                      Load when = Load::Now )
    {
        REQUIRE( file.open() );
        for ( int line = 0; line < 100; ++line ) {
            file.write( ( lineText( line ) + '\n' ).toUtf8() );
        }
        file.flush();

        if ( when == Load::Now ) {
            load();
        }
    }

    void load()
    {
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

// Making another Search current stops the one that was current. A running one
// reports that it was interrupted while it is stopped, synchronously; that
// report is the Search's that was current, and never reaches the views as
// the new current Search's (#557).
SCENARIO( "A Search stopped as another is made current does not tell of its interruption",
          "[keptsearches]" )
{
    LogFile logFile;
    auto* first = logFile.keptSearches.showCurrentSearch();
    auto* another = logFile.keptSearches.startAnother();
    logFile.keptSearches.makeCurrent( first );
    QTest::qWait( 100 );

    std::vector<SearchSession::State> told;
    QObject::connect( &logFile.keptSearches, &KeptSearches::currentSearchUpdated,
                      [ &told ]( const SearchSession::State& state ) { told.push_back( state ); } );

    GIVEN( "the current Search running" )
    {
        logFile.openLogFile->requestSearch( RegularExpressionPattern( "line 00001" ) );
        REQUIRE( logFile.currentSearch().lock()->searchState().phase
                 == SearchSession::Phase::Running );

        WHEN( "another kept Search is made current before the run ended" )
        {
            logFile.keptSearches.makeCurrent( another );
            QTest::qWait( 300 );

            THEN( "nothing is told: neither its progress nor its interruption" )
            {
                REQUIRE( told.empty() );
            }
        }
    }
}

// Showing the current Search makes no other Search current: what the Open Log
// File told of it before is still told (#557).
SCENARIO( "Showing the current Search keeps what it told before", "[keptsearches]" )
{
    LogFile logFile;

    std::vector<SearchSession::State> told;
    QObject::connect( &logFile.keptSearches, &KeptSearches::currentSearchUpdated,
                      [ &told ]( const SearchSession::State& state ) { told.push_back( state ); } );

    GIVEN( "a Search requested with an invalid pattern before it is shown" )
    {
        // Told at once and once only: nothing runs.
        logFile.openLogFile->requestSearch( RegularExpressionPattern( "(" ) );

        WHEN( "the current Search is shown" )
        {
            logFile.keptSearches.showCurrentSearch();

            THEN( "that the pattern is invalid is told" )
            {
                REQUIRE( waitUiState( [ &told ] { return !told.empty(); }, 5000 ) );
                REQUIRE( told.back().phase == SearchSession::Phase::InvalidPattern );
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
            logFile.openLogFile->setDecodingPolicy( decoding );

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

namespace {

bool samePattern( const RegularExpressionPattern& left, const RegularExpressionPattern& right )
{
    return left.pattern == right.pattern && left.isCaseSensitive == right.isCaseSensitive
           && left.isExclude == right.isExclude && left.isBoolean == right.isBoolean
           && left.isPlainText == right.isPlainText;
}

} // namespace

SCENARIO( "The Searches a Session saved are rebuilt and run again", "[keptsearches][session]" )
{
    LogFile logFile;
    auto* first = logFile.keptSearches.showCurrentSearch();

    bool finished = false;
    QObject::connect( &logFile.keptSearches, &KeptSearches::restoredSearchesFinished,
                      [ &finished ] { finished = true; } );

    GIVEN( "three saved Searches, the second current, the third with an empty pattern" )
    {
        const KeptSearches::Requested saved{ { RegularExpressionPattern( "line 00001" ),
                                               RegularExpressionPattern( "LINE 00002", false, false,
                                                                         false, true ),
                                               RegularExpressionPattern() },
                                             1 };

        WHEN( "they are restored, and the caller requests the current one" )
        {
            const auto views = logFile.keptSearches.restore( saved );
            REQUIRE( views.size() == 3 );
            REQUIRE( views.front() == first );
            const auto current = logFile.currentSearch().lock();
            logFile.keptSearches.requestCurrent( saved.patterns[ 1 ] );

            THEN( "they are kept in their order, the second one current in every view" )
            {
                REQUIRE( logFile.keptSearches.count() == 3 );
                REQUIRE( logFile.keptSearches.currentView() == views[ 1 ] );
                REQUIRE( logFile.viewSet.currentSearch() == current.get() );
            }

            THEN( "each one with a pattern runs, the empty one does not, and the end is told" )
            {
                REQUIRE( waitUiState( [ & ] { return finished; }, 30000 ) );
                REQUIRE( current->searchState().phase == SearchSessionPhase::Complete );
                REQUIRE( current->getNbMatches() == 10_lcount );
                // The first one, kept beside the current one.
                logFile.keptSearches.makeCurrent( views[ 0 ] );
                REQUIRE( logFile.openLogFile->searchState().phase == SearchSessionPhase::Complete );
                REQUIRE( logFile.openLogFile->matchCount() == 10_lcount );
                logFile.keptSearches.makeCurrent( views[ 2 ] );
                REQUIRE( logFile.openLogFile->searchState().phase == SearchSessionPhase::Idle );
            }

            THEN( "what each was requested for is what the Session saves again" )
            {
                const auto requested = logFile.keptSearches.requested();
                REQUIRE( requested.current == 1 );
                REQUIRE( requested.patterns.size() == 3 );
                for ( std::size_t index = 0; index < 3; ++index ) {
                    REQUIRE( samePattern( requested.patterns[ index ], saved.patterns[ index ] ) );
                }
            }
        }
    }

    GIVEN( "a Search requested by the user and another one started after it" )
    {
        logFile.keptSearches.requestCurrent( RegularExpressionPattern( "line 00001" ) );
        logFile.keptSearches.startAnother();
        logFile.keptSearches.requestCurrent( RegularExpressionPattern( "line 00002" ) );
        logFile.keptSearches.clearCurrent();

        THEN( "the Session saves the first one's pattern and the second one requested for "
              "nothing, current" )
        {
            const auto requested = logFile.keptSearches.requested();
            REQUIRE( requested.current == 1 );
            REQUIRE( requested.patterns.size() == 2 );
            REQUIRE( requested.patterns[ 0 ].pattern == "line 00001" );
            REQUIRE( requested.patterns[ 1 ].pattern.isEmpty() );
        }
    }

    GIVEN( "a saved Search list with only an empty Search" )
    {
        logFile.keptSearches.restore(
            KeptSearches::Requested{ { RegularExpressionPattern() }, 0 } );

        THEN( "no Search runs, and the end is told all the same" )
        {
            REQUIRE( waitUiState( [ & ] { return finished; }, 5000 ) );
            REQUIRE( logFile.keptSearches.count() == 1 );
        }
    }
}

struct KeptSearchesTest {};

template <>
struct LogData::access_by<KeptSearchesTest> {
    // The Index of the Log Data, whose lock an index run takes to write it.
    static const IndexingData* index( const LogData& logData )
    {
        return logData.indexing_data_.get();
    }
};

SCENARIO( "The Searches a Session saved are dropped when the first load does not succeed",
          "[keptsearches][session]" )
{
    QTemporaryFile file{ "keptsearches_test_XXXXXX" };
    REQUIRE( file.open() );
    for ( int line = 0; line < 100; ++line ) {
        file.write( ( numberedLine( line ) + '\n' ).toUtf8() );
    }
    file.flush();

    auto policies = testSettingsPolicies();
    auto openLogFile = std::make_shared<OpenLogFile>(
        policies.indexing, policies.search, policies.fileAccess, policies.decoding,
        policies.recognition, std::make_shared<LogFormatCatalog>(), nullptr );
    QuickFindPattern quickFindPattern;
    ViewSet viewSet;
    std::vector<std::unique_ptr<FilteredView>> views;
    KeptSearches keptSearches{ openLogFile, viewSet, [ & ]( LogFilteredData* search ) {
                                  views.push_back( std::make_unique<FilteredView>(
                                      search, &quickFindPattern, false ) );
                                  return views.back().get();
                              } };
    // Filtered Views read their Searches: they go first.
    struct ViewsGoFirst {
        std::vector<std::unique_ptr<FilteredView>>& views;
        ~ViewsGoFirst()
        {
            views.clear();
        }
    } viewsGoFirst{ views };
    keptSearches.showCurrentSearch();

    bool finished = false;
    QObject::connect( &keptSearches, &KeptSearches::restoredSearchesFinished,
                      [ &finished ] { finished = true; } );
    std::optional<LoadingStatus> loaded;
    QObject::connect(
        openLogFile.get(), &OpenLogFile::loadingFinished,
        [ &loaded ]( const OpenLogFile::LoadFinished& load ) { loaded = load.status; } );

    GIVEN( "the first load interrupted before it could finish" )
    {
        // Held by the Index's lock, the index run cannot write the Index, so
        // cannot finish, until the interruption was asked for: it ends
        // interrupted every time, however the threads run (#751). Nothing
        // here reads the Log Data while the lock is held.
        std::optional<IndexingData::ConstAccessor> indexHeld;
        indexHeld.emplace( LogData::access_by<KeptSearchesTest>::index( *openLogFile->logData() ) );
        openLogFile->open( file.fileName() );
        openLogFile->logData()->interruptLoading();
        indexHeld.reset();

        AND_GIVEN( "two saved Searches restored before the Log File is told loaded, the second "
                   "current" )
        {
            const KeptSearches::Requested saved{ { RegularExpressionPattern( "line 00001" ),
                                                   RegularExpressionPattern( "line 00002" ) },
                                                 1 };
            keptSearches.restore( saved );
            keptSearches.requestCurrent( saved.patterns[ 1 ] );

            WHEN( "the load ends" )
            {
                REQUIRE( waitUiState( [ & ] { return loaded.has_value(); }, 30000 ) );
                REQUIRE( loaded == LoadingStatus::Interrupted );

                THEN( "neither runs, and the end is told all the same" )
                {
                    REQUIRE( waitUiState( [ & ] { return finished; }, 5000 ) );
                    REQUIRE( openLogFile->searchState().phase == SearchSessionPhase::Idle );
                }
            }

            WHEN( "they are held, and the load ends" )
            {
                openLogFile->holdWaitingSearches();
                REQUIRE( waitUiState( [ & ] { return loaded.has_value(); }, 30000 ) );
                REQUIRE( loaded == LoadingStatus::Interrupted );

                THEN( "they are dropped as unheld ones are: neither runs, and the end is told" )
                {
                    REQUIRE( waitUiState( [ & ] { return finished; }, 5000 ) );
                    REQUIRE_FALSE( openLogFile->isSearchHeld( openLogFile->filteredData() ) );
                    REQUIRE( openLogFile->searchState().phase == SearchSessionPhase::Idle );
                }
            }
        }
    }
}

SCENARIO( "The Searches a Session saved, held beyond the first load, are told finished once "
          "released and run, or dropped",
          "[keptsearches][session]" )
{
    LogFile logFile( numberedLine, LogFile::Load::Later );
    auto& keptSearches = logFile.keptSearches;
    const auto& openLogFile = logFile.openLogFile;
    keptSearches.showCurrentSearch();

    bool finished = false;
    QObject::connect( &keptSearches, &KeptSearches::restoredSearchesFinished,
                      [ &finished ] { finished = true; } );

    GIVEN( "two saved Searches restored and held before the first load, the second current" )
    {
        const KeptSearches::Requested saved{ { RegularExpressionPattern( "line 00001" ),
                                               RegularExpressionPattern( "line 00002" ) },
                                             1 };
        const auto restored = keptSearches.restore( saved );
        keptSearches.requestCurrent( saved.patterns[ 1 ] );
        openLogFile->holdWaitingSearches();
        const auto current = openLogFile->filteredData();

        WHEN( "the Log File has loaded" )
        {
            logFile.load();

            THEN( "neither runs, and the end is not told" )
            {
                REQUIRE_FALSE( waitUiState( [ & ] { return finished; }, 300 ) );
                REQUIRE( current->searchState().phase == SearchSessionPhase::Idle );
            }

            AND_WHEN( "they are released" )
            {
                openLogFile->releaseHeldSearches();

                THEN( "both run, and the end is told once they have finished" )
                {
                    REQUIRE( waitUiState( [ & ] { return finished; }, 30000 ) );
                    REQUIRE( current->searchState().phase == SearchSessionPhase::Complete );
                    REQUIRE( current->getNbMatches() == 10_lcount );
                    keptSearches.makeCurrent( restored[ 0 ] );
                    REQUIRE( openLogFile->searchState().phase == SearchSessionPhase::Complete );
                    REQUIRE( openLogFile->matchCount() == 10_lcount );
                }
            }

            AND_WHEN( "the user requests the current one" )
            {
                keptSearches.requestCurrent( RegularExpressionPattern( "line 00003" ) );

                THEN( "it runs at once, and the end waits for the one still held" )
                {
                    REQUIRE( waitUiState(
                        [ & ] {
                            return current->searchState().phase == SearchSessionPhase::Complete;
                        },
                        30000 ) );
                    REQUIRE( current->getNbMatches() == 10_lcount );
                    REQUIRE_FALSE( waitUiState( [ & ] { return finished; }, 300 ) );

                    AND_THEN( "dropping the one still held tells the end, nothing released" )
                    {
                        REQUIRE( keptSearches.drop( restored[ 0 ] ) );
                        REQUIRE( waitUiState( [ & ] { return finished; }, 5000 ) );
                    }
                }
            }
        }

        WHEN( "they are released before the Log File has loaded" )
        {
            openLogFile->releaseHeldSearches();
            logFile.load();

            THEN( "both run once it has, and the end is told" )
            {
                REQUIRE( waitUiState( [ & ] { return finished; }, 30000 ) );
                REQUIRE( current->getNbMatches() == 10_lcount );
            }
        }
    }
}
