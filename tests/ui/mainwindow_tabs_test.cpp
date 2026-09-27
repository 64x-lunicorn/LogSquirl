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

// The main window's tabs: which of them hold a Log File, whether or not the
// window shows the Dashboard (#535), the merged Log File whose rebuild ends
// with its tab (#537), and the Dashboard setting, which reaches the windows
// opened after it changes (#562).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <optional>

#include "applicationplugins.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "mergecontroller.h"
#include "openlogfile.h"
#include "optionsdialog.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "tabgroupinfo.h"
#include "test_policies.h"
#include "test_utils.h"
#include "welcomedashboard.h"

// What the merge scenario reads from a Crawler Widget beyond its public face:
// how many Log Lines its Log File holds.
struct MainWindowTabsAccess {};

template <>
struct CrawlerWidget::access_by<MainWindowTabsAccess> {
    CrawlerWidget& crawler;

    LinesCount nbLines() const
    {
        return crawler.openLogFile_->logData()->getNbLine();
    }
};

namespace {

constexpr int UiTimeoutMs = 10'000;

bool writeLines( const QString& path, const QByteArray& lines, QIODevice::OpenMode mode )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | mode ) ) {
        return false;
    }
    return file.write( lines ) == lines.size();
}

// A main window over a Session of its own, shown and active. It is built with
// the Dashboard on or off in the settings when one is given, else as the
// settings have it.
struct TabsWindow {
    explicit TabsWindow( std::optional<bool> showDashboard = {} )
        : previousShowDashboard( Configuration::get().showDashboard() )
        , session( std::make_shared<Session>( testSettingsPolicies(),
                                              std::make_shared<LogFormatCatalog>() ) )
        , plugins( std::make_shared<logsquirl::plugins::ApplicationPlugins>() )
    {
        if ( showDashboard.has_value() ) {
            Configuration::get().setShowDashboard( *showDashboard );
        }
        mainWindow = std::make_unique<MainWindow>( WindowSession{ session, "Main", 0 }, plugins );
        mainWindow->show();
        mainWindow->activateWindow();
        REQUIRE( QTest::qWaitForWindowActive( mainWindow.get(), 5000 ) );
        tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabArea != nullptr );
    }

    ~TabsWindow()
    {
        mainWindow.reset();
        Configuration::get().setShowDashboard( previousShowDashboard );
        QTest::qWait( 50 );
    }

    TabsWindow( const TabsWindow& ) = delete;
    TabsWindow& operator=( const TabsWindow& ) = delete;

    // Opens these files, each in its tab, and returns the paths the tabs
    // hold them under, in tab order.
    QStringList open( const QStringList& files )
    {
        for ( const auto& file : files ) {
            mainWindow->loadFileNonInteractive( file );
        }
        REQUIRE( waitUiState( [ & ] { return logFileTabPaths().size() == files.size(); },
                              UiTimeoutMs ) );
        return logFileTabPaths();
    }

    // The paths of the tabs that hold a Log File, in tab order.
    QStringList logFileTabPaths() const
    {
        QStringList paths;
        for ( int i = 0; i < tabArea->count(); ++i ) {
            if ( qobject_cast<CrawlerWidget*>( tabArea->widget( i ) ) != nullptr ) {
                paths.append( QDir::fromNativeSeparators( tabArea->tabToolTip( i ) ) );
            }
        }
        return paths;
    }

    bool showsDashboard() const
    {
        for ( int i = 0; i < tabArea->count(); ++i ) {
            if ( qobject_cast<WelcomeDashboard*>( tabArea->widget( i ) ) != nullptr ) {
                return true;
            }
        }
        return false;
    }

    // Whether the Session lists none of these Log Files as open.
    bool noneOpenInSession( const QStringList& paths ) const
    {
        return std::ranges::none_of(
            paths, [ this ]( const QString& path ) { return session->getViewIfOpen( path ); } );
    }

    // Chooses the entry with this text from the context menu of a tab, the
    // way a right click on the tab opens it. Returns whether the menu showed
    // and had the entry.
    bool chooseFromTabMenu( int tab, const char* text )
    {
        auto* tabBar = qobject_cast<CrawlerTabBar*>( tabArea->tabBar() );
        REQUIRE( tabBar != nullptr );

        const auto entry = QApplication::translate( "TabbedCrawlerWidget", text );
        bool chosen = false;
        // The menu is modal: it is driven from inside its own event loop.
        QTimer::singleShot( 0, tabArea, [ &chosen, &entry ] {
            auto* menu = qobject_cast<QMenu*>( QApplication::activePopupWidget() );
            if ( menu == nullptr ) {
                return;
            }
            // The entries of the submenus are the menu's children too.
            for ( auto* action : menu->findChildren<QAction*>() ) {
                if ( action->text() == entry && action->isEnabled() ) {
                    action->trigger();
                    chosen = true;
                    break;
                }
            }
            menu->close();
        } );
        Q_EMIT tabBar->showTabContextMenu( tab,
                                           tabBar->mapToGlobal( tabBar->tabRect( tab ).center() ) );
        QTest::qWait( 50 );
        return chosen;
    }

    bool previousShowDashboard;
    std::shared_ptr<Session> session;
    std::shared_ptr<logsquirl::plugins::ApplicationPlugins> plugins;
    std::unique_ptr<MainWindow> mainWindow;
    TabbedCrawlerWidget* tabArea = nullptr;
};

QAction* fileMenuAction( const MainWindow& window, const char* text )
{
    const auto translated = QApplication::translate( "logsquirl::mainwindow::action", text );
    for ( auto* action : window.findChildren<QAction*>() ) {
        if ( action->text() == translated ) {
            return action;
        }
    }
    return nullptr;
}

// Three small Log Files in a directory of their own.
struct ThreeLogFiles {
    ThreeLogFiles()
    {
        REQUIRE( dir.isValid() );
        for ( const auto* name : { "first.log", "second.log", "third.log" } ) {
            const auto path = dir.filePath( QString::fromLatin1( name ) );
            REQUIRE( writeLines( path, "one Log Line\nanother Log Line\n", QIODevice::Truncate ) );
            paths.append( path );
        }
    }

    QTemporaryDir dir;
    QStringList paths;
};

} // namespace

// With the Dashboard off the first tab holds a Log File, and every path that
// closes tabs in bulk reaches it; with the Dashboard on, none closes the
// Dashboard (#535).
SCENARIO( "Every bulk close reaches the first Log File whether or not the window shows the "
          "Dashboard",
          "[ui][tabs]" )
{
    const bool showDashboard = GENERATE( true, false );
    CAPTURE( showDashboard );

    ThreeLogFiles files;
    TabsWindow window( showDashboard );
    REQUIRE( window.showsDashboard() == showDashboard );
    const auto dashboardTabs = showDashboard ? 1 : 0;

    GIVEN( "three Log Files open in their tabs" )
    {
        const auto paths = window.open( files.paths );
        REQUIRE( window.tabArea->count() == dashboardTabs + 3 );
        const auto firstLogFileTab = dashboardTabs;
        const auto lastLogFileTab = dashboardTabs + 2;

        WHEN( "Close all is chosen from the File menu" )
        {
            auto* closeAll
                = fileMenuAction( *window.mainWindow, logsquirl::mainwindow::action::closeAllText );
            REQUIRE( closeAll != nullptr );
            closeAll->trigger();

            THEN( "no Log File is left, in the window or in the Session" )
            {
                REQUIRE( window.logFileTabPaths().isEmpty() );
                REQUIRE( window.tabArea->count() == dashboardTabs );
                REQUIRE( window.showsDashboard() == showDashboard );
                REQUIRE( window.noneOpenInSession( paths ) );
            }
        }

        WHEN( "Close all is chosen from the menu of a Log File's tab" )
        {
            REQUIRE( window.chooseFromTabMenu( lastLogFileTab, "Close all" ) );

            THEN( "no Log File is left, in the window or in the Session" )
            {
                REQUIRE( window.logFileTabPaths().isEmpty() );
                REQUIRE( window.tabArea->count() == dashboardTabs );
                REQUIRE( window.noneOpenInSession( paths ) );
            }
        }

        WHEN( "Close others is chosen from the menu of the last Log File's tab" )
        {
            REQUIRE( window.chooseFromTabMenu( lastLogFileTab, "Close others" ) );

            THEN( "only the last Log File is left" )
            {
                REQUIRE( window.logFileTabPaths() == QStringList{ paths[ 2 ] } );
                REQUIRE( window.tabArea->count() == dashboardTabs + 1 );
                REQUIRE( window.noneOpenInSession( { paths[ 0 ], paths[ 1 ] } ) );
            }
        }

        WHEN( "Close to the left is chosen from the menu of the last Log File's tab" )
        {
            REQUIRE( window.chooseFromTabMenu( lastLogFileTab, "Close to the left" ) );

            THEN( "only the last Log File is left" )
            {
                REQUIRE( window.logFileTabPaths() == QStringList{ paths[ 2 ] } );
                REQUIRE( window.tabArea->count() == dashboardTabs + 1 );
                REQUIRE( window.noneOpenInSession( { paths[ 0 ], paths[ 1 ] } ) );
            }
        }

        WHEN( "Close to the left is chosen from the menu of the first Log File's tab" )
        {
            THEN( "the menu does not offer it: no Log File is left of that tab" )
            {
                REQUIRE_FALSE( window.chooseFromTabMenu( firstLogFileTab, "Close to the left" ) );
                REQUIRE( window.logFileTabPaths() == paths );
            }
        }

        WHEN( "Close All in Group is chosen for a group holding every Log File" )
        {
            auto& groups = TabGroupInfo::getSynced();
            const auto groupId = groups.addGroup( QStringLiteral( "Group 535" ), Qt::darkGreen );
            for ( const auto& path : paths ) {
                groups.addTabToGroup( groupId, path );
            }
            groups.save();

            const auto chosen = window.chooseFromTabMenu( lastLogFileTab, "Close All in Group" );
            QTest::qWait( 50 );

            TabGroupInfo::getSynced().removeGroup( groupId ).save();

            THEN( "no Log File is left, in the window or in the Session" )
            {
                REQUIRE( chosen );
                REQUIRE( waitUiState( [ & ] { return window.logFileTabPaths().isEmpty(); },
                                      UiTimeoutMs ) );
                REQUIRE( window.tabArea->count() == dashboardTabs );
                REQUIRE( window.noneOpenInSession( paths ) );
            }
        }

        WHEN( "Merge All Left is chosen from the menu of the last Log File's tab" )
        {
            QSignalSpy mergeRequested( window.tabArea, &TabbedCrawlerWidget::mergeRequested );
            REQUIRE( window.chooseFromTabMenu( lastLogFileTab, "Merge All Left" ) );

            THEN( "the two Log Files left of it are merged, the first one among them" )
            {
                REQUIRE( mergeRequested.size() == 1 );
                REQUIRE( mergeRequested.at( 0 ).at( 0 ).toStringList()
                         == QStringList{ paths[ 0 ], paths[ 1 ] } );
            }
        }

        WHEN( "the first tab is clicked with the middle button" )
        {
            auto* tabBar = window.tabArea->tabBar();
            QTest::mouseClick( tabBar, Qt::MiddleButton, {}, tabBar->tabRect( 0 ).center() );
            QTest::qWait( 50 );

            THEN( "it is closed when it holds a Log File, and stays when it is the Dashboard" )
            {
                if ( showDashboard ) {
                    REQUIRE( window.showsDashboard() );
                    REQUIRE( window.logFileTabPaths() == paths );
                }
                else {
                    REQUIRE( window.logFileTabPaths() == QStringList{ paths[ 1 ], paths[ 2 ] } );
                    REQUIRE( window.noneOpenInSession( { paths[ 0 ] } ) );
                }
            }
        }

        WHEN( "the window is closed" )
        {
            window.mainWindow->close();

            THEN( "every Log File is handed to the Session's close" )
            {
                REQUIRE( window.noneOpenInSession( paths ) );
            }
        }
    }
}

// The merge controller belongs to the merged tab: closing the tab ends the
// rebuild, and the temporary file goes with it (#537).
SCENARIO( "A merged Log File's rebuild ends with its tab", "[ui][tabs][merge]" )
{
    ThreeLogFiles files;
    TabsWindow window( true );
    const auto sources = window.open( { files.paths[ 0 ], files.paths[ 1 ] } );

    GIVEN( "the two Log Files merged into a tab of their own" )
    {
        Q_EMIT window.tabArea->mergeRequested( sources, false );

        CrawlerWidget* merged = nullptr;
        QString mergedPath;
        REQUIRE( waitUiState(
            [ & ] {
                for ( int i = 0; i < window.tabArea->count(); ++i ) {
                    const auto path = QDir::fromNativeSeparators( window.tabArea->tabToolTip( i ) );
                    auto* crawler = qobject_cast<CrawlerWidget*>( window.tabArea->widget( i ) );
                    if ( crawler != nullptr && path.contains( "logsquirl_merged_" )
                         && CrawlerWidget::access_by<MainWindowTabsAccess>{ *crawler }
                                    .nbLines()
                                    .get()
                                == 4 ) {
                        merged = crawler;
                        mergedPath = path;
                        return true;
                    }
                }
                return false;
            },
            UiTimeoutMs ) );
        REQUIRE( QFile::exists( mergedPath ) );

        THEN( "the merged tab holds its merge controller, and the window keeps none" )
        {
            REQUIRE( merged->findChild<MergeController*>() != nullptr );
            REQUIRE( window.mainWindow->findChildren<MergeController*>( Qt::FindDirectChildrenOnly )
                         .isEmpty() );
        }

        WHEN( "a Log Line is appended to a source" )
        {
            REQUIRE( writeLines( sources[ 0 ], "an appended Log Line\n", QIODevice::Append ) );

            THEN( "the merged tab is rebuilt with it" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return CrawlerWidget::access_by<MainWindowTabsAccess>{ *merged }
                                   .nbLines()
                                   .get()
                               == 5;
                    },
                    UiTimeoutMs ) );
            }
        }

        WHEN( "the merged tab is closed" )
        {
            Q_EMIT window.tabArea->tabCloseRequested( window.tabArea->indexOf( merged ) );

            THEN( "the temporary file is gone" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return !QFile::exists( mergedPath ); }, UiTimeoutMs ) );

                AND_WHEN( "a Log Line is appended to a source" )
                {
                    REQUIRE(
                        writeLines( sources[ 0 ], "an appended Log Line\n", QIODevice::Append ) );

                    THEN( "no rebuild writes the temporary file again" )
                    {
                        // Longer than the rebuild's debounce and its recheck.
                        QTest::qWait( 1500 );
                        REQUIRE_FALSE( QFile::exists( mergedPath ) );
                    }
                }
            }
        }
    }
}

// The Dashboard setting is read when a window is built, so the Options Dialog
// says it applies to the windows opened from then on (#562).
SCENARIO( "The Dashboard setting reaches the windows opened after it changes", "[ui][tabs]" )
{
    const bool showDashboard = GENERATE( true, false );
    CAPTURE( showDashboard );

    auto& config = Configuration::get();
    const auto previousShowDashboard = config.showDashboard();
    config.setShowDashboard( !showDashboard );

    GIVEN( "the Options Dialog" )
    {
        auto catalog = LogFormatCatalog{};
        OptionsDialog dialog( catalog );
        dialog.show();

        THEN( "a hint beside the Dashboard checkbox says it applies to new windows" )
        {
            REQUIRE( dialog.showDashboardHintLabel->isVisible() );
            REQUIRE_FALSE( dialog.showDashboardHintLabel->text().isEmpty() );

            auto* grid = qobject_cast<QGridLayout*>( dialog.sessionBox->layout() );
            REQUIRE( grid != nullptr );
            int checkBoxRow = -1;
            int hintRow = -2;
            int unused = 0;
            grid->getItemPosition( grid->indexOf( dialog.showDashboardCheckBox ), &checkBoxRow,
                                   &unused, &unused, &unused );
            grid->getItemPosition( grid->indexOf( dialog.showDashboardHintLabel ), &hintRow,
                                   &unused, &unused, &unused );
            REQUIRE( hintRow == checkBoxRow );
        }

        WHEN( "the Dashboard is turned on or off there and the dialog is confirmed" )
        {
            dialog.showDashboardCheckBox->setChecked( showDashboard );
            dialog.buttonBox->button( QDialogButtonBox::Ok )->click();

            THEN( "a window opened afterwards shows the Dashboard or not, as set" )
            {
                REQUIRE( Configuration::get().showDashboard() == showDashboard );
                TabsWindow window;
                REQUIRE( window.showsDashboard() == showDashboard );
            }
        }
    }

    config.setShowDashboard( previousShowDashboard );
    config.save();
}
