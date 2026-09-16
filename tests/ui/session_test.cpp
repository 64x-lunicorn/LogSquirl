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

#include <memory>
#include <vector>

#include <QTemporaryFile>

#include "recording_views.h"
#include "test_policies.h"

#include "logformatcatalog.h"
#include "openlogfile.h"
#include "savedsearches.h"
#include "session.h"

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
