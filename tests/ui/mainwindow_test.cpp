/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch.hpp>

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include <QToolBar>

#include "test_utils.h"

#include <algorithm>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QTemporaryFile>

#include "configuration.h"
#include "crawlerwidget.h"
#include "filteredview.h"
#include "log.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "overviewwidget.h"
#include "session.h"
#include "settingspolicies.h"
#include "test_policies.h"

SCENARIO( "Main window tests", "[ui]" )
{
    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    WindowSession windowSession{ appSession, "Main", 0 };

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<SafeQSignalSpy> activateSpy;
    std::unique_ptr<SafeQSignalSpy> exitSpy;
    QTimer::singleShot( 0, [ & ] {
        LOG_INFO << "Initialize main window";
        mainWindow.reset( new MainWindow( windowSession ) );
        exitSpy.reset( new SafeQSignalSpy( mainWindow.get(), SIGNAL( exitRequested() ) ) );
        activateSpy.reset( new SafeQSignalSpy( mainWindow.get(), SIGNAL( windowActivated() ) ) );
    } );

    QTest::qWait( 100 );
    mainWindow->show();
    QTest::qWait( 100 );
    REQUIRE( activateSpy->safeWait() );

    auto runInUiThread = [ uiObject = mainWindow.get() ]( auto&& func ) {
        QTimer::singleShot( 0, Qt::VeryCoarseTimer, uiObject,
                            std::forward<decltype( func )>( func ) );
        QTest::qWait( 100 );
    };

    GIVEN( "Opened main window" )
    {
        auto toolBar = mainWindow->findChild<QToolBar*>();
        REQUIRE( toolBar != nullptr );

        auto filePathLabel = toolBar->findChild<PathLine*>();
        REQUIRE( filePathLabel != nullptr );

        auto tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabArea != nullptr );

        // The welcome dashboard occupies a permanent pinned tab at index 0
        // when enabled (default). Record the baseline so file-tab assertions
        // are independent of that setting.
        const int baseTabCount = tabArea->count();

        THEN( "Has no tabs" )
        {
            REQUIRE( tabArea->count() == baseTabCount );
            AND_THEN( "Path label empty" )
            {
                REQUIRE( filePathLabel->text().isEmpty() );
            }
        }

        WHEN( "Exit hotkey pressed" )
        {
            runInUiThread( [ &mainWindow ] {
                LOG_INFO << "ExitFromMainMenu";
                QTest::keyPress( mainWindow.get(), Qt::Key_Q, Qt::ControlModifier );
            } );

            THEN( "Exit signalled" )
            {
                REQUIRE( exitSpy->safeWait() );
            }
        }

        WHEN( "Load file" )
        {
            runInUiThread( [ &mainWindow ] {
                LOG_INFO << "Load file";
                mainWindow->loadInitialFile( "logsquirl.conf", false );
            } );

            THEN( "Path line has file name" )
            {
                REQUIRE( waitUiState(
                    [ & ] { return filePathLabel->text().contains( "logsquirl.conf" ); } ) );

                AND_THEN( "Has one tab" )
                {
                    REQUIRE(
                        waitUiState( [ & ] { return tabArea->count() == baseTabCount + 1; } ) );
                }
            }

            AND_WHEN( "Close tab hotkey pressed" )
            {
                runInUiThread( [ &mainWindow ] {
                    LOG_INFO << "Close tab";
                    QTest::keyPress( mainWindow.get(), Qt::Key_W, Qt::ControlModifier );
                } );

                THEN( "Has no tabs" )
                {
                    REQUIRE( waitUiState( [ & ] { return tabArea->count() == baseTabCount; } ) );

                    AND_THEN( "Path label empty" )
                    {
                        REQUIRE( waitUiState( [ & ] { return filePathLabel->text().isEmpty(); } ) );
                    }
                }
            }
        }
    }
}
namespace {

// Whether every open Log File shows what the View menu says: line numbers in
// its main view as main, in its Filtered Views as filtered, and the overview
// beside its main view as overview.
bool everyLogFileShows( const std::vector<CrawlerWidget*>& crawlers, bool main, bool filtered,
                        bool overview )
{
    return std::ranges::all_of( crawlers, [ = ]( const CrawlerWidget* crawler ) {
        const auto* mainView = crawler->findChild<LogMainView*>();
        const auto* overviewWidget
            = mainView != nullptr ? mainView->findChild<OverviewWidget*>() : nullptr;
        const auto filteredViews = crawler->findChildren<FilteredView*>();
        return overviewWidget != nullptr && !filteredViews.isEmpty()
               && mainView->viewportLayout().input().lineNumbersVisible == main
               && overviewWidget->isHidden() == !overview
               && std::ranges::all_of( filteredViews, [ filtered ]( const FilteredView* view ) {
                      return view->viewportLayout().input().lineNumbersVisible == filtered;
                  } );
    } );
}

QAction* viewMenuAction( const MainWindow& window, const char* text )
{
    const auto actions = window.findChildren<QAction*>();
    const auto found = std::ranges::find_if( actions, [ text ]( const QAction* action ) {
        return action->text() == QApplication::translate( "logsquirl::mainwindow::action", text );
    } );
    return found == actions.end() ? nullptr : *found;
}

} // namespace

// The View menu's toggles write a setting the Presentation Policy names, so
// they must reach the re-derive the Options Dialog reaches (#192): the signal
// mux delivers what it carries to the active tab only, which would leave
// every other open Log File showing the old setting.
SCENARIO( "Toggling line numbers or the overview from the View menu reaches every open Log File",
          "[ui][settings]" )
{
    auto& config = Configuration::get();
    const auto mainLineNumbersVisible = config.mainLineNumbersVisible();
    const auto filteredLineNumbersVisible = config.filteredLineNumbersVisible();
    const auto overviewVisible = config.isOverviewVisible();
    config.setMainLineNumbersVisible( false );
    config.setFilteredLineNumbersVisible( true );
    config.setOverviewVisible( true );

    auto appSession = std::make_shared<Session>( deriveSettingsPolicies( config ),
                                                 std::make_shared<LogFormatCatalog>() );
    WindowSession windowSession{ appSession, "Main", 0 };

    QTemporaryFile firstFile{ "mainwindow_toggle_first_XXXXXX" };
    QTemporaryFile secondFile{ "mainwindow_toggle_second_XXXXXX" };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0, [ & ] { mainWindow.reset( new MainWindow( windowSession ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    // What the application does with the signal: re-derive the Policies from
    // the settings store and hand the changed axes to every open Log File.
    QObject::connect( mainWindow.get(), &MainWindow::settingsChanged, [ &appSession ]() {
        appSession->applyPolicies( deriveSettingsPolicies( Configuration::get() ) );
    } );

    mainWindow->loadFileNonInteractive( firstFile.fileName() );
    mainWindow->loadFileNonInteractive( secondFile.fileName() );

    std::vector<CrawlerWidget*> crawlers;
    REQUIRE( waitUiState( [ & ] {
        const auto found = mainWindow->findChildren<CrawlerWidget*>();
        crawlers.assign( found.cbegin(), found.cend() );
        return crawlers.size() == 2;
    } ) );
    REQUIRE( waitUiState( [ & ] { return everyLogFileShows( crawlers, false, true, true ); } ) );

    GIVEN( "two Log Files open in their tabs, the second one current" )
    {
        WHEN( "line numbers in the main view are ticked in the View menu" )
        {
            auto* action = viewMenuAction(
                *mainWindow, logsquirl::mainwindow::action::lineNumbersVisibleInMainText );
            REQUIRE( action != nullptr );
            action->setChecked( true );

            THEN( "the main view of both Log Files draws them, not only the current one" )
            {
                REQUIRE( waitUiState(
                    [ & ] { return everyLogFileShows( crawlers, true, true, true ); } ) );
            }
        }

        WHEN( "line numbers in the Filtered View are unticked in the View menu" )
        {
            auto* action = viewMenuAction(
                *mainWindow, logsquirl::mainwindow::action::lineNumbersVisibleInFilteredText );
            REQUIRE( action != nullptr );
            action->setChecked( false );

            THEN( "the Filtered Views of both Log Files draw none" )
            {
                REQUIRE( waitUiState(
                    [ & ] { return everyLogFileShows( crawlers, false, false, true ); } ) );
            }
        }

        WHEN( "the overview is unticked in the View menu" )
        {
            auto* action
                = viewMenuAction( *mainWindow, logsquirl::mainwindow::action::overviewVisibleText );
            REQUIRE( action != nullptr );
            action->setChecked( false );

            THEN( "neither Log File shows its overview" )
            {
                REQUIRE( waitUiState(
                    [ & ] { return everyLogFileShows( crawlers, false, true, false ); } ) );
            }
        }
    }

    mainWindow.reset();
    config.setMainLineNumbersVisible( mainLineNumbersVisible );
    config.setFilteredLineNumbersVisible( filteredLineNumbersVisible );
    config.setOverviewVisible( overviewVisible );
    config.save();
}
