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

// The View Set of a Log File, without the widget coordinating its panes and
// without the Search bar: what it is handed reaches every view in it, and a
// view added later -- a kept Search's Filtered View -- starts with all of it
// (#242).

#include "filteredview.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logpresentation.h"
#include "quickfindpattern.h"
#include "test_policies.h"
#include "viewset.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPoint>
#include <QPointer>
#include <QShortcut>
#include <QString>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch.hpp>

struct ViewSetTest {};

template <>
struct AbstractLogView::access_by<ViewSetTest> {
    static const DecorationPolicy& decorationPolicy( const AbstractLogView& view )
    {
        return view.decorationSetup_.policy();
    }
    static const PresentationPolicy& presentationPolicy( const AbstractLogView& view )
    {
        return view.scrolling_.presentationPolicy();
    }
    static bool lineNumbersVisible( const AbstractLogView& view )
    {
        return view.lineNumbersVisible_;
    }
    static bool followAllowed( const AbstractLogView& view )
    {
        return view.scrolling_.followAllowed();
    }
    static const std::vector<QStringList>& colorLabels( const AbstractLogView& view )
    {
        return view.quickHighlighters_;
    }
    static std::pair<LineNumber, LineNumber> searchLimits( const AbstractLogView& view )
    {
        return { view.searchStart_, view.searchEnd_ };
    }
};

namespace {

using ViewAccess = AbstractLogView::access_by<ViewSetTest>;

// A Presentation that keeps what it was handed last.
class RecordingPresentation final : public LogPresentation {
public:
    QString selectedText() const override
    {
        return {};
    }
    OptionalLineNumber logLineAt( const QPoint& ) const override
    {
        return {};
    }
    void showLogLine( LineNumber ) override {}
    void showLogLinePortion( LineNumber, LinesCount, LineColumn, LineLength ) override {}
    void updateDecorations() override
    {
        ++decorationUpdates;
    }
    void rereadLogLines() override
    {
        ++rereads;
    }
    void updateFont( const QFont& newFont ) override
    {
        font = newFont;
    }
    void setDecorationPolicy( const DecorationPolicy& policy ) override
    {
        decorationPolicy = policy;
    }
    void setPresentationPolicy( const PresentationPolicy& policy ) override
    {
        presentationPolicy = policy;
    }
    void setQuickFindPolicy( const QuickFindPolicy& policy ) override
    {
        quickFindPolicy = policy;
    }
    void allowFollowMode( bool allow ) override
    {
        followAllowed = allow;
    }
    void setColorLabels( const std::vector<QStringList>& labels ) override
    {
        colorLabels = labels;
    }
    void setSearchLimits( LineNumber startLine, LineNumber endLine ) override
    {
        searchLimits = std::make_pair( startLine, endLine );
    }
    void saveSelectedTo( const QString& ) override {}
    void registerShortcuts() override
    {
        ++shortcutRegistrations;
    }

    std::optional<DecorationPolicy> decorationPolicy;
    std::optional<PresentationPolicy> presentationPolicy;
    std::optional<QuickFindPolicy> quickFindPolicy;
    std::optional<bool> followAllowed;
    std::optional<QFont> font;
    std::optional<std::vector<QStringList>> colorLabels;
    std::optional<std::pair<LineNumber, LineNumber>> searchLimits;
    int decorationUpdates = 0;
    int rereads = 0;
    int shortcutRegistrations = 0;
};

// What a Log File's views are shown under, none of it what a view starts with.
DecorationPolicy handedDecorationPolicy()
{
    return DecorationPolicy{ .mainSearchHighlight = true,
                             .variateMainSearchHighlight = true,
                             .mainSearchBackColor = QColor{ 0x12, 0x34, 0x56 },
                             .quickFindBackColor = QColor{ 0x65, 0x43, 0x21 } };
}

PresentationPolicy handedPresentationPolicy()
{
    auto policy = testSettingsPolicies().presentation;
    policy.fastScrollEnabled = true;
    policy.fastScrollMultiplier = 7;
    policy.mainLineNumbersVisible = false;
    policy.filteredLineNumbersVisible = true;
    return policy;
}

QuickFindPolicy handedQuickFindPolicy()
{
    auto policy = testSettingsPolicies().quickFind;
    policy.quickFindRegexpType = SearchRegexpType::ExtendedRegexp;
    policy.incremental = !policy.incremental;
    return policy;
}

QFont handedFont()
{
    auto font = QFontDatabase::systemFont( QFontDatabase::FixedFont );
    font.setPointSize( 23 );
    return font;
}

ViewSet::ColorLabels handedColorLabels()
{
    auto labels = ViewSet::ColorLabels( ViewSet::ColorLabelCount );
    labels[ 0 ] << QStringLiteral( "warning" );
    labels[ 4 ] << QStringLiteral( "timeout" ) << QStringLiteral( "retry" );
    return labels;
}

const std::pair<LineNumber, LineNumber> HandedSearchLimits{ 3_lnum, 8_lnum };

// Hands every piece of state to the View Set.
void handEverything( ViewSet& viewSet )
{
    viewSet.setDecorationPolicy( handedDecorationPolicy() );
    viewSet.setPresentationPolicy( handedPresentationPolicy() );
    viewSet.setQuickFindPolicy( handedQuickFindPolicy() );
    viewSet.setFollowAllowed( false );
    viewSet.setFont( handedFont() );
    viewSet.setColorLabels( handedColorLabels() );
    viewSet.setSearchLimits( HandedSearchLimits.first, HandedSearchLimits.second );
}

// The Log File every Filtered View here shows the Matches of.
struct LogFile {
    SettingsPolicies policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };
    QuickFindPattern quickFindPattern;

    // What a kept Search adds: a Filtered View of Matches of its own.
    struct Search {
        std::unique_ptr<LogFilteredData> matches;
        std::unique_ptr<FilteredView> view;
    };

    Search newSearch()
    {
        Search search;
        search.matches = logData.getNewFilteredData();
        search.view
            = std::make_unique<FilteredView>( search.matches.get(), &quickFindPattern, false );
        return search;
    }
};

// The shortcuts a view answers to now.
std::vector<QPointer<QShortcut>> shortcutsOf( const QObject& view )
{
    std::vector<QPointer<QShortcut>> shortcuts;
    for ( auto* shortcut : view.findChildren<QShortcut*>() ) {
        shortcuts.emplace_back( shortcut );
    }
    return shortcuts;
}

// Whether a Filtered View shows the font, as it would draw in it.
bool drawsIn( const FilteredView& view, const QFont& font )
{
    return view.font().pointSize() == font.pointSize();
}

} // namespace

SCENARIO( "A Filtered View added to the View Set starts with everything it holds", "[viewset]" )
{
    LogFile logFile;
    ViewSet viewSet;

    GIVEN( "a View Set handed every piece of state, with a Filtered View already in it" )
    {
        auto first = logFile.newSearch();
        viewSet.addFilteredView( first.view.get() );
        handEverything( viewSet );

        WHEN( "a Search is kept and its Filtered View added" )
        {
            auto kept = logFile.newSearch();
            REQUIRE_FALSE( ViewAccess::decorationPolicy( *kept.view ) == handedDecorationPolicy() );
            viewSet.addFilteredView( kept.view.get() );

            THEN( "it starts with the Decoration Policy" )
            {
                REQUIRE( ViewAccess::decorationPolicy( *kept.view ) == handedDecorationPolicy() );
            }

            THEN( "it starts with the Presentation Policy, and the Filtered View's line numbers" )
            {
                REQUIRE( ViewAccess::presentationPolicy( *kept.view )
                         == handedPresentationPolicy() );
                REQUIRE( ViewAccess::lineNumbersVisible( *kept.view ) );
            }

            THEN( "it starts with the font" )
            {
                REQUIRE( drawsIn( *kept.view, handedFont() ) );
            }

            THEN( "it starts with following not allowed" )
            {
                REQUIRE_FALSE( ViewAccess::followAllowed( *kept.view ) );
            }

            THEN( "it starts with the Color Labels" )
            {
                REQUIRE( ViewAccess::colorLabels( *kept.view ) == handedColorLabels() );
            }

            THEN( "it starts with the Search Limits" )
            {
                REQUIRE( ViewAccess::searchLimits( *kept.view ) == HandedSearchLimits );
            }
        }
    }
}

SCENARIO( "A Presentation added to the View Set starts with everything it holds", "[viewset]" )
{
    ViewSet viewSet;

    GIVEN( "a View Set handed every piece of state" )
    {
        handEverything( viewSet );

        WHEN( "a Presentation is added" )
        {
            RecordingPresentation presentation;
            viewSet.addPresentation( &presentation );

            THEN( "it starts with all of it" )
            {
                REQUIRE( presentation.decorationPolicy == handedDecorationPolicy() );
                REQUIRE( presentation.presentationPolicy == handedPresentationPolicy() );
                REQUIRE( presentation.quickFindPolicy == handedQuickFindPolicy() );
                REQUIRE( presentation.followAllowed == false );
                REQUIRE( presentation.font == handedFont() );
                REQUIRE( presentation.colorLabels == handedColorLabels() );
                REQUIRE( presentation.searchLimits == HandedSearchLimits );
            }
        }
    }

    GIVEN( "a View Set handed no font and no Search Limits" )
    {
        WHEN( "a Presentation is added" )
        {
            RecordingPresentation presentation;
            viewSet.addPresentation( &presentation );

            THEN( "it keeps its own font and Search Limits, and may follow" )
            {
                REQUIRE_FALSE( presentation.font.has_value() );
                REQUIRE_FALSE( presentation.searchLimits.has_value() );
                REQUIRE( presentation.followAllowed == true );
            }
        }
    }
}

SCENARIO( "What the View Set is handed reaches every view in it", "[viewset]" )
{
    LogFile logFile;
    ViewSet viewSet;

    GIVEN( "a View Set with both Presentations and the Filtered Views of two Searches" )
    {
        RecordingPresentation textView;
        RecordingPresentation tableView;
        auto current = logFile.newSearch();
        auto kept = logFile.newSearch();
        viewSet.addPresentation( &textView );
        viewSet.addPresentation( &tableView );
        viewSet.addFilteredView( kept.view.get() );
        viewSet.addFilteredView( current.view.get() );

        WHEN( "every piece of state is handed to it" )
        {
            handEverything( viewSet );

            THEN( "both Presentations show all of it" )
            {
                for ( const auto* presentation : { &textView, &tableView } ) {
                    REQUIRE( presentation->decorationPolicy == handedDecorationPolicy() );
                    REQUIRE( presentation->presentationPolicy == handedPresentationPolicy() );
                    REQUIRE( presentation->quickFindPolicy == handedQuickFindPolicy() );
                    REQUIRE( presentation->followAllowed == false );
                    REQUIRE( presentation->font == handedFont() );
                    REQUIRE( presentation->colorLabels == handedColorLabels() );
                    REQUIRE( presentation->searchLimits == HandedSearchLimits );
                }
            }

            THEN( "both Filtered Views show all of it, the kept Search's included" )
            {
                for ( const auto* view : { kept.view.get(), current.view.get() } ) {
                    REQUIRE( ViewAccess::decorationPolicy( *view ) == handedDecorationPolicy() );
                    REQUIRE( ViewAccess::presentationPolicy( *view )
                             == handedPresentationPolicy() );
                    REQUIRE( ViewAccess::lineNumbersVisible( *view ) );
                    REQUIRE_FALSE( ViewAccess::followAllowed( *view ) );
                    REQUIRE( drawsIn( *view, handedFont() ) );
                    REQUIRE( ViewAccess::colorLabels( *view ) == handedColorLabels() );
                    REQUIRE( ViewAccess::searchLimits( *view ) == HandedSearchLimits );
                }
            }
        }

        WHEN( "the Highlighter Sets change" )
        {
            viewSet.setColorLabels( handedColorLabels() );
            textView.colorLabels.reset();
            viewSet.applyHighlighterSetChange();

            THEN( "the Presentations repaint, handed the Color Labels again" )
            {
                REQUIRE( textView.decorationUpdates == 1 );
                REQUIRE( tableView.decorationUpdates == 1 );
                REQUIRE( textView.colorLabels == handedColorLabels() );
            }
        }

        WHEN( "the Log File's Decoding Policy is replaced" )
        {
            viewSet.rereadLogLines();

            THEN( "both Presentations read their Log Lines again" )
            {
                REQUIRE( textView.rereads == 1 );
                REQUIRE( tableView.rereads == 1 );
            }
        }

        WHEN( "the shortcuts are registered anew" )
        {
            viewSet.registerShortcuts();
            const auto keptShortcuts = shortcutsOf( *kept.view );
            const auto currentShortcuts = shortcutsOf( *current.view );

            THEN( "both Presentations register theirs" )
            {
                REQUIRE( textView.shortcutRegistrations == 1 );
                REQUIRE( tableView.shortcutRegistrations == 1 );
            }

            THEN( "both Filtered Views answer to them, the kept Search's included" )
            {
                REQUIRE_FALSE( keptShortcuts.empty() );
                REQUIRE_FALSE( currentShortcuts.empty() );
            }

            AND_WHEN( "they are registered anew once more" )
            {
                viewSet.registerShortcuts();
                QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );

                THEN( "the kept Search's Filtered View let the ones before go" )
                {
                    REQUIRE(
                        std::none_of( keptShortcuts.cbegin(), keptShortcuts.cend(),
                                      []( const auto& shortcut ) { return !shortcut.isNull(); } ) );
                    REQUIRE( shortcutsOf( *kept.view ).size() == keptShortcuts.size() );
                }
            }
        }

        WHEN( "the kept Search's tab is closed and state is handed on" )
        {
            kept.view.reset();
            viewSet.setSearchLimits( HandedSearchLimits.first, HandedSearchLimits.second );
            viewSet.setFont( handedFont() );

            THEN( "the Filtered View left still shows it" )
            {
                REQUIRE( ViewAccess::searchLimits( *current.view ) == HandedSearchLimits );
                REQUIRE( drawsIn( *current.view, handedFont() ) );
            }

            AND_WHEN( "another Search is kept" )
            {
                auto another = logFile.newSearch();
                viewSet.addFilteredView( another.view.get() );

                THEN( "its Filtered View starts with the Search Limits too" )
                {
                    REQUIRE( ViewAccess::searchLimits( *another.view ) == HandedSearchLimits );
                }
            }
        }
    }
}
