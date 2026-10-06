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
// window shows the dashboard (#535), what Merge… merges (#571), the merged Log
// File whose rebuild ends with its tab (#537), and the dashboard setting,
// which reaches the windows opened after it changes (#562). Every close of
// tabs takes one path, whoever asks for it (#536).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QActionEvent>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <optional>
#include <utility>

#include "applicationplugins.h"
#include "commandoutputdialog.h"
#include "commandsource.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "filewatcher.h"
#include "instancehandover.h"
#include "loadingstatus.h"
#include "logfileprovenance.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "mergecontroller.h"
#include "mergedialog.h"
#include "openlogfile.h"
#include "optionsdialog.h"
#include "recentfiles.h"
#include "session.h"
#include "sessioninfo.h"
#include "shown_widget.h"
#include "streamwriter.h"
#include "tabbedcrawlerwidget.h"
#include "tabgroupinfo.h"
#include "tabnamemapping.h"
#include "test_policies.h"
#include "test_utils.h"
#include "welcomedashboard.h"

#ifndef Q_OS_WIN
#include "command_process_probe.h"
#endif

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
// settings have it. Its Log Files are watched only through `fileWatch`.
struct TabsWindow {
    explicit TabsWindow( std::optional<bool> showDashboard = {},
                         std::shared_ptr<PolicyFileWatchPort> fileWatch = {} )
        : previousShowDashboard( Configuration::get().showDashboard() )
        , session( std::make_shared<Session>( testSettingsPolicies(),
                                              std::make_shared<LogFormatCatalog>(),
                                              std::move( fileWatch ) ) )
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

// Answers the Merge dialog with what `answer` does to it, once.
class MergeDialogAnswerer {
public:
    explicit MergeDialogAnswerer( std::function<void( MergeDialog& )> answer )
        : answer_( std::move( answer ) )
    {
        QObject::connect( &driver_, &QTimer::timeout, [ this ] {
            if ( auto* dialog = qobject_cast<MergeDialog*>( QApplication::activeModalWidget() ) ) {
                if ( dialog->isVisible() ) {
                    driver_.stop();
                    answer_( *dialog );
                }
            }
        } );
        driver_.start( 10 );
    }

private:
    std::function<void( MergeDialog& )> answer_;
    QTimer driver_;
};

QPushButton* buttonOf( const MergeDialog& dialog, const char* text )
{
    const auto translated = QApplication::translate( "MergeDialog", text );
    for ( auto* button : dialog.findChildren<QPushButton*>() ) {
        if ( button->text() == translated ) {
            return button;
        }
    }
    FAIL( "The Merge dialog has no button " << text );
    return nullptr;
}

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

        WHEN( "Merge… is chosen from a tab's menu, and the dialog confirmed with the first Log "
              "File unchecked, the last moved first and duplicate lines dropped" )
        {
            QSignalSpy mergeRequested( window.tabArea, &TabbedCrawlerWidget::mergeRequested );
            QStringList offered;
            MergeDialogAnswerer answerer( [ &offered ]( MergeDialog& dialog ) {
                auto* list = dialog.findChild<QListWidget*>();
                for ( int row = 0; row < list->count(); ++row ) {
                    offered.append( QDir::fromNativeSeparators( list->item( row )->toolTip() ) );
                }
                list->item( 0 )->setCheckState( Qt::Unchecked );
                list->setCurrentRow( 2 );
                buttonOf( dialog, "Move Up" )->click();
                buttonOf( dialog, "Move Up" )->click();
                dialog.findChild<QCheckBox*>()->setChecked( true );
                buttonOf( dialog, "Merge" )->click();
            } );
            REQUIRE( window.chooseFromTabMenu( firstLogFileTab, "Merge…" ) );

            THEN( "the dialog offered every Log File in tab order, and exactly the checked ones "
                  "are merged in the order chosen" )
            {
                REQUIRE( offered == paths );
                REQUIRE( mergeRequested.size() == 1 );
                REQUIRE( mergeRequested.at( 0 ).at( 0 ).toStringList()
                         == QStringList{ paths[ 2 ], paths[ 1 ] } );
                REQUIRE( mergeRequested.at( 0 ).at( 1 ).toBool() );
            }
        }

        WHEN( "Merge… is chosen, and the dialog cancelled" )
        {
            QSignalSpy mergeRequested( window.tabArea, &TabbedCrawlerWidget::mergeRequested );
            MergeDialogAnswerer answerer( []( MergeDialog& dialog ) { dialog.reject(); } );
            REQUIRE( window.chooseFromTabMenu( lastLogFileTab, "Merge…" ) );

            THEN( "nothing is merged" )
            {
                REQUIRE( mergeRequested.isEmpty() );
            }
        }

        WHEN( "the menu of a Log File's tab is opened" )
        {
            THEN( "it offers none of the former Merge All entries" )
            {
                for ( const auto* entry : { "Merge All Left", "Merge All Left (dedup)",
                                            "Merge All Right", "Merge All Right (dedup)" } ) {
                    REQUIRE_FALSE( window.chooseFromTabMenu( firstLogFileTab + 1, entry ) );
                }
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

// A reload that a newer one interrupts, as a merged tab's rebuilds do, leaves
// the tab of a Log File that has loaded open (#621).
SCENARIO( "An interrupted reload leaves the tab of a loaded Log File open", "[ui][tabs]" )
{
    ThreeLogFiles files;
    TabsWindow window( true );
    window.open( { files.paths[ 0 ] } );

    auto* crawler = qobject_cast<CrawlerWidget*>( window.tabArea->currentWidget() );
    REQUIRE( crawler != nullptr );
    REQUIRE( waitUiState( [ crawler ] { return crawler->hasLoaded(); }, UiTimeoutMs ) );
    const QPointer<CrawlerWidget> tab( crawler );

    WHEN( "a load of it ends interrupted" )
    {
        Q_EMIT crawler->loadingFinished( LoadingStatus::Interrupted, {} );
        QTest::qWait( 100 );

        THEN( "its tab is still open" )
        {
            REQUIRE( tab );
            REQUIRE( window.tabArea->indexOf( tab ) >= 0 );
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
// A merge opens its file through the window's one open call, with the title
// its tab is to open with (#643). An open that fails forgets that title with
// it: a tab opened later for the same path shows the file's own name.
//
// What a merge writes is never an archive, so its open does not fail on its
// own; this merged file is named as one, and the user declines extracting it.
SCENARIO( "A merged open that fails leaves no opening title behind", "[ui][tabs][merge]" )
{
    TabsWindow window( false );
    REQUIRE( waitUiState( [ & ] { return window.plugins->isLoaded(); }, UiTimeoutMs ) );
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto mergedPath = folder.filePath( "merged.log.gz" );
    REQUIRE( writeLines( mergedPath, "a Log Line\n", QIODevice::Truncate ) );

    // Answers No when the window asks whether to extract the archive.
    int questionsDeclined = 0;
    QTimer questionDriver;
    QObject::connect( &questionDriver, &QTimer::timeout, [ &questionsDeclined ] {
        if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
            if ( box->isVisible() && !box->property( "answered" ).toBool() ) {
                box->setProperty( "answered", true );
                ++questionsDeclined;
                box->button( QMessageBox::No )->click();
            }
        }
    } );
    questionDriver.start( 10 );

    std::optional<CrawlerWidget*> openedTab;
    auto provenance = LogFileProvenance::transient( QStringLiteral( "Merged" ) );
    provenance.whenOpened = [ &openedTab ]( CrawlerWidget* crawler ) { openedTab = crawler; };
    const auto isOpen = window.mainWindow->openLogFile( mergedPath, std::move( provenance ) );

    THEN( "no tab opens, and the merged file's title is forgotten" )
    {
        REQUIRE_FALSE( isOpen );
        REQUIRE( questionsDeclined == 1 );
        REQUIRE( openedTab.has_value() );
        REQUIRE( *openedTab == nullptr );
        REQUIRE( window.tabArea->logFileTabs().isEmpty() );

        // Opened again with archives read as they are, the same path gets a
        // tab of its own, named after its file.
        auto policies = testSettingsPolicies();
        policies.fileAccess.extractArchives = false;
        window.session->applyPolicies( policies );
        window.mainWindow->loadFileNonInteractive( mergedPath );
        REQUIRE( waitUiState( [ & ] { return window.tabArea->logFileTabs().size() == 1; },
                              UiTimeoutMs ) );
        const auto tab = window.tabArea->logFileTabs().front();
        REQUIRE( window.tabArea->tabText( tab ) == QStringLiteral( "merged.log.gz" ) );
    }
}

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
        showUntilExposed( dialog );

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

// --- A command's output (#575) ---

namespace {

// The tab whose title starts with `title`, -1 when none does.
int tabTitled( const TabbedCrawlerWidget& tabs, const QString& title )
{
    for ( int i = 0; i < tabs.count(); ++i ) {
        if ( tabs.tabText( i ).startsWith( title ) ) {
            return i;
        }
    }
    return -1;
}

// Waits for the tab titled `title` to open and returns it.
int waitForTab( const TabsWindow& window, const QString& title )
{
    int tab = -1;
    REQUIRE( waitUiState(
        [ & ] {
            tab = tabTitled( *window.tabArea, title );
            return tab >= 0;
        },
        UiTimeoutMs ) );
    return tab;
}

} // namespace

#ifndef Q_OS_WIN

namespace {

// The spool file a tab reads.
QString spoolOf( const TabsWindow& window, int tab )
{
    auto* crawler = qobject_cast<CrawlerWidget*>( window.tabArea->widget( tab ) );
    REQUIRE( crawler != nullptr );
    return window.session->getFilename( crawler );
}

// The process id a command wrote as the first line of its output.
qint64 processIdIn( const QString& spool )
{
    qint64 pid = 0;
    REQUIRE( waitUiState(
        [ & ] {
            QFile file( spool );
            if ( file.open( QIODevice::ReadOnly ) ) {
                pid = file.readLine().trimmed().toLongLong();
            }
            return pid > 0;
        },
        UiTimeoutMs ) );
    return pid;
}

} // namespace

SCENARIO( "A command's output opens in a followed tab that tells how the command ended",
          "[ui][tabs][command]" )
{
    const ShellForTests shell;
    // Watched as the application watches it: the output arrives after the
    // tab has started loading.
    TabsWindow window( false, FileWatcher::sharedFileWatcher() );
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );

    GIVEN( "a command that writes two lines and exits with 4" )
    {
        const auto commandLine = QStringLiteral( "printf 'first\\nsecond\\n'; exit 4" );
        REQUIRE( window.mainWindow->openCommandOutput(
            RecentCommand{ commandLine, folder.path(), true } ) );

        const auto tab = waitForTab( window, commandLine );
        auto* crawler = qobject_cast<CrawlerWidget*>( window.tabArea->widget( tab ) );
        REQUIRE( crawler != nullptr );

        THEN( "its tab shows the output, is transient, names the exit code and is no longer "
              "followed" )
        {
            REQUIRE( waitUiState(
                [ & ] {
                    return window.tabArea->tabText( window.tabArea->indexOf( crawler ) )
                           == commandLine + " [exit 4]";
                },
                UiTimeoutMs ) );
            const auto index = window.tabArea->indexOf( crawler );
            REQUIRE( window.tabArea->holdsTransientLogFile( index ) );
            REQUIRE( waitUiState( [ & ] { return !crawler->isFollowEnabled(); }, UiTimeoutMs ) );
            crawler->followSet( true );
            REQUIRE_FALSE( crawler->isFollowEnabled() );

            const auto toolTip = window.tabArea->tabToolTip( index );
            REQUIRE( toolTip.startsWith( commandLine + "\n" ) );
            REQUIRE( toolTip.contains( "Working folder: "
                                       + QDir::toNativeSeparators( folder.path() ) ) );
            REQUIRE( toolTip.contains( QDir::toNativeSeparators( spoolOf( window, index ) ) ) );
            REQUIRE( toolTip.endsWith( "\nEnded with exit code 4" ) );

            REQUIRE( window.mainWindow->statusBar()->currentMessage()
                     == "\"" + commandLine + "\" ended with exit code 4" );

            const auto linesLoaded = waitUiState(
                [ & ] {
                    return CrawlerWidget::access_by<MainWindowTabsAccess>{ *crawler }
                               .nbLines()
                               .get()
                           == 2;
                },
                UiTimeoutMs );
            INFO( "lines: "
                  << CrawlerWidget::access_by<MainWindowTabsAccess>{ *crawler }.nbLines().get() );
            REQUIRE( linesLoaded );
        }
    }

    GIVEN( "a command whose tab is renamed while it runs" )
    {
        const auto commandLine = QStringLiteral( "sleep 1; exit 2" );
        REQUIRE( window.mainWindow->openCommandOutput(
            RecentCommand{ commandLine, folder.path(), true } ) );
        const auto tab = waitForTab( window, commandLine );
        auto* crawler = window.tabArea->widget( tab );
        window.tabArea->renameTab( tab, "Build" );
        REQUIRE( window.tabArea->tabText( tab ) == "Build" );

        THEN( "its new name tells how the command ended" )
        {
            REQUIRE( waitUiState(
                [ & ] {
                    return window.tabArea->tabText( window.tabArea->indexOf( crawler ) )
                           == "Build [exit 2]";
                },
                UiTimeoutMs ) );

            AND_THEN( "a name it is given after the end does too" )
            {
                window.tabArea->renameTab( window.tabArea->indexOf( crawler ), "Done" );
                REQUIRE( window.tabArea->tabText( window.tabArea->indexOf( crawler ) )
                         == "Done [exit 2]" );
            }
        }
    }

    GIVEN( "a command the shell does not know" )
    {
        const auto commandLine = QStringLiteral( "no_such_command_for_logsquirl_575" );
        REQUIRE( window.mainWindow->openCommandOutput(
            RecentCommand{ commandLine, folder.path(), true } ) );

        THEN( "its tab opens and ends with 127, which the status bar calls not found" )
        {
            REQUIRE( waitUiState(
                [ & ] { return tabTitled( *window.tabArea, commandLine + " [exit 127]" ) >= 0; },
                UiTimeoutMs ) );
            REQUIRE( window.mainWindow->statusBar()->currentMessage()
                     == "\"" + commandLine + "\" ended with exit code 127: command not found" );
        }
    }
}

SCENARIO( "A command does not open in a working folder that does not exist", "[ui][tabs][command]" )
{
    TabsWindow window( false );
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto missing = folder.filePath( "missing" );
    const auto tabsBefore = window.tabArea->count();

    QString shownText;
    QTimer answerer;
    QObject::connect( &answerer, &QTimer::timeout, [ &shownText ] {
        if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
            shownText = box->text();
            box->accept();
        }
    } );
    answerer.start( 20 );

    const auto opened
        = window.mainWindow->openCommandOutput( RecentCommand{ "echo hello", missing, true } );
    answerer.stop();

    REQUIRE_FALSE( opened );
    REQUIRE( shownText.contains( QDir::toNativeSeparators( missing ) ) );
    QTest::qWait( 100 );
    REQUIRE( window.tabArea->count() == tabsBefore );
}

SCENARIO( "Closing a command's tab or window stops the command and its child processes",
          "[ui][tabs][command]" )
{
    const ShellForTests shell;
    TabsWindow window( false );

    // The command starts a grandchild and tells its process id.
    const auto commandLine = QStringLiteral( "sleep 60 & echo $!; wait" );
    REQUIRE( window.mainWindow->openCommandOutput( RecentCommand{ commandLine, {}, true } ) );
    const auto tab = waitForTab( window, commandLine );
    const auto spool = spoolOf( window, tab );
    const auto grandchild = processIdIn( spool );
    REQUIRE( processIsRunning( grandchild ) );

    const auto closeWindow = GENERATE( false, true );
    if ( closeWindow ) {
        window.mainWindow->close();
    }
    else {
        Q_EMIT window.tabArea->tabCloseRequested( tab );
        REQUIRE( tabTitled( *window.tabArea, commandLine ) < 0 );
    }

    CHECK( waitUiState( [ grandchild ] { return !processIsRunning( grandchild ); }, UiTimeoutMs ) );
    CHECK( waitUiState( [ &spool ] { return !QFileInfo::exists( spool ); }, UiTimeoutMs ) );
}

SCENARIO( "Two commands and standard input are read side by side in one window",
          "[ui][tabs][command]" )
{
    const ShellForTests shell;
    // The window reads from a stream that ends at once, not from the tests'
    // own.
    REQUIRE( std::freopen( "/dev/null", "r", stdin ) != nullptr );

    TabsWindow window( false );
    const auto first = QStringLiteral( "echo first; sleep 30" );
    const auto second = QStringLiteral( "echo second; sleep 30" );
    REQUIRE( window.mainWindow->openCommandOutput( RecentCommand{ first, {}, true } ) );
    window.mainWindow->openStandardInput();
    REQUIRE( window.mainWindow->openCommandOutput( RecentCommand{ second, {}, true } ) );

    const auto firstTab = waitForTab( window, first );
    const auto secondTab = waitForTab( window, second );
    const auto stdinTab = waitForTab( window, "stdin" );
    REQUIRE( window.tabArea->logFileTabs().size() == 3 );
    const QStringList spools{ spoolOf( window, firstTab ), spoolOf( window, secondTab ),
                              spoolOf( window, stdinTab ) };
    auto distinct = spools;
    REQUIRE( distinct.removeDuplicates() == 0 );

    WHEN( "the window is saved and closed as the application quits" )
    {
        window.session->setExitRequested( true );
        window.mainWindow->close();
        window.session->setExitRequested( false );

        THEN( "none of them is saved with the Session" )
        {
            const auto saved = SessionInfo::get().openFiles( "Main" );
            for ( const auto& spool : spools ) {
                REQUIRE(
                    std::none_of( saved.cbegin(), saved.cend(), [ &spool ]( const auto& file ) {
                        return file.fileName == spool;
                    } ) );
            }
        }
    }
}

// --- Standard input handed over by a secondary instance (#623) ---

namespace {

// A spool file of standard input as a secondary instance makes and hands
// over: the secondary keeps writing, the window owns it.
QString handedOverSpool( const QByteArray& content )
{
    logsquirl::plugins::StreamWriter writer( "stdin" );
    writer.pushBytes( content.constData(), static_cast<size_t>( content.size() ) );
    writer.keepFile();
    return writer.filePath();
}

// The tabs titled exactly `title`.
std::vector<int> tabsTitled( const TabbedCrawlerWidget& tabs, const QString& title )
{
    std::vector<int> found;
    for ( int i = 0; i < tabs.count(); ++i ) {
        if ( tabs.tabText( i ) == title ) {
            found.push_back( i );
        }
    }
    return found;
}

} // namespace

SCENARIO( "Standard input handed over by another instance opens in a stdin tab, as often as it "
          "comes",
          "[ui][tabs][command]" )
{
    TabsWindow window( false, FileWatcher::sharedFileWatcher() );

    const auto first = handedOverSpool( "1\n2\n3\n4\n5\n" );
    const auto second = handedOverSpool( "a\n" );
    REQUIRE( first != second );

    // The name a secondary instance sends is the tab's, "stdin" when it sent
    // none.
    window.mainWindow->openHandedOverStandardInput( first );
    window.mainWindow->openHandedOverStandardInput( second, "stdin" );

    REQUIRE( waitUiState( [ & ] { return tabsTitled( *window.tabArea, "stdin" ).size() == 2; },
                          UiTimeoutMs ) );
    const auto tabs = tabsTitled( *window.tabArea, "stdin" );
    REQUIRE( spoolOf( window, tabs[ 0 ] ) == first );
    REQUIRE( spoolOf( window, tabs[ 1 ] ) == second );
    REQUIRE( window.tabArea->holdsTransientLogFile( tabs[ 0 ] ) );
    // The secondary instances are told the spool files were taken over.
    REQUIRE( QFileInfo::exists( spoolAdoptionMarker( first ) ) );
    REQUIRE( QFileInfo::exists( spoolAdoptionMarker( second ) ) );
    // The last one handed over is in front.
    REQUIRE( window.tabArea->currentIndex() == tabs[ 1 ] );

    auto* crawler = qobject_cast<CrawlerWidget*>( window.tabArea->widget( tabs[ 0 ] ) );
    REQUIRE( crawler != nullptr );
    REQUIRE( waitUiState(
        [ & ] {
            return CrawlerWidget::access_by<MainWindowTabsAccess>{ *crawler }.nbLines().get() == 5;
        },
        UiTimeoutMs ) );

    WHEN( "a stdin tab is closed" )
    {
        Q_EMIT window.tabArea->tabCloseRequested( tabs[ 0 ] );

        THEN( "its spool file, marker and folder are removed, and the other tab keeps its own" )
        {
            REQUIRE( waitUiState( [ & ] { return !QFileInfo::exists( first ); }, UiTimeoutMs ) );
            REQUIRE_FALSE( QFileInfo::exists( spoolAdoptionMarker( first ) ) );
            REQUIRE_FALSE( QFileInfo::exists( QFileInfo( first ).absolutePath() ) );
            REQUIRE( QFileInfo::exists( second ) );
            REQUIRE( QFileInfo::exists( spoolAdoptionMarker( second ) ) );
        }
    }

    window.mainWindow->close();
    REQUIRE( waitUiState( [ & ] { return !QFileInfo::exists( second ); }, UiTimeoutMs ) );
}

// A path the window can't own is not taken over at all: the secondary instance
// then finds no marker, reports the failed hand-over and removes its own file,
// instead of stopping half-way under an open tab.
SCENARIO( "A handed-over file that is no spool of standard input is not taken over",
          "[ui][tabs][command]" )
{
    TabsWindow window( false );
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto path = folder.filePath( "mine.log" );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "keep me\n" );
    file.close();
    const auto tabsBefore = window.tabArea->count();

    window.mainWindow->openHandedOverStandardInput( path, "journal" );
    QTest::qWait( 200 );

    REQUIRE( window.tabArea->count() == tabsBefore );
    REQUIRE( QFileInfo::exists( path ) );
    REQUIRE_FALSE( QFileInfo::exists( spoolAdoptionMarker( path ) ) );
}

// On Windows a console program writes to a pipe in the OEM code page; the
// Command Source is started with one here as it is there (#655).
SCENARIO( "A command's tab is read in the Encoding its output decided", "[ui][tabs][command]" )
{
    const ShellForTests shell;
    TabsWindow window( false, FileWatcher::sharedFileWatcher() );

    GIVEN( "a command that writes a line of ASCII, then a German word in CP850" )
    {
        const auto commandLine
            = QStringLiteral( "printf 'Ping\\n'; sleep 1; printf 'Gr\\224\\341e\\n'" );
        QString error;
        auto source
            = CommandSource::startCommand( RecentCommand{ commandLine, {}, true }, &error, 850 );
        REQUIRE( source != nullptr );
        const auto spool = source->spoolPath();
        REQUIRE( window.mainWindow->openLogFile(
            spool, LogFileProvenance::commandOutput( std::move( source ), commandLine, {} ),
            true ) );

        const auto tab = waitForTab( window, commandLine );
        auto* crawler = qobject_cast<CrawlerWidget*>( window.tabArea->widget( tab ) );
        REQUIRE( crawler != nullptr );

        THEN( "its tab reads it in IBM850 once the output held more than ASCII" )
        {
            REQUIRE( waitUiState( [ & ] { return crawler->encodingMib() == 2009; }, UiTimeoutMs ) );
            REQUIRE( crawler->encodingText() == "Displayed as IBM850" );

            AND_THEN( "the user can still read it in another Encoding" )
            {
                crawler->setEncoding( std::nullopt );
                REQUIRE( waitUiState(
                    [ & ] {
                        return tabTitled( *window.tabArea,
                                          commandTabTitle( commandLine ) + " [exit 0]" )
                               >= 0;
                    },
                    UiTimeoutMs ) );
                REQUIRE_FALSE( crawler->encodingMib().has_value() );
            }
        }
    }
}

#endif

// The File menu runs a command from its dialog, and the command is remembered
// (#575).
SCENARIO( "Open Command Output asks for a command and remembers it", "[ui][tabs][command]" )
{
#ifndef Q_OS_WIN
    const ShellForTests shell;
#endif
    const auto savedConfiguration = Configuration::get();
    TabsWindow window( false );

    auto* openCommand = fileMenuAction( *window.mainWindow,
                                        logsquirl::mainwindow::action::openCommandOutputText );
    REQUIRE( openCommand != nullptr );

    const auto commandLine = QStringLiteral( "echo from the dialog" );
    bool answered = false;
    QTimer answerer;
    QObject::connect( &answerer, &QTimer::timeout, [ & ] {
        auto* dialog = qobject_cast<CommandOutputDialog*>( QApplication::activeModalWidget() );
        if ( dialog == nullptr ) {
            return;
        }
        dialog->commandBox()->setEditText( commandLine );
        dialog->standardErrorBox()->setChecked( false );
        answered = true;
        dialog->accept();
    } );
    answerer.start( 20 );
    openCommand->trigger();
    answerer.stop();
    REQUIRE( answered );

    waitForTab( window, commandLine );
    const auto recent = Configuration::get().recentCommands();
    Configuration::get() = savedConfiguration;
    REQUIRE_FALSE( recent.empty() );
    REQUIRE( recent.front() == RecentCommand{ commandLine, {}, false } );
}

// View -> Show Value Names switches them for the tab in front, as Wrap text
// does: each tab keeps its own, and the action shows the front tab's (#647).
SCENARIO( "Show Value Names is switched per tab", "[ui][tabs][valuenames]" )
{
    TabsWindow window( false );
    const ThreeLogFiles files;
    window.open( files.paths.mid( 0, 2 ) );

    auto* showValueNames
        = fileMenuAction( *window.mainWindow, logsquirl::mainwindow::action::showValueNamesText );
    REQUIRE( showValueNames != nullptr );
    REQUIRE( showValueNames->isCheckable() );

    const auto crawlerAt = [ &window ]( int tab ) {
        auto* crawler = qobject_cast<CrawlerWidget*>( window.tabArea->widget( tab ) );
        REQUIRE( crawler != nullptr );
        return crawler;
    };

    GIVEN( "both tabs opened without Value Names, as the Policy says" )
    {
        window.tabArea->setCurrentIndex( 0 );
        REQUIRE_FALSE( showValueNames->isChecked() );

        WHEN( "the action is turned on in the first tab" )
        {
            showValueNames->trigger();

            THEN( "the first tab shows Value Names and the second does not" )
            {
                REQUIRE( crawlerAt( 0 )->isValueNamesShownSet() );
                REQUIRE_FALSE( crawlerAt( 1 )->isValueNamesShownSet() );
            }

            AND_WHEN( "the second tab comes to the front, and then the first again" )
            {
                window.tabArea->setCurrentIndex( 1 );
                const bool checkedForSecond = showValueNames->isChecked();
                window.tabArea->setCurrentIndex( 0 );

                THEN( "the action shows each tab's own" )
                {
                    REQUIRE_FALSE( checkedForSecond );
                    REQUIRE( showValueNames->isChecked() );
                    REQUIRE( crawlerAt( 0 )->isValueNamesShownSet() );
                    REQUIRE_FALSE( crawlerAt( 1 )->isValueNamesShownSet() );
                }
            }
        }
    }
}
