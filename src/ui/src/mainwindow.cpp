/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

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

// This file implements MainWindow. It is responsible for creating and
// managing the menus, the toolbar, and the CrawlerWidget. It also
// load/save the settings on opening/closing of the app

#include "commandoutputdialog.h"
#include "configuration.h"
#include "containers.h"
#include "log.h"
#include <QNetworkReply>
#include <algorithm>
#include <cassert>
#include <exception>

#include <iterator>
#include <qaction.h>
#include <qapplication.h>
#include <tuple>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // Q_OS_WIN

#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProgressDialog>
#include <QResource>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStringListModel>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QUrlQuery>
#include <QWindow>

#include "mainwindow.h"

#include "chipmunkimporter.h"
#include "clipboard.h"
#include "commandpalette.h"
#include "crawlerwidget.h"
#include "decompressor.h"
#include "dispatch_to.h"
#include "downloader.h"
#include "encodings.h"
#include "favoritefiles.h"
#include "fileassociationchoice.h"
#include "fileassociations.h"
#include "fileassociationsdialog.h"
#include "highlightersdialog.h"
#include "highlightersmenu.h"
#include "indexcache.h"
#include "instancehandover.h"
#include "issuereporter.h"
#include "logger.h"
#include "logsquirl_version.h"
#include "mainwindowtext.h"
#include "menu.h"
#include "mergecontroller.h"
#include "openfilehelper.h"
#include "optionsdialog.h"
#include "plugindialog.h"
#include "predefinedfiltersdialog.h"
#include "progress.h"
#include "readablesize.h"
#include "recentfiles.h"
#include "regexlabsource.h"
#include "regexlabwindow.h"
#include "shortcuts.h"
#include "tabbedcrawlerwidget.h"
#include "teamfolder.h"
#include "theme.h"
#include "valuenamescollection.h"
#include "valuenamesdialog.h"

namespace {

// Followed as it opens, not one event loop turn later: a follow queued for
// then would overrule what came in between -- the user turning it off, or a
// tab shown unfollowed for a moment to a command that ends at once (#635).
// The window's follow action follows from the View Set.
void followOnOpen( CrawlerWidget* crawler_widget )
{
    crawler_widget->followSet( true );
}

static constexpr auto ClipboardMaxTry = 5;

// Shows the Team groups of a kind in the dialog that edits that kind, and
// publishes what OK or Apply change of them. What Apply published has new
// revisions: the dialog is still open and edits on them.
template <typename Dialog, typename Group>
void showTeamGroupsIn( Dialog& dialog, TeamFolder& folder, const QList<Group>& groups,
                       QHash<QString, QString> ( TeamFolder::*revisions )() const )
{
    dialog.showTeamGroups( groups, folder.isWritable(), ( folder.*revisions )() );
    QObject::connect( &dialog, &Dialog::publishRequested, &folder, &TeamFolder::publish );
    QObject::connect( &folder, &TeamFolder::publishFinished, &dialog,
                      [ &dialog, &folder, revisions ]( const auto& outcome ) {
                          QStringList ids;
                          for ( const auto& result : outcome.results ) {
                              if ( result.status == logsquirl::teamfolder::PublishStatus::Published
                                   || result.status
                                          == logsquirl::teamfolder::PublishStatus::Pending ) {
                                  ids.append( result.request.id );
                              }
                          }
                          dialog.updateTeamRevisions( ids, ( folder.*revisions )() );
                      } );
}

} // namespace

QTranslator MainWindow::mTranslator;
QTranslator MainWindow::mQtTranslator;

MainWindow::MainWindow( WindowSession session,
                        std::shared_ptr<logsquirl::plugins::ApplicationPlugins> plugins )
    : session_( std::move( session ) )
    , mainIcon_()
    , quickFindMux_( session_.getQuickFindPattern() )
    , mainTabWidget_()
    , tempDir_( QDir::temp().filePath( "logsquirl_temp_" ) )
    , archiveRestores_( tempDir_.path(), this )
    , plugins_( std::move( plugins ) )
{
    createActions();

    // The plugins are not discovered here: they load once for the
    // application, after the first window shows (#303). The Sources menu and
    // the dashboard list them once they have.
    createMenus();
    createToolBars();

    setAcceptDrops( true );

    // Default geometry
    const QRect geometry = QApplication::primaryScreen()->availableGeometry();
    setGeometry( geometry.x() + 20, geometry.y() + 40, geometry.width() - 140,
                 geometry.height() - 140 );

    mainIcon_.addFile( ":/images/hicolor/16x16/logsquirl.png" );
    // mainIcon_.addFile( ":/images/hicolor/24x24/logsquirl.png" );
    mainIcon_.addFile( ":/images/hicolor/32x32/logsquirl.png" );
    mainIcon_.addFile( ":/images/hicolor/48x48/logsquirl.png" );

    setWindowIcon( mainIcon_ );
    readSettings();

    createTrayIcon();

    // Configure the main tabbed widget
    mainTabWidget_.setDocumentMode( true );
    mainTabWidget_.setMovable( true );
    // mainTabWidget_.setTabShape( QTabWidget::Triangular );
    mainTabWidget_.setTabsClosable( true );

    // Create the right sidebar dock with a tabbed panel
    sidebarDock_ = new QDockWidget( tr( "Sidebar" ), this );
    sidebarDock_->setObjectName( "sidebarDock" );
    sidebarDock_->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );

    sidebarTabs_ = new QTabWidget( sidebarDock_ );
    sidebarTabs_->addTab( &filtersPanel_, tr( "Filters" ) );
    sidebarTabs_->addTab( &scratchPad_, tr( "Scratchpad" ) );
    sidebarTabs_->addTab( &valueNamesPanel_, tr( "Value Names" ) );
    sidebarDock_->setWidget( sidebarTabs_ );
    addDockWidget( Qt::RightDockWidgetArea, sidebarDock_ );

    // Replace the default title bar with a custom widget for reliable button sizing
    {
        auto* titleBar = new QWidget( sidebarDock_ );
        auto* titleLayout = new QHBoxLayout( titleBar );
        titleLayout->setContentsMargins( 6, 2, 6, 2 );

        auto* titleLabel = new QLabel( tr( "Sidebar" ), titleBar );
        titleLayout->addWidget( titleLabel );
        titleLayout->addStretch();

        constexpr int kButtonSize = 24;
        constexpr int kIconSize = 16;

        // Their icons are set by loadIcons().
        auto* floatButton = new QToolButton( titleBar );
        sidebarFloatButton_ = floatButton;
        floatButton->setFixedSize( kButtonSize, kButtonSize );
        floatButton->setIconSize( QSize( kIconSize, kIconSize ) );
        floatButton->setAutoRaise( true );
        floatButton->setToolTip( tr( "Float" ) );
        titleLayout->addWidget( floatButton );

        auto* closeButton = new QToolButton( titleBar );
        sidebarCloseButton_ = closeButton;
        closeButton->setFixedSize( kButtonSize, kButtonSize );
        closeButton->setIconSize( QSize( kIconSize, kIconSize ) );
        closeButton->setAutoRaise( true );
        closeButton->setToolTip( tr( "Close" ) );
        titleLayout->addWidget( closeButton );

        sidebarDock_->setTitleBarWidget( titleBar );

        connect( closeButton, &QToolButton::clicked, sidebarDock_, &QDockWidget::close );
        connect( floatButton, &QToolButton::clicked, sidebarDock_,
                 [ this ] { sidebarDock_->setFloating( !sidebarDock_->isFloating() ); } );
    }

    sidebarDock_->hide();

    // The sidebar opens at the width the user left it at in this window, or
    // at a moderate share of the window, leaving the Log File the rest (#261).
    // Docked at its size hint it took almost half the window.
    sidebarWidth_ = session_.sidebarWidth();
    connect( sidebarDock_, &QDockWidget::visibilityChanged, this, [ this ]( bool visible ) {
        if ( sidebarDock_->isFloating() ) {
            return;
        }
        if ( visible && !sidebarWidthApplied_ ) {
            sidebarWidthApplied_ = true;
            const auto width = sidebarWidth_ > 0 ? sidebarWidth_
                                                 : this->width() * SidebarDefaultWidthPercent / 100;
            resizeDocks( { sidebarDock_ }, { width }, Qt::Horizontal );
        }
        else if ( !visible && sidebarWidthApplied_ ) {
            sidebarWidth_ = sidebarDock_->width();
        }
    } );

    // Route filter panel selections to the active crawler widget. Its Search
    // line decides whether the Search runs now, as for adding a word to it:
    // starting it here as well ran every Search twice (#538).
    connect( &filtersPanel_, &FiltersPanel::filtersChanged, this,
             [ this ]( const QList<PredefinedFilter>& filters ) {
                 if ( auto crawler = currentCrawlerWidget() ) {
                     crawler->setSearchPatternFromPredefinedFilters( filters );
                 }
             } );

    // Open the predefined filters dialog when the sidebar "Edit..." button is clicked.
    connect( &filtersPanel_, &FiltersPanel::editFiltersRequested, this,
             [ this ]() { editPredefinedFilters(); } );

    // A check of the Value Names tab is global: every open Log File and every
    // window's sidebar is told (#647).
    connect( &valueNamesPanel_, &ValueNamesPanel::valueNamesChanged, this,
             [ this ]() { session_.applyChange( Changed::ValueNames ); } );
    connect( &valueNamesPanel_, &ValueNamesPanel::editRequested, this,
             [ this ]() { editValueNames(); } );

    connect( &mainTabWidget_, &TabbedCrawlerWidget::tabCloseRequested, this,
             [ this ]( int index ) { this->closeTab( index, ActionInitiator::User ); } );
    connect( &mainTabWidget_, &TabbedCrawlerWidget::bulkTabCloseRequested, this,
             [ this ]( const QList<int>& indices ) {
                 this->closeTabs( indices, ActionInitiator::User );
             } );
    connect( &mainTabWidget_, &TabbedCrawlerWidget::currentChanged, this,
             &MainWindow::currentTabChanged );
    connect( &mainTabWidget_, &TabbedCrawlerWidget::mergeRequested, this,
             &MainWindow::openMergedFiles );

    // Establish the QuickFindWidget and mux ( to send requests from the
    // QFWidget to the right window )
    connect( &quickFindWidget_, SIGNAL( patternConfirmed( const QString&, bool, bool ) ),
             &quickFindMux_, SLOT( confirmPattern( const QString&, bool, bool ) ) );
    connect( &quickFindWidget_, SIGNAL( patternUpdated( const QString&, bool, bool ) ),
             &quickFindMux_, SLOT( setNewPattern( const QString&, bool, bool ) ) );
    connect( &quickFindWidget_, SIGNAL( cancelSearch() ), &quickFindMux_, SLOT( cancelSearch() ) );
    connect( &quickFindWidget_, SIGNAL( searchForward() ), &quickFindMux_,
             SLOT( searchForward() ) );
    connect( &quickFindWidget_, SIGNAL( searchBackward() ), &quickFindMux_,
             SLOT( searchBackward() ) );
    connect( &quickFindWidget_, SIGNAL( searchNext() ), &quickFindMux_, SLOT( searchNext() ) );

    // QuickFind changes coming from the views
    connect( &quickFindMux_, SIGNAL( patternChanged( const QString& ) ), this,
             SLOT( changeQFPattern( const QString& ) ) );
    connect( &quickFindMux_, SIGNAL( notify( const QFNotification& ) ), &quickFindWidget_,
             SLOT( notify( const QFNotification& ) ) );
    connect( &quickFindMux_, SIGNAL( clearNotification() ), &quickFindWidget_,
             SLOT( clearNotification() ) );

    // Construct the QuickFind bar
    quickFindWidget_.hide();
    applyQuickFindPolicy();

    // Told of every settings change, whichever window it was made in.
    session_.addWindow( this );

    // Build the central layout with the tab widget, quick-find bar, and
    // welcome dashboard as a permanent pinned first tab (if enabled).
    const auto& config = Configuration::get();

    if ( config.showDashboard() ) {
        welcomeDashboard_ = new WelcomeDashboard();
        welcomeDashboard_->setPlugins( &plugins_->catalog(), &plugins_->host() );

        // Insert dashboard as the permanent first tab (index 0)
        mainTabWidget_.insertTab( 0, welcomeDashboard_, tr( "Dashboard" ) );
        // Disable the close button on the dashboard tab
        mainTabWidget_.tabBar()->setTabButton( 0, QTabBar::RightSide, nullptr );
        mainTabWidget_.tabBar()->setTabButton( 0, QTabBar::LeftSide, nullptr );
        // Always show the tab bar so the dashboard is accessible
        mainTabWidget_.tabBar()->show();
        mainTabWidget_.setCurrentIndex( 0 );
    }

    QWidget* centralContainer = new QWidget();
    auto* centralLayout = new QVBoxLayout();
    centralLayout->setContentsMargins( 0, 0, 0, 0 );
    centralLayout->setSpacing( 0 );
    centralLayout->addWidget( &mainTabWidget_ );
    centralLayout->addWidget( &quickFindWidget_ );
    centralContainer->setLayout( centralLayout );
    setCentralWidget( centralContainer );

    // Wire dashboard signals (only if dashboard is enabled)
    if ( welcomeDashboard_ ) {
        connect( welcomeDashboard_, &WelcomeDashboard::openFileRequested, this,
                 [ this ]( const QString& path ) { loadFile( path ); } );
        connect( welcomeDashboard_, &WelcomeDashboard::openFileDialogRequested, this,
                 &MainWindow::open );
        connect( welcomeDashboard_, &WelcomeDashboard::loadSessionRequested, this,
                 &MainWindow::reloadSession );

        welcomeDashboard_->refresh();
    }

    // What plugins contribute shows in every window (#303); added before the
    // plugins load, so what they register while loading is shown.
    pluginUi_ = std::make_unique<PluginUiAdapter>( *this, *pluginsMenu, pluginMenuSeparator_,
                                                   *sidebarTabs_ );
    // A plugin's Regex Lab samples the tab in front, as the menu's does (#662).
    pluginUi_->setRegexLabSampleSource( [ this ]() { return tabInFrontAsRegexLabSample(); } );
    // A plugin goes to a Log Line of the tab in front and reads its selected
    // Log Lines (#663).
    pluginUi_->setTabInFront( [ this ]() { return currentCrawlerWidget(); } );
    plugins_->uiPort().addWindow( pluginUi_.get() );
    servePluginCallbacks();

    connectTeamFolder();

    connect( &archiveRestores_, &ArchiveMemberDecompression::decompressing, this,
             &MainWindow::showArchiveRestoreProgress );
    connect( &archiveRestores_, &ArchiveMemberDecompression::idle, this,
             &MainWindow::closeArchiveRestoreProgress );

    plugins_->whenLoaded( this, [ this ] {
        updateSourcesMenu();
        if ( welcomeDashboard_ ) {
            welcomeDashboard_->refresh();
        }
    } );

    // A plugin installed, enabled or disabled later -- from this window's
    // Plugins dialog or another's -- shows on the dashboard right away (#710).
    if ( welcomeDashboard_ ) {
        const auto refreshDashboard = [ this ] { welcomeDashboard_->refresh(); };
        auto& pluginHost = plugins_->host();
        connect( &pluginHost, &logsquirl::plugins::PluginHost::pluginLoaded, this,
                 refreshDashboard );
        connect( &pluginHost, &logsquirl::plugins::PluginHost::pluginUnloaded, this,
                 refreshDashboard );
    }

    updateTitleBar( "" );
    loadIcons();
    Theme::whenApplied( this, [ this ] {
        loadIcons();
        updateOpenedFilesMenu();
        updateFavoritesMenu();
        updateHighlightersMenu();
    } );
    reTranslateUI();

    // Accessibility: set accessible names on main widgets
    setAccessibleName( tr( "LogSquirl main window" ) );
    mainTabWidget_.setAccessibleName( tr( "Open files" ) );
}

void MainWindow::reloadGeometry()
{
    QByteArray geometry;

    session_.restoreGeometry( &geometry );
    restoreGeometry( geometry );
}

void MainWindow::reloadSession()
{
    restoreWindow( session_.storedSnapshot() );
}

std::vector<QString> MainWindow::restoreWindow( const WindowSnapshot& window )
{
    const auto& config = Configuration::get();
    const auto followFileOnLoad
        = config.followFileOnLoad() && session_.watchPolicy().anyWatchEnabled();

    // The widgets are kept as they are built, in the order the Session opens
    // their Log Files, so that nothing has to be cast back from the views.
    std::vector<CrawlerWidget*> crawlers;
    int currentFileIndex = -1;
    std::vector<WindowSession::DeferredArchiveFile> fromArchives;
    const auto openedFiles = session_.restore(
        window,
        [ &crawlers ]( const ViewBuild& build ) {
            crawlers.push_back( new CrawlerWidget( build ) );
            return crawlers.back();
        },
        &currentFileIndex, {}, &fromArchives );

    // Only the current tab's Log File loads now, the others after it (#300).
    // Adding a tab makes it current for a moment, which is not the user
    // activating it.
    restoringSession_ = true;
    std::vector<QString> tabsAdded;
    for ( size_t i = 0; i < crawlers.size() && i < openedFiles.size(); ++i ) {
        auto* crawlerWidget = crawlers[ i ];
        const auto& fileName = openedFiles[ i ].first;
        tabsAdded.push_back( fileName );
        // Its name and group are found by its archive, not by the new
        // temporary path (#609).
        mainTabWidget_.addCrawler( crawlerWidget, fileName, LogFileLifetime::Ordinary,
                                   archiveMembers_.value( fileName ).key() );

        if ( followFileOnLoad ) {
            followOnOpen( crawlerWidget );
        }
    }
    restoringSession_ = false;

    if ( currentFileIndex >= 0 && static_cast<size_t>( currentFileIndex ) < crawlers.size() ) {
        // By widget: the dashboard tab, if any, comes before the Log Files.
        mainTabWidget_.setCurrentWidget( crawlers[ static_cast<size_t>( currentFileIndex ) ] );
    }

    mainTabWidget_.refreshAllTabGroupAppearances();

    updateOpenedFilesMenu();

    // A Log File decompressed from an archive is decompressed again, as the
    // user opened it, with no question asked (#596), and in the background:
    // the window is in use meanwhile, and its tab comes once it is done (#610).
    for ( const auto& file : fromArchives ) {
        archiveRestores_.decompress(
            file.archiveMember,
            [ this, id = file.id, member = file.archiveMember ]( const QString& fileName ) {
                openRestoredFromArchive( id, member, fileName );
            } );
    }

    return tabsAdded;
}

void MainWindow::openRestoredFromArchive( int deferredId, const ArchiveMember& member,
                                          const QString& fileName )
{
    // Gone, broken or cancelled: left out as quietly as a gone Log File (#596).
    if ( fileName.isEmpty() ) {
        LOG_INFO << "Not restoring " << member.archive << ": it cannot be decompressed again";
        session_.dropDeferred( deferredId );
        return;
    }
    archiveMembers_.insert( fileName, member );

    std::vector<const ViewInterface*> tabs;
    const auto logFileTabs = mainTabWidget_.logFileTabs();
    for ( const auto i : logFileTabs ) {
        tabs.push_back( qobject_cast<const CrawlerWidget*>( mainTabWidget_.widget( i ) ) );
    }

    CrawlerWidget* crawlerWidget = nullptr;
    const auto opened = session_.openDeferred(
        deferredId, fileName,
        [ &crawlerWidget ]( const ViewBuild& build ) {
            crawlerWidget = new CrawlerWidget( build );
            return crawlerWidget;
        },
        tabs, currentCrawlerWidget() );
    if ( !crawlerWidget ) {
        return;
    }

    // Named and grouped as the Session File it came from says, now that it
    // opens (#576).
    applyTabLabels( pendingTabLabels_, [ &member ]( const SessionInfo::OpenFile& file ) {
        return file.archiveMember.key() == member.key();
    } );

    // Where it stood among the tabs: before the Log File tab at its position,
    // else after the last one.
    const auto tabIndex = opened.position < static_cast<size_t>( logFileTabs.size() )
                              ? logFileTabs[ static_cast<qsizetype>( opened.position ) ]
                          : logFileTabs.isEmpty() ? mainTabWidget_.count()
                                                  : logFileTabs.back() + 1;

    // Adding a tab makes it current for a moment, which is neither the user
    // activating it nor the tab in front changing.
    auto* front = mainTabWidget_.currentWidget();
    restoringSession_ = true;
    // Its name and group are found by its archive (#609).
    mainTabWidget_.addCrawler( crawlerWidget, fileName, LogFileLifetime::Ordinary, member.key(),
                               tabIndex );
    restoringSession_ = false;
    if ( !opened.inFront && front ) {
        mainTabWidget_.setCurrentWidget( front );
    }

    const auto& config = Configuration::get();
    if ( config.followFileOnLoad() && session_.watchPolicy().anyWatchEnabled() ) {
        followOnOpen( crawlerWidget );
    }

    mainTabWidget_.refreshAllTabGroupAppearances();
    updateOpenedFilesMenu();
}

void MainWindow::showArchiveRestoreProgress( const QString& archive )
{
    // The progress opening an archive by hand shows, but beside the window,
    // which stays in use; only an archive that takes a while shows it.
    if ( !archiveRestoreProgress_ ) {
        archiveRestoreProgress_ = new QProgressDialog( this );
        archiveRestoreProgress_->setAttribute( Qt::WA_ShowWithoutActivating );
        archiveRestoreProgress_->setWindowModality( Qt::NonModal );
        archiveRestoreProgress_->setAutoClose( false );
        archiveRestoreProgress_->setAutoReset( false );
        archiveRestoreProgress_->setRange( 0, 0 );
        // Cancelling it leaves out the tab of the archive it shows.
        connect( archiveRestoreProgress_, &QProgressDialog::canceled, this, [ this ] {
            archiveRestores_.cancelCurrent();
            closeArchiveRestoreProgress();
        } );
        QTimer::singleShot( archiveRestoreProgress_->minimumDuration(), archiveRestoreProgress_,
                            [ progress = archiveRestoreProgress_ ] { progress->show(); } );
    }
    archiveRestoreProgress_->setLabelText(
        tr( "Extracting %1" ).arg( QDir::toNativeSeparators( archive ) ) );
}

void MainWindow::closeArchiveRestoreProgress()
{
    if ( archiveRestoreProgress_ ) {
        archiveRestoreProgress_->deleteLater();
        archiveRestoreProgress_ = nullptr;
    }
}

void MainWindow::loadInitialFile( QString fileName, bool followFile )
{
    LOG_DEBUG << "loadInitialFile";

    // Is there a file passed as argument?
    if ( !fileName.isEmpty() ) {
        loadFile( fileName, followFile );
    }
}

void MainWindow::openStandardInput()
{
    if ( std::ranges::any_of( commandSources_, []( const auto& source ) {
             return source.second->kind() == CommandSource::Kind::StandardInput;
         } ) ) {
        return;
    }

    QString error;
    auto source = CommandSource::readStandardInput( 0, &error );
    if ( !source ) {
        QMessageBox::warning( this, tr( "Standard input" ), error );
        return;
    }
    const auto filePath = source->spoolPath();
    openCommandSource( std::move( source ), tr( "stdin" ), standardInputToolTip( filePath ) );
}

void MainWindow::openHandedOverStandardInput( const QString& spoolPath, const QString& displayName )
{
    if ( !isStandardInputSpool( spoolPath ) ) {
        // Not a file this window may own and remove. It is not taken over, so
        // the secondary instance finds no marker, reports the failed hand-over
        // and removes its file itself.
        LOG_WARNING << "Handed over as standard input, but no spool file: " << spoolPath;
        return;
    }
    const auto title = displayName.isEmpty() ? tr( "stdin" ) : displayName;
    openCommandSource( CommandSource::adoptSpoolFile( spoolPath ), title,
                       standardInputToolTip( spoolPath ) );
    bringToFront();
}

QString MainWindow::standardInputToolTip( const QString& spoolPath )
{
    return tr( "Standard input\n%1" ).arg( QDir::toNativeSeparators( spoolPath ) );
}

QString MainWindow::commandToolTip( const CommandSource& source )
{
    const auto& command = source.command();
    return tr( "%1\nWorking folder: %2\n%3" )
        .arg( command.commandLine, QDir::toNativeSeparators( command.workingFolder ),
              QDir::toNativeSeparators( source.spoolPath() ) );
}

bool MainWindow::openCommandOutput( const RecentCommand& command )
{
    QString error;
    auto source = CommandSource::startCommand( command, &error );
    if ( !source ) {
        QMessageBox::warning( this, tr( "Open Command Output" ), error );
        return false;
    }
    const auto title = commandTabTitle( source->command().commandLine );
    const auto toolTip = commandToolTip( *source );
    return openCommandSource( std::move( source ), title, toolTip );
}

bool MainWindow::openCommandSource( std::unique_ptr<CommandSource> source, const QString& title,
                                    const QString& toolTip )
{
    const auto filePath = source->spoolPath();
    // The spool lives as long as its tab: it is not saved with the Session
    // (#570).
    return openLogFile(
        filePath, LogFileProvenance::commandOutput( std::move( source ), title, toolTip ), true );
}

void MainWindow::showCommandSourceEnded( const QString& spoolPath, const CommandEnd& end )
{
    const auto source = commandSources_.find( spoolPath );
    if ( source == commandSources_.end() ) {
        return;
    }

    // The spool file grows no more. A tab not open yet -- waiting for the
    // plugins to load -- ends following when it opens.
    if ( const auto tab = mainTabWidget_.tabOfPath( spoolPath ); tab >= 0 ) {
        if ( auto* crawler = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( tab ) ) ) {
            crawler->endFollowing();
        }
    }

    if ( source->second->kind() != CommandSource::Kind::Command ) {
        statusBar()->showMessage( tr( "Standard input closed" ) );
        return;
    }

    // The tab keeps what the command wrote; its title, whatever the tab is
    // named, and its tooltip tell how the command ended.
    const auto title = commandTabTitle( source->second->command().commandLine );
    mainTabWidget_.setOpeningTitle( spoolPath, title,
                                    commandToolTip( *source->second ) + "\n"
                                        + CommandSource::endedToolTip( end ) );
    mainTabWidget_.setTitleFormat( spoolPath,
                                   CommandSource::endedTitle( QStringLiteral( "%1" ), end ) );
    showStatusMessage( CommandSource::endedMessage( title, end ) );
}

void MainWindow::applyCommandOutputEncoding( const QString& spoolPath )
{
    const auto source = commandSources_.find( spoolPath );
    if ( source == commandSources_.end() || source->second->outputEncoding() == nullptr ) {
        return;
    }
    const auto tab = mainTabWidget_.tabOfPath( spoolPath );
    auto* crawler
        = tab >= 0 ? qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( tab ) ) : nullptr;
    // The one the settings force or the user chose stays.
    if ( crawler == nullptr || crawler->encodingMib() ) {
        return;
    }

    crawler->setEncoding( source->second->outputEncoding()->mibEnum() );
    if ( crawler == currentCrawlerWidget() ) {
        updateMenuBarFromDocument( crawler->state() );
        updateInfoLine();
    }
}

void MainWindow::reTranslateUI()
{
    using namespace logsquirl::mainwindow;
    // menu
    auto transMenu = []( const char* text ) -> auto {
        return QApplication::translate( "logsquirl::mainwindow::menu", text );
    };
    fileMenu->setTitle( transMenu( menu::fileTitle ) );
    editMenu->setTitle( transMenu( menu::editTitle ) );
    viewMenu->setTitle( transMenu( menu::viewTitle ) );
    openedFilesMenu->setTitle( transMenu( menu::openedFilesTitle ) );
    toolsMenu->setTitle( transMenu( menu::toolsTitle ) );
    highlightersMenu->setTitle( transMenu( menu::highlightersTitle ) );
    favoritesMenu->setTitle( transMenu( menu::favoritesTitle ) );
    helpMenu->setTitle( transMenu( menu::helpTitle ) );

    // toolbar
    toolBar->setToolTip(
        QApplication::translate( "logsquirl::mainwindow::toolbar", toolbar::toolbarTitle ) );

    // action
    auto transAction = []( const char* text ) -> auto {
        return QApplication::translate( "logsquirl::mainwindow::action", text );
    };
    newWindowAction->setText( transAction( action::newWindowText ) );
    newWindowAction->setStatusTip( transAction( action::newWindowStatusTip ) );

    openAction->setText( transAction( action::openText ) );
    openAction->setStatusTip( transAction( action::openStatusTip ) );

    recentFilesCleanup->setText( transAction( action::recentFilesCleanupText ) );

    closeAction->setText( transAction( action::closeText ) );
    closeAction->setStatusTip( transAction( action::closeStatusTip ) );

    closeAllAction->setText( transAction( action::closeAllText ) );
    closeAllAction->setStatusTip( transAction( action::closeAllStatusTip ) );

    exitAction->setText( transAction( action::exitText ) );
    exitAction->setStatusTip( transAction( action::exitStatusTip ) );

    copyAction->setText( transAction( action::copyText ) );
    copyAction->setStatusTip( transAction( action::copyStatusTip ) );
    copyAsShownAction->setText( transAction( action::copyAsShownText ) );
    copyAsShownAction->setStatusTip( transAction( action::copyAsShownStatusTip ) );

    selectAllAction->setText( transAction( action::selectAllText ) );
    selectAllAction->setStatusTip( transAction( action::selectAllStatusTip ) );

    goToLineAction->setText( transAction( action::goToLineText ) );
    goToLineAction->setStatusTip( transAction( action::goToLineStatusTip ) );

    goToTimestampAction->setText( transAction( action::goToTimestampText ) );
    goToTimestampAction->setStatusTip( transAction( action::goToTimestampStatusTip ) );

    searchLimitsTimeRangeAction->setText( transAction( action::searchLimitsTimeRangeText ) );
    searchLimitsTimeRangeAction->setStatusTip(
        transAction( action::searchLimitsTimeRangeStatusTip ) );
    searchLimitsAroundLineAction->setText( transAction( action::searchLimitsAroundLineText ) );
    searchLimitsAroundLineAction->setStatusTip(
        transAction( action::searchLimitsAroundLineStatusTip ) );

    findAction->setText( transAction( action::findText ) );
    findAction->setStatusTip( transAction( action::findStatusTip ) );

    clearLogAction->setText( transAction( action::clearLogText ) );
    clearLogAction->setStatusTip( transAction( action::clearLogStatusTip ) );

    openContainingFolderAction->setText( transAction( action::openContainingFolderText ) );
    openContainingFolderAction->setStatusTip(
        transAction( action::openContainingFolderStatusTip ) );

    openInEditorAction->setText( transAction( action::openInEditorText ) );
    openInEditorAction->setStatusTip( transAction( action::openInEditorStatusTip ) );

    copyPathToClipboardAction->setText( transAction( action::copyPathToClipboardText ) );
    copyPathToClipboardAction->setStatusTip( transAction( action::copyPathToClipboardStatusTip ) );

    openClipboardAction->setText( transAction( action::openClipboardText ) );
    openClipboardAction->setStatusTip( transAction( action::openClipboardStatusTip ) );

    openUrlAction->setText( transAction( action::openUrlText ) );
    openUrlAction->setStatusTip( transAction( action::openUrlStatusTip ) );

    openSessionAction->setText( transAction( action::openSessionText ) );
    openSessionAction->setStatusTip( transAction( action::openSessionStatusTip ) );
    saveSessionAsAction->setText( transAction( action::saveSessionAsText ) );
    saveSessionAsAction->setStatusTip( transAction( action::saveSessionAsStatusTip ) );

    openCommandOutputAction->setText( transAction( action::openCommandOutputText ) );
    openCommandOutputAction->setStatusTip( transAction( action::openCommandOutputStatusTip ) );

    overviewVisibleAction->setText( transAction( action::overviewVisibleText ) );

    lineNumbersVisibleInMainAction->setText( transAction( action::lineNumbersVisibleInMainText ) );
    lineNumbersVisibleInFilteredAction->setText(
        transAction( action::lineNumbersVisibleInFilteredText ) );

    followAction->setText( transAction( action::followText ) );
    textWrapAction->setText( transAction( action::wrapText ) );
    showValueNamesAction->setText( transAction( action::showValueNamesText ) );
    showValueNamesAction->setStatusTip( transAction( action::showValueNamesStatusTip ) );
    reloadAction->setText( transAction( action::reloadText ) );
    stopAction->setText( transAction( action::stopText ) );

    optionsAction->setText( transAction( action::optionsText ) );
    optionsAction->setStatusTip( transAction( action::optionsStatusTip ) );

    editHighlightersAction->setText( transAction( action::editHighlightersText ) );
    editHighlightersAction->setStatusTip( transAction( action::editHighlightersStatusTip ) );

    showDocumentationAction->setText( transAction( action::showDocumentationText ) );
    showDocumentationAction->setStatusTip( transAction( action::showDocumentationStatusTip ) );

    aboutAction->setText( transAction( action::aboutText ) );
    aboutAction->setStatusTip( transAction( action::aboutStatusTip ) );

    aboutQtAction->setText( transAction( action::aboutQtText ) );
    aboutQtAction->setStatusTip( transAction( action::aboutQtStatusTip ) );

    reportIssueAction->setText( transAction( action::reportIssueText ) );
    reportIssueAction->setStatusTip( transAction( action::reportIssueStatusTip ) );

    generateDumpAction->setText( transAction( action::generateDumpText ) );
    generateDumpAction->setStatusTip( transAction( action::generateDumpStatusTip ) );

    showScratchPadAction->setText( transAction( action::showScratchPadText ) );
    showScratchPadAction->setStatusTip( transAction( action::showScratchPadStatusTip ) );

    commandPaletteAction->setText( transAction( action::commandPaletteText ) );
    commandPaletteAction->setStatusTip( transAction( action::commandPaletteStatusTip ) );

    showFiltersPanelAction->setText( transAction( action::showFiltersPanelText ) );
    showFiltersPanelAction->setStatusTip( transAction( action::showFiltersPanelStatusTip ) );
    showValueNamesPanelAction->setText( transAction( action::showValueNamesPanelText ) );
    showValueNamesPanelAction->setStatusTip( transAction( action::showValueNamesPanelStatusTip ) );

    toggleSidebarAction->setText( transAction( action::toggleSidebarText ) );
    toggleSidebarAction->setStatusTip( transAction( action::toggleSidebarStatusTip ) );

    toggleChartPanelAction->setText( transAction( action::toggleChartPanelText ) );
    toggleChartPanelAction->setStatusTip( transAction( action::toggleChartPanelStatusTip ) );

    showFilterFrequencyAction->setText( transAction( action::showFilterFrequencyText ) );
    showFilterFrequencyAction->setStatusTip( transAction( action::showFilterFrequencyStatusTip ) );

    importChipmunkFiltersAction->setText( transAction( action::importChipmunkFiltersText ) );
    importChipmunkFiltersAction->setStatusTip(
        transAction( action::importChipmunkFiltersStatusTip ) );

    auto curFavoritesIconText = addToFavoritesAction->data().toBool()
                                    ? transAction( action::addToFavoritesText )
                                    : transAction( action::removeFromFavoritesText );
    addToFavoritesAction->setText( curFavoritesIconText );
    addToFavoritesMenuAction->setText( transAction( action::addToFavoritesText ) );

    removeFromFavoritesAction->setText( transAction( action::removeFromFavoritesText ) );

    selectOpenFileAction->setText( transAction( action::selectOpenFileText ) );

    predefinedFiltersDialogAction->setText( transAction( action::predefinedFiltersDialogText ) );
    predefinedFiltersDialogAction->setStatusTip(
        transAction( action::predefinedFiltersDialogStatusTip ) );
    valueNamesDialogAction->setText( transAction( action::valueNamesDialogText ) );
    valueNamesDialogAction->setStatusTip( transAction( action::valueNamesDialogStatusTip ) );

    regexLabAction->setText( transAction( action::regexLabText ) );
    regexLabAction->setStatusTip( transAction( action::regexLabStatusTip ) );

    // trayIcon
    trayIcon_->setToolTip( QApplication::translate(
        "logsquirl::mainwindow::trayicon", logsquirl::mainwindow::trayicon::trayiconTip ) );
}

int MainWindow::installLanguage( QString lang )
{
    if ( lang.isEmpty() ) {
        return -1;
    }

    QApplication::removeTranslator( &mTranslator );
    QApplication::removeTranslator( &mQtTranslator );

    // Qt's own strings (standard buttons, file dialogs) come from the Qt the
    // build found, which need not have every language LogSquirl has: without
    // them the application's strings are still translated (#448).
    QString qtPath( ":/i18n/qt_" + lang + ".qm" );
    QResource qtTranslations( qtPath );
    if ( !qtTranslations.isValid()
         || !mQtTranslator.load( qtTranslations.data(), (int)qtTranslations.size() ) ) {
        LOG_WARNING << "No Qt translation for " << lang;
    }
    else if ( !QApplication::installTranslator( &mQtTranslator ) ) {
        LOG_ERROR << "install fail";
        return -1;
    }

    QString appPath( ":/i18n/" + lang + ".qm" );
    QResource appTranslations( appPath );
    if ( !appTranslations.isValid()
         || !mTranslator.load( appTranslations.data(), (int)appTranslations.size() ) ) {
        LOG_ERROR << "No translation for " << lang;
        return -1;
    }
    if ( !QApplication::installTranslator( &mTranslator ) ) {
        LOG_ERROR << "install fail";
        return -1;
    }

    return 0;
}

// Menu actions
void MainWindow::createActions()
{
    const auto& config = Configuration::get();
    const auto shortcuts = config.shortcuts();

    using namespace logsquirl::mainwindow;

    newWindowAction = new QAction( tr( action::newWindowText ), this );
    newWindowAction->setStatusTip( tr( action::newWindowStatusTip ) );
    connect( newWindowAction, &QAction::triggered, [ this ] { Q_EMIT newWindow(); } );
    newWindowAction->setVisible( config.allowMultipleWindows() );

    openAction = new QAction( tr( action::openText ), this );
    openAction->setStatusTip( tr( action::openStatusTip ) );
    connect( openAction, &QAction::triggered, [ this ]( auto ) { this->open(); } );

    recentFilesCleanup = new QAction( tr( action::recentFilesCleanupText ), this );
    connect( recentFilesCleanup, &QAction::triggered, this,
             [ this ]( auto ) { this->clearRecentFileActions(); } );

    closeAction = new QAction( tr( action::closeText ), this );
    closeAction->setStatusTip( tr( action::closeStatusTip ) );
    connect( closeAction, &QAction::triggered, this,
             [ this ]( auto ) { this->closeTab( ActionInitiator::User ); } );

    closeAllAction = new QAction( tr( action::closeAllText ), this );
    closeAllAction->setStatusTip( tr( action::closeAllStatusTip ) );
    connect( closeAllAction, &QAction::triggered, this,
             [ this ]( auto ) { this->closeAll( ActionInitiator::User ); } );

    recentFilesGroup = new QActionGroup( this );
    connect( recentFilesGroup, &QActionGroup::triggered, this, &MainWindow::openFileFromRecent );
    for ( auto i = 0u; i < recentFileActions.size(); ++i ) {
        recentFileActions[ i ] = new QAction( this );
        connect( recentFileActions[ i ], &QAction::hovered, [ this, a = recentFileActions[ i ] ]() {
            QToolTip::showText( QCursor::pos(), a->toolTip(), this );
        } );
        recentFileActions[ i ]->setVisible( false );
        recentFileActions[ i ]->setActionGroup( recentFilesGroup );
    }

    exitAction = new QAction( tr( action::exitText ), this );
    exitAction->setStatusTip( tr( action::exitStatusTip ) );
    connect( exitAction, &QAction::triggered, this, &MainWindow::exitRequested );

    copyAction = new QAction( tr( action::copyText ), this );
    copyAction->setStatusTip( tr( action::copyStatusTip ) );
    connect( copyAction, &QAction::triggered, this, [ this ]( auto ) { this->copy(); } );

    copyAsShownAction = new QAction( tr( action::copyAsShownText ), this );
    copyAsShownAction->setStatusTip( tr( action::copyAsShownStatusTip ) );
    connect( copyAsShownAction, &QAction::triggered, this,
             [ this ]( auto ) { this->copyAsShown(); } );

    selectAllAction = new QAction( tr( action::selectAllText ), this );
    selectAllAction->setStatusTip( tr( action::selectAllStatusTip ) );
    connect( selectAllAction, &QAction::triggered, this, [ this ]( auto ) { this->selectAll(); } );

    goToLineAction = new QAction( tr( action::goToLineText ), this );
    goToLineAction->setStatusTip( tr( action::goToLineStatusTip ) );

    goToTimestampAction = new QAction( tr( action::goToTimestampText ), this );
    goToTimestampAction->setStatusTip( tr( action::goToTimestampStatusTip ) );

    searchLimitsTimeRangeAction = new QAction( tr( action::searchLimitsTimeRangeText ), this );
    searchLimitsTimeRangeAction->setStatusTip( tr( action::searchLimitsTimeRangeStatusTip ) );

    searchLimitsAroundLineAction = new QAction( tr( action::searchLimitsAroundLineText ), this );
    searchLimitsAroundLineAction->setStatusTip( tr( action::searchLimitsAroundLineStatusTip ) );

    findAction = new QAction( tr( action::findText ), this );
    findAction->setStatusTip( tr( action::findStatusTip ) );
    // Named for the benchmark mode, which opens QuickFind as the user does (#668).
    findAction->setObjectName( "findAction" );
    connect( findAction, &QAction::triggered, this, [ this ]( auto ) { this->find(); } );

    clearLogAction = new QAction( tr( action::clearLogText ), this );
    clearLogAction->setStatusTip( tr( action::clearLogStatusTip ) );
    connect( clearLogAction, &QAction::triggered, this, [ this ]( auto ) { this->clearLog(); } );

    openContainingFolderAction = new QAction( tr( action::openContainingFolderText ), this );
    openContainingFolderAction->setStatusTip( tr( action::openContainingFolderStatusTip ) );
    connect( openContainingFolderAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openContainingFolder(); } );

    openInEditorAction = new QAction( tr( action::openInEditorText ), this );
    openInEditorAction->setStatusTip( tr( action::openInEditorStatusTip ) );
    connect( openInEditorAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openInEditor(); } );

    copyPathToClipboardAction = new QAction( tr( action::copyPathToClipboardText ), this );
    copyPathToClipboardAction->setStatusTip( tr( action::copyPathToClipboardStatusTip ) );
    connect( copyPathToClipboardAction, &QAction::triggered, this,
             [ this ]( auto ) { this->copyFullPath(); } );

    openClipboardAction = new QAction( tr( action::openClipboardText ), this );
    openClipboardAction->setStatusTip( tr( action::openClipboardStatusTip ) );
    connect( openClipboardAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openClipboard(); } );

    openUrlAction = new QAction( tr( action::openUrlText ), this );
    openUrlAction->setStatusTip( tr( action::openUrlStatusTip ) );
    connect( openUrlAction, &QAction::triggered, this, [ this ]( auto ) { this->openUrl(); } );

    openSessionAction = new QAction( tr( action::openSessionText ), this );
    openSessionAction->setStatusTip( tr( action::openSessionStatusTip ) );
    connect( openSessionAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openSession(); } );

    saveSessionAsAction = new QAction( tr( action::saveSessionAsText ), this );
    saveSessionAsAction->setStatusTip( tr( action::saveSessionAsStatusTip ) );
    connect( saveSessionAsAction, &QAction::triggered, this,
             [ this ]( auto ) { this->saveSessionAs(); } );

    openCommandOutputAction = new QAction( tr( action::openCommandOutputText ), this );
    openCommandOutputAction->setStatusTip( tr( action::openCommandOutputStatusTip ) );
    connect( openCommandOutputAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openCommandOutputDialog(); } );

    overviewVisibleAction = new QAction( tr( action::overviewVisibleText ), this );
    overviewVisibleAction->setCheckable( true );
    overviewVisibleAction->setChecked( config.isOverviewVisible() );
    connect( overviewVisibleAction, &QAction::toggled, this,
             &MainWindow::toggleOverviewVisibility );

    lineNumbersVisibleInMainAction
        = new QAction( tr( action::lineNumbersVisibleInMainText ), this );
    lineNumbersVisibleInMainAction->setCheckable( true );
    lineNumbersVisibleInMainAction->setChecked( config.mainLineNumbersVisible() );
    connect( lineNumbersVisibleInMainAction, &QAction::toggled, this,
             &MainWindow::toggleMainLineNumbersVisibility );

    lineNumbersVisibleInFilteredAction
        = new QAction( tr( action::lineNumbersVisibleInFilteredText ), this );
    lineNumbersVisibleInFilteredAction->setCheckable( true );
    lineNumbersVisibleInFilteredAction->setChecked( config.filteredLineNumbersVisible() );
    connect( lineNumbersVisibleInFilteredAction, &QAction::toggled, this,
             &MainWindow::toggleFilteredLineNumbersVisibility );

    followAction = new QAction( tr( action::followText ), this );
    // Named for the benchmark mode, which follows a growing Log File as the
    // user does (#670).
    followAction->setObjectName( "followAction" );
    followAction->setCheckable( true );
    followAction->setEnabled( session_.watchPolicy().anyWatchEnabled() );
    connect( followAction, &QAction::toggled, this, &MainWindow::followSet );

    textWrapAction = new QAction( tr( action::wrapText ), this );
    textWrapAction->setCheckable( true );
    textWrapAction->setEnabled( true );
    connect( textWrapAction, &QAction::toggled, this, &MainWindow::textWrapSet );

    showValueNamesAction = new QAction( tr( action::showValueNamesText ), this );
    showValueNamesAction->setStatusTip( tr( action::showValueNamesStatusTip ) );
    showValueNamesAction->setCheckable( true );
    connect( showValueNamesAction, &QAction::toggled, this, &MainWindow::valueNamesShownSet );

    reloadAction = new QAction( tr( action::reloadText ), this );

    stopAction = new QAction( tr( action::stopText ), this );
    stopAction->setEnabled( true );

    optionsAction = new QAction( tr( action::optionsText ), this );
    optionsAction->setMenuRole( QAction::PreferencesRole );
    optionsAction->setStatusTip( tr( action::optionsStatusTip ) );
    connect( optionsAction, &QAction::triggered, this, [ this ]( auto ) { this->options(); } );

    editHighlightersAction = new QAction( tr( action::editHighlightersText ), this );
    editHighlightersAction->setMenuRole( QAction::NoRole );
    editHighlightersAction->setStatusTip( tr( action::editHighlightersStatusTip ) );
    connect( editHighlightersAction, &QAction::triggered, this,
             [ this ]( auto ) { this->editHighlighters(); } );

    showDocumentationAction = new QAction( tr( action::showDocumentationText ), this );
    showDocumentationAction->setStatusTip( tr( action::showDocumentationStatusTip ) );
    connect( showDocumentationAction, &QAction::triggered, this,
             [ this ]( auto ) { this->documentation(); } );

    aboutAction = new QAction( tr( action::aboutText ), this );
    aboutAction->setStatusTip( tr( action::aboutStatusTip ) );
    connect( aboutAction, &QAction::triggered, this, [ this ]( auto ) { this->about(); } );

    aboutQtAction = new QAction( tr( action::aboutQtText ), this );
    aboutQtAction->setStatusTip( tr( action::aboutQtStatusTip ) );
    connect( aboutQtAction, &QAction::triggered, this, [ this ]( auto ) { this->aboutQt(); } );

    reportIssueAction = new QAction( tr( action::reportIssueText ), this );
    reportIssueAction->setStatusTip( tr( action::reportIssueStatusTip ) );
    connect( reportIssueAction, &QAction::triggered, this,
             []( auto ) { IssueReporter::reportIssue( IssueTemplate::Bug ); } );

    generateDumpAction = new QAction( tr( action::generateDumpText ), this );
    generateDumpAction->setStatusTip( tr( action::generateDumpStatusTip ) );
    connect( generateDumpAction, &QAction::triggered, this,
             [ this ]( auto ) { this->generateDump(); } );

    showScratchPadAction = new QAction( tr( action::showScratchPadText ), this );
    showScratchPadAction->setStatusTip( tr( action::showScratchPadStatusTip ) );
    connect( showScratchPadAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showScratchPad(); } );

    commandPaletteAction = new QAction( tr( action::commandPaletteText ), this );
    commandPaletteAction->setStatusTip( tr( action::commandPaletteStatusTip ) );
    connect( commandPaletteAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showCommandPalette(); } );

    showFiltersPanelAction = new QAction( tr( action::showFiltersPanelText ), this );
    showFiltersPanelAction->setStatusTip( tr( action::showFiltersPanelStatusTip ) );
    connect( showFiltersPanelAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showFiltersPanel(); } );

    showValueNamesPanelAction = new QAction( tr( action::showValueNamesPanelText ), this );
    showValueNamesPanelAction->setStatusTip( tr( action::showValueNamesPanelStatusTip ) );
    connect( showValueNamesPanelAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showValueNamesPanel(); } );

    toggleSidebarAction = new QAction( tr( action::toggleSidebarText ), this );
    toggleSidebarAction->setStatusTip( tr( action::toggleSidebarStatusTip ) );
    connect( toggleSidebarAction, &QAction::triggered, this,
             [ this ]( auto ) { this->toggleSidebar(); } );

    toggleChartPanelAction = new QAction( tr( action::toggleChartPanelText ), this );
    // Named for the benchmark mode, which shows the chart as the user does (#670).
    toggleChartPanelAction->setObjectName( "toggleChartPanelAction" );
    toggleChartPanelAction->setStatusTip( tr( action::toggleChartPanelStatusTip ) );
    connect( toggleChartPanelAction, &QAction::triggered, this, [ this ]( auto ) {
        auto* crawler = currentCrawlerWidget();
        if ( crawler != nullptr ) {
            crawler->toggleChartPanel();
        }
    } );

    showFilterFrequencyAction = new QAction( tr( action::showFilterFrequencyText ), this );
    showFilterFrequencyAction->setStatusTip( tr( action::showFilterFrequencyStatusTip ) );
    connect( showFilterFrequencyAction, &QAction::triggered, this, [ this ]( auto ) {
        auto* crawler = currentCrawlerWidget();
        if ( crawler != nullptr ) {
            crawler->showFilterFrequency();
        }
    } );

    importChipmunkFiltersAction = new QAction( tr( action::importChipmunkFiltersText ), this );
    importChipmunkFiltersAction->setStatusTip( tr( action::importChipmunkFiltersStatusTip ) );
    connect( importChipmunkFiltersAction, &QAction::triggered, this,
             [ this ]( auto ) { this->importChipmunkFilters(); } );

    encodingGroup = new QActionGroup( this );
    connect( encodingGroup, &QActionGroup::triggered, this, &MainWindow::encodingChanged );

    favoritesGroup = new QActionGroup( this );
    connect( favoritesGroup, &QActionGroup::triggered, this, &MainWindow::openFileFromFavorites );

    openedFilesGroup = new QActionGroup( this );
    connect( openedFilesGroup, &QActionGroup::triggered, this, &MainWindow::switchToOpenedFile );

    addToFavoritesAction = new QAction( tr( action::addToFavoritesText ), this );
    addToFavoritesAction->setData( true );
    connect( addToFavoritesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->addToFavorites(); } );

    addToFavoritesMenuAction = new QAction( tr( action::addToFavoritesText ), this );
    connect( addToFavoritesMenuAction, &QAction::triggered, this,
             [ this ]( auto ) { this->addToFavorites(); } );

    removeFromFavoritesAction = new QAction( tr( action::removeFromFavoritesText ), this );
    connect( removeFromFavoritesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->removeFromFavorites(); } );

    selectOpenFileAction = new QAction( tr( action::selectOpenFileText ), this );
    connect( selectOpenFileAction, &QAction::triggered, this,
             [ this ]( auto ) { this->selectOpenedFile(); } );

    predefinedFiltersDialogAction = new QAction( tr( action::predefinedFiltersDialogText ), this );
    predefinedFiltersDialogAction->setStatusTip( tr( action::predefinedFiltersDialogStatusTip ) );
    connect( predefinedFiltersDialogAction, &QAction::triggered, this,
             [ this ]( auto ) { this->editPredefinedFilters(); } );

    valueNamesDialogAction = new QAction( tr( action::valueNamesDialogText ), this );
    valueNamesDialogAction->setStatusTip( tr( action::valueNamesDialogStatusTip ) );
    connect( valueNamesDialogAction, &QAction::triggered, this,
             [ this ]( auto ) { this->editValueNames(); } );

    regexLabAction = new QAction( tr( action::regexLabText ), this );
    regexLabAction->setStatusTip( tr( action::regexLabStatusTip ) );
    connect( regexLabAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openRegexLab(); } );

    manageTabGroupsAction = new QAction( tr( "Manage Tab Groups..." ), this );
    manageTabGroupsAction->setStatusTip( tr( "Rename, recolor, or delete tab groups" ) );
    connect( manageTabGroupsAction, &QAction::triggered, this,
             [ this ]( auto ) { this->manageTabGroups(); } );

    pluginsAction = new QAction( tr( "Plugin Management..." ), this );
    pluginsAction->setStatusTip( tr( "Manage, install, and update plugins" ) );
    connect( pluginsAction, &QAction::triggered, this, &MainWindow::showPluginDialog );

    updateShortcuts();
}

void MainWindow::updateShortcuts()
{
    const auto& config = Configuration::get();
    const auto shortcuts = config.shortcuts();

    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }

    shortcuts_.clear();
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowOpenQfForward,
                                      [ this ] { displayQuickFindBar( QuickFindMux::Forward ); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowOpenQfBackward,
                                      [ this ] { displayQuickFindBar( QuickFindMux::Backward ); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowFocusSearchInput, [ this ] {
                                          if ( auto crawler = currentCrawlerWidget() ) {
                                              crawler->focusSearchEdit();
                                          }
                                      } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowFullScreen,
                                      [ this ] { this->showFullScreen(); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowMax,
                                      [ this ] { this->showMaximized(); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowMin,
                                      [ this ] { this->showMinimized(); } );

    auto setShortcuts = [ &shortcuts ]( auto* action, const auto& actionName ) {
        action->setShortcuts( ShortcutAction::shortcutKeys( actionName, shortcuts ) );
    };

    setShortcuts( newWindowAction, ShortcutAction::MainWindowNewWindow );
    setShortcuts( openAction, ShortcutAction::MainWindowOpenFile );
    setShortcuts( closeAction, ShortcutAction::MainWindowCloseFile );
    setShortcuts( closeAllAction, ShortcutAction::MainWindowCloseAll );
    setShortcuts( exitAction, ShortcutAction::MainWindowQuit );
    setShortcuts( copyAction, ShortcutAction::MainWindowCopy );
    setShortcuts( selectAllAction, ShortcutAction::MainWindowSelectAll );
    setShortcuts( findAction, ShortcutAction::MainWindowOpenQf );
    setShortcuts( clearLogAction, ShortcutAction::MainWindowClearFile );
    setShortcuts( openContainingFolderAction, ShortcutAction::MainWindowOpenContainingFolder );
    setShortcuts( openInEditorAction, ShortcutAction::MainWindowOpenInEditor );
    setShortcuts( copyPathToClipboardAction, ShortcutAction::MainWindowCopyPathToClipboard );
    setShortcuts( openClipboardAction, ShortcutAction::MainWindowOpenFromClipboard );
    setShortcuts( openUrlAction, ShortcutAction::MainWindowOpenFromUrl );
    setShortcuts( openSessionAction, ShortcutAction::MainWindowOpenSession );
    setShortcuts( saveSessionAsAction, ShortcutAction::MainWindowSaveSessionAs );
    setShortcuts( openCommandOutputAction, ShortcutAction::MainWindowOpenCommandOutput );
    setShortcuts( followAction, ShortcutAction::MainWindowFollowFile );
    setShortcuts( textWrapAction, ShortcutAction::MainWindowTextWrap );
    setShortcuts( showValueNamesAction, ShortcutAction::MainWindowShowValueNames );
    setShortcuts( reloadAction, ShortcutAction::MainWindowReload );
    setShortcuts( stopAction, ShortcutAction::MainWindowStop );
    setShortcuts( showScratchPadAction, ShortcutAction::MainWindowScratchpad );
    setShortcuts( commandPaletteAction, ShortcutAction::MainWindowCommandPalette );
    setShortcuts( selectOpenFileAction, ShortcutAction::MainWindowSelectOpenFile );
    setShortcuts( goToLineAction, ShortcutAction::LogViewJumpToLine );
    setShortcuts( goToTimestampAction, ShortcutAction::LogViewJumpToTimestamp );
    setShortcuts( searchLimitsTimeRangeAction, ShortcutAction::LogViewSearchLimitsTimeRange );
    setShortcuts( searchLimitsAroundLineAction, ShortcutAction::LogViewSearchLimitsAroundLine );
    setShortcuts( optionsAction, ShortcutAction::MainWindowPreference );
}

// Whether the tab at `index` is the welcome dashboard itself, for what only
// the dashboard gets -- its title, a refresh when shown. Whether a tab holds a
// Log File is the tab widget's to answer (#535).
bool isDashboardTab( const TabbedCrawlerWidget& tabs, int index )
{
    return qobject_cast<WelcomeDashboard*>( tabs.widget( index ) ) != nullptr;
}

// Refresh the welcome dashboard content.
void MainWindow::showDashboardOrTabs()
{
    if ( welcomeDashboard_ ) {
        welcomeDashboard_->refresh();
    }
}

void MainWindow::loadIcons()
{
    openAction->setIcon( iconLoader_.load( "icons8-open-file" ) );
    stopAction->setIcon( iconLoader_.load( "icons8-delete" ) );
    reloadAction->setIcon( iconLoader_.load( "icons8-restore-page" ) );
    followAction->setIcon( iconLoader_.load( "icons8-fast-forward" ) );
    showScratchPadAction->setIcon( iconLoader_.load( "icons8-create" ) );
    showFiltersPanelAction->setIcon( iconLoader_.load( "icons8-filter" ) );
    toggleSidebarAction->setIcon( iconLoader_.load( "icons8-sidebar" ) );
    toggleChartPanelAction->setIcon( iconLoader_.load( "icons8-chart" ) );
    showFilterFrequencyAction->setIcon( iconLoader_.load( "icons8-frequency" ) );
    addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star" ) );
    addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star" ) );
    sidebarFloatButton_->setIcon( iconLoader_.load( "icons8-undock-16" ) );
    sidebarCloseButton_->setIcon( iconLoader_.load( "icons8-close-window-16" ) );

#ifdef Q_OS_MACOS
    // The menu bar is the system's here, and macOS draws its popups in the
    // system's appearance rather than the Theme's: the icons just loaded are
    // inverse for a dark Theme and would sit on a light menu. Runs after every
    // load, so a Theme change does not bring them back (#421).
    hideIconsInMenus( menuBar() );
#endif
}

void MainWindow::createMenus()
{
    using namespace logsquirl::mainwindow;

    fileMenu = menuBar()->addMenu( tr( menu::fileTitle ) );
    fileMenu->setToolTipsVisible( true );
    fileMenu->addAction( newWindowAction );
    fileMenu->addAction( openAction );
    fileMenu->addAction( openClipboardAction );
    fileMenu->addAction( openCommandOutputAction );
    fileMenu->addAction( openUrlAction );
    recentFilesMenu = fileMenu->addMenu( tr( "Open Recent" ) );
    for ( auto i = 0u; i < recentFileActions.size(); ++i ) {
        recentFilesMenu->addAction( recentFileActions[ i ] );
    }
    recentFilesMenu->addSeparator();
    recentFilesMenu->addAction( recentFilesCleanup );
    recentFilesMenu->setEnabled( false );
    fileMenu->addSeparator();

    fileMenu->addAction( openSessionAction );
    fileMenu->addAction( saveSessionAsAction );
    fileMenu->addSeparator();

    fileMenu->addAction( closeAction );
    fileMenu->addAction( closeAllAction );
    fileMenu->addSeparator();

    fileMenu->addAction( optionsAction );
    fileMenu->addSeparator();

    fileMenu->addSeparator();
    fileMenu->addAction( exitAction );

    editMenu = menuBar()->addMenu( tr( menu::editTitle ) );
    editMenu->addAction( copyAction );
    editMenu->addAction( copyAsShownAction );
    editMenu->addAction( selectAllAction );
    editMenu->addSeparator();
    editMenu->addAction( findAction );
    editMenu->addSeparator();
    editMenu->addAction( goToLineAction );
    editMenu->addAction( goToTimestampAction );
    editMenu->addSeparator();
    editMenu->addAction( searchLimitsTimeRangeAction );
    editMenu->addAction( searchLimitsAroundLineAction );
    editMenu->addSeparator();
    editMenu->addAction( copyPathToClipboardAction );
    editMenu->addAction( openContainingFolderAction );
    editMenu->addSeparator();
    editMenu->addAction( openInEditorAction );
    editMenu->addAction( clearLogAction );
    editMenu->setEnabled( false );

    viewMenu = menuBar()->addMenu( tr( menu::viewTitle ) );
    openedFilesMenu = viewMenu->addMenu( tr( menu::openedFilesTitle ) );
    viewMenu->addSeparator();
    viewMenu->addAction( overviewVisibleAction );
    viewMenu->addSeparator();
    viewMenu->addAction( lineNumbersVisibleInMainAction );
    viewMenu->addAction( lineNumbersVisibleInFilteredAction );
    viewMenu->addSeparator();
    viewMenu->addAction( textWrapAction );
    viewMenu->addAction( showValueNamesAction );
    viewMenu->addSeparator();
    viewMenu->addAction( followAction );
    viewMenu->addSeparator();
    viewMenu->addAction( reloadAction );
    viewMenu->addSeparator();
    viewMenu->addAction( toggleChartPanelAction );
    viewMenu->addAction( showFilterFrequencyAction );

    toolsMenu = menuBar()->addMenu( tr( menu::toolsTitle ) );

    highlightersMenu = new HighlightersMenu( tr( menu::highlightersTitle ), menuBar() );
    menuBar()->addMenu( highlightersMenu );
    // Every open Log File is re-colored, not only the one the current tab
    // shows: the others would keep the colors their Color Labels had.
    highlightersMenu->setApplyChange(
        [ this ]() { session_.applyChange( Changed::HighlighterSets ); } );

    toolsMenu->addAction( predefinedFiltersDialogAction );
    toolsMenu->addAction( valueNamesDialogAction );
    toolsMenu->addAction( importChipmunkFiltersAction );
    toolsMenu->addAction( regexLabAction );
    toolsMenu->addSeparator();
    toolsMenu->addAction( manageTabGroupsAction );

    toolsMenu->addSeparator();
    toolsMenu->addAction( showScratchPadAction );
    toolsMenu->addAction( showFiltersPanelAction );
    toolsMenu->addAction( showValueNamesPanelAction );
    toolsMenu->addSeparator();
    toolsMenu->addAction( commandPaletteAction );

    menuBar()->addMenu( EncodingMenu::generate( encodingGroup, session_.fileAccessPolicy() ) );
    menuBar()->addSeparator();

    favoritesMenu = menuBar()->addMenu( tr( menu::favoritesTitle ) );
    favoritesMenu->setToolTipsVisible( true );

    pluginsMenu = menuBar()->addMenu( tr( "Plugins" ) );
    // Plugin-contributed actions are inserted at the top (before this separator)
    // by the Plugin UI Port (PluginUiAdapter).  Management actions live below the separator.
    pluginMenuSeparator_ = pluginsMenu->addSeparator();
    pluginsMenu->addAction( pluginsAction );

    sourcesMenu = menuBar()->addMenu( tr( "Sources" ) );
    updateSourcesMenu();

    helpMenu = menuBar()->addMenu( tr( menu::helpTitle ) );
    helpMenu->addAction( showDocumentationAction );
    helpMenu->addSeparator();
    helpMenu->addAction( reportIssueAction );
    helpMenu->addSeparator();
    helpMenu->addAction( generateDumpAction );
    helpMenu->addSeparator();
    helpMenu->addAction( aboutQtAction );
    helpMenu->addAction( aboutAction );
}

void MainWindow::createToolBars()
{
    infoLine = new PathLine();
    infoLine->setFrameStyle( QFrame::StyledPanel );
    infoLine->setFrameShadow( QFrame::Sunken );
    infoLine->setLineWidth( 0 );
    // A read-only field: the window's background, not a button's, which read
    // as disabled; its edge comes from the Theme's stylesheet (#264).
    infoLine->setBackgroundRole( QPalette::Window );
    infoLine->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );

    sizeField = new QLabel();
    sizeField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );
    sizeField->setContentsMargins( 6, 0, 6, 0 );

    dateField = new QLabel();
    dateField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );
    dateField->setContentsMargins( 6, 0, 6, 0 );

    encodingField = new QLabel();
    encodingField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );
    encodingField->setContentsMargins( 6, 0, 6, 0 );

    lineNbField = new QLabel();
    lineNbField->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
    lineNbField->setContentsMargins( 6, 0, 6, 0 );

    toolBar = addToolBar( QApplication::translate( "logsquirl::mainwindow::toolbar",
                                                   logsquirl::mainwindow::toolbar::toolbarTitle ) );
    // Read once, while the toolbar is built: there is no path that resizes
    // an existing toolbar, so a change to this setting shows up on the next
    // window opened.
    const auto iconSize = Configuration::get().toolbarIconSize();
    toolBar->setIconSize( QSize( iconSize, iconSize ) );
    toolBar->setMovable( false );
    toolBar->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
    toolBar->addAction( openAction );
    toolBar->addAction( reloadAction );
    toolBar->addAction( followAction );
    toolBar->addAction( addToFavoritesAction );
    toolBar->addSeparator();
    toolBar->addWidget( infoLine );
    toolBar->addAction( stopAction );

    infoToolbarSeparators.reserve( 5 );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( sizeField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( dateField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( encodingField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( lineNbField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );

    teamFolderButton_ = new QToolButton();
    teamFolderButton_->setAutoRaise( true );
    teamFolderButton_->setToolButtonStyle( Qt::ToolButtonTextOnly );
    teamFolderButtonAction_ = toolBar->addWidget( teamFolderButton_ );
    teamFolderButtonAction_->setVisible( false );

    toolBar->addAction( toggleSidebarAction );

    showInfoLabels( false );
}

void MainWindow::createTrayIcon()
{
    trayIcon_ = new QSystemTrayIcon( this );

    QMenu* trayMenu = new QMenu( this );
    QAction* openWindowAction = new QAction( tr( "Open window" ), this );
    QAction* quitAction = new QAction( tr( "Quit" ), this );

    trayMenu->addAction( openWindowAction );
    trayMenu->addAction( quitAction );

    connect( openWindowAction, &QAction::triggered, this, &QMainWindow::show );
    connect( quitAction, &QAction::triggered, [ this ] {
        this->isCloseFromTray_ = true;
        this->close();
    } );

    trayIcon_->setIcon( mainIcon_ );
    trayIcon_->setToolTip( tr( logsquirl::mainwindow::trayicon::trayiconTip ) );
    trayIcon_->setContextMenu( trayMenu );

    connect( trayIcon_, &QSystemTrayIcon::activated,
             [ this ]( QSystemTrayIcon::ActivationReason reason ) {
                 switch ( reason ) {
                 case QSystemTrayIcon::Trigger:
                     if ( !this->isVisible() ) {
                         this->show();
                     }
                     else {
                         this->hide();
                     }
                     break;
                 default:
                     break;
                 }
             } );

    // Read once, while this window is built. The checkbox for it is hidden
    // in the options dialog, so there is no way to change it mid-session
    // and nothing to tell the user about.
    if ( Configuration::get().minimizeToTray() ) {
        trayIcon_->show();
    }
}
//
// Q_SLOTS:
//

// Opens the file selection dialog to select a new log file
void MainWindow::open()
{
    QString defaultDir = ".";

    // Default to the path of the current file if there is one
    if ( auto current = currentCrawlerWidget() ) {
        QString current_file = session_.getFilename( current );
        QFileInfo fileInfo = QFileInfo( current_file );
        defaultDir = fileInfo.path();
    }

    // Build file filter including converter plugins
    QStringList filters;
    filters << tr( "All files (*)" );
    filters << plugins_->host().converterFileFilters();
    const auto filter = filters.join( ";;" );

    const auto selectedFiles = QFileDialog::getOpenFileUrls(
        this, tr( "Open file" ), QUrl::fromLocalFile( defaultDir ), filter );

    std::vector<QUrl> localFiles;
    std::vector<QUrl> remoteFiles;

    std::partition_copy( selectedFiles.cbegin(), selectedFiles.cend(),
                         std::back_inserter( localFiles ), std::back_inserter( remoteFiles ),
                         []( const QUrl& url ) { return url.isLocalFile(); } );

    for ( const auto& localFile : localFiles ) {
        loadFile( localFile.toLocalFile() );
    }

    for ( const auto& remoteFile : remoteFiles ) {
        openRemoteFile( remoteFile );
    }
}

void MainWindow::openRemoteFile( const QUrl& url )
{
    Downloader downloader;

    QProgressDialog progressDialog;
    progressDialog.setLabelText( tr( "Downloading %1" ).arg( url.toString() ) );

    connect( &downloader, &Downloader::downloadProgress,
             [ &progressDialog ]( qint64 bytesReceived, qint64 bytesTotal ) {
                 const auto progress = calculateProgress( bytesReceived, bytesTotal );
                 progressDialog.setRange( 0, 100 );
                 progressDialog.setValue( progress );
             } );

    connect( &downloader, &Downloader::finished,
             [ &progressDialog ]( bool isOk ) { progressDialog.done( isOk ? 0 : 1 ); } );

    auto tempFile = new QTemporaryFile( tempDir_.filePath( url.fileName() ), this );
    if ( tempFile->open() ) {
        downloader.download( url, tempFile );
        if ( !progressDialog.exec() ) {
            // Not saved with the Session: a start never fetches anything
            // unasked (#596).
            openLogFile( tempFile->fileName(), LogFileProvenance::transient() );
        }
        else {
            QMessageBox::critical( this, tr( "LogSquirl - File download" ),
                                   downloader.lastError() );
        }
    }
    else {
        QMessageBox::critical( this, tr( "LogSquirl - File download" ),
                               tr( "Failed to create temp file" ) );
    }
}

void MainWindow::switchToOpenedFile( QAction* action )
{
    if ( !action ) {
        return;
    }

    loadFile( action->data().toString() );
}

void MainWindow::openFileFromRecent( QAction* action )
{
    if ( !action ) {
        return;
    }

    const auto filename = action->data().toString();
    if ( QFileInfo{ filename }.isReadable() ) {
        loadFile( filename );
    }
    else {
        const auto userAction = QMessageBox::question(
            this, tr( "logsquirl - remove from recent" ),
            tr( "Could not read file %1. Remove it from recent files?" ).arg( filename ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

        if ( userAction == QMessageBox::Yes ) {
            removeFromRecent( filename );
        }
    }
}

void MainWindow::openFileFromFavorites( QAction* action )
{
    if ( !action ) {
        return;
    }

    const auto filename = action->data().toString();
    if ( QFileInfo{ filename }.isReadable() ) {
        loadFile( filename );
    }
    else {
        const auto userAction = QMessageBox::question(
            this, tr( "logsquirl - remove from favorites" ),
            tr( "Could not read file %1. Remove it from favorites?" ).arg( filename ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

        if ( userAction == QMessageBox::Yes ) {
            removeFromFavorites( filename );
        }
    }
}

// Close current tab
void MainWindow::closeTab( ActionInitiator initiator )
{
    int currentIndex = mainTabWidget_.currentIndex();

    if ( currentIndex >= 0 && mainTabWidget_.holdsLogFile( currentIndex ) ) {
        closeTab( currentIndex, initiator );
    }
    else if ( currentIndex < 0 ) {
        this->close();
    }
    else {
        // The dashboard tab is the only/current tab — closing it should close
        // the window if there is nothing else open, otherwise it is a no-op.
        if ( mainTabWidget_.logFileTabs().isEmpty() ) {
            this->close();
        }
    }
}

// Close every tab that holds a Log File; the dashboard stays.
void MainWindow::closeAll( ActionInitiator initiator )
{
    closeTabs( mainTabWidget_.logFileTabs(), initiator );
}

// Select all the text in the currently selected view
void MainWindow::selectAll()
{
    if ( infoLine->hasFocus() ) {
        infoLine->setSelection( 0, logsquirl::isize( infoLine->text() ) );
    }
    else if ( auto current = currentCrawlerWidget(); current != nullptr ) {
        current->selectAll();
    }
}

// Copy the currently selected line into the clipboard
void MainWindow::copy()
{
    try {
        if ( infoLine->hasFocus() && infoLine->hasSelectedText() ) {
            sendTextToClipboard( infoLine->selectedText() );
            return;
        }

        if ( auto current = currentCrawlerWidget(); current != nullptr ) {
            auto text = current->getSelectedText();
            text.replace( QChar::Null, QChar::Space );

            sendTextToClipboard( text, true );
        }
    } catch ( std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}

// Copy the selection as the view shows it into the clipboard
void MainWindow::copyAsShown()
{
    try {
        if ( auto current = currentCrawlerWidget(); current != nullptr ) {
            auto text = current->getSelectedTextAsShown();
            text.replace( QChar::Null, QChar::Space );

            sendTextToClipboard( text, true );
        }
    } catch ( std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}

// Display the QuickFind bar
void MainWindow::find()
{
    displayQuickFindBar( QuickFindMux::Forward );
}

void MainWindow::clearLog()
{
    const auto current_file = session_.getFilename( currentCrawlerWidget() );
    const auto userAction = QMessageBox::question(
        this, tr( "logsquirl - clear file" ),
        tr( "Clear file %1? File content will be removed from disk, this is irreversible" )
            .arg( current_file ),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

    if ( userAction == QMessageBox::Yes ) {
        QFile::resize( current_file, 0 );
    }
}

void MainWindow::copyFullPath()
{
    const auto current_file = session_.getFilename( currentCrawlerWidget() );
    sendTextToClipboard( QDir::toNativeSeparators( current_file ) );
}

void MainWindow::openContainingFolder()
{
    showPathInFileExplorer( session_.getFilename( currentCrawlerWidget() ) );
}

void MainWindow::openInEditor()
{
    openFileInDefaultApplication( session_.getFilename( currentCrawlerWidget() ) );
}

void MainWindow::tryOpenClipboard( int tryTimes )
{
    auto clipboard = QGuiApplication::clipboard();
    auto text = clipboard->text();

    if ( text.isEmpty() && tryTimes > 0 ) {
        QTimer::singleShot( 50, [ tryTimes, this ]() { tryOpenClipboard( tryTimes - 1 ); } );
    }
    else {
        auto tempFile = new QTemporaryFile( tempDir_.filePath( "logsquirl_clipboard" ), this );
        if ( tempFile->open() ) {
            tempFile->write( text.toUtf8() );
            tempFile->flush();

            // The file goes with this window: it is not saved with the
            // Session (#570).
            openLogFile( tempFile->fileName(), LogFileProvenance::transient() );
        }
    }
}

void MainWindow::openClipboard()
{
    tryOpenClipboard( ClipboardMaxTry );
}

void MainWindow::openUrl()
{
    bool ok;
    const auto urlInClipboard = QUrl::fromUserInput( QApplication::clipboard()->text() );
    const auto selectedUrl = urlInClipboard.isValid() ? urlInClipboard.toString() : QString{};

    QString url
        = QInputDialog::getText( this, tr( "Open URL as log file" ), tr( "URL to download:" ),
                                 QLineEdit::Normal, selectedUrl, &ok );
    if ( ok && !url.isEmpty() ) {
        openRemoteFile( url );
    }
}

void MainWindow::openCommandOutputDialog()
{
    CommandOutputDialog dialog( Configuration::get().recentCommands(), this );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }
    const auto command = dialog.command();
    if ( command.commandLine.isEmpty() ) {
        return;
    }
    if ( openCommandOutput( command ) ) {
        auto& config = Configuration::get();
        config.addRecentCommand( command );
        config.save();
    }
}

// Opens the 'Highlighters' dialog box
void MainWindow::editHighlighters()
{
    HighlightersDialog dialog( this );
    dialog.setRegexLabAccess( regexLabAccess() );
    if ( const auto teamFolder = session_.teamFolder();
         teamFolder && teamFolder->state() != TeamFolder::State::Off ) {
        showTeamGroupsIn( dialog, *teamFolder, teamFolder->highlighterGroups(),
                          &TeamFolder::highlighterGroupRevisions );
    }

    // Reaches every open Log File, in every window, not only the current tab.
    connect( &dialog, &HighlightersDialog::optionsChanged, [ this ]() {
        session_.applyChange( Changed::HighlighterSets );
        updateHighlightersMenu();
    } );

    dialog.exec();
}

void MainWindow::openRegexLab()
{
    if ( regexLab_.isNull() ) {
        regexLab_ = new RegexLabWindow( session_.searchPolicy().regexpEngine, this );
        regexLab_->setAttribute( Qt::WA_DeleteOnClose );
    }
    else {
        regexLab_->setEngine( session_.searchPolicy().regexpEngine );
    }

    regexLab_->setSampleSource( tabInFrontAsRegexLabSample() );

    regexLab_->show();
    regexLab_->raise();
    regexLab_->activateWindow();
}

RegexLabSampleSource MainWindow::tabInFrontAsRegexLabSample()
{
    auto* crawler = currentCrawlerWidget();
    return crawler != nullptr
               ? regexLabSampleSource( *crawler,
                                       mainTabWidget_.tabText( mainTabWidget_.currentIndex() ) )
               : RegexLabSampleSource{};
}

// The editors are modal, so the tab in front is the one they were opened
// over, and its Search Line is the one a Predefined Filter would go to; the
// sample is asked for as a Lab opens all the same. Without a tab, a Search
// Line reads a pattern as one starts out.
RegexLabAccess MainWindow::regexLabAccess()
{
    RegexLabAccess access;
    access.searchEngine = session_.searchPolicy().regexpEngine;
    if ( const auto* crawler = currentCrawlerWidget(); crawler != nullptr ) {
        const auto flags = crawler->searchFlags();
        access.searchMatchesCase = flags.matchCase;
        access.searchUsesRegexp = flags.useRegexp;
    }
    else {
        const auto& policy = session_.quickFindPolicy();
        access.searchMatchesCase = !policy.searchIgnoreCaseDefault;
        access.searchUsesRegexp = policy.mainRegexpType == SearchRegexpType::ExtendedRegexp;
    }
    access.sampleSource = [ this ]() { return tabInFrontAsRegexLabSample(); };
    return access;
}

// Opens dialog to configure predefined filters
void MainWindow::editPredefinedFilters( const QString& newFilter )
{
    PredefinedFiltersDialog dialog( newFilter, this );
    dialog.setRegexLabAccess( regexLabAccess() );
    if ( const auto teamFolder = session_.teamFolder();
         teamFolder && teamFolder->state() != TeamFolder::State::Off ) {
        showTeamGroupsIn( dialog, *teamFolder, teamFolder->filterGroups(),
                          &TeamFolder::filterGroupRevisions );
    }

    // The Predefined Filters are no setting a Log File shows: only the filters
    // panel lists them.
    connect( &dialog, &PredefinedFiltersDialog::optionsChanged,
             [ this ]() { filtersPanel_.refreshFilters(); } );

    dialog.exec();
}

// Opens the Value Names dialog. OK and Apply change the Value Names
// Collection, which every open Log File and every window is told of (#647).
void MainWindow::editValueNames()
{
    // The dialog reads the groups again from the settings store, which may
    // hold what another instance saved: that is a change too, OK or Cancel.
    const auto& collection = ValueNamesCollection::get();
    auto applied = collection.generation();
    ValueNamesDialog dialog( this );
    if ( const auto teamFolder = session_.teamFolder();
         teamFolder && teamFolder->state() != TeamFolder::State::Off ) {
        showTeamGroupsIn( dialog, *teamFolder, teamFolder->namingGroups(),
                          &TeamFolder::namingGroupRevisions );
    }
    const auto applyIfChanged = [ this, &collection, &applied ] {
        if ( collection.generation() != applied ) {
            applied = collection.generation();
            session_.applyChange( Changed::ValueNames );
        }
    };
    connect( &dialog, &ValueNamesDialog::valueNamesChanged, this, applyIfChanged );
    dialog.exec();
    applyIfChanged();
}

void MainWindow::applyValueNamesChange()
{
    valueNamesPanel_.refresh();
}

// Opens the 'Options' modal dialog box
void MainWindow::options()
{
    const auto logFormatCatalog = session_.logFormatCatalog();
    // The file types LogSquirl opens, as this platform tells them (#720): the
    // application's, or one for this dialog where there is none.
    std::shared_ptr<FileAssociations> fileAssociations = session_.fileAssociations();
    if ( !fileAssociations ) {
        fileAssociations = createFileAssociations();
    }
    OptionsDialog dialog( *logFormatCatalog, this );
    if ( const auto teamFolder = session_.teamFolder() ) {
        dialog.showTeamFolder( *teamFolder );
    }
    dialog.showFileAssociations( *fileAssociations );

    // The dialog only says that the settings changed; the Session takes it
    // from there, to every open Log File and every window, this one included.
    connect( &dialog, &OptionsDialog::optionsChanged,
             [ this ]() { session_.applyChange( Changed::Settings ); } );
    dialog.exec();
}

void MainWindow::checkFileAssociationsAtStart( bool mayAsk )
{
    const auto fileAssociations = session_.fileAssociations();
    if ( !fileAssociations ) {
        return;
    }
    const auto choice = fileAssociationChoice( Configuration::getSynced() );
    const auto atStart = FileAssociationsAtStart::of( *fileAssociations, choice, mayAsk );
    if ( !atStart.ask ) {
        return;
    }

    LOG_INFO << "Asking which file types LogSquirl opens";
    auto* dialog = new FirstStartFileAssociationsDialog( *fileAssociations, atStart.checks, this );
    dialog->setAttribute( Qt::WA_DeleteOnClose );
    connect( dialog, &FirstStartFileAssociationsDialog::answered, this, [ dialog ] {
        // Read again: the Options Dialog may have kept a choice meanwhile.
        auto& config = Configuration::getSynced();
        auto answered = fileAssociationChoice( config );
        dialog->updateChoice( answered );
        keepFileAssociationChoice( config, answered );
    } );
    dialog->open();
}

void MainWindow::connectTeamFolder()
{
    const auto teamFolder = session_.teamFolder();
    if ( !teamFolder ) {
        return;
    }

    connect( teamFolder.get(), &TeamFolder::stateChanged, this,
             &MainWindow::updateTeamFolderIndicator );
    // A changed or removed Team group shows at the next sync, in every
    // window's Filters panel.
    connect( teamFolder.get(), &TeamFolder::groupsChanged, this,
             [ this ] { filtersPanel_.setTeamGroups( session_.teamFolder()->filterGroups() ); } );
    // A changed or removed Team Highlighter Set re-colors every open Log File
    // at once, and a removed one is no longer active.
    connect( teamFolder.get(), &TeamFolder::highlighterGroupsChanged, this,
             [ this ] { applyTeamHighlighterSets( true ); } );
    // A changed or removed Team Naming Group names values anew in every open
    // Log File, and shows in the Value Names tab.
    connect( teamFolder.get(), &TeamFolder::namingGroupsChanged, this,
             &MainWindow::applyTeamNamingGroups );
    // A sync that reached the repository knows the groups, however few: only
    // then is an activation of a Team set that is not there dropped.
    connect( teamFolder.get(), &TeamFolder::syncFinished, this, [ this ] {
        if ( const auto folder = session_.teamFolder();
             folder && folder->state() == TeamFolder::State::Synced ) {
            applyTeamHighlighterSets( true );
        }
    } );
    connect( teamFolder.get(), &TeamFolder::publishFinished, this,
             &MainWindow::askAboutPublishConflicts );
    connect( teamFolderButton_, &QToolButton::clicked, teamFolder.get(), &TeamFolder::sync );

    filtersPanel_.setTeamGroups( teamFolder->filterGroups() );
    // Before the first sync no group is known yet: nothing is dropped.
    applyTeamHighlighterSets( teamFolder->state() == TeamFolder::State::Synced );
    applyTeamNamingGroups();
    updateTeamFolderIndicator();
}

namespace {
// The conflicts of publishes that no window has asked about yet, shared by
// every window: each window hears of a publish, but one conflict is asked
// once. It waits here while no window can ask -- the app is in the
// background, or a dialog of it is open.
QList<logsquirl::teamfolder::PublishResult>& pendingPublishConflicts()
{
    static QList<logsquirl::teamfolder::PublishResult> pending;
    return pending;
}
} // namespace

void MainWindow::askAboutPublishConflicts( const logsquirl::teamfolder::PublishOutcome& outcome )
{
    using logsquirl::teamfolder::PublishStatus;

    auto& pending = pendingPublishConflicts();
    for ( const auto& result : outcome.results ) {
        if ( result.status != PublishStatus::Conflict ) {
            continue;
        }
        // Every window hears of the same publish: remembered once.
        const auto known = std::any_of( pending.cbegin(), pending.cend(), [ &result ]( auto& p ) {
            return p.request.kind == result.request.kind && p.request.id == result.request.id;
        } );
        if ( !known ) {
            pending.append( result );
        }
    }
    askAboutPendingConflicts();
}

void MainWindow::askAboutPendingConflicts()
{
    using logsquirl::teamfolder::ConflictChoice;

    auto& pending = pendingPublishConflicts();
    if ( pending.isEmpty() ) {
        return;
    }

    // The window that has the focus asks, and not while a dialog is open on
    // top of it (then the dialog is the active window): the question waits,
    // and every window looks again shortly.
    if ( !isActiveWindow() || QApplication::activeModalWidget() ) {
        QTimer::singleShot( 500, this, &MainWindow::askAboutPendingConflicts );
        return;
    }

    const auto teamFolder = session_.teamFolder();
    while ( !pending.isEmpty() ) {
        const auto result = pending.takeFirst();
        if ( !teamFolder ) {
            continue;
        }

        QMessageBox question( QMessageBox::Question, tr( "Team group changed" ),
                              tr( "Somebody else changed the Team group \"%1\" since you started "
                                  "editing it." )
                                  .arg( result.request.name ),
                              QMessageBox::NoButton, this );
        question.setInformativeText(
            result.hasTheirs()
                ? tr( "Keep your version and replace theirs, take theirs and drop your change, or "
                      "save yours as a copy next to theirs?" )
                : tr( "Somebody deleted it. Keep your version to publish it again, or take the "
                      "deletion and drop your change?" ) );
        auto* keepMine = question.addButton( tr( "Keep mine" ), QMessageBox::AcceptRole );
        auto* takeTheirs = question.addButton( tr( "Take theirs" ), QMessageBox::DestructiveRole );
        auto* saveCopy = question.addButton( tr( "Save mine as a copy" ), QMessageBox::ActionRole );
        question.setDefaultButton( saveCopy );
        question.exec();

        if ( question.clickedButton() == keepMine ) {
            teamFolder->resolveConflict( result.request, ConflictChoice::KeepMine );
        }
        else if ( question.clickedButton() == saveCopy ) {
            teamFolder->resolveConflict( result.request, ConflictChoice::SaveAsCopy );
        }
        else if ( question.clickedButton() == takeTheirs ) {
            teamFolder->resolveConflict( result.request, ConflictChoice::TakeTheirs );
        }
    }
}

void MainWindow::applyTeamHighlighterSets( bool dropUnknownActivations )
{
    const auto teamFolder = session_.teamFolder();
    if ( !teamFolder ) {
        return;
    }

    // Every window comes here for the same sync; the first one changes the
    // collection and saves it, the others only bring their menu up to date.
    auto& collection = HighlighterSetCollection::get();
    if ( collection.setTeamHighlighterSets( teamFolder->highlighterGroups(),
                                            dropUnknownActivations ) ) {
        collection.save();
        session_.applyChange( Changed::HighlighterSets );
    }
    updateHighlightersMenu();
}

void MainWindow::applyTeamNamingGroups()
{
    const auto teamFolder = session_.teamFolder();
    if ( !teamFolder ) {
        return;
    }

    // Every window comes here for the same sync; the first one changes the
    // collection, which tells every window. The Team groups are never saved:
    // the Team Folder holds them.
    if ( ValueNamesCollection::get().setTeamGroups( teamFolder->namingGroups() ) ) {
        session_.applyChange( Changed::ValueNames );
    }
}

void MainWindow::updateTeamFolderIndicator()
{
    const auto teamFolder = session_.teamFolder();
    const bool shown = teamFolder && teamFolder->state() != TeamFolder::State::Off;
    teamFolderButtonAction_->setVisible( shown );
    if ( !shown ) {
        return;
    }

    teamFolderButton_->setText( teamFolder->summary() );
    const auto details = teamFolder->details();
    teamFolderButton_->setToolTip(
        ( details.isEmpty() ? teamFolder->summary() : teamFolder->summary() + "\n" + details )
        + "\n" + tr( "Click to sync now." ) );
}

void MainWindow::applySettingsChange()
{
    const auto& config = Configuration::get();
    logging::enableFileLogging( config.enableLogging(),
                                static_cast<logging::LogLevel>( config.loggingLevel() ) );

    newWindowAction->setVisible( config.allowMultipleWindows() );
    followAction->setEnabled( session_.watchPolicy().anyWatchEnabled() );
    applyQuickFindPolicy();
    // The Regex Lab matches with the engine a Search runs on.
    if ( !regexLab_.isNull() ) {
        regexLab_->setEngine( session_.searchPolicy().regexpEngine );
    }

    updateShortcuts();
    updateRecentFileActions();
}

void MainWindow::updateSourcesMenu()
{
    sourcesMenu->clear();
    for ( const auto& meta : plugins_->catalog().discoveredPlugins() ) {
        if ( meta.type() == LOGSQUIRL_PLUGIN_DATASOURCE ) {
            auto* action = new QAction( meta.name(), sourcesMenu );
            action->setStatusTip( tr( "Start %1 data source" ).arg( meta.name() ) );
            const auto& id = meta.id();
            connect( action, &QAction::triggered, this,
                     [ this, id ]() { startPluginDataSource( id ); } );
            sourcesMenu->addAction( action );
        }
    }
    if ( sourcesMenu->isEmpty() ) {
        sourcesMenu->addAction( tr( "(no data source plugins)" ) )->setEnabled( false );
    }
}

void MainWindow::servePluginCallbacks()
{
    auto& pluginHost = plugins_->host();

    // Plugins open files in, and ask for the active file of, the window the
    // user worked in last. A window that is gone serves them no more.
    pluginHost.setOpenFileCallback(
        [ window = QPointer<MainWindow>( this ) ]( const QString& path, bool follow ) {
            if ( window ) {
                window->loadFile( path, follow );
            }
        } );
    pluginHost.setActiveFilePathCallback( [ window = QPointer<MainWindow>( this ) ]() -> QString {
        if ( !window ) {
            return {};
        }
        auto* crawler = window->currentCrawlerWidget();
        return crawler ? window->session_.getFilename( crawler ) : QString();
    } );
}

void MainWindow::showPluginDialog()
{
    PluginDialog dialog( plugins_->catalog(), plugins_->host(), this );
    dialog.exec();
    updateSourcesMenu();
}

void MainWindow::startPluginDataSource( const QString& pluginId )
{
    auto& pluginHost = plugins_->host();

    // Auto-load the plugin if it is not yet loaded
    if ( !pluginHost.isLoaded( pluginId ) ) {
        const auto loadError = pluginHost.loadPlugin( pluginId );
        if ( !loadError.isEmpty() ) {
            QMessageBox::warning( this, tr( "Plugin Error" ),
                                  tr( "Failed to load plugin:\n%1" ).arg( loadError ) );
            return;
        }
    }

    // Every window shares the Plugin Host: the data source this window starts
    // opens in this window only.
    const auto started = connect( &pluginHost, &logsquirl::plugins::PluginHost::dataSourceStarted,
                                  this, &MainWindow::handleDataSourceStarted );
    const auto error = pluginHost.startDataSource( pluginId );
    disconnect( started );
    if ( !error.isEmpty() ) {
        QMessageBox::warning( this, tr( "DataSource Error" ), error );
    }
}

void MainWindow::handleDataSourceStarted( const QString& pluginId, const QString& displayName,
                                          const QString& filePath )
{
    LOG_INFO << "DataSource started: " << pluginId << " -> " << filePath;

    // Open the temp file with follow mode so it tails as the plugin pushes
    // lines, with a friendly tab title instead of the temp file path. The
    // file goes with the data source's run: it is not saved with the Session
    // (#570).
    openLogFile( filePath,
                 LogFileProvenance::transient(
                     displayName, tr( "DataSource: %1\n%2" ).arg( displayName, filePath ) ),
                 true );
}

void MainWindow::about()
{
    QMessageBox::about(
        this, tr( "About LogSquirl" ),
        tr( "<h2>LogSquirl %1</h2>"
            "<p>A fast, advanced log explorer.</p>"
            "<p>Built %2 from %3</p>"
            "<p><a "
            "href=\"https://github.com/64x-lunicorn/LogSquirl\">https://github.com/64x-lunicorn/"
            "LogSquirl</a></p>"
            "<p>This is a fork of <a href=\"https://github.com/variar/klogg\">klogg</a> "
            "by Anton Filimonov, which is a fork of "
            "<a href=\"https://github.com/nickbnf/glogg\">glogg</a> "
            "by Nicolas Bonnefon.</p>"
            "<p>Using icons from <a href=\"https://icons8.com\">icons8.com</a> project</p>"
            "<p>Copyright &copy; 2020 Nicolas Bonnefon, Anton Filimonov and other contributors</p>"
            "<p>You may modify and redistribute the program under the terms of the GPL (version 3 "
            "or later).</p>" )
            .arg( logsquirlVersion(), logsquirlBuildDate(), logsquirlCommit() ) );
}

void MainWindow::aboutQt()
{
    QMessageBox::aboutQt( this, tr( "About Qt" ) );
}

void MainWindow::documentation()
{
    QFile doc( ":/documentation.html" );
    if ( doc.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        const auto text = QString::fromUtf8( doc.readAll() );
        QTextBrowser* tb = new QTextBrowser();
        tb->setOpenExternalLinks( true );
        tb->setHtml( text );
        tb->setWindowFlags( Qt::Window );
        tb->setAttribute( Qt::WA_DeleteOnClose );
        tb->setWindowTitle( tr( "logsquirl documentation" ) );
        tb->resize( this->width() / 2, this->height() );
        tb->show();
    }
    else {
        LOG_ERROR << "Can't open documentation resource";
    }
}

void MainWindow::showScratchPad()
{
    showSidebar( SidebarScratchPadTab );
}

void MainWindow::showValueNamesPanel()
{
    showSidebar( SidebarValueNamesTab );
}

void MainWindow::sendToScratchpad( QString newData )
{
    scratchPad_.addData( newData );
    showScratchPad();
}

void MainWindow::replaceDataInScratchpad( QString newData )
{
    scratchPad_.replaceData( newData );
    showScratchPad();
}

void MainWindow::showFiltersPanel()
{
    showSidebar( SidebarFiltersPanelTab );
}

void MainWindow::manageTabGroups()
{
    TabGroupManagerDialog dialog( this );
    dialog.exec();
    mainTabWidget_.refreshAllTabGroupAppearances();
}

void MainWindow::clearIndexCache()
{
    // Only cleared, so which Log Files it would exclude and how large it may
    // grow do not matter here.
    const auto freed
        = IndexCache{ session_.indexingPolicy().indexCacheDirectory, QString{}, 0 }.clearAll();
    const auto freedMb = static_cast<double>( freed ) / ( 1024.0 * 1024.0 );
    statusBar()->showMessage( tr( "Index cache cleared (%1 MB freed)" ).arg( freedMb, 0, 'f', 1 ),
                              5000 );
}

void MainWindow::showCommandPalette()
{
    if ( !commandPalette_ ) {
        commandPalette_ = new CommandPalette( this );
    }

    // Collect commands from the menu bar.
    std::vector<CommandEntry> entries;

    const auto collectFromMenu
        = [ this, &entries ]( QMenu* menu, const QString& category, auto&& self ) -> void {
        for ( QAction* action : menu->actions() ) {
            // Opening the palette from inside itself would do nothing useful.
            if ( action->isSeparator() || !action->isEnabled() || action == commandPaletteAction ) {
                continue;
            }
            if ( action->menu() ) {
                self( action->menu(), category + " › " + action->text().remove( '&' ), self );
                continue;
            }
            CommandEntry entry;
            entry.name = action->text().remove( '&' );
            entry.category = category;
            entry.shortcut = action->shortcut().toString( QKeySequence::NativeText );
            // Capture a QPointer so the action firing path is safe if the
            // QAction has been destroyed (menus rebuilt) before invocation.
            QPointer<QAction> safeAction( action );
            entry.action = [ safeAction ]() {
                if ( safeAction ) {
                    safeAction->trigger();
                }
            };
            entries.push_back( std::move( entry ) );
        }
    };

    for ( QAction* topAction : menuBar()->actions() ) {
        if ( topAction->menu() ) {
            const auto category = topAction->text().remove( '&' );
            collectFromMenu( topAction->menu(), category, collectFromMenu );
        }
    }

    commandPalette_->setCommands( std::move( entries ) );
    commandPalette_->show();
}

void MainWindow::openMergedFiles( QStringList filePaths, bool dedup )
{
    if ( filePaths.isEmpty() ) {
        return;
    }

    // The merge controller belongs to the merged tab: closing the tab ends
    // the rebuild and removes the temporary file. The window holds it only
    // until the tab is there.
    auto* controller = new MergeController( this );
    const auto mergedPath = controller->merge( filePaths, dedup );

    // Open the merged temp file as a tab of its own. It goes with its tab, so
    // it is not saved with the Session (#570).
    auto provenance
        = LogFileProvenance::transient( dedup ? tr( "Merged (dedup)" ) : tr( "Merged" ) );
    // The tab is there once the open is done, which is after the plugins
    // have loaded when they have not yet (#537).
    provenance.whenOpened = [ controller, mergedPath ]( CrawlerWidget* crawler ) {
        if ( crawler == nullptr ) {
            LOG_WARNING << "No tab holds the merged file " << mergedPath;
            delete controller;
            return;
        }
        controller->setParent( crawler );
        // When the merged file is rebuilt, its tab reads it again
        QObject::connect( controller, &MergeController::mergedFileUpdated, crawler,
                          &CrawlerWidget::reload );
    };
    openLogFile( mergedPath, std::move( provenance ) );
}

void MainWindow::toggleSidebar()
{
    if ( sidebarDock_->isVisible() ) {
        sidebarDock_->hide();
    }
    else {
        sidebarDock_->show();
        sidebarDock_->raise();
    }
}

void MainWindow::showSidebar( int tabIndex )
{
    sidebarDock_->show();
    sidebarDock_->raise();
    sidebarTabs_->setCurrentIndex( tabIndex );
}

void MainWindow::importChipmunkFilters()
{
    const auto file
        = QFileDialog::getOpenFileName( this, tr( "Import Chipmunk filters" ), "",
                                        tr( "Chipmunk filters (*.json);;All files (*)" ) );

    if ( file.isEmpty() ) {
        return;
    }

    QFile jsonFile( file );
    if ( !jsonFile.open( QIODevice::ReadOnly ) ) {
        QMessageBox::warning( this, tr( "Import error" ),
                              tr( "Could not open file: %1" ).arg( file ) );
        return;
    }

    const auto jsonData = jsonFile.readAll();
    const auto chipmunkFilters = logsquirl::chipmunk::parseChipmunkJson( jsonData );

    if ( chipmunkFilters.isEmpty() ) {
        QMessageBox::information( this, tr( "Import result" ),
                                  tr( "No filters found in the selected file." ) );
        return;
    }

    // Import as PredefinedFilterSet (group named after the file)
    const auto groupName = QFileInfo( file ).baseName();
    auto& filtersCollection = PredefinedFiltersCollection::getSynced();

    int filtersAdded = 0;
    if ( filtersCollection.hasSetByName( groupName ) ) {
        QMessageBox::information(
            this, tr( "Import result" ),
            tr( "A filter group named '%1' already exists. Skipping filter import." )
                .arg( groupName ) );
    }
    else {
        auto filterSet = logsquirl::chipmunk::toFilterSet( chipmunkFilters, groupName );
        filtersAdded = static_cast<int>( filterSet.filters().size() );
        auto sets = filtersCollection.filterSets();
        sets.append( filterSet );
        filtersCollection.setFilterSets( sets );
        filtersCollection.save();
    }

    // Import as HighlighterSet
    const auto& setName = groupName;
    const auto highlighterSet = logsquirl::chipmunk::toHighlighterSet( chipmunkFilters, setName );

    auto& highlighterCollection = HighlighterSetCollection::getSynced();
    auto sets = highlighterCollection.highlighterSets();

    bool highlighterAdded = false;
    if ( !highlighterCollection.hasSetByName( setName ) ) {
        sets.append( highlighterSet );
        highlighterCollection.setHighlighterSets( sets );
        highlighterCollection.save();
        highlighterAdded = true;
        updateHighlightersMenu();
    }

    // Refresh the filters panel if visible
    filtersPanel_.refreshFilters();

    QMessageBox::information( this, tr( "Import result" ),
                              tr( "Imported %1 filter(s) and %2 highlighter set." )
                                  .arg( filtersAdded )
                                  .arg( highlighterAdded ? 1 : 0 ) );

    // The imported Highlighter Set may be active: paint every open Log File
    // again with it, not only the one the current tab shows.
    session_.applyChange( Changed::HighlighterSets );
}

void MainWindow::encodingChanged( QAction* action )
{
    const auto mibData = action->data();
    std::optional<int> mib;
    if ( mibData.isValid() ) {
        mib = mibData.toInt();
    }

    LOG_DEBUG << "encodingChanged, encoding " << mib.value_or( 0 );
    if ( auto crawler = currentCrawlerWidget() ) {
        crawler->setEncoding( mib );
        updateInfoLine();
    }
}

// The three View-menu toggles below write a setting the Presentation Policy
// names. They take the Options Dialog's path back to what is running, the
// Session, so that every open Log File shows the new setting, not only the
// current tab (#192, #245).
void MainWindow::toggleOverviewVisibility( bool isVisible )
{
    auto& config = Configuration::get();
    config.setOverviewVisible( isVisible );
    config.save();
    session_.applyChange( Changed::Settings );
}

void MainWindow::toggleMainLineNumbersVisibility( bool isVisible )
{
    auto& config = Configuration::get();

    config.setMainLineNumbersVisible( isVisible );
    config.save();
    session_.applyChange( Changed::Settings );
}

void MainWindow::toggleFilteredLineNumbersVisibility( bool isVisible )
{
    auto& config = Configuration::get();

    config.setFilteredLineNumbersVisible( isVisible );
    config.save();
    session_.applyChange( Changed::Settings );
}

// The one place the follow action is written: from what the View Set of the
// Log File in front holds, as it says it or as it is read when the tab comes
// to the front (#558, #635).
void MainWindow::changeFollowMode( bool follow )
{
    if ( follow && !session_.watchPolicy().anyWatchEnabled() ) {
        LOG_WARNING << "File watch disabled in settings";
    }

    followAction->setChecked( follow );
}

void MainWindow::lineNumberHandler( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                                    LineLength nSymbols )
{
    // The line number received is the internal (starts at 0)
    uint64_t fileSize{};
    uint64_t fileNbLine{};
    QDateTime lastModified;

    session_.getFileInfo( currentCrawlerWidget(), &fileSize, &fileNbLine, &lastModified );

    if ( fileNbLine != 0 ) {
        if ( nSymbols.get() == 0 ) {
            lineNbField->setText( tr( "Ln:%1/%2" ).arg( startLine.get() + 1 ).arg( fileNbLine ) );
        }
        else {
            if ( nLines.get() == 1 ) {
                // portion selection on one line
                lineNbField->setText( tr( "Ln:%1/%2 Col:%3 Sel:%4|%5" )
                                          .arg( startLine.get() + 1 )
                                          .arg( fileNbLine )
                                          .arg( startCol.get() )
                                          .arg( nSymbols.get() )
                                          .arg( nLines.get() ) );
            }
            else {
                // multiple lines selection
                lineNbField->setText( tr( "Ln:%1/%2 Sel:%4|%5" )
                                          .arg( startLine.get() + 1 )
                                          .arg( fileNbLine )
                                          .arg( nSymbols.get() )
                                          .arg( nLines.get() ) );
            }
        }
    }
    else {
        lineNbField->clear();
    }
}

void MainWindow::newPredefinedFilterHandler( QString newFilter )
{
    editPredefinedFilters( newFilter );
}

void MainWindow::updateLoadingProgress( int progress )
{
    LOG_DEBUG << "Loading progress: " << progress;

    // We ignore 0% and 100% to avoid a flash when the file (or update)
    // is very short.
    if ( progress > 0 && progress < 100 ) {
        showLoadingProgress( progress );
    }
}

void MainWindow::showLoadingProgress( int progress )
{
    // Guard: currentCrawlerWidget() returns nullptr when the active tab is
    // not a CrawlerWidget.
    auto* crawler = currentCrawlerWidget();
    if ( !crawler ) {
        return;
    }

    QString current_file = QDir::toNativeSeparators( session_.getFilename( crawler ) );

    infoLine->setText( current_file + tr( " - Indexing lines... (%1 %)" ).arg( progress ) );
    infoLine->displayGauge( progress );

    showInfoLabels( false );

    stopAction->setEnabled( true );
    reloadAction->setEnabled( false );
}

void MainWindow::handleLoadingFinished( LoadingStatus status, const QString& failure )
{
    LOG_DEBUG << "handleLoadingFinished success=" << ( status == LoadingStatus::Successful );

    // No file is loading
    loadingFileName.clear();

    // Guard: active tab may not be a CrawlerWidget.
    auto* crawler = currentCrawlerWidget();
    if ( !crawler ) {
        return;
    }

    if ( status == LoadingStatus::Successful ) {
        updateInfoLine();

        infoLine->hideGauge();
        showInfoLabels( true );
        stopAction->setEnabled( false );
        reloadAction->setEnabled( true );

        lineNumberHandler( 0_lnum, LinesCount( 0 ), LineColumn( 0 ), LineLength( 0 ) );

        // The Log Format is recognized once the load has finished.
        updateGoToTimestampAction( crawler->state() );

        // Now everything is ready, we can finally show the file!
        crawler->show();
    }
    else if ( status == LoadingStatus::Interrupted && crawler->hasLoaded() ) {
        // A reload a newer one interrupted, as a merged tab's rebuilds do:
        // the Log File stays open with what it shows, and the newer load goes
        // on (#621). Only a first load that never finished closes the tab.
        infoLine->hideGauge();
        stopAction->setEnabled( false );
        reloadAction->setEnabled( true );
    }
    else {
        if ( status == LoadingStatus::NoMemory ) {
            QMessageBox alertBox;
            alertBox.setText( tr( "Not enough memory." ) );
            alertBox.setInformativeText(
                tr( "The system does not have enough memory to hold the index for this file. The "
                    "file will now be closed." ) );
            alertBox.setIcon( QMessageBox::Critical );
            alertBox.exec();
        }

        // Heard as the load ended, or read as its tab is brought to the front
        // after it failed there (#540): the tab is closed once the tab switch
        // is done, and a Failed load is offered to be reported.
        QTimer::singleShot(
            0, this, [ this, failed = QPointer<CrawlerWidget>( crawler ), status, failure ] {
                const auto index = failed ? mainTabWidget_.indexOf( failed ) : -1;
                if ( index < 0 ) {
                    return;
                }
                closeTab( index, ActionInitiator::App );
                if ( status == LoadingStatus::Failed ) {
                    IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, failure );
                }
            } );
    }

    // mainTabWidget_.setEnabled( true );
}

void MainWindow::showStatusMessage( QString message )
{
    statusBar()->showMessage( message, 8000 );
}

void MainWindow::handleFilteredViewChanged()
{
    int currentIndex = mainTabWidget_.currentIndex();
    if ( currentIndex >= 0 && mainTabWidget_.holdsLogFile( currentIndex ) ) {
        auto* crawler_widget
            = dynamic_cast<CrawlerWidget*>( mainTabWidget_.widget( currentIndex ) );
        if ( crawler_widget ) {
            quickFindMux_.registerSelector( crawler_widget );
        }
    }
}

void MainWindow::applyQuickFindPolicy()
{
    const auto& policy = session_.quickFindPolicy();
    quickFindMux_.setQuickFindPolicy( policy );
    quickFindWidget_.setQuickFindPolicy( policy );
}

void MainWindow::closeTab( int index, ActionInitiator initiator )
{
    closeTabs( { index }, initiator );
}

void MainWindow::closeTabs( const QList<int>& indices, ActionInitiator initiator )
{
    // Never close a tab that holds no Log File: the pinned dashboard. The
    // widgets are held rather than the indices: an index moves as the tabs
    // before it close, and a tab may close while the question is asked.
    std::vector<QPointer<CrawlerWidget>> crawlers;
    for ( const auto index : indices ) {
        if ( !mainTabWidget_.holdsLogFile( index ) ) {
            continue;
        }
        auto* crawler = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
        if ( crawler != nullptr
             && std::ranges::find( crawlers, crawler, &QPointer<CrawlerWidget>::data )
                    == crawlers.end() ) {
            crawlers.emplace_back( crawler );
        }
    }
    if ( crawlers.empty() ) {
        return;
    }

    // The user is asked once for the whole set, if the settings say so; a
    // close the application asks for is never questioned.
    if ( initiator == ActionInitiator::User && Configuration::get().confirmTabClose() ) {
        QMessageBox msgBox( this );
        msgBox.setIcon( QMessageBox::Question );
        if ( crawlers.size() == 1 ) {
            const auto displayName
                = QFileInfo( session_.getFilename( crawlers.front() ) ).fileName();
            msgBox.setWindowTitle( tr( "Close Tab" ) );
            msgBox.setText( tr( "Close tab \"%1\"?" ).arg( displayName ) );
        }
        else {
            msgBox.setWindowTitle( tr( "Close Tabs" ) );
            msgBox.setText( tr( "Close %n tab(s)?", "", static_cast<int>( crawlers.size() ) ) );
        }
        msgBox.setStandardButtons( QMessageBox::Yes | QMessageBox::No );
        msgBox.setDefaultButton( QMessageBox::No );

        auto* dontAskCheckBox = new QCheckBox( tr( "Don't ask again" ), &msgBox );
        msgBox.setCheckBox( dontAskCheckBox );

        if ( msgBox.exec() != QMessageBox::Yes ) {
            return;
        }

        if ( dontAskCheckBox->isChecked() ) {
            Configuration::get().setConfirmTabClose( false );
            Configuration::get().save();
        }
    }

    for ( const auto& crawler : crawlers ) {
        const auto index = crawler ? mainTabWidget_.indexOf( crawler ) : -1;
        if ( index < 0 ) {
            continue;
        }

        // Only a Log File the user closed becomes a recent file, and never a
        // Transient one: its path is gone after a restart (#597). A converted
        // one is kept by the file it was converted from (#605), a decompressed
        // one by its archive (#609).
        const auto fileName = session_.getFilename( crawler );
        if ( initiator == ActionInitiator::User ) {
            const auto recentFile = session_.originOf( crawler ).recentFile( fileName );
            if ( !recentFile.isEmpty() ) {
                addRecentFile( recentFile );
            }
        }

        crawler->stopLoading();
        mainTabWidget_.removeCrawler( index );
        session_.close( crawler );
        crawler->deleteLater();

        // What feeds the tab stops now.
        stopCommandSource( fileName );
    }

    updateOpenedFilesMenu();
}

void MainWindow::currentTabChanged( int index )
{
    LOG_DEBUG << "currentTabChanged";

    if ( index >= 0 && mainTabWidget_.holdsLogFile( index ) ) {
        auto* crawler_widget = dynamic_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
        if ( !crawler_widget ) {
            return;
        }
        // A restored Log File still waiting for its turn loads now that the
        // user looks at its tab (#300).
        if ( !restoringSession_ ) {
            session_.startLoading( crawler_widget );
        }

        connectFrontTab( crawler_widget );
        quickFindMux_.registerSelector( crawler_widget );

        // The window heard nothing of this Log File while its tab was not in
        // front: it shows what the Log File's state says (#540, #635).
        const auto loadState = crawler_widget->state();
        if ( loadState.loadStatus ) {
            // As the last load ended, failed included.
            handleLoadingFinished( *loadState.loadStatus, loadState.loadFailure );
        }
        else {
            // A load under way is shown loading whatever its progress: the
            // info line still describes the tab shown before.
            showLoadingProgress( loadState.loadingProgress );
        }

        // No configuration is applied here: a settings change has already
        // reached this Log File, in front or not (#245).
        crawler_widget->broughtToFront();

        // Read again: a failed load's message box runs an event loop, in which
        // the Log File may have changed, a queued follow among others. The
        // Log Line selected in it is shown after its load, which shows the
        // first (#692).
        const auto state = crawler_widget->state();
        lineNumberHandler( state.selectedLine, 0_lcount, 0_lcol, 0_length );
        updateMenuBarFromDocument( state );
        updateTitleBar( session_.getFilename( crawler_widget ) );
        updateFavoritesMenu();

        editMenu->setEnabled( true );

        // Notify plugins about the active file change
        plugins_->host().notifyActiveFileChanged( session_.getFilename( crawler_widget ) );
    }
    else {
        // No tab, or one that holds no Log File, such as the dashboard -- clear
        // the document state
        connectFrontTab( nullptr );
        quickFindMux_.registerSelector( nullptr );

        infoLine->hideGauge();
        infoLine->clear();
        showInfoLabels( false );

        // Show "Dashboard" in title bar when on the dashboard tab,
        // otherwise clear title (avoids misleading "Untitled")
        if ( isDashboardTab( mainTabWidget_, index ) ) {
            updateTitleBar( tr( "Dashboard" ) );
        }
        else {
            updateTitleBar( QString() );
        }

        editMenu->setEnabled( false );
        addToFavoritesAction->setEnabled( false );
        addToFavoritesMenuAction->setEnabled( false );

        // Notify plugins that no file is active
        plugins_->host().notifyActiveFileChanged( QString() );

        // Refresh dashboard when it becomes visible
        if ( isDashboardTab( mainTabWidget_, index ) ) {
            showDashboardOrTabs();
        }
    }
}

void MainWindow::connectFrontTab( CrawlerWidget* crawler )
{
    for ( const auto& connection : frontTabConnections_ ) {
        disconnect( connection );
    }
    frontTabConnections_.clear();
    if ( crawler == nullptr ) {
        return;
    }

    frontTabConnections_ = {
        // What the window asks of the Log File in front
        connect( this, &MainWindow::followSet, crawler, &CrawlerWidget::followSet ),
        connect( this, &MainWindow::textWrapSet, crawler, &CrawlerWidget::textWrapSet ),
        connect( this, &MainWindow::valueNamesShownSet, crawler,
                 &CrawlerWidget::valueNamesShownSet ),
        connect( this, &MainWindow::enteringQuickFind, crawler, &CrawlerWidget::enteringQuickFind ),
        connect( &quickFindWidget_, &QuickFindWidget::close, crawler,
                 &CrawlerWidget::exitingQuickFind ),
        connect( goToLineAction, &QAction::triggered, crawler, &CrawlerWidget::goToLine ),
        connect( goToTimestampAction, &QAction::triggered, crawler, &CrawlerWidget::goToTimestamp ),
        connect( searchLimitsTimeRangeAction, &QAction::triggered, crawler,
                 &CrawlerWidget::setSearchLimitsToTimeRange ),
        connect( searchLimitsAroundLineAction, &QAction::triggered, crawler,
                 &CrawlerWidget::setSearchLimitsAroundCurrentLine ),
        connect( reloadAction, &QAction::triggered, crawler, &CrawlerWidget::reload ),
        connect( stopAction, &QAction::triggered, crawler, &CrawlerWidget::stopLoading ),

        // What the Log File in front tells the window
        connect( crawler, &CrawlerWidget::followModeChanged, this, &MainWindow::changeFollowMode ),
        connect( crawler, &CrawlerWidget::newSelection, this, &MainWindow::lineNumberHandler ),
        connect( crawler, &CrawlerWidget::saveCurrentSearchAsPredefinedFilter, this,
                 &MainWindow::newPredefinedFilterHandler ),
        connect( crawler, &CrawlerWidget::sendToScratchpad, this, &MainWindow::sendToScratchpad ),
        connect( crawler, &CrawlerWidget::replaceDataInScratchpad, this,
                 &MainWindow::replaceDataInScratchpad ),
        connect( crawler, &CrawlerWidget::loadingProgressed, this,
                 &MainWindow::updateLoadingProgress ),
        connect( crawler, &CrawlerWidget::loadingFinished, this,
                 &MainWindow::handleLoadingFinished ),
        connect( crawler, &CrawlerWidget::statusMessage, this, &MainWindow::showStatusMessage ),
        connect( crawler, &CrawlerWidget::filteredViewChanged, this,
                 &MainWindow::handleFilteredViewChanged ),
    };
}

void MainWindow::changeQFPattern( const QString& newPattern )
{
    quickFindWidget_.changeDisplayedPattern( newPattern, true );
}

void MainWindow::loadFileNonInteractive( const QString& file_name )
{
    LOG_DEBUG << "loadFileNonInteractive( " << file_name.toStdString() << " )";

    loadFile( file_name );
    bringToFront();
}

void MainWindow::bringToFront()
{
    // Try to get the window to the front
    // This is a bit of a hack but has been tested on:
    // Qt 5.3 / Gnome / Linux
    // Qt 5.11 / Win10
#ifdef Q_OS_WIN
    const auto isMaximized = isMaximized_;

    if ( isMaximized ) {
        showMaximized();
    }
    else {
        showNormal();
    }

    activateWindow();
    raise();
#else
    Qt::WindowFlags window_flags = windowFlags();
    window_flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags( window_flags );

    raise();
    activateWindow();

    window_flags = windowFlags();
    window_flags &= ~Qt::WindowStaysOnTopHint;
    setWindowFlags( window_flags );
    show();
#endif

    if ( auto currentCrawler = currentCrawlerWidget() ) {
        currentCrawler->setFocus();
    }
}

//
// Events
//

// Closes the application
MainWindow::~MainWindow()
{
    session_.removeWindow( this );

    // The Plugin Host outlives this window. What plugins show here is taken
    // out before the window's widgets go, so no plugin widget is deleted with
    // them; their widgets move to the most recently active remaining window.
    plugins_->uiPort().removeWindow( pluginUi_.get() );
}

void MainWindow::closeEvent( QCloseEvent* event )
{
    if ( !isCloseFromTray_ && this->isVisible() && Configuration::get().minimizeToTray() ) {
        event->ignore();
        trayIcon_->show();
        this->hide();
    }
    else {
        const auto saveSettings = session_.close();
        if ( saveSettings ) {
            writeSettings();
        }

        // A Log File still decompressing for a restore was saved as it was
        // stored, and gets no tab in a closed window (#610).
        archiveRestores_.cancelAll();

        closeAll( ActionInitiator::App );
        trayIcon_->hide();
        Q_EMIT windowClosed();

        event->accept();
    }
}

// Minimize handling the application
void MainWindow::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::WindowStateChange ) {
        isMaximized_ = windowState().testFlag( Qt::WindowMaximized );

        if ( this->windowState() & Qt::WindowMinimized ) {
            if ( Configuration::get().minimizeToTray() ) {
                dispatchToMainThread( [ this ] {
                    trayIcon_->show();
                    this->hide();
                } );
            }
        }
    }
    else if ( event->type() == QEvent::LanguageChange ) {
        reTranslateUI();
    }

    QMainWindow::changeEvent( event );
}

// Accepts the drag event if it looks like a filename
void MainWindow::dragEnterEvent( QDragEnterEvent* event )
{
    if ( event->mimeData()->hasFormat( "text/uri-list" ) )
        event->acceptProposedAction();
}

// Tries and loads the file if the URL dropped is local
void MainWindow::dropEvent( QDropEvent* event )
{
    const QList<QUrl> urls = event->mimeData()->urls();

    for ( const auto& url : urls ) {
        auto fileName = url.toLocalFile();
        if ( fileName.isEmpty() )
            continue;

        loadFile( fileName );
    }
}

bool MainWindow::eventFilter( QObject* watched, QEvent* event )
{
    if ( waitingForExposure_ && watched == windowHandle() && event->type() == QEvent::Expose
         && windowHandle()->isExposed() ) {
        waitingForExposure_ = false;
        windowHandle()->removeEventFilter( this );
        plugins_->loadSoon();
    }
    return QMainWindow::eventFilter( watched, event );
}

bool MainWindow::event( QEvent* event )
{
    if ( event->type() == QEvent::WindowActivate ) {
        servePluginCallbacks();
        plugins_->uiPort().activateWindow( pluginUi_.get() );
        Q_EMIT windowActivated();
    }
    else if ( event->type() == QEvent::Show ) {
        if ( this->windowHandle() ) {
            // The plugins load once for the application, after this window
            // is on screen rather than while it is built (#303): once the
            // window system has exposed it, which paints it before the
            // loading queued here runs.
            if ( !plugins_->isLoaded() && !waitingForExposure_ ) {
                waitingForExposure_ = true;
                this->windowHandle()->installEventFilter( this );
            }

            std::call_once( screenChangesConnect_, [ this ]() {
                logScreenInfo( this->windowHandle()->screen() );
                connect( this->windowHandle(), &QWindow::screenChanged,
                         [ this ]( QScreen* screen ) { logScreenInfo( screen ); } );
            } );
        }
    }

    return QMainWindow::event( event );
}

//
// Private functions
//

bool MainWindow::extractAndLoadFile( const QString& fileName )
{
    const auto& fileAccess = session_.fileAccessPolicy();

    if ( !fileAccess.extractArchives ) {
        return false;
    }

    if ( !fileAccess.extractArchivesAlways ) {
        const auto userChoice = QMessageBox::question( this, tr( "logsquirl" ),
                                                       tr( "Extract archive to temp folder?" ) );
        if ( userChoice == QMessageBox::No ) {
            return false;
        }
    }

    const auto decompressAction = Decompressor::action( fileName );

    // The archive this is, or the member of one it was decompressed from: a
    // Log File decompressed from it is saved with that, one level down (#596).
    const auto archiveMember = archiveMembers_.value( fileName );

    Decompressor decompressor;
    AtomicFlag decompressInterrupt;

    QProgressDialog progressDialog;
    progressDialog.setLabelText( tr( "Extracting %1" ).arg( fileName ) );
    progressDialog.setRange( 0, 0 );

    connect( &decompressor, &Decompressor::finished,
             [ &progressDialog ]( bool isOk ) { progressDialog.done( isOk ? 0 : 1 ); } );
    connect( &progressDialog, &QProgressDialog::canceled,
             [ &decompressInterrupt, &decompressor ]() {
                 decompressInterrupt.set();
                 decompressor.waitForResult();
             } );

    if ( decompressAction == DecompressAction::Decompress ) {

        auto tempFile = new QTemporaryFile(
            this->tempDir_.filePath( QFileInfo( fileName ).fileName() ), this );

        if ( tempFile->open() && decompressor.decompress( fileName, tempFile, decompressInterrupt )
             && !progressDialog.exec() ) {

            if ( decompressInterrupt ) {
                return false;
            }

            return openLogFile( tempFile->fileName(), LogFileProvenance::fromArchive(
                                                          archiveMember.inside( fileName, {} ) ) );
        }
        else {
            QMessageBox::warning(
                this, tr( "logsquirl" ),
                tr( "Failed to decompress %1" ).arg( QDir::toNativeSeparators( fileName ) ) );
        }
    }
    else if ( decompressAction == DecompressAction::Extract ) {
        QTemporaryDir archiveDir{ this->tempDir_.filePath( QFileInfo( fileName ).fileName() ) };
        archiveDir.setAutoRemove( false );
        if ( decompressor.extract( fileName, archiveDir.path(), decompressInterrupt )
             && !progressDialog.exec() ) {

            if ( decompressInterrupt ) {
                return false;
            }

            const auto selectedFiles
                = chooseArchiveMembers_
                      ? chooseArchiveMembers_( this, archiveDir.path() )
                      : QFileDialog::getOpenFileNames( this, tr( "Open file from archive" ),
                                                       archiveDir.path(), tr( "All files (*)" ) );

            const QDir extracted{ archiveDir.path() };
            for ( const auto& extractedFile : selectedFiles ) {
                // A file picked beside the archive's is a Log File of its own.
                const auto member = extracted.relativeFilePath( extractedFile );
                openLogFile(
                    extractedFile,
                    !member.startsWith( "../" ) && !QDir::isAbsolutePath( member )
                        ? LogFileProvenance::fromArchive( archiveMember.inside( fileName, member ) )
                        : LogFileProvenance::ordinary() );
            }

            return true;
        }
        else {
            QMessageBox::warning(
                this, tr( "logsquirl" ),
                tr( "Failed to extract %1" ).arg( QDir::toNativeSeparators( fileName ) ) );
        }
    }

    return false;
}

void MainWindow::setArchiveMemberChooser( ArchiveMemberChooser chooser )
{
    chooseArchiveMembers_ = std::move( chooser );
}

bool MainWindow::loadFile( const QString& fileName, bool followFile )
{
    return openLogFile( fileName, LogFileProvenance::ordinary(), followFile );
}

bool MainWindow::openLogFile( const QString& fileName, LogFileProvenance provenance,
                              bool followFile )
{
    LOG_DEBUG << "openLogFile ( " << fileName.toStdString() << " )";

    // What the tab is, kept by the path until the tab closes. The tab is
    // named when it opens, which is later than here while the plugins load:
    // the title is the file's, not the current tab's (#606).
    if ( !provenance.openingTitle.isEmpty() ) {
        mainTabWidget_.setOpeningTitle( fileName, provenance.openingTitle, provenance.toolTip );
    }
    // The Session saves a decompressed Log File with its archive (#596), and
    // the recent files, tab names and groups know it by that (#609). One
    // decompressed into tempDir_ before is known by it again.
    if ( provenance.origin.archiveMember.isEmpty() ) {
        provenance.origin.archiveMember = archiveMembers_.value( fileName );
    }
    else {
        archiveMembers_.insert( fileName, provenance.origin.archiveMember );
    }
    // The Command Source lives as long as the tab (#575).
    if ( provenance.commandSource ) {
        connect( provenance.commandSource.get(), &CommandSource::ended, this,
                 [ this, fileName ]( const CommandEnd& end ) {
                     showCommandSourceEnded( fileName, end );
                 } );
        connect( provenance.commandSource.get(), &CommandSource::outputEncodingDecided, this,
                 [ this, fileName ] { applyCommandOutputEncoding( fileName ); } );
        commandSources_[ fileName ] = std::move( provenance.commandSource );
    }

    auto open = [ this, fileName, followFile, origin = provenance.origin,
                  whenOpened = std::move( provenance.whenOpened ) ] {
        const auto isOpen = openNow( fileName, followFile, origin );
        // Nothing of a tab that did not open is kept.
        if ( !isOpen ) {
            forgetOpening( fileName );
        }
        else {
            // The command's output may have decided its Encoding before the
            // tab opened.
            applyCommandOutputEncoding( fileName );
        }
        if ( whenOpened ) {
            const auto tab = isOpen ? mainTabWidget_.tabOfPath( fileName ) : -1;
            whenOpened( tab >= 0 ? qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( tab ) )
                                 : nullptr );
        }
        return isOpen;
    };

    // Whether a converter plugin opens this file is only known once the
    // plugins have loaded. A file asked for before -- from the command line,
    // by another instance, dropped at once -- waits for them rather than
    // being opened without its converter (#303).
    if ( plugins_->isLoaded() ) {
        return open();
    }
    LOG_INFO << "Opening " << fileName << " once the plugins have loaded";
    plugins_->whenLoaded( this, [ open ] { open(); } );
    // A window just shown has them load once it is on screen; any other
    // window asks for them itself.
    if ( !waitingForExposure_ ) {
        plugins_->loadSoon();
    }
    return true;
}

void MainWindow::forgetOpening( const QString& fileName )
{
    mainTabWidget_.setOpeningTitle( fileName, {} );
    archiveMembers_.remove( fileName );
    stopCommandSource( fileName );
}

void MainWindow::stopCommandSource( const QString& fileName )
{
    // Its spool file goes after the tab, which still reads it until then
    // (#575).
    if ( auto source = commandSources_.extract( fileName ) ) {
        source.mapped()->stop();
        source.mapped()->setParent( this );
        source.mapped().release()->deleteLater();
    }
}

// Create a CrawlerWidget for the passed file, start its loading
// and update the title bar.
// The loading is done asynchronously.
bool MainWindow::openNow( const QString& fileName, bool followFile, const LogFileOrigin& origin )
{
    // First check if the file is already open, or converted (#615), in any
    // window: the Session knows, and the window showing it brings its tab to
    // the front (#642).
    if ( session_.showOpen( fileName ) ) {
        return true;
    }

    // Check if a converter plugin handles this file extension (Phase 4)
    auto& pluginHost = plugins_->host();
    const auto ext = QFileInfo( fileName ).suffix().toLower();
    const auto converterId = pluginHost.converterForExtension( ext );
    if ( !converterId.isEmpty() ) {
        auto* tempFile = new QTemporaryFile(
            tempDir_.filePath( QFileInfo( fileName ).fileName() + ".txt" ), this );
        if ( tempFile->open() ) {
            const auto rc = pluginHost.runConverter( converterId, fileName, tempFile->fileName() );
            if ( rc == 0 ) {
                // The converted Log File is a Transient one, whatever the
                // file it was converted from: it is read from a temporary
                // file that is gone after a restart, so neither the Session
                // nor the recent files keep its path (#605). It is not
                // converted again on restore either: the Session is restored
                // before the plugins load (#303), so the converter would not
                // be there for it. The recent files keep the Log File it was
                // converted from instead, unless that one is Transient too;
                // opening that one again shows this tab (#615).
                return openLogFile( tempFile->fileName(),
                                    LogFileProvenance::conversionOf( fileName, origin ),
                                    followFile );
            }
            LOG_ERROR << "Converter plugin " << converterId << " failed with rc=" << rc;
        }
    }

    const auto decompressAction = Decompressor::action( fileName );

    if ( decompressAction == DecompressAction::None
         || !session_.fileAccessPolicy().extractArchives ) {
        // Load the file
        loadingFileName = fileName;

        try {
            // The view context saved for this Log File, if any, is restored
            // as the Session restores it: while its views are built.
            CrawlerWidget* crawlerWidget = nullptr;
            session_.open(
                fileName,
                [ &crawlerWidget ]( const ViewBuild& build ) {
                    crawlerWidget = new CrawlerWidget( build );
                    return crawlerWidget;
                },
                origin );

            if ( !crawlerWidget ) {
                LOG_ERROR << "Can't create crawler for " << fileName.toStdString();
                return false;
            }

            // We won't show the widget until the file is fully loaded
            crawlerWidget->hide();

            // We disable the tab widget to avoid having someone switch
            // tab during loading. (maybe FIXME)
            // mainTabWidget_.setEnabled( false );

            // It opens with the title given to it, if any (#606). One from an
            // archive keeps its name and group by the archive (#609).
            int index = mainTabWidget_.addCrawler( crawlerWidget, fileName, origin.lifetime,
                                                   origin.storedKey( fileName ) );

            // Setting the new tab, the user will see a blank page for the duration
            // of the loading, with no way to switch to another tab
            mainTabWidget_.setCurrentIndex( index );

            // A Transient Log File's path is gone after a restart (#597); a
            // converted one is kept by the file it was converted from (#605),
            // a decompressed one by its archive (#609).
            if ( const auto recentFile = origin.recentFile( fileName ); !recentFile.isEmpty() ) {
                addRecentFile( recentFile );
            }
            updateOpenedFilesMenu();

            const auto& config = Configuration::get();
            if ( session_.watchPolicy().anyWatchEnabled()
                 && ( followFile || config.followFileOnLoad() ) ) {
                followOnOpen( crawlerWidget );
            }
            // A command or standard input that ended before its tab opened.
            if ( const auto source = commandSources_.find( fileName );
                 source != commandSources_.end() && source->second->hasEnded() ) {
                crawlerWidget->endFollowing();
            }
        } catch ( ... ) {
            LOG_ERROR << "Can't open file " << fileName.toStdString();
            return false;
        }

        LOG_DEBUG << "Success loading file " << fileName.toStdString();
        return true;
    }
    else {
        return extractAndLoadFile( fileName );
    }
}

bool MainWindow::showView( const ViewInterface* view )
{
    for ( const auto i : mainTabWidget_.logFileTabs() ) {
        auto* crawler = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( i ) );
        if ( crawler && static_cast<const ViewInterface*>( crawler ) == view ) {
            mainTabWidget_.setCurrentWidget( crawler );
            activateWindow();
            return true;
        }
    }
    return false;
}

// Strips the passed filename from its directory part.
QString MainWindow::strippedName( const QString& fullFileName ) const
{
    return QFileInfo( fullFileName ).fileName();
}

// Return the currently active CrawlerWidget, or NULL if none
CrawlerWidget* MainWindow::currentCrawlerWidget() const
{
    auto current = qobject_cast<CrawlerWidget*>( mainTabWidget_.currentWidget() );

    return current;
}

// Update the title bar.
void MainWindow::updateTitleBar( const QString& file_name )
{
    QString shownName = tr( "Untitled" );
    if ( !file_name.isEmpty() ) {
        shownName = strippedName( file_name );
    }

    QString indexPart = "";
    if ( session_.windowIndex() > 0 ) {
        indexPart = QString( " #%1" ).arg( session_.windowIndex() + 1 );
    }

    setWindowTitle( tr( "%1 - %2%3" ).arg( shownName, tr( "logsquirl" ), indexPart )
                    + tr( " (build " ) + logsquirlVersion() + ")" );
}

void MainWindow::addRecentFile( const QString& fileName )
{
    auto& recentFiles = RecentFiles::getSynced();
    recentFiles.addRecent( fileName );
    recentFiles.save();
    updateRecentFileActions();
}

// Updates the actions for the recent files.
// Must be called after having added a new name to the list.
void MainWindow::updateRecentFileActions()
{
    auto& recentFiles = RecentFiles::get();
    QStringList recent_files = recentFiles.recentFiles();
    int recent_files_max_items = recentFiles.getNumberItemsToShow();

    if ( recentFiles.recentFiles().count() > 0 ) {
        recentFilesMenu->setEnabled( true );
        for ( auto j = 0; j < MAX_RECENT_FILES; ++j ) {
            const auto actionIndex = static_cast<size_t>( j );
            if ( j < recent_files_max_items && j < recent_files.size() ) {
                int key = j + ( ( j < 9 ) ? 0x31 : ( 0x61 - 9 ) ); // shortcuts: 1..9 next a,b...
                QString text
                    = tr( "&%1 %2" ).arg( QChar( key ) ).arg( strippedName( recent_files[ j ] ) );
                recentFileActions[ actionIndex ]->setText( text );
                recentFileActions[ actionIndex ]->setToolTip( recent_files[ j ] );
                recentFileActions[ actionIndex ]->setData( recent_files[ j ] );
                recentFileActions[ actionIndex ]->setVisible( true );
            }
            else {
                recentFileActions[ actionIndex ]->setVisible( false );
            }
        }
    }
    else {
        recentFilesMenu->setEnabled( false );
    }

    // separatorAction->setVisible(!recentFiles.isEmpty());
}

// Clear the list of the recent files
void MainWindow::clearRecentFileActions()
{
    auto& recentFiles = RecentFiles::getSynced();
    recentFiles.removeAll();
    recentFiles.save();
    updateRecentFileActions();
}
// Update our menu bar to match the settings of the crawler
// (used when the tab is changed)
void MainWindow::updateMenuBarFromDocument( const CrawlerWidget::State& state )
{
    const auto& encodingMib = state.encodingMib;

    auto encodingActions = encodingGroup->actions();
    auto encodingItem = std::find_if( encodingActions.begin(), encodingActions.end(),
                                      [ &encodingMib ]( const auto& action ) {
                                          return ( !encodingMib && !action->data().isValid() )
                                                 || ( encodingMib && action->data().isValid()
                                                      && *encodingMib == action->data().toInt() );
                                      } );

    if ( encodingItem != encodingActions.end() ) {
        ( *encodingItem )->setChecked( true );
    }

    changeFollowMode( state.follows );
    textWrapAction->setChecked( state.textWrap );
    showValueNamesAction->setChecked( state.valueNamesShown );
    updateGoToTimestampAction( state );
}

// "Go to timestamp" is there for a Log File whose Log Format has a timestamp
// field; without one it says why it is not.
void MainWindow::updateGoToTimestampAction( const CrawlerWidget::State& state )
{
    const auto& reason = state.goToTimestampUnavailable;
    goToTimestampAction->setEnabled( reason.isEmpty() );
    goToTimestampAction->setToolTip( reason.isEmpty() ? goToTimestampAction->statusTip() : reason );

    // The time Search Limits need the same: a Timestamp on the Log Lines.
    const auto& limitsReason = state.searchLimitsByTimeUnavailable;
    for ( auto* action : { searchLimitsTimeRangeAction, searchLimitsAroundLineAction } ) {
        action->setEnabled( limitsReason.isEmpty() );
        action->setToolTip( limitsReason.isEmpty() ? action->statusTip() : limitsReason );
    }
}

// Update the top info line from the session
void MainWindow::updateInfoLine()
{
    QLocale defaultLocale;

    // Following should always work as we will only receive enter
    // this slot if there is a crawler connected.
    const auto* crawler = currentCrawlerWidget();
    if ( !crawler ) {
        return;
    }

    QString current_file = QDir::toNativeSeparators( session_.getFilename( crawler ) );

    uint64_t fileSize;
    uint64_t fileNbLine;
    QDateTime lastModified;

    session_.getFileInfo( crawler, &fileSize, &fileNbLine, &lastModified );

    infoLine->setText( current_file );
    infoLine->setPath( current_file );
    sizeField->setText( readableSize( fileSize ) );
    encodingField->setText( crawler->encodingText() );

    if ( lastModified.isValid() ) {
        const QString date = defaultLocale.toString( lastModified, QLocale::NarrowFormat );
        dateField->setText( tr( "modified on %1" ).arg( date ) );
        dateField->show();
    }
    else {
        dateField->hide();
    }
}

void MainWindow::updateOpenedFilesMenu()
{
    openedFilesMenu->clear();

    const auto& files = session_.openedFiles();

    openedFilesMenu->setEnabled( !files.empty() );

    openedFilesMenu->addAction( selectOpenFileAction );
    openedFilesMenu->addSeparator();

    for ( const auto& file : files ) {
        const auto displayFile = DisplayFilePath{ file };
        auto action = openedFilesMenu->addAction( displayFile.displayName() );

        action->setActionGroup( openedFilesGroup );
        action->setToolTip( displayFile.nativeFullPath() );
        action->setData( displayFile.fullPath() );
    }

    selectOpenFileAction->setDisabled( files.empty() );
}

void MainWindow::updateHighlightersMenu()
{
    highlightersMenu->clearHighlightersMenu();
    highlightersMenu->createHighlightersMenu();
    highlightersMenu->addAction( editHighlightersAction, true );
    highlightersMenu->populateHighlightersMenu();
}

void MainWindow::updateFavoritesMenu()
{
    favoritesMenu->clear();

    favoritesMenu->addAction( addToFavoritesMenuAction );
    favoritesMenu->addAction( removeFromFavoritesAction );

    addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star" ) );

    using namespace logsquirl::mainwindow;

    addToFavoritesAction->setText(
        QApplication::translate( "logsquirl::mainwindow::action", action::addToFavoritesText ) );
    addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star" ) );
    addToFavoritesAction->setData( true );

    const auto& favorites = FavoriteFiles::get().favorites();
    auto crawler = currentCrawlerWidget();

    addToFavoritesAction->setEnabled( crawler != nullptr );
    addToFavoritesMenuAction->setEnabled( crawler != nullptr );
    removeFromFavoritesAction->setEnabled( !favorites.empty() );

    if ( crawler ) {
        const auto path = session_.getFilename( crawler );
        if ( std::any_of( favorites.begin(), favorites.end(), FullPathComparator( path ) ) ) {

            addToFavoritesAction->setText( QApplication::translate(
                "logsquirl::mainwindow::action", action::removeFromFavoritesText ) );
            addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star-filled" ) );
            addToFavoritesAction->setData( false );

            addToFavoritesMenuAction->setEnabled( false );
            addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star-filled" ) );
        }
    }

    favoritesMenu->addSeparator();

    for ( const auto& file : favorites ) {
        auto action = favoritesMenu->addAction( file.displayName() );

        action->setActionGroup( favoritesGroup );
        action->setToolTip( file.nativeFullPath() );
        action->setData( file.fullPath() );
    }
}

void MainWindow::addToFavorites()
{
    if ( const auto crawler = currentCrawlerWidget() ) {
        auto& favorites = FavoriteFiles::get();
        const auto path = session_.getFilename( crawler );

        if ( addToFavoritesAction->data().toBool() ) {
            favorites.add( path );
        }
        else {
            favorites.remove( path );
        }

        favorites.save();

        updateFavoritesMenu();
    }
}

void MainWindow::removeFromFavorites()
{
    const auto& favoriteFiles = FavoriteFiles::get();
    const auto& favorites = favoriteFiles.favorites();
    QStringList files;
    std::transform( favorites.cbegin(), favorites.cend(), std::back_inserter( files ),
                    []( const auto& f ) { return f.nativeFullPath(); } );

    auto currentIndex = 0;

    if ( const auto crawler = currentCrawlerWidget() ) {
        const auto currentPath = session_.getFilename( crawler );
        const auto currentItem
            = std::find_if( favorites.begin(), favorites.end(), FullPathComparator( currentPath ) );
        if ( currentItem != favorites.end() ) {
            currentIndex = static_cast<int>( std::distance( favorites.begin(), currentItem ) );
        }
    }

    bool ok = false;
    const auto pathToRemove = QInputDialog::getItem( this, tr( "Remove from favorites" ),
                                                     tr( "Select item to remove from favorites" ),
                                                     files, currentIndex, false, &ok );
    if ( ok ) {
        removeFromFavorites( pathToRemove );
    }
}

void MainWindow::removeFromFavorites( const QString& pathToRemove )
{
    auto& favoriteFiles = FavoriteFiles::get();
    const auto& favorites = favoriteFiles.favorites();
    const auto selectedFile = std::find_if( favorites.begin(), favorites.end(),
                                            [ pathToRemove ]( const DisplayFilePath& f ) {
                                                return f.nativeFullPath() == pathToRemove;
                                            } );

    if ( selectedFile != favorites.end() ) {
        favoriteFiles.remove( selectedFile->fullPath() );
        favoriteFiles.save();
        updateFavoritesMenu();
    }
}

void MainWindow::removeFromRecent( const QString& pathToRemove )
{
    auto& recentFiles = RecentFiles::get();
    recentFiles.removeRecent( pathToRemove );
    recentFiles.save();
    updateRecentFileActions();
}

void MainWindow::selectOpenedFile()
{
    auto openedFilesPaths = session_.openedFiles();
    std::vector<DisplayFilePath> openedFiles;
    openedFiles.reserve( openedFilesPaths.size() );
    std::transform( openedFilesPaths.cbegin(), openedFilesPaths.cend(),
                    std::back_inserter( openedFiles ),
                    []( const auto& path ) { return DisplayFilePath{ path }; } );

    QStringList filesToShow;
    std::transform( openedFiles.cbegin(), openedFiles.cend(), std::back_inserter( filesToShow ),
                    []( const auto& f ) { return f.nativeFullPath(); } );

    auto selectFileDialog = std::make_unique<QDialog>( this );
    selectFileDialog->setWindowTitle( tr( "logsquirl -- switch to file" ) );
    selectFileDialog->setMinimumWidth( 800 );
    selectFileDialog->setMinimumHeight( 600 );

    auto filesModel = std::make_unique<QStringListModel>( filesToShow, selectFileDialog.get() );
    auto filteredModel = std::make_unique<QSortFilterProxyModel>( selectFileDialog.get() );
    filteredModel->setSourceModel( filesModel.get() );

    auto filesView = std::make_unique<QListView>();
    filesView->setModel( filteredModel.get() );
    filesView->setEditTriggers( QAbstractItemView::NoEditTriggers );
    filesView->setSelectionMode( QAbstractItemView::SingleSelection );

    auto filterEdit = std::make_unique<QLineEdit>();
    auto buttonBox
        = std::make_unique<QDialogButtonBox>( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );

    connect( buttonBox.get(), &QDialogButtonBox::accepted, selectFileDialog.get(),
             &QDialog::accept );
    connect( buttonBox.get(), &QDialogButtonBox::rejected, selectFileDialog.get(),
             &QDialog::reject );

    connect( filterEdit.get(), &QLineEdit::textEdited,
             [ model = filteredModel.get(), view = filesView.get() ]( const QString& filter ) {
                 model->setFilterWildcard( filter );
                 model->invalidate();
                 view->selectionModel()->select( model->index( 0, 0 ),
                                                 QItemSelectionModel::SelectCurrent );
             } );

    dispatchToMainThread( [ edit = filterEdit.get() ]() { edit->setFocus(); } );

    connect( selectFileDialog.get(), &QDialog::finished,
             [ this, openedFiles, dialog = selectFileDialog.get(), model = filteredModel.get(),
               view = filesView.get() ]( auto result ) {
                 dialog->deleteLater();
                 if ( result != QDialog::Accepted || !view->selectionModel()->hasSelection() ) {
                     return;
                 }
                 const auto& selectedPath
                     = model->data( view->selectionModel()->selectedIndexes().front() ).toString();
                 const auto selectedFile
                     = std::find_if( openedFiles.begin(), openedFiles.end(),
                                     [ selectedPath ]( const DisplayFilePath& f ) {
                                         return f.nativeFullPath() == selectedPath;
                                     } );

                 if ( selectedFile != openedFiles.end() ) {
                     loadFile( selectedFile->fullPath() );
                 }
             } );

    auto layout = std::make_unique<QVBoxLayout>();
    layout->addWidget( filesView.release() );
    layout->addWidget( filterEdit.release() );
    layout->addWidget( buttonBox.release() );

    selectFileDialog->setLayout( layout.release() );
    selectFileDialog->setModal( true );
    selectFileDialog->open();

    // Ownership passes to the Qt parent chain; the raw pointers are not needed.
    std::ignore = filesModel.release();
    std::ignore = filteredModel.release();
    std::ignore = selectFileDialog.release();
}

void MainWindow::showInfoLabels( bool show )
{
    for ( auto separator : infoToolbarSeparators ) {
        separator->setVisible( show );
    }
    if ( !show ) {
        sizeField->clear();
        dateField->clear();
        encodingField->clear();
        lineNbField->clear();
    }
}

std::vector<SaveFileInfo> MainWindow::tabViewStates() const
{
    // Generate the ordered list of widgets and their view state
    std::vector<SaveFileInfo> widget_list;
    for ( const auto i : mainTabWidget_.logFileTabs() ) {
        const auto* view = qobject_cast<const CrawlerWidget*>( mainTabWidget_.widget( i ) );
        widget_list.emplace_back( view, view->context() );
    }
    return widget_list;
}

// Write settings to permanent storage
void MainWindow::writeSettings()
{
    // Save the session
    if ( sidebarWidthApplied_ && sidebarDock_->isVisible() && !sidebarDock_->isFloating() ) {
        sidebarWidth_ = sidebarDock_->width();
    }
    session_.save( tabViewStates(), currentCrawlerWidget(), saveGeometry(), sidebarWidth_ );
}

void MainWindow::saveSessionAs()
{
    // Beside the Log File in front, where a folder of logs keeps its session.
    QString proposed = QDir::home().filePath( QStringLiteral( "session" ) );
    if ( const auto* current = currentCrawlerWidget() ) {
        // A decompressed Log File is read from a temporary file: beside its
        // archive instead.
        const auto fileName = session_.getFilename( current );
        const auto member = archiveMembers_.value( fileName );
        const auto shown = member.isEmpty() ? fileName : member.archive;
        proposed = QFileInfo( shown ).dir().filePath( QStringLiteral( "session" ) );
    }
    proposed += QStringLiteral( "." ) + SessionFileExtension;

    auto path = QFileDialog::getSaveFileName(
        this, tr( "Save Session As" ), proposed,
        tr( "LogSquirl sessions (*.%1)" ).arg( SessionFileExtension ) );
    if ( path.isEmpty() ) {
        return;
    }
    if ( QFileInfo( path ).suffix() != QLatin1String( SessionFileExtension ) ) {
        path += QStringLiteral( "." ) + SessionFileExtension;
    }
    saveSessionFile( path );
}

bool MainWindow::saveSessionFile( const QString& path )
{
    // The same snapshot the automatic Session saves, and the tab names and
    // groups of its Log Files.
    auto window = session_.snapshot( tabViewStates(), currentCrawlerWidget() );
    takeTabLabels( window );

    const auto text = writeSessionFile( window, QFileInfo( path ).absoluteDir() );
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) || file.write( text ) != text.size()
         || !file.commit() ) {
        LOG_ERROR << "Cannot write the session file " << path << ": " << file.errorString();
        QMessageBox::critical( this, tr( "Save Session As" ),
                               tr( "The session could not be saved to %1:\n%2" )
                                   .arg( QDir::toNativeSeparators( path ), file.errorString() ) );
        return false;
    }
    return true;
}

void MainWindow::openSession()
{
    const auto path = QFileDialog::getOpenFileName(
        this, tr( "Open Session" ), QDir::homePath(),
        tr( "LogSquirl sessions (*.%1)" ).arg( SessionFileExtension ) );
    if ( path.isEmpty() ) {
        return;
    }
    openSessionFile( path );
}

void MainWindow::openSessionFile( const QString& path )
{
    const auto title = tr( "Open Session" );

    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        QMessageBox::critical( this, title,
                               tr( "The session file %1 could not be read:\n%2" )
                                   .arg( QDir::toNativeSeparators( path ), file.errorString() ) );
        return;
    }

    auto read = readSessionFile( file.readAll(), QFileInfo( path ).absoluteDir() );
    if ( !read ) {
        QMessageBox::critical( this, title, sessionFileErrorText( read.error() ) );
        return;
    }

    // A Log File is open once in the application: where it is open already,
    // it stays.
    read->leaveOut(
        [ this ]( const SessionInfo::OpenFile& openFile ) { return session_.isOpen( openFile ); } );

    if ( read->window.files.empty() ) {
        // No window is left empty: the notice alone.
        QMessageBox::information(
            this, title,
            read->leftOut.isEmpty()
                ? tr( "The session holds no log files." )
                : tr( "None of the log files of this session could be opened. They are missing "
                      "or already open:\n\n%1" )
                      .arg( read->leftOut.join( QLatin1Char( '\n' ) ) ) );
        return;
    }

    Q_EMIT sessionFileOpened( *read );
}

void MainWindow::restoreSessionFile( const SessionFileRead& read )
{
    // Named and grouped as they were, but only the Log Files that open: those
    // that did now, and those from an archive as each opens.
    const auto tabsAdded = restoreWindow( read.window );
    applyTabLabels( read.window, [ &tabsAdded ]( const SessionInfo::OpenFile& file ) {
        return file.archiveMember.isEmpty()
               && std::ranges::find( tabsAdded, file.fileName ) != tabsAdded.end();
    } );
    pendingTabLabels_ = read.window;
    mainTabWidget_.refreshAllTabGroupAppearances();

    if ( !read.leftOut.isEmpty() ) {
        // Once the window is shown.
        QTimer::singleShot( 0, this, [ this, leftOut = read.leftOut ] {
            QMessageBox::information(
                this, tr( "Open Session" ),
                tr( "These log files of the session were left out. They are missing or "
                    "already open in another window:\n\n%1" )
                    .arg( leftOut.join( QLatin1Char( '\n' ) ) ) );
        } );
    }
}

// Read settings from permanent storage
void MainWindow::readSettings()
{
    // Get and restore the session
    // auto& session = SessionInfo::getSynced();
    /*
     * FIXME: should be in the session
    crawlerWidget->restoreState( session.crawlerState() );
    */

    // History of recent files
    RecentFiles::getSynced();
    updateRecentFileActions();

    FavoriteFiles::getSynced();
    updateFavoritesMenu();

    HighlighterSetCollection::getSynced();
    updateHighlightersMenu();
}

void MainWindow::displayQuickFindBar( QuickFindMux::QFDirection direction )
{
    LOG_DEBUG << "MainWindow::displayQuickFindBar";

    // Warn crawlers so they can save the position of the focus in order
    // to do incremental search in the right view.
    Q_EMIT enteringQuickFind();

    const auto crawler = currentCrawlerWidget();
    if ( crawler != nullptr && crawler->isPartialSelection() ) {
        auto selection = crawler->getSelectedText();
        if ( !selection.isEmpty() ) {
            quickFindWidget_.changeDisplayedPattern( selection, false );
        }
    }

    quickFindMux_.setDirection( direction );
    quickFindWidget_.userActivate();
}

void MainWindow::logScreenInfo( QScreen* screen )
{
    LOG_INFO << "screen changed for " << session_.windowIndex();
    if ( screen == nullptr ) {
        return;
    }

    LOG_INFO << "screen name " << screen->name();
    LOG_INFO << "screen size " << screen->size().width() << "x" << screen->size().height();
    LOG_INFO << "screen ratio " << screen->devicePixelRatio();
    LOG_INFO << "screen logical dpi " << screen->logicalDotsPerInch();
    LOG_INFO << "screen physical dpi " << screen->physicalDotsPerInch();
}

void MainWindow::generateDump()
{
    const auto userAction = QMessageBox::warning(
        this, tr( "logsquirl - generate crash dump" ),
        tr( "This will shutdown logsquirl and generate diagnostic crash dump. Continue?" ),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

    if ( userAction == QMessageBox::Yes ) {
        throw std::logic_error( "test dump" );
    }
}
