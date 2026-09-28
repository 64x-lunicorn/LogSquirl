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
// window shows the dashboard (#535), the merged Log File whose rebuild ends
// with its tab (#537), and the dashboard setting, which reaches the windows
// opened after it changes (#562). Every close of tabs takes one path, whoever
// asks for it (#536).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QActionEvent>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <optional>
#include <utility>

#include "applicationplugins.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "mergecontroller.h"
#include "openlogfile.h"
#include "optionsdialog.h"
#include "recentfiles.h"
#include "session.h"
#include "sessioninfo.h"
#include "tabbedcrawlerwidget.h"
#include "tabgroupinfo.h"
#include "tabnamemapping.h"
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
// the dashboard on or off in the settings when one is given, else as the
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

// Counts the times the Opened files menu is filled anew: each time it gets
// back its first entry.
class OpenedFilesMenuRefreshes : public QObject {
public:
    explicit OpenedFilesMenuRefreshes( const MainWindow& window )
    {
        const auto title = QApplication::translate( "logsquirl::mainwindow::menu",
                                                    logsquirl::mainwindow::menu::openedFilesTitle );
        const auto firstEntry = QApplication::translate(
            "logsquirl::mainwindow::action", logsquirl::mainwindow::action::selectOpenFileText );
        for ( auto* menu : window.findChildren<QMenu*>() ) {
            if ( menu->title() == title ) {
                menu_ = menu;
            }
        }
        REQUIRE( menu_ != nullptr );
        firstEntry_ = firstEntry;
        menu_->installEventFilter( this );
    }

    int count() const
    {
        return count_;
    }

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override
    {
        if ( watched == menu_ && event->type() == QEvent::ActionAdded ) {
            const auto* added = static_cast<QActionEvent*>( event )->action();
            if ( added->text() == firstEntry_ ) {
                ++count_;
            }
        }
        return false;
    }

private:
    QMenu* menu_ = nullptr;
    QString firstEntry_;
    int count_ = 0;
};

// Answers Yes to every question the window asks, and keeps what it asked.
class QuestionAnswerer {
public:
    QuestionAnswerer()
    {
        QObject::connect( &driver_, &QTimer::timeout, [ this ] {
            if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
                if ( box->isVisible() && !box->property( "answered" ).toBool() ) {
                    box->setProperty( "answered", true );
                    questions_.append( box->text() );
                    box->button( QMessageBox::Yes )->click();
                }
            }
        } );
        driver_.start( 10 );
    }

    const QStringList& questions() const
    {
        return questions_;
    }

private:
    QTimer driver_;
    QStringList questions_;
};

// Answers every text the window asks for with this one.
class InputAnswerer {
public:
    explicit InputAnswerer( QString answer )
        : answer_( std::move( answer ) )
    {
        QObject::connect( &driver_, &QTimer::timeout, [ this ] {
            if ( auto* dialog = qobject_cast<QInputDialog*>( QApplication::activeModalWidget() ) ) {
                if ( dialog->isVisible() ) {
                    dialog->setTextValue( answer_ );
                    dialog->accept();
                }
            }
        } );
        driver_.start( 10 );
    }

private:
    QString answer_;
    QTimer driver_;
};

// Accepts every text the window asks for as it is offered, and keeps what
// was offered.
class InputAccepter {
public:
    InputAccepter()
    {
        QObject::connect( &driver_, &QTimer::timeout, [ this ] {
            if ( auto* dialog = qobject_cast<QInputDialog*>( QApplication::activeModalWidget() ) ) {
                if ( dialog->isVisible() ) {
                    offered_ << dialog->textValue();
                    dialog->accept();
                }
            }
        } );
        driver_.start( 10 );
    }

    const QStringList& offered() const
    {
        return offered_;
    }

private:
    QStringList offered_;
    QTimer driver_;
};

// Turns the confirmation of a tab close on for its lifetime.
struct ConfirmTabClose {
    ConfirmTabClose()
    {
        Configuration::get().setConfirmTabClose( true );
        Configuration::get().save();
    }
    ~ConfirmTabClose()
    {
        Configuration::get().setConfirmTabClose( false );
        Configuration::get().save();
    }
    ConfirmTabClose( const ConfirmTabClose& ) = delete;
    ConfirmTabClose& operator=( const ConfirmTabClose& ) = delete;
};

QStringList recentFiles()
{
    QStringList files;
    for ( const auto& file : RecentFiles::getSynced().recentFiles() ) {
        files.append( QDir::fromNativeSeparators( file ) );
    }
    return files;
}

void forgetRecentFiles()
{
    auto& recent = RecentFiles::getSynced();
    recent.removeAll();
    recent.save();
}

} // namespace

// With the dashboard off the first tab holds a Log File, and every path that
// closes tabs in bulk reaches it; with the dashboard on, none closes the
// dashboard (#535).
SCENARIO( "Every bulk close reaches the first Log File whether or not the window shows the "
          "dashboard",
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

            THEN( "it is closed when it holds a Log File, and stays when it is the dashboard" )
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

        WHEN( "the application quits, which saves the Session" )
        {
            // Quitting saves every window, whichever others the Session holds.
            window.session->setExitRequested( true );
            window.mainWindow->close();
            window.session->setExitRequested( false );

            THEN( "the sources are saved, and the merged Log File, which is transient, is not" )
            {
                // A restart would find its temporary file gone (#570).
                QStringList saved;
                for ( const auto& file : SessionInfo::get().openFiles( "Main" ) ) {
                    saved.append( QDir::fromNativeSeparators( file.fileName ) );
                }
                REQUIRE( saved == sources );
            }
        }

        // Its temporary path is gone after a restart, so no stored state
        // keeps it (#597).
        THEN( "the merged Log File is not among the recent files" )
        {
            REQUIRE_FALSE( recentFiles().contains( mergedPath ) );
        }

        WHEN( "the user closes the merged tab" )
        {
            Q_EMIT window.tabArea->tabCloseRequested( window.tabArea->indexOf( merged ) );

            THEN( "it does not become a recent file" )
            {
                REQUIRE( waitUiState( [ & ] { return window.tabArea->indexOf( merged ) < 0; },
                                      UiTimeoutMs ) );
                REQUIRE_FALSE( recentFiles().contains( mergedPath ) );
            }
        }

        WHEN( "the merged tab is renamed from its menu" )
        {
            const InputAnswerer answerer( QStringLiteral( "Renamed 597" ) );
            const auto tab = window.tabArea->indexOf( merged );
            REQUIRE( window.chooseFromTabMenu( tab, "Rename tab" ) );

            THEN( "the tab shows the name, and the stored tab names do not hold its path" )
            {
                REQUIRE( window.tabArea->tabText( tab ) == QStringLiteral( "Renamed 597" ) );
                REQUIRE( TabNameMapping::getSynced().tabName( mergedPath ).isEmpty() );
            }
        }

        WHEN( "the merged tab is put in a tab group from its menu" )
        {
            auto& groups = TabGroupInfo::getSynced();
            const auto groupId = groups.addGroup( QStringLiteral( "Group 597" ), Qt::darkGreen );
            groups.save();

            const auto tab = window.tabArea->indexOf( merged );
            const auto chosen = window.chooseFromTabMenu( tab, "Group 597" );
            const auto tabText = window.tabArea->tabText( tab );
            const auto stored = TabGroupInfo::getSynced().groupForTab( mergedPath );

            TabGroupInfo::getSynced().removeGroup( groupId ).save();

            THEN( "the tab shows the group, and the stored tab groups do not hold its path" )
            {
                REQUIRE( chosen );
                REQUIRE( tabText.startsWith( QString::fromUtf8( "● " ) ) );
                REQUIRE_FALSE( stored.has_value() );
            }
        }
    }
}

// Text opened from the clipboard is a Transient Log File: its temporary path
// is gone after a restart, so it is never a recent file (#597).
SCENARIO( "Text opened from the clipboard is not added to the recent files", "[ui][tabs]" )
{
    TabsWindow window( true );
    QGuiApplication::clipboard()->setText( QStringLiteral( "a Log Line from the clipboard\n" ) );

    GIVEN( "the clipboard opened in a tab" )
    {
        auto* openClipboard = fileMenuAction( *window.mainWindow,
                                              logsquirl::mainwindow::action::openClipboardText );
        REQUIRE( openClipboard != nullptr );
        openClipboard->trigger();

        QString clipboardPath;
        REQUIRE( waitUiState(
            [ & ] {
                for ( const auto& path : window.logFileTabPaths() ) {
                    if ( path.contains( "logsquirl_clipboard" ) ) {
                        clipboardPath = path;
                        return true;
                    }
                }
                return false;
            },
            UiTimeoutMs ) );

        THEN( "it is not among the recent files" )
        {
            REQUIRE_FALSE( recentFiles().contains( clipboardPath ) );
        }

        WHEN( "the user closes its tab" )
        {
            Q_EMIT window.tabArea->tabCloseRequested( window.tabArea->tabOfPath( clipboardPath ) );

            THEN( "it does not become a recent file" )
            {
                REQUIRE( waitUiState( [ & ] { return window.logFileTabPaths().isEmpty(); },
                                      UiTimeoutMs ) );
                REQUIRE_FALSE( recentFiles().contains( clipboardPath ) );
            }
        }
    }
}

// A data source's tab opens under the source's name, not its temporary file's.
// Grouping a tab, the Manage Tab Groups dialog and a reset of a rename restyle
// every tab: each keeps the title it opened with (#606). Standard input's tab
// is titled the same way.
SCENARIO( "A data source's tab keeps its name when tabs are grouped and renamed", "[ui][tabs]" )
{
    TabsWindow window( false );
    const ThreeLogFiles logFiles;
    window.open( { logFiles.paths[ 0 ] } );

    const auto sourcePath = logFiles.dir.filePath( QStringLiteral( "datasource_606.log" ) );
    REQUIRE( writeLines( sourcePath, "a Log Line from a data source\n", QIODevice::Truncate ) );
    const auto sourceName = QStringLiteral( "Source 606" );
    REQUIRE( QMetaObject::invokeMethod(
        window.mainWindow.get(), "handleDataSourceStarted", Qt::DirectConnection,
        Q_ARG( QString, QStringLiteral( "datasource.606" ) ), Q_ARG( QString, sourceName ),
        Q_ARG( QString, sourcePath ) ) );

    int sourceTab = -1;
    REQUIRE( waitUiState(
        [ & ] {
            sourceTab = window.tabArea->tabOfPath( sourcePath );
            return sourceTab >= 0;
        },
        UiTimeoutMs ) );
    const auto fileTab = window.tabArea->tabOfPath( logFiles.paths[ 0 ] );
    REQUIRE( fileTab >= 0 );

    REQUIRE( window.tabArea->tabText( sourceTab ) == sourceName );
    REQUIRE( window.tabArea->tabToolTip( sourceTab ).startsWith( "DataSource: " + sourceName ) );

    auto& groups = TabGroupInfo::getSynced();
    const auto groupId = groups.addGroup( QStringLiteral( "Group 606" ), Qt::darkCyan );
    groups.save();

    WHEN( "another tab is put in a tab group from its menu" )
    {
        const auto chosen = window.chooseFromTabMenu( fileTab, "Group 606" );

        THEN( "the data source's tab keeps its name and tooltip" )
        {
            REQUIRE( chosen );
            REQUIRE( window.tabArea->tabText( fileTab ).startsWith( QString::fromUtf8( "● " ) ) );
            REQUIRE( window.tabArea->tabText( sourceTab ) == sourceName );
            REQUIRE(
                window.tabArea->tabToolTip( sourceTab ).startsWith( "DataSource: " + sourceName ) );
        }
    }

    WHEN( "the data source's tab is put in a tab group from its menu" )
    {
        REQUIRE( window.chooseFromTabMenu( sourceTab, "Group 606" ) );

        THEN( "it shows the group before its name" )
        {
            REQUIRE( window.tabArea->tabText( sourceTab )
                     == QString::fromUtf8( "● " ) + sourceName );
        }
    }

    WHEN( "the grouped data source's tab is renamed and the offered name accepted as it is" )
    {
        REQUIRE( window.chooseFromTabMenu( sourceTab, "Group 606" ) );
        QStringList offered;
        {
            const InputAccepter accepter;
            REQUIRE( window.chooseFromTabMenu( sourceTab, "Rename tab" ) );
            offered = accepter.offered();
        }

        THEN( "the name is offered without the group's bullet and stays as it was (#612)" )
        {
            REQUIRE( offered == QStringList{ sourceName } );
            REQUIRE( window.tabArea->tabText( sourceTab )
                     == QString::fromUtf8( "● " ) + sourceName );
        }
    }

    WHEN( "the data source's tab is renamed, then its name is reset, from its menu" )
    {
        QString renamed;
        {
            const InputAnswerer answerer( QStringLiteral( "Renamed 606" ) );
            REQUIRE( window.chooseFromTabMenu( sourceTab, "Rename tab" ) );
            renamed = window.tabArea->tabText( sourceTab );
        }
        REQUIRE( window.chooseFromTabMenu( sourceTab, "Reset tab name" ) );

        THEN( "it shows the rename, then the data source's name again" )
        {
            REQUIRE( renamed == QStringLiteral( "Renamed 606" ) );
            REQUIRE( window.tabArea->tabText( sourceTab ) == sourceName );
        }
    }

    // Close the tab while its file is there, then leave the settings store as
    // it was.
    Q_EMIT window.tabArea->tabCloseRequested( window.tabArea->tabOfPath( sourcePath ) );
    TabGroupInfo::getSynced().removeGroup( groupId ).save();
}

// The dashboard setting is read when a window is built, so the Options Dialog
// says it applies to the windows opened from then on (#562).
SCENARIO( "The dashboard setting reaches the windows opened after it changes", "[ui][tabs]" )
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

        THEN( "a hint beside the dashboard checkbox says it applies to new windows" )
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

        WHEN( "the dashboard is turned on or off there and the dialog is confirmed" )
        {
            dialog.showDashboardCheckBox->setChecked( showDashboard );
            dialog.buttonBox->button( QDialogButtonBox::Ok )->click();

            THEN( "a window opened afterwards shows the dashboard or not, as set" )
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

// One path closes any set of tabs: it asks once for the whole set when the
// user asked for the close and confirmation is on, adds the Log Files to the
// recent files only then, and fills the Opened files menu anew once (#536).
SCENARIO( "Closing tabs asks once and remembers the Log Files only when the user asked",
          "[ui][tabs]" )
{
    ThreeLogFiles files;
    const ConfirmTabClose confirmation;
    TabsWindow window( true );

    GIVEN( "three Log Files open in their tabs, with the close of a tab to be confirmed" )
    {
        const auto paths = window.open( files.paths );
        QTest::qWait( 50 );
        // Opening them made them recent files too.
        forgetRecentFiles();
        OpenedFilesMenuRefreshes refreshes( *window.mainWindow );
        QuestionAnswerer answerer;

        WHEN( "Close all is chosen from the File menu" )
        {
            auto* closeAll
                = fileMenuAction( *window.mainWindow, logsquirl::mainwindow::action::closeAllText );
            REQUIRE( closeAll != nullptr );
            closeAll->trigger();

            THEN( "one question is asked, and all three are closed and in the recent files" )
            {
                REQUIRE( answerer.questions().size() == 1 );
                REQUIRE( answerer.questions().front().contains( "3" ) );
                REQUIRE( window.logFileTabPaths().isEmpty() );
                REQUIRE( window.noneOpenInSession( paths ) );
                const auto recent = recentFiles();
                for ( const auto& path : paths ) {
                    REQUIRE( recent.contains( path ) );
                }
                REQUIRE( refreshes.count() == 1 );
            }
        }

        WHEN( "Close all is chosen from the menu of a Log File's tab" )
        {
            REQUIRE(
                window.chooseFromTabMenu( window.tabArea->logFileTabs().back(), "Close all" ) );

            THEN( "one question is asked, and all three are closed and in the recent files" )
            {
                REQUIRE( answerer.questions().size() == 1 );
                REQUIRE( window.logFileTabPaths().isEmpty() );
                const auto recent = recentFiles();
                for ( const auto& path : paths ) {
                    REQUIRE( recent.contains( path ) );
                }
                REQUIRE( refreshes.count() == 1 );
            }
        }

        WHEN( "the tab in front is closed from the File menu" )
        {
            auto* close
                = fileMenuAction( *window.mainWindow, logsquirl::mainwindow::action::closeText );
            REQUIRE( close != nullptr );
            window.tabArea->setCurrentIndex( window.tabArea->logFileTabs().back() );
            close->trigger();

            THEN( "the question names its Log File, and only it is closed and remembered" )
            {
                REQUIRE( answerer.questions()
                         == QStringList{ QStringLiteral( "Close tab \"third.log\"?" ) } );
                REQUIRE( window.logFileTabPaths() == QStringList{ paths[ 0 ], paths[ 1 ] } );
                REQUIRE( recentFiles().contains( paths[ 2 ] ) );
                REQUIRE_FALSE( recentFiles().contains( paths[ 0 ] ) );
                REQUIRE( refreshes.count() == 1 );
            }
        }

        WHEN( "the window is closed" )
        {
            window.mainWindow->close();

            THEN( "no question is asked, and none of them is in the recent files" )
            {
                REQUIRE( answerer.questions().isEmpty() );
                REQUIRE( window.noneOpenInSession( paths ) );
                const auto recent = recentFiles();
                for ( const auto& path : paths ) {
                    REQUIRE_FALSE( recent.contains( path ) );
                }
            }
        }
    }

    forgetRecentFiles();
}
