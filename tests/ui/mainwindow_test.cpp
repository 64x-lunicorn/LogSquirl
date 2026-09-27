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

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include <QToolBar>

#include "test_utils.h"

#include <algorithm>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryFile>

#include "applicationplugins.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "filteredview.h"
#include "log.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "optionsdialog.h"
#include "overviewwidget.h"
#include "pathline.h"
#include "quickfindwidget.h"
#include "session.h"
#include "sessioninfo.h"
#include "settingspolicies.h"
#include "test_policies.h"
#include "textencoding.h"

SCENARIO( "Main window tests", "[ui]" )
{
    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    WindowSession windowSession{ appSession, "Main", 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<SafeQSignalSpy> activateSpy;
    std::unique_ptr<SafeQSignalSpy> exitSpy;
    QTimer::singleShot( 0, [ & ] {
        LOG_INFO << "Initialize main window";
        mainWindow.reset( new MainWindow( windowSession, plugins ) );
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
// they must reach the re-derive the Options Dialog reaches (#192): the Session
// re-derives and reaches every open Log File (#245), where the signal mux
// would deliver to the active tab only.
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
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_toggle_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_toggle_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

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

namespace {

// Whether the QuickFind bar reads what the user types into it as a regexp: the
// bar says so with every pattern it reports.
bool quickFindBarReadsRegexp( QuickFindWidget& bar )
{
    auto* patternEdit = bar.findChild<QLineEdit*>();
    REQUIRE( patternEdit != nullptr );
    patternEdit->clear();

    // Only what the bar reports is looked at: no QuickFind is to run on the
    // Log File in front, which would outlive this check.
    QObject::disconnect( &bar, &QuickFindWidget::patternUpdated, nullptr, nullptr );

    QSignalSpy updated( &bar, &QuickFindWidget::patternUpdated );
    QTest::keyClicks( patternEdit, "abc" );
    REQUIRE( !updated.isEmpty() );
    return updated.last().at( 2 ).toBool();
}

} // namespace

// The QuickFind bar and the mux belong to the window, not to a Log File, so the
// window takes the QuickFind Policy from its session (#231) -- the same one
// however many Log Files are open, and whichever tab or Filtered View is in
// front. A changed QuickFind setting reaches the bar when the Options Dialog
// applies it, not only at the next tab switch.
SCENARIO( "A changed QuickFind setting reaches the window's QuickFind bar with several Log Files "
          "open",
          "[ui][settings]" )
{
    auto& config = Configuration::get();
    const auto quickfindRegexpType = config.quickfindRegexpType();
    config.setQuickfindRegexpType( SearchRegexpType::FixedString );

    auto appSession = std::make_shared<Session>( deriveSettingsPolicies( config ),
                                                 std::make_shared<LogFormatCatalog>() );
    WindowSession windowSession{ appSession, "Main", 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_quickfind_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_quickfind_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    mainWindow->loadFileNonInteractive( firstFile.fileName() );
    mainWindow->loadFileNonInteractive( secondFile.fileName() );

    std::vector<CrawlerWidget*> crawlers;
    REQUIRE( waitUiState( [ & ] {
        const auto found = mainWindow->findChildren<CrawlerWidget*>();
        crawlers.assign( found.cbegin(), found.cend() );
        return crawlers.size() == 2;
    } ) );

    auto* quickFindBar = mainWindow->findChild<QuickFindWidget*>();
    REQUIRE( quickFindBar != nullptr );
    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );

    GIVEN( "two Log Files open in their tabs, and a QuickFind reading a fixed string" )
    {
        REQUIRE_FALSE( quickFindBarReadsRegexp( *quickFindBar ) );

        WHEN( "QuickFind is set to read an extended regexp in the Options Dialog" )
        {
            // The dialog is modal: it is driven from inside its own event loop.
            QTimer dialogDriver;
            QObject::connect( &dialogDriver, &QTimer::timeout, [ & ] {
                auto* modal = QApplication::activeModalWidget();
                if ( auto* box = qobject_cast<QMessageBox*>( modal ) ) {
                    box->accept();
                }
                else if ( auto* dialog = qobject_cast<OptionsDialog*>( modal ) ) {
                    dialog->quickFindSearchBox->setCurrentIndex( 0 );
                    dialog->buttonBox->button( QDialogButtonBox::Ok )->click();
                }
            } );
            dialogDriver.start( 10 );

            auto* optionsAction
                = viewMenuAction( *mainWindow, logsquirl::mainwindow::action::optionsText );
            REQUIRE( optionsAction != nullptr );
            optionsAction->trigger();
            dialogDriver.stop();

            THEN( "the QuickFind bar reads an extended regexp, without a tab switch" )
            {
                REQUIRE( Configuration::get().quickfindRegexpType()
                         == SearchRegexpType::ExtendedRegexp );
                REQUIRE( quickFindBarReadsRegexp( *quickFindBar ) );
            }

            AND_WHEN( "the other tab is brought to the front" )
            {
                const auto current = tabArea->currentIndex();
                tabArea->setCurrentWidget(
                    tabArea->currentWidget() == crawlers[ 0 ] ? crawlers[ 1 ] : crawlers[ 0 ] );
                REQUIRE( tabArea->currentIndex() != current );

                THEN( "the QuickFind bar still reads an extended regexp" )
                {
                    REQUIRE( quickFindBarReadsRegexp( *quickFindBar ) );
                }

                AND_WHEN( "the Log File in front switches to another Filtered View" )
                {
                    Q_EMIT static_cast<CrawlerWidget*>( tabArea->currentWidget() )
                        ->filteredViewChanged();

                    THEN( "the QuickFind bar still reads an extended regexp" )
                    {
                        REQUIRE( quickFindBarReadsRegexp( *quickFindBar ) );
                    }
                }
            }
        }
    }

    mainWindow.reset();
    config.setQuickfindRegexpType( quickfindRegexpType );
    config.save();
}

struct MainWindowLoadAccess;

template <>
struct CrawlerWidget::access_by<MainWindowLoadAccess> {
    static OpenLogFile& openLogFile( CrawlerWidget& crawler )
    {
        return *crawler.openLogFile_;
    }
};

namespace {

using LoadAccess = CrawlerWidget::access_by<MainWindowLoadAccess>;

// Reloads the Log File of crawler with an Encoding the indexing fails on --
// no converter exists for its name -- and waits until the load has finished
// Failed. encoding must outlive the Log File, which holds on to it.
bool failToLoad( CrawlerWidget& crawler, const TextEncoding& encoding )
{
    auto& openLogFile = LoadAccess::openLogFile( crawler );
    bool failed = false;
    QObject context;
    QObject::connect( &openLogFile, &OpenLogFile::loadingFinished, &context,
                      [ &failed ]( const OpenLogFile::LoadFinished& load ) {
                          failed = load.status == LoadingStatus::Failed;
                      } );
    openLogFile.logData()->reload( &encoding );
    return waitUiState( [ &failed ] { return failed; }, 10000 );
}

} // namespace

// The window hears only the tab in front. A Log File that failed to load in a
// background tab keeps that status and replays it when its tab is shown, and
// the window treats it like a failure it heard live: it offers to report it
// and closes the tab (#540).
SCENARIO( "A Log File that failed to load in a background tab says so when its tab is shown",
          "[ui][loading]" )
{
    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    WindowSession windowSession{ appSession, "Main", 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    QTemporaryFile failingFile{ QDir::temp().filePath( "mainwindow_failing_XXXXXX" ) };
    QTemporaryFile otherFile{ QDir::temp().filePath( "mainwindow_loaded_XXXXXX" ) };
    for ( auto* file : { &failingFile, &otherFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }
    const auto failingName = QFileInfo( failingFile.fileName() ).fileName();

    // Outlives the Log Files, which hold on to the Encoding they were reloaded with.
    const TextEncoding unusableEncoding( -4242, "LogSquirl-Unusable-Encoding", std::nullopt );

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    // Every question the window asks is answered No, so that no issue is
    // opened in a browser; the ones asked are counted.
    int questionsAsked = 0;
    QTimer questionDriver;
    QObject::connect( &questionDriver, &QTimer::timeout, [ &questionsAsked ] {
        if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
            ++questionsAsked;
            box->reject();
        }
    } );
    questionDriver.start( 10 );

    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );
    const int baseTabCount = tabArea->count();
    auto* toolBar = mainWindow->findChild<QToolBar*>();
    REQUIRE( toolBar != nullptr );
    auto* infoLine = toolBar->findChild<PathLine*>();
    REQUIRE( infoLine != nullptr );

    mainWindow->loadFileNonInteractive( failingFile.fileName() );
    REQUIRE( waitUiState( [ & ] { return infoLine->text().contains( failingName ); }, 10000 ) );
    auto* failing = qobject_cast<CrawlerWidget*>( tabArea->currentWidget() );
    REQUIRE( failing != nullptr );

    GIVEN( "two Log Files, the one in the background failed to load" )
    {
        mainWindow->loadFileNonInteractive( otherFile.fileName() );
        REQUIRE( waitUiState(
            [ & ] {
                return tabArea->count() == baseTabCount + 2
                       && infoLine->text().contains( QFileInfo( otherFile ).fileName() );
            },
            10000 ) );
        REQUIRE( tabArea->currentWidget() != failing );

        REQUIRE( failToLoad( *failing, unusableEncoding ) );
        QTest::qWait( 100 );
        questionsAsked = 0;

        WHEN( "its tab is shown" )
        {
            tabArea->setCurrentWidget( failing );

            THEN( "the failure is offered to be reported and the tab is closed" )
            {
                REQUIRE( waitUiState(
                    [ & ] { return questionsAsked == 1 && tabArea->count() == baseTabCount + 1; },
                    10000 ) );
                REQUIRE( tabArea->indexOf( failing ) < 0 );

                AND_THEN( "the info line does not report the Log File as loaded" )
                {
                    REQUIRE_FALSE( infoLine->text().contains( failingName ) );
                }
            }
        }
    }

    GIVEN( "a Log File in front that fails to load" )
    {
        REQUIRE( failToLoad( *failing, unusableEncoding ) );

        THEN( "the failure is offered to be reported and the tab is closed" )
        {
            REQUIRE( waitUiState(
                [ & ] { return questionsAsked == 1 && tabArea->count() == baseTabCount; },
                10000 ) );
            REQUIRE_FALSE( infoLine->text().contains( failingName ) );
        }
    }

    questionDriver.stop();
    mainWindow.reset();
}

// A window restored from the Session shows the tab that was in front when the
// Session was saved, not the last one (#542).
SCENARIO( "A restored window shows the tab that was in front", "[ui][session]" )
{
    const auto windowId = QStringLiteral( "mainwindow_test_window_542" );

    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_restore_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_restore_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }

    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    // Stored after the Session was built, which reads the settings store.
    auto& stored = SessionInfo::get();
    stored.add( windowId );
    stored.setOpenFiles( windowId,
                         { { QFileInfo( firstFile ).absoluteFilePath(), 0, QString{} },
                           { QFileInfo( secondFile ).absoluteFilePath(), 0, QString{} } },
                         0 );
    WindowSession windowSession{ appSession, windowId, 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    WHEN( "the window's Session is restored with its first tab saved in front" )
    {
        mainWindow->reloadSession();

        THEN( "the first tab is in front" )
        {
            auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
            REQUIRE( tabArea != nullptr );
            REQUIRE( tabArea->findChildren<CrawlerWidget*>().size() == 2 );
            // The Log Files' tabs are the last two, in the order they were
            // saved; a Dashboard tab, if any, comes before them.
            REQUIRE( tabArea->currentIndex() == tabArea->count() - 2 );
        }
    }

    mainWindow.reset();
    // Leave the in-memory Session info as the settings store has it.
    SessionInfo::getSynced();
}
