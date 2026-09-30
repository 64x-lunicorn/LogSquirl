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

// The Search Line widget on its own (#638): a QApplication, no Log File, no
// index thread. It mirrors the Search Line in its buttons and info line, and
// exchanges values and signals with whoever holds it -- here the test.

#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QLineEdit>
#include <QShortcut>
#include <QSignalSpy>
#include <QTest>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "searchlinewidget.h"
#include "searchlinewidget_access.h"
#include "settingspolicies.h"
#include "shortcuts.h"
#include "theme.h"
#include "theme_lists.h"

namespace {

using Flags = SearchLine::Flags;
using Phase = SearchSession::Phase;

// Every button the other way round from the shipped defaults, so a widget
// that read the settings store would start otherwise.
QuickFindPolicy unusualPolicy()
{
    QuickFindPolicy policy;
    policy.searchIgnoreCaseDefault = true;
    policy.mainRegexpType = SearchRegexpType::FixedString;
    policy.searchLogicalCombiningDefault = true;
    policy.searchAutoRefreshDefault = true;
    policy.autoRunSearchOnPatternChange = false;
    return policy;
}

// What the five flag buttons show.
Flags buttonFlags( const SearchLineWidget& line )
{
    return { .matchCase = SearchLineAccess::matchCaseButton( line )->isChecked(),
             .useRegexp = SearchLineAccess::useRegexpButton( line )->isChecked(),
             .inverse = SearchLineAccess::inverseButton( line )->isChecked(),
             .booleanCombination = SearchLineAccess::booleanButton( line )->isChecked(),
             .autoRefresh = SearchLineAccess::autoRefreshButton( line )->isChecked() };
}

// Which of Search, Stop and Clear show.
struct Buttons {
    bool search;
    bool stop;
    bool clear;

    bool operator==( const Buttons& ) const = default;
};

Buttons shownButtons( const SearchLineWidget& line )
{
    return { .search = !SearchLineAccess::searchButton( line )->isHidden(),
             .stop = !SearchLineAccess::stopButton( line )->isHidden(),
             .clear = !SearchLineAccess::clearButton( line )->isHidden() };
}

constexpr Buttons NoSearchRuns{ .search = true, .stop = false, .clear = true };
constexpr Buttons SearchRuns{ .search = false, .stop = true, .clear = false };

SearchSession::State sessionState( Phase phase, int progress = 0, LinesCount matches = 0_lcount )
{
    SearchSession::State state;
    state.phase = phase;
    state.progress = progress;
    state.matchCount = matches;
    if ( phase == Phase::InvalidPattern ) {
        state.errorString = "missing )";
    }
    return state;
}

// The flags a flagsChanged() carried: the new ones and the previous ones.
std::pair<Flags, Flags> changedFlags( const QSignalSpy& spy, qsizetype index )
{
    return { spy.at( index ).at( 0 ).value<Flags>(), spy.at( index ).at( 1 ).value<Flags>() };
}

} // namespace

SCENARIO( "The Search Line widget mirrors the Search Line in its buttons", "[ui][searchline]" )
{
    SearchLineWidget line{ unusualPolicy(), {} };
    QSignalSpy flagsChanged( &line, &SearchLineWidget::flagsChanged );

    const Flags starting{ .matchCase = false,
                          .useRegexp = false,
                          .inverse = false,
                          .booleanCombination = true,
                          .autoRefresh = true };

    THEN( "the buttons start as the QuickFind Policy says, and nothing is told" )
    {
        REQUIRE( buttonFlags( line ) == starting );
        REQUIRE( line.flags() == starting );
        REQUIRE( flagsChanged.isEmpty() );
    }

    WHEN( "the user clicks the Match case button" )
    {
        QTest::mouseClick( SearchLineAccess::matchCaseButton( line ), Qt::LeftButton );

        THEN( "the Search Line matches case, and the change is told once" )
        {
            auto expected = starting;
            expected.matchCase = true;
            REQUIRE( line.flags() == expected );
            REQUIRE( line.request().isCaseSensitive );
            REQUIRE( flagsChanged.count() == 1 );
            REQUIRE( changedFlags( flagsChanged, 0 ) == std::pair{ expected, starting } );
        }
    }

    WHEN( "the flags are set, every one of them changed" )
    {
        const Flags set{ .matchCase = true,
                         .useRegexp = true,
                         .inverse = true,
                         .booleanCombination = false,
                         .autoRefresh = false };
        line.setFlags( set );

        THEN( "the buttons show them, and the change is told exactly once" )
        {
            REQUIRE( buttonFlags( line ) == set );
            REQUIRE( line.flags() == set );
            REQUIRE( flagsChanged.count() == 1 );
            REQUIRE( changedFlags( flagsChanged, 0 ) == std::pair{ set, starting } );
        }
    }

    WHEN( "the flags are set to what they are" )
    {
        line.setFlags( starting );

        THEN( "nothing changed, and nothing is told" )
        {
            REQUIRE( flagsChanged.isEmpty() );
        }
    }
}

SCENARIO( "The Search Line widget keeps the user's buttons when a later QuickFind Policy arrives",
          "[ui][searchline][settings]" )
{
    SearchLineWidget line{ unusualPolicy(), {} };

    GIVEN( "the user set the buttons away from the Policy the line started in" )
    {
        QTest::mouseClick( SearchLineAccess::matchCaseButton( line ), Qt::LeftButton );
        QTest::mouseClick( SearchLineAccess::autoRefreshButton( line ), Qt::LeftButton );
        QTest::mouseClick( SearchLineAccess::booleanButton( line ), Qt::LeftButton );
        const auto set = buttonFlags( line );
        REQUIRE( set.matchCase );
        REQUIRE_FALSE( set.autoRefresh );
        REQUIRE_FALSE( set.booleanCombination );

        WHEN( "a Policy with the same defaults, but auto-run on, arrives" )
        {
            QSignalSpy flagsChanged( &line, &SearchLineWidget::flagsChanged );
            auto changed = unusualPolicy();
            changed.autoRunSearchOnPatternChange = true;
            line.setQuickFindPolicy( changed );

            THEN( "the buttons keep what the user set" )
            {
                REQUIRE( buttonFlags( line ) == set );
                REQUIRE( line.flags() == set );
                REQUIRE( flagsChanged.isEmpty() );
            }

            THEN( "an edited pattern now answers that the Search is to run" )
            {
                REQUIRE( line.replace( "word" ) );
            }
        }
    }
}

SCENARIO( "The Search Line widget shows Search, Stop and Clear as the Search runs",
          "[ui][searchline]" )
{
    SearchLineWidget line{ QuickFindPolicy{}, {} };
    line.show();
    QCoreApplication::processEvents();

    THEN( "no Search runs at first" )
    {
        REQUIRE( shownButtons( line ) == NoSearchRuns );
        REQUIRE( SearchLineAccess::infoLine( line )->isHidden() );
    }

    WHEN( "a Search is requested" )
    {
        line.requested( sessionState( Phase::Running ) );

        THEN( "only Stop shows" )
        {
            REQUIRE( shownButtons( line ) == SearchRuns );
            REQUIRE( SearchLineAccess::stopButton( line )->isEnabled() );
        }

        AND_WHEN( "it progresses" )
        {
            line.progressed( sessionState( Phase::Running, 40, 3_lcount ),
                             SearchAutoRefresh::State::Static );

            THEN( "Stop still shows, and the line says how far it came" )
            {
                REQUIRE( shownButtons( line ) == SearchRuns );
                REQUIRE( SearchLineAccess::infoLine( line )->text().contains( "40" ) );
            }
        }

        AND_WHEN( "it completes" )
        {
            line.progressed( sessionState( Phase::Complete, 100, 3_lcount ),
                             SearchAutoRefresh::State::Static );

            THEN( "Search and Clear come back, and the line says the Matches" )
            {
                REQUIRE( shownButtons( line ) == NoSearchRuns );
                REQUIRE( SearchLineAccess::infoLine( line )->text() == "3 matches found" );
            }
        }

        AND_WHEN( "the user stops it" )
        {
            QSignalSpy stopRequested( &line, &SearchLineWidget::stopRequested );
            QTest::mouseClick( SearchLineAccess::stopButton( line ), Qt::LeftButton );
            REQUIRE( stopRequested.count() == 1 );
            // Whoever stops the Search tells the line.
            line.stopped( SearchAutoRefresh::State::Static, 2_lcount );

            THEN( "Search and Clear come back, with the Matches found until then" )
            {
                REQUIRE( shownButtons( line ) == NoSearchRuns );
                REQUIRE( SearchLineAccess::infoLine( line )->text() == "2 matches found" );
            }
        }

        AND_WHEN( "it is replaced by none" )
        {
            line.cleared();

            THEN( "the line says nothing" )
            {
                REQUIRE( SearchLineAccess::infoLine( line )->isHidden() );
            }
        }
    }

    WHEN( "a Search with an invalid pattern is requested" )
    {
        line.requested( sessionState( Phase::InvalidPattern ) );

        THEN( "Search and Clear still show, and the line says the error" )
        {
            REQUIRE( shownButtons( line ) == NoSearchRuns );
            REQUIRE(
                SearchLineAccess::infoLine( line )->text().startsWith( "Error in expression" ) );
            REQUIRE( line.display().isError );
        }
    }
}

SCENARIO( "The Search Line widget shows an error in the error colors of every Theme",
          "[ui][searchline][theme]" )
{
    Theme::apply( Theme::LightKey );

    SearchLineWidget line{ QuickFindPolicy{}, {} };
    line.resize( 900, 40 );
    line.show();
    QCoreApplication::processEvents();

    // The error background fills the line behind its text.
    const auto background = [ & ] {
        const auto image = SearchLineAccess::infoLine( line )->grab().toImage().convertToFormat(
            QImage::Format_RGB32 );
        return image.pixelColor( image.width() - 4, image.height() / 2 ).rgb();
    };

    GIVEN( "a Search whose pattern is in error" )
    {
        line.requested( sessionState( Phase::InvalidPattern ) );
        QCoreApplication::processEvents();

        THEN( "the info line shows it in the Light Theme's error background" )
        {
            REQUIRE( background() == Theme::active().color( ColorToken::ErrorBackground ).rgb() );
        }

        for ( const auto& name : themeSwitchesFrom( Theme::LightKey ) ) {
            WHEN( "the " << name.toStdString() << " Theme is applied" )
            {
                Theme::apply( name );
                QCoreApplication::processEvents();

                THEN( "the error is shown in that Theme's error background" )
                {
                    REQUIRE( background()
                             == Theme::active().color( ColorToken::ErrorBackground ).rgb() );
                }
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "Return in the Search Line widget asks for the Search and says Keep Results",
          "[ui][searchline]" )
{
    SearchLineWidget line{ QuickFindPolicy{}, { "earlier" } };
    line.show();
    QCoreApplication::processEvents();
    QSignalSpy searchRequested( &line, &SearchLineWidget::searchRequested );

    THEN( "the line starts with the latest Search of the history" )
    {
        REQUIRE( line.pattern() == "earlier" );
    }

    GIVEN( "a pattern typed, Keep Results set" )
    {
        auto* edit = SearchLineAccess::patternEdit( line );
        edit->clearEditText();
        QTest::keyClicks( edit, "needle" );
        REQUIRE( line.pattern() == "needle" );
        SearchLineAccess::keepResultsButton( line )->setChecked( true );

        WHEN( "Return is pressed" )
        {
            QTest::keyClick( edit, Qt::Key_Return );

            // Once: the combo box, which has the focus, hands Return to its
            // line edit, and the press that comes back is not taken again
            // (#648).
            THEN( "the Search is asked for once, keeping the results, and Keep Results is off" )
            {
                REQUIRE( searchRequested.count() == 1 );
                REQUIRE( searchRequested.at( 0 ).at( 0 ).toBool() );
                REQUIRE_FALSE( SearchLineAccess::keepResultsButton( line )->isChecked() );
                REQUIRE( line.request().pattern == "needle" );
            }

            AND_WHEN( "Return is pressed again" )
            {
                QTest::keyClick( edit, Qt::Key_Return );

                THEN( "that Search is asked for once and keeps nothing" )
                {
                    REQUIRE( searchRequested.count() == 2 );
                    REQUIRE_FALSE( searchRequested.at( 1 ).at( 0 ).toBool() );
                }
            }
        }
    }
}

SCENARIO( "Return reaches the Search Line widget the way the keyboard focus sends it",
          "[ui][searchline]" )
{
    SearchLineWidget line{ QuickFindPolicy{}, {} };
    line.show();
    line.activateWindow();
    auto* edit = SearchLineAccess::patternEdit( line );
    edit->setFocus();
    QCoreApplication::processEvents();
    QSignalSpy searchRequested( &line, &SearchLineWidget::searchRequested );
    QTest::keyClicks( edit, "needle" );

    WHEN( "Return is pressed on the widget that has the focus" )
    {
        auto* focused = QApplication::focusWidget();
        REQUIRE( focused != nullptr );
        QTest::keyClick( focused, Qt::Key_Return );

        // The combo box has the focus and hands the key to its line edit;
        // the only Return must get through, once (#648).
        THEN( "the Search is asked for once" )
        {
            REQUIRE( searchRequested.count() == 1 );
        }
    }

    WHEN( "Return is pressed twice on the widget that has the focus" )
    {
        QTest::keyClick( QApplication::focusWidget(), Qt::Key_Return );
        QTest::keyClick( QApplication::focusWidget(), Qt::Key_Return );

        THEN( "the Search is asked for twice" )
        {
            REQUIRE( searchRequested.count() == 2 );
        }
    }
}

SCENARIO( "The shortcuts of the Search Line widget toggle their buttons",
          "[ui][searchline][shortcuts]" )
{
    // A widget around the line, as the Crawler Widget is: the shortcuts answer
    // wherever the focus is in it.
    QWidget scope;
    auto* line = new SearchLineWidget( QuickFindPolicy{}, {}, &scope );

    const auto [ action, key ] = GENERATE( table<std::string, QString>( {
        { ShortcutAction::CrawlerEnableCaseMatching, "Ctrl+Alt+Shift+F1" },
        { ShortcutAction::CrawlerEnableRegex, "Ctrl+Alt+Shift+F2" },
        { ShortcutAction::CrawlerEnableInverseMatching, "Ctrl+Alt+Shift+F3" },
        { ShortcutAction::CrawlerEnableRegexCombining, "Ctrl+Alt+Shift+F4" },
        { ShortcutAction::CrawlerEnableAutoRefresh, "Ctrl+Alt+Shift+F5" },
        { ShortcutAction::CrawlerKeepResults, "Ctrl+Alt+Shift+F6" },
    } ) );

    const std::vector<std::pair<std::string, QToolButton*>> buttons{
        { ShortcutAction::CrawlerEnableCaseMatching, SearchLineAccess::matchCaseButton( *line ) },
        { ShortcutAction::CrawlerEnableRegex, SearchLineAccess::useRegexpButton( *line ) },
        { ShortcutAction::CrawlerEnableInverseMatching, SearchLineAccess::inverseButton( *line ) },
        { ShortcutAction::CrawlerEnableRegexCombining, SearchLineAccess::booleanButton( *line ) },
        { ShortcutAction::CrawlerEnableAutoRefresh, SearchLineAccess::autoRefreshButton( *line ) },
        { ShortcutAction::CrawlerKeepResults, SearchLineAccess::keepResultsButton( *line ) },
    };

    ShortcutAction::ConfiguredShortcuts configured;
    configured[ ShortcutAction::CrawlerEnableCaseMatching ] = { "Ctrl+Alt+Shift+F1" };
    configured[ ShortcutAction::CrawlerEnableRegex ] = { "Ctrl+Alt+Shift+F2" };
    configured[ ShortcutAction::CrawlerEnableInverseMatching ] = { "Ctrl+Alt+Shift+F3" };
    configured[ ShortcutAction::CrawlerEnableRegexCombining ] = { "Ctrl+Alt+Shift+F4" };
    configured[ ShortcutAction::CrawlerEnableAutoRefresh ] = { "Ctrl+Alt+Shift+F5" };
    configured[ ShortcutAction::CrawlerKeepResults ] = { "Ctrl+Alt+Shift+F6" };
    line->registerShortcuts( configured, &scope );

    // Registered anew, the ones before go.
    line->registerShortcuts( configured, &scope );
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );

    GIVEN( "the shortcuts registered in the scope" )
    {
        QShortcut* pressed = nullptr;
        int registered = 0;
        for ( auto* shortcut : scope.findChildren<QShortcut*>() ) {
            ++registered;
            if ( shortcut->key() == QKeySequence( key ) ) {
                pressed = shortcut;
            }
        }
        REQUIRE( registered == 6 );
        REQUIRE( pressed != nullptr );
        REQUIRE( pressed->context() == Qt::WidgetWithChildrenShortcut );

        std::vector<bool> before;
        for ( const auto& [ name, button ] : buttons ) {
            before.push_back( button->isChecked() );
        }

        DYNAMIC_SECTION( "When: the shortcut of " << action << " is pressed" )
        {
            Q_EMIT pressed->activated();
            QCoreApplication::processEvents();

            THEN( "its button toggles, and no other" )
            {
                for ( auto index = 0u; index < buttons.size(); ++index ) {
                    const auto& [ name, button ] = buttons[ index ];
                    INFO( name );
                    REQUIRE( button->isChecked()
                             == ( name == action ? !before[ index ] : before[ index ] ) );
                }
            }
        }
    }
}

SCENARIO( "An edited pattern sets the buttons of the Search Line widget before its text",
          "[ui][searchline][search]" )
{
    auto policy = QuickFindPolicy{};
    policy.mainRegexpType = SearchRegexpType::FixedString;
    policy.searchLogicalCombiningDefault = false;
    policy.autoRunSearchOnPatternChange = true;

    SearchLineWidget line{ policy, {} };
    line.show();
    QCoreApplication::processEvents();

    GIVEN( "a plain Search for beta" )
    {
        SearchLineAccess::patternEdit( line )->setEditText( "beta" );
        REQUIRE_FALSE( line.flags().booleanCombination );

        std::vector<std::string> shown;
        QObject::connect(
            SearchLineAccess::booleanButton( line ), &QToolButton::toggled, &line,
            [ & ]( bool on ) { shown.push_back( on ? "button on" : "button off" ); } );
        QObject::connect( SearchLineAccess::patternEdit( line ), &QComboBox::editTextChanged, &line,
                          [ & ]( const QString& ) { shown.push_back( "text" ); } );
        QSignalSpy flagsChanged( &line, &SearchLineWidget::flagsChanged );
        QSignalSpy searchRequested( &line, &SearchLineWidget::searchRequested );

        WHEN( "a word is added to it" )
        {
            const bool runNow = line.add( "alpha" );

            THEN( "the logical combination button is set first, then the pattern shown" )
            {
                REQUIRE( shown == std::vector<std::string>{ "button on", "text" } );
                REQUIRE( line.pattern() == R"("beta" or "alpha")" );
                REQUIRE( SearchLineAccess::patternEdit( line )->currentText() == line.pattern() );
            }

            THEN( "the change of the flags is told once" )
            {
                REQUIRE( flagsChanged.count() == 1 );
                REQUIRE( changedFlags( flagsChanged, 0 ).first.booleanCombination );
            }

            THEN( "the Search is to run, but the line does not ask for it by itself" )
            {
                REQUIRE( runNow );
                QCoreApplication::processEvents();
                REQUIRE( searchRequested.isEmpty() );
            }
        }
    }
}
