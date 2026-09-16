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

#include "qfnotifications.h"
#include "quickfindmux.h"
#include "quickfindpattern.h"
#include "test_policies.h"

#include <QObject>
#include <QRegularExpression>
#include <QString>

#include <memory>
#include <vector>

// The mux that dispatches a QuickFind to the widget the user is in holds a
// QuickFind Policy and reads no setting of its own (#185). The Policy says
// whether typing searches at once and how the typed text is read, so every
// scenario below states the Policy it expects an answer under -- and the mux
// is built here with no settings store anywhere in sight.

namespace {

// A searchable that records what the mux asked of it, standing in for the
// Presentation the user is in.
class RecordingSearchable : public QObject, public SearchableWidgetInterface {
    Q_OBJECT

public:
    int incrementalForwardCount = 0;
    int incrementalBackwardCount = 0;
    int stopCount = 0;
    int abortCount = 0;

    void searchForward() override {}
    void searchBackward() override {}
    void incrementallySearchForward() override
    {
        ++incrementalForwardCount;
    }
    void incrementallySearchBackward() override
    {
        ++incrementalBackwardCount;
    }
    void incrementalSearchStop() override
    {
        ++stopCount;
    }
    void incrementalSearchAbort() override
    {
        ++abortCount;
    }

Q_SIGNALS:
    // Declared so the mux's connections find them; no scenario emits one.
    void changeQuickFind( const QString& pattern, QuickFindMux::QFDirection direction );
    void notifyQuickFind( const QFNotification& notification );
    void clearQuickFindNotification();
    void searchNext();
    void searchPrevious();
};

// Hands the mux the one searchable of the fixture below.
class SingleSearchableSelector : public QuickFindMuxSelectorInterface {
public:
    explicit SingleSearchableSelector( RecordingSearchable* searchable )
        : searchable_( searchable )
    {
    }

protected:
    SearchableWidgetInterface* doGetActiveSearchable() const override
    {
        return searchable_;
    }

    std::vector<QObject*> doGetAllSearchables() const override
    {
        return { searchable_ };
    }

private:
    RecordingSearchable* searchable_;
};

// A mux wired to one recording searchable, with the QuickFind pattern it
// drives. Nothing here reads a setting.
struct MuxFixture {
    std::shared_ptr<QuickFindPattern> pattern = std::make_shared<QuickFindPattern>();
    RecordingSearchable searchable;
    SingleSearchableSelector selector{ &searchable };
    QuickFindMux mux{ pattern };
    QuickFindPolicy policy = testSettingsPolicies().quickFind;

    MuxFixture()
    {
        mux.registerSelector( &selector );
        // The mux has no direction until it is told one.
        mux.setDirection( QuickFindMux::Forward );
    }
};

} // namespace

SCENARIO( "The QuickFind mux searches while typing only when its Policy says so",
          "[quickfind][settings]" )
{
    MuxFixture fixture;

    GIVEN( "a Policy under which QuickFind is incremental" )
    {
        fixture.policy.incremental = true;
        fixture.mux.setQuickFindPolicy( fixture.policy );

        WHEN( "a pattern is typed but not confirmed" )
        {
            fixture.mux.setNewPattern( "abc", false, false );

            THEN( "the QuickFind pattern followed it and the widget searched at once" )
            {
                REQUIRE( fixture.pattern->getPattern() == "abc" );
                REQUIRE( fixture.searchable.incrementalForwardCount == 1 );
            }
        }

        WHEN( "the user cancels" )
        {
            fixture.mux.cancelSearch();

            THEN( "the search in progress is abandoned" )
            {
                REQUIRE( fixture.searchable.abortCount == 1 );
            }
        }
    }

    GIVEN( "a Policy under which QuickFind waits for the pattern to be confirmed" )
    {
        fixture.policy.incremental = false;
        fixture.mux.setQuickFindPolicy( fixture.policy );

        WHEN( "a pattern is typed but not confirmed" )
        {
            fixture.mux.setNewPattern( "abc", false, false );

            THEN( "nothing was searched and the QuickFind pattern is untouched" )
            {
                REQUIRE( fixture.pattern->getPattern().isEmpty() );
                REQUIRE( fixture.searchable.incrementalForwardCount == 0 );
            }
        }

        WHEN( "the pattern is confirmed" )
        {
            fixture.mux.confirmPattern( "abc", false, false );

            THEN( "the QuickFind pattern followed it, with no incremental search to stop" )
            {
                REQUIRE( fixture.pattern->getPattern() == "abc" );
                REQUIRE( fixture.searchable.stopCount == 0 );
            }
        }

        WHEN( "the user cancels" )
        {
            fixture.mux.cancelSearch();

            THEN( "there was nothing to abandon" )
            {
                REQUIRE( fixture.searchable.abortCount == 0 );
            }
        }
    }
}

SCENARIO( "The QuickFind Policy decides how the mux reads a typed pattern",
          "[quickfind][settings]" )
{
    MuxFixture fixture;

    GIVEN( "a Policy reading a QuickFind pattern as an extended regexp" )
    {
        fixture.policy.quickFindRegexpType = SearchRegexpType::ExtendedRegexp;
        fixture.mux.setQuickFindPolicy( fixture.policy );

        WHEN( "text the user did not mean as a regexp is confirmed" )
        {
            fixture.mux.confirmPattern( "a.c", false, false );

            THEN( "its metacharacters are escaped, so they stand for themselves" )
            {
                REQUIRE( fixture.pattern->getPattern() == QRegularExpression::escape( "a.c" ) );
            }
        }
    }

    GIVEN( "a Policy reading a QuickFind pattern as a fixed string" )
    {
        fixture.policy.quickFindRegexpType = SearchRegexpType::FixedString;
        fixture.mux.setQuickFindPolicy( fixture.policy );

        WHEN( "the same text is confirmed" )
        {
            fixture.mux.confirmPattern( "a.c", false, false );

            THEN( "the pattern is the text as it stands" )
            {
                REQUIRE( fixture.pattern->getPattern() == "a.c" );
            }
        }
    }
}

#include "quickfindmux_test.moc"
