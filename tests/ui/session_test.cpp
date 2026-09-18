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

#include "recording_views.h"
#include "test_policies.h"

#include "containers.h"
#include "logdata.h"
#include "logformatcatalog.h"
#include "openlogfile.h"
#include "savedsearches.h"
#include "session.h"
#include "sessioninfo.h"

#include <QTemporaryFile>
#include <QTest>

#include <algorithm>
#include <memory>
#include <vector>

#include <catch2/catch.hpp>

// The Session builds the views of every Log File it opens through one seam
// (#248): one value to build them from, and afterwards only what changed and
// what their view context is. These scenarios stand a recording fake behind
// that seam, so what the Session hands out can be read off without a widget.

namespace {

struct TwoLogFiles {
    QTemporaryFile first{ "session_test_first_XXXXXX" };
    QTemporaryFile second{ "session_test_second_XXXXXX" };

    TwoLogFiles()
    {
        REQUIRE( first.open() );
        REQUIRE( second.open() );
    }
};

// Owns the views the Session built, as a window does, and lets them go before
// the Session they were opened from.
struct OpenedViews {
    std::vector<RecordingViews*> built;

    ~OpenedViews()
    {
        for ( auto* views : built ) {
            delete views;
        }
    }
};

} // namespace

SCENARIO( "A Log File's views are built from one value", "[ui][session]" )
{
    TwoLogFiles files;
    const auto policies = testSettingsPolicies();
    const auto catalog = std::make_shared<LogFormatCatalog>();
    Session session{ policies, catalog };
    OpenedViews views;

    GIVEN( "a Log File opened without a saved view context" )
    {
        session.open( files.first.fileName(), RecordingViews::factory( views.built ) );
        REQUIRE( views.built.size() == 1 );
        const auto& build = views.built.front()->build();

        THEN( "its views were built once, from everything they show the Log File with" )
        {
            REQUIRE( build.openLogFile != nullptr );
            REQUIRE( build.openLogFile->logFormatCatalog().get() == catalog.get() );
            REQUIRE( build.quickFindPattern == session.quickFindPattern() );
            REQUIRE( build.policies == policies );
            REQUIRE( build.savedSearches == &session.savedSearches() );
            REQUIRE( build.viewContext.isEmpty() );
            REQUIRE( build.changeReport );
        }

        THEN( "nothing is handed to them afterwards" )
        {
            REQUIRE( views.built.front()->changes().empty() );
        }
    }

    GIVEN( "a Log File opened with the view context saved for it" )
    {
        session.open( files.first.fileName(), RecordingViews::factory( views.built ),
                      QStringLiteral( "saved context" ) );
        REQUIRE( views.built.size() == 1 );

        THEN( "the view context is part of the value its views were built from" )
        {
            REQUIRE( views.built.front()->build().viewContext == "saved context" );
            REQUIRE( views.built.front()->changes().empty() );
        }
    }
}

SCENARIO( "A change to one Axis reaches every open Log File, and no other Axis is handed out",
          "[ui][session]" )
{
    TwoLogFiles files;
    const auto policies = testSettingsPolicies();
    Session session{ policies, std::make_shared<LogFormatCatalog>() };
    OpenedViews views;

    session.open( files.first.fileName(), RecordingViews::factory( views.built ) );
    session.open( files.second.fileName(), RecordingViews::factory( views.built ) );
    REQUIRE( views.built.size() == 2 );

    WHEN( "only the Decoration Policy changes" )
    {
        auto changed = policies;
        changed.decoration.mainSearchHighlight = !policies.decoration.mainSearchHighlight;
        session.applyPolicies( changed );

        THEN( "every open Log File is handed that Axis alone, once" )
        {
            ViewChange expected;
            expected.decoration = changed.decoration;
            for ( const auto* opened : views.built ) {
                REQUIRE( opened->changes().size() == 1 );
                REQUIRE( opened->changes().front() == expected );
            }
        }
    }

    WHEN( "only the Presentation Policy changes" )
    {
        auto changed = policies;
        changed.presentation.useTextWrap = !policies.presentation.useTextWrap;
        session.applyPolicies( changed );

        THEN( "every open Log File is handed that Axis alone, once" )
        {
            ViewChange expected;
            expected.presentation = changed.presentation;
            for ( const auto* opened : views.built ) {
                REQUIRE( opened->changes().size() == 1 );
                REQUIRE( opened->changes().front() == expected );
            }
        }
    }

    WHEN( "the Watch and QuickFind Policies change together" )
    {
        auto changed = policies;
        changed.watch.pollingEnabled = !policies.watch.pollingEnabled;
        changed.quickFind.incremental = !policies.quickFind.incremental;
        session.applyPolicies( changed );

        THEN( "every open Log File is handed both Axes in one change, and no other" )
        {
            ViewChange expected;
            expected.watch = changed.watch;
            expected.quickFind = changed.quickFind;
            for ( const auto* opened : views.built ) {
                REQUIRE( opened->changes().size() == 1 );
                REQUIRE( opened->changes().front() == expected );
            }
        }
    }

    WHEN( "only an Axis the views do not hold changes" )
    {
        auto changed = policies;
        changed.search.contextLinesCount = policies.search.contextLinesCount + 1;
        session.applyPolicies( changed );

        THEN( "nothing is handed to the views" )
        {
            for ( const auto* opened : views.built ) {
                REQUIRE( opened->changes().empty() );
            }
        }
    }

    WHEN( "the same Policies are applied again" )
    {
        session.applyPolicies( policies );

        THEN( "nothing is handed to the views" )
        {
            for ( const auto* opened : views.built ) {
                REQUIRE( opened->changes().empty() );
            }
        }
    }

    WHEN( "the views of one Log File report that the Highlighter Sets changed" )
    {
        views.built.front()->reportChange( Changed::HighlighterSets );

        THEN( "every open Log File hears of it, and of nothing else" )
        {
            ViewChange expected;
            expected.highlighterSets = true;
            for ( const auto* opened : views.built ) {
                REQUIRE( opened->changes().size() == 1 );
                REQUIRE( opened->changes().front() == expected );
            }
        }
    }

    WHEN( "a Log File opened after a change" )
    {
        auto changed = policies;
        changed.decoration.mainSearchHighlight = !policies.decoration.mainSearchHighlight;
        session.applyPolicies( changed );

        QTemporaryFile third{ "session_test_third_XXXXXX" };
        REQUIRE( third.open() );
        session.open( third.fileName(), RecordingViews::factory( views.built ) );
        REQUIRE( views.built.size() == 3 );

        THEN( "its views are built with the changed Policies, and handed nothing afterwards" )
        {
            REQUIRE( views.built.back()->build().policies == changed );
            REQUIRE( views.built.back()->changes().empty() );
        }
    }
}

namespace {

// A window as the Session sees it: it counts the settings changes it is told of.
struct CountingWindow final : SessionWindow {
    int settingsChanges = 0;

    void applySettingsChange() override
    {
        ++settingsChanges;
    }
};

} // namespace

SCENARIO( "A zoom hands every open Log File the font alone", "[ui][session]" )
{
    TwoLogFiles files;
    const auto policies = testSettingsPolicies();
    // Never rebuilt so far, so it holds no Log Format: a rebuild would read
    // the built-in ones.
    const auto catalog = std::make_shared<LogFormatCatalog>();
    Session session{ policies, catalog };
    OpenedViews views;
    CountingWindow window;
    session.addWindow( &window );

    session.open( files.first.fileName(), RecordingViews::factory( views.built ) );
    session.open( files.second.fileName(), RecordingViews::factory( views.built ) );
    REQUIRE( views.built.size() == 2 );
    REQUIRE( catalog->formatCount() == 0 );

    WHEN( "the views of one Log File report that the font changed" )
    {
        views.built.front()->reportChange( Changed::Font );

        THEN( "every open Log File is told to read the font again, and nothing else" )
        {
            ViewChange expected;
            expected.font = true;
            for ( const auto* opened : views.built ) {
                REQUIRE( opened->changes().size() == 1 );
                REQUIRE( opened->changes().front() == expected );
            }
        }

        THEN( "no setting is applied again: the Log Format Catalog is not rebuilt and no window "
              "is told" )
        {
            REQUIRE( catalog->formatCount() == 0 );
            REQUIRE( window.settingsChanges == 0 );
        }
    }

    session.removeWindow( &window );
}

SCENARIO( "Restoring a Session reads the settings store once, at startup, not per Log File",
          "[ui][session]" )
{
    // What the Session read at startup stands in the in-memory Session info
    // only, never saved: were a Log File restored or opened from a fresh read
    // of the settings store, it would not find its view context there (#301).
    // The settings store holds a saved Session, as after any earlier run.
    SessionInfo::getSynced().save();

    TwoLogFiles files;
    const auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    const auto windowId = QStringLiteral( "session_test_window_301" );

    auto& readAtStartup = SessionInfo::get();
    readAtStartup.add( windowId );
    readAtStartup.setOpenFiles(
        windowId, { { files.first.fileName(), 0, QStringLiteral( "first context" ) },
                    { files.second.fileName(), 0, QStringLiteral( "second context" ) } } );

    WindowSession window{ appSession, windowId, 0 };
    OpenedViews views;

    WHEN( "the window's Log Files are restored" )
    {
        int currentFileIndex = -1;
        const auto restored
            = window.restore( RecordingViews::factory( views.built ), &currentFileIndex );

        THEN( "each is built with the view context read at startup" )
        {
            REQUIRE( restored.size() == 2 );
            REQUIRE( views.built.size() == 2 );
            REQUIRE( views.built[ 0 ]->build().viewContext == "first context" );
            REQUIRE( views.built[ 1 ]->build().viewContext == "second context" );
            REQUIRE( currentFileIndex == 1 );
        }
    }

    WHEN( "the Log Files are opened one by one in the window" )
    {
        window.open( files.first.fileName(), RecordingViews::factory( views.built ) );
        window.open( files.second.fileName(), RecordingViews::factory( views.built ) );

        THEN( "each is built with the view context read at startup" )
        {
            REQUIRE( views.built.size() == 2 );
            REQUIRE( views.built[ 0 ]->build().viewContext == "first context" );
            REQUIRE( views.built[ 1 ]->build().viewContext == "second context" );
        }
    }

    // Leave the in-memory Session info as the settings store has it.
    SessionInfo::getSynced();
}

namespace {

// Writes Log Lines to file until it holds at least `bytes`.
void writeLogLines( QTemporaryFile& file, qint64 bytes )
{
    REQUIRE( file.open() );
    const QByteArray line = "2026-09-17 12:34:56.789 INFO [worker-1] request handled in 42 ms\n";
    qint64 written = 0;
    while ( written < bytes ) {
        written += file.write( line );
    }
    REQUIRE( file.flush() );
}

// Three Log Files to be saved as the one window of the last Session, the last
// one its current tab (#300). The first is far larger than the other two, so that
// Log Files loading side by side would finish in a different order than Log
// Files loading one after another.
struct ThreeTabSession {
    QTemporaryFile large{ "session_test_large_XXXXXX" };
    QTemporaryFile small{ "session_test_small_XXXXXX" };
    QTemporaryFile current{ "session_test_current_XXXXXX" };
    const QString windowId = QStringLiteral( "session_test_window_300" );

    ThreeTabSession()
    {
        writeLogLines( large, 16 * 1024 * 1024 );
        writeLogLines( small, 1024 );
        writeLogLines( current, 1024 * 1024 );
    }

    // Saves them in the Session info read at startup. Building a Session reads
    // the settings store again, so this comes after.
    void store() const
    {
        auto& readAtStartup = SessionInfo::get();
        readAtStartup.add( windowId );
        readAtStartup.setOpenFiles( windowId, { { large.fileName(), 0, QString{} },
                                                { small.fileName(), 0, QString{} },
                                                { current.fileName(), 0, QString{} } } );
    }

    ~ThreeTabSession()
    {
        // Leave the in-memory Session info as the settings store has it.
        SessionInfo::getSynced();
    }

    ThreeTabSession( const ThreeTabSession& ) = delete;
    ThreeTabSession& operator=( const ThreeTabSession& ) = delete;
};

// The tabs whose Log File finished its first load, in the order they did.
struct LoadOrder {
    std::vector<int> finished;

    void follow( const std::vector<RecordingViews*>& views )
    {
        for ( auto tab = 0; tab < logsquirl::isize( views ); ++tab ) {
            QObject::connect( views[ static_cast<size_t>( tab ) ]->build().openLogFile.get(),
                              &OpenLogFile::loadingFinished, [ this, tab ] {
                                  if ( std::find( finished.begin(), finished.end(), tab )
                                       == finished.end() ) {
                                      finished.push_back( tab );
                                  }
                              } );
        }
    }

    bool waitFor( size_t count )
    {
        return QTest::qWaitFor( [ this, count ] { return finished.size() >= count; }, 120000 );
    }

    bool waitForTab( int tab )
    {
        return QTest::qWaitFor(
            [ this, tab ] {
                return std::find( finished.begin(), finished.end(), tab ) != finished.end();
            },
            120000 );
    }
};

} // namespace

SCENARIO( "Restoring a Session loads the current tab's Log File before the others",
          "[ui][session]" )
{
    ThreeTabSession stored;
    const auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    stored.store();
    WindowSession window{ appSession, stored.windowId, 0 };
    OpenedViews views;
    LoadOrder order;

    int currentFileIndex = -1;
    const auto restored
        = window.restore( RecordingViews::factory( views.built ), &currentFileIndex );
    REQUIRE( restored.size() == 3 );
    REQUIRE( currentFileIndex == 2 );
    order.follow( views.built );

    WHEN( "no other tab is activated" )
    {
        REQUIRE( order.waitFor( 3 ) );

        THEN( "the current tab loads first, and the others one after another, in tab order" )
        {
            REQUIRE( order.finished == std::vector<int>{ 2, 0, 1 } );
        }
    }

    WHEN( "another tab is activated once the current tab has loaded" )
    {
        REQUIRE( order.waitFor( 1 ) );
        REQUIRE( order.finished == std::vector<int>{ 2 } );
        window.startLoading( restored[ 1 ].second );

        THEN( "its Log File loads at once, without waiting for the tabs before it" )
        {
            // Out of the queue there and then, rather than left waiting for the
            // tab ahead of it. Not which of the two reports first: the moment
            // the current tab finished, the queue started tab 0, so the two
            // load side by side, and their order is a race between 16 MiB and
            // 1 KiB that a loaded machine can decide either way.
            REQUIRE( !appSession->isLoadQueued( restored[ 1 ].second ) );
            REQUIRE( order.waitForTab( 1 ) );

            // And every tab still gets there.
            REQUIRE( order.waitFor( 3 ) );
            auto loaded = order.finished;
            std::sort( loaded.begin(), loaded.end() );
            REQUIRE( loaded == std::vector<int>{ 0, 1, 2 } );
        }
    }

    WHEN( "a tab whose Log File has not been attached yet is reloaded" )
    {
        // Every tab but the current one waits in the queue, with no Log File
        // attached to reload.
        REQUIRE( appSession->isLoadQueued( restored[ 0 ].second ) );
        REQUIRE( appSession->isLoadQueued( restored[ 1 ].second ) );
        REQUIRE( !appSession->isLoadQueued( restored[ 2 ].second ) );

        // Reloading it means loading it, and the Session starts that load the
        // one way it starts any: out of the queue (#332).
        views.built[ 1 ]->build().openLogFile->reload();

        THEN( "its Log File loads, and no tab queued behind it starts with it" )
        {
            // Read before the event loop runs again, so this is the queue as
            // the reload left it: the reloaded tab took its turn, the tab
            // queued ahead of it did not.
            REQUIRE( !appSession->isLoadQueued( restored[ 1 ].second ) );
            REQUIRE( appSession->isLoadQueued( restored[ 0 ].second ) );

            REQUIRE( order.waitForTab( 1 ) );
            REQUIRE( views.built[ 1 ]->build().openLogFile->logData()->getNbLine().get() > 0 );

            // And the queue is not left broken behind it: every tab loads.
            REQUIRE( order.waitFor( 3 ) );
            auto loaded = order.finished;
            std::sort( loaded.begin(), loaded.end() );
            REQUIRE( loaded == std::vector<int>{ 0, 1, 2 } );
        }
    }

    WHEN( "the current tab is closed before its Log File has loaded" )
    {
        window.close( restored[ 2 ].second );
        // Its views are still here, so it goes on loading; only the other
        // tabs are looked at.
        REQUIRE( order.waitFor( 3 ) );
        order.finished.erase( std::remove( order.finished.begin(), order.finished.end(), 2 ),
                              order.finished.end() );

        THEN( "the other tabs' Log Files load all the same, one after another" )
        {
            REQUIRE( order.finished == std::vector<int>{ 0, 1 } );
        }
    }
}
