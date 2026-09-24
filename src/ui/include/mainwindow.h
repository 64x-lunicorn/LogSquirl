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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMenu>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTemporaryDir>

#include <QDockWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QTranslator>
#include <array>
#include <memory>
#include <mutex>

#include "applicationplugins.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "downloader.h"
#include "filterspanel.h"
#include "iconloader.h"
#include "mergecontroller.h"
#include "pathline.h"
#include "pluginuiadapter.h"
#include "quickfindmux.h"
#include "quickfindwidget.h"
#include "session.h"
#include "signalmux.h"
#include "tabbedcrawlerwidget.h"
#include "tabbedscratchpad.h"
#include "tabgroupmanagerdialog.h"
#include "welcomedashboard.h"

class CommandPalette;
class QAction;
class QActionGroup;
class Session;
class RecentFiles;
class HighlightersMenu;

// Main window of the application, creates menus, toolbar and
// the CrawlerWidget
class MainWindow : public QMainWindow, public SessionWindow {
    Q_OBJECT

public:
    // Every window of the application uses the same Application Plugins
    // (#303); the first window shown has them loaded.
    MainWindow( WindowSession session,
                std::shared_ptr<logsquirl::plugins::ApplicationPlugins> plugins );
    ~MainWindow() override;

    MainWindow( const MainWindow& ) = delete;
    MainWindow& operator=( const MainWindow& ) = delete;

    // Re-install the geometry stored in config file
    // (should be done before 'Widget::show()')
    void reloadGeometry();
    // Re-load the files from the previous session
    void reloadSession();
    // Loads the initial file (parameter passed or from config file)
    void loadInitialFile( QString fileName, bool followFile );

    void reTranslateUI();

    static int installLanguage( QString lang );

    // The Session tells every window of a settings change, whichever window
    // it was made in, once it has re-derived the Policies (#245): the window
    // takes its QuickFind Policy, its shortcuts and its chrome again.
    void applySettingsChange() override;

public Q_SLOTS:
    // Load a file in a new tab (non-interactive)
    // (for use from e.g. IPC)
    void loadFileNonInteractive( const QString& file_name );

protected:
    void closeEvent( QCloseEvent* event ) override;
    void changeEvent( QEvent* event ) override;

    // Drag and drop support
    void dragEnterEvent( QDragEnterEvent* event ) override;
    void dropEvent( QDropEvent* event ) override;

    bool event( QEvent* event ) override;
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    enum class ActionInitiator { User, App };

    // Hand the QuickFind bar and the mux the QuickFind Policy this window's
    // session holds now. Neither of them reads a setting of its own, and
    // neither belongs to a Log File, so the Policy is the same whichever tab
    // or Filtered View is in front: a tab switch leaves it alone. The window
    // takes it once when it is built and again on every settings change.
    void applyQuickFindPolicy();

private Q_SLOTS:
    void open();
    void openFileFromRecent( QAction* action );
    void openFileFromFavorites( QAction* action );
    void switchToOpenedFile( QAction* action );
    void closeTab( ActionInitiator initiator );
    void closeAll( ActionInitiator initiator );
    void selectAll();
    void copy();
    void find();
    void clearLog();
    void copyFullPath();
    void openContainingFolder();
    void openInEditor();
    void openClipboard();
    void openUrl();
    void editHighlighters();
    void editPredefinedFilters( const QString& newFilter = {} );
    void options();
    void about();
    void aboutQt();
    void documentation();
    void showScratchPad();
    void showFiltersPanel();
    void clearIndexCache();
    void manageTabGroups();
    void showCommandPalette();
    void openMergedFiles( QStringList filePaths, bool dedup );
    void toggleSidebar();
    void showSidebar( int tabIndex );
    void importChipmunkFilters();
    void sendToScratchpad( QString );
    void replaceDataInScratchpad( QString );
    void showPluginDialog();
    void startPluginDataSource( const QString& pluginId );
    void handleDataSourceStarted( const QString& pluginId, const QString& displayName,
                                  const QString& filePath );
    // List the data source plugins the catalog knows in the Sources menu.
    void updateSourcesMenu();
    // Make this window the one plugins open files in and ask for the active file.
    void servePluginCallbacks();
    void encodingChanged( QAction* action );
    void addToFavorites();
    void removeFromFavorites();
    void selectOpenedFile();
    void generateDump();

    // Change the view settings
    void toggleOverviewVisibility( bool isVisible );
    void toggleMainLineNumbersVisibility( bool isVisible );
    void toggleFilteredLineNumbersVisibility( bool isVisible );

    // Change the follow mode checkbox and send the followSet signal down
    void changeFollowMode( bool follow );

    // Update the selection information displayed in the status bar.
    // Must be passed as the internal (starts at 0) line number.
    void lineNumberHandler( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                            LineLength nSymbols );

    // Save current search in line edit as predefined filter.
    // Opens dialog with new entry.
    void newPredefinedFilterHandler( QString newFilter );

    // Instructs the widget to update the loading progress gauge
    void updateLoadingProgress( int progress );
    // Instructs the widget to display the 'normal' status bar,
    // without the progress gauge and with file info
    // or an error recovery when loading is finished
    void handleLoadingFinished( LoadingStatus status );

    // Update quick find searchable
    void handleFilteredViewChanged();

    // Close the tab with the passed index
    void closeTab( int index, ActionInitiator initiator );
    // Close multiple tabs at once with a single confirmation dialog
    void closeTabs( QList<int> indices );
    // Setup the tab with current index for view
    void currentTabChanged( int index );

    // Instructs the widget to change the pattern in the QuickFind widget
    // and confirm it.
    void changeQFPattern( const QString& newPattern );

Q_SIGNALS:
    // Is emitted when the 'follow' option is enabled/disabled
    void followSet( bool checked );
    // Is emitted when the 'text wrap' option is enabled/disabled
    void textWrapSet( bool checked );
    // Is emitted before the QuickFind box is activated,
    // to allow crawlers to get search in the right view.
    void enteringQuickFind();
    // Emitted when the quickfind bar is closed.
    void exitingQuickFind();

    void newWindow();
    void windowActivated();
    void windowClosed();
    void exitRequested();

private:
    void createActions();
    void loadIcons();
    void createMenus();
    void createToolBars();
    void createTrayIcon();
    void readSettings();
    void writeSettings();
    bool loadFile( const QString& fileName, bool followFile = false );
    bool extractAndLoadFile( const QString& fileName );
    void openRemoteFile( const QUrl& url );
    void updateTitleBar( const QString& fileName );
    void addRecentFile( const QString& fileName );
    void updateRecentFileActions();
    void clearRecentFileActions();
    void updateFavoritesMenu();
    void updateOpenedFilesMenu();
    void updateHighlightersMenu();
    QString strippedName( const QString& fullFileName ) const;
    CrawlerWidget* currentCrawlerWidget() const;
    void displayQuickFindBar( QuickFindMux::QFDirection direction );
    void updateMenuBarFromDocument( const CrawlerWidget* crawler );
    void updateGoToTimestampAction( const CrawlerWidget* crawler );
    void updateInfoLine();
    void showInfoLabels( bool show );
    void logScreenInfo( QScreen* screen );
    void removeFromFavorites( const QString& pathToRemove );
    void removeFromRecent( const QString& pathToRemove );
    void tryOpenClipboard( int tryTimes );
    void updateShortcuts();
    void showDashboardOrTabs();

    /// Build the full list of commands for the command palette by
    /// collecting menu actions, plugin actions, recent files, and
    /// favorites.
    std::vector<struct CommandEntry> collectCommands();

    WindowSession session_;
    QString loadingFileName;
    // While the Session's tabs are added: each becomes current in turn, and
    // none of them is to start loading for that (#300).
    bool restoringSession_ = false;

    std::array<QAction*, MAX_RECENT_FILES> recentFileActions;
    QActionGroup* recentFilesGroup;

    QMenu* fileMenu;
    QMenu* recentFilesMenu;
    QMenu* editMenu;
    QMenu* viewMenu;
    QMenu* toolsMenu;
    QMenu* favoritesMenu;
    HighlightersMenu* highlightersMenu;
    QMenu* openedFilesMenu;
    QMenu* pluginsMenu;
    QMenu* sourcesMenu;
    QMenu* helpMenu;

    PathLine* infoLine;
    QLabel* lineNbField;
    QLabel* sizeField;
    QLabel* dateField;
    QLabel* encodingField;
    std::vector<QAction*> infoToolbarSeparators;

    QToolBar* toolBar;

    QAction* newWindowAction;
    QAction* openAction;
    QAction* closeAction;
    QAction* closeAllAction;
    QAction* exitAction;
    QAction* copyAction;
    QAction* selectAllAction;
    QAction* goToLineAction;
    QAction* goToTimestampAction;
    QAction* findAction;
    QAction* clearLogAction;
    QAction* copyPathToClipboardAction;
    QAction* openContainingFolderAction;
    QAction* openInEditorAction;
    QAction* openClipboardAction;
    QAction* openUrlAction;
    QAction* overviewVisibleAction;
    QAction* lineNumbersVisibleInMainAction;
    QAction* lineNumbersVisibleInFilteredAction;
    QAction* followAction;
    QAction* textWrapAction;
    QAction* reloadAction;
    QAction* stopAction;
    QAction* editHighlightersAction;
    QAction* optionsAction;
    QAction* showScratchPadAction;
    QAction* showFiltersPanelAction;
    QAction* toggleSidebarAction;
    QAction* toggleChartPanelAction;
    QAction* showFilterFrequencyAction;
    QAction* importChipmunkFiltersAction;
    QAction* showDocumentationAction;
    QAction* aboutAction;
    QAction* aboutQtAction;
    QAction* predefinedFiltersDialogAction;
    QAction* manageTabGroupsAction;
    QAction* reportIssueAction;
    QAction* generateDumpAction;
    QAction* pluginsAction;
    QActionGroup* encodingGroup;
    QAction* addToFavoritesAction;
    QAction* addToFavoritesMenuAction;
    QAction* removeFromFavoritesAction;
    QAction* selectOpenFileAction;
    QAction* recentFilesCleanup;
    QActionGroup* favoritesGroup;
    QActionGroup* openedFilesGroup;
    QActionGroup* highlightersActionGroup = nullptr;

    std::map<QString, QShortcut*> shortcuts_;

    QSystemTrayIcon* trayIcon_;

    QIcon mainIcon_;

    IconLoader iconLoader_;

    // Multiplex signals to any of the CrawlerWidgets
    SignalMux signalMux_;

    static QTranslator mTranslator;
    static QTranslator mQtTranslator;

    // QuickFind widget
    QuickFindWidget quickFindWidget_;

    // Multiplex signals to/from the QuickFindWidget
    QuickFindMux quickFindMux_;

    // The main widget
    TabbedCrawlerWidget mainTabWidget_;

    // Welcome dashboard shown as the permanent first tab
    WelcomeDashboard* welcomeDashboard_ = nullptr;

    TabbedScratchPad scratchPad_;

    FiltersPanel filtersPanel_;

    // Right sidebar dock with tabbed panels
    QDockWidget* sidebarDock_{ nullptr };
    QToolButton* sidebarFloatButton_{ nullptr };
    QToolButton* sidebarCloseButton_{ nullptr };
    QTabWidget* sidebarTabs_{ nullptr };
    static constexpr int SidebarFiltersPanelTab = 0;
    static constexpr int SidebarScratchPadTab = 1;
    // The share of the window the sidebar opens at while no width was saved.
    static constexpr int SidebarDefaultWidthPercent = 27;
    // The width the sidebar opens at, or was last left at while docked; 0
    // while none was saved.
    int sidebarWidth_ = 0;
    // Whether the sidebar was docked open since the window was built.
    bool sidebarWidthApplied_ = false;

    QTemporaryDir tempDir_;

    bool isMaximized_ = false;
    bool isCloseFromTray_ = false;

    // Shown, and waiting for the window system to expose the window before
    // asking for the plugins to load (#303).
    bool waitingForExposure_ = false;

    std::once_flag screenChangesConnect_;

    // The application's one Plugin Catalog and Plugin Host, shared by every
    // window and loaded once, after the first window shows (#303).
    std::shared_ptr<logsquirl::plugins::ApplicationPlugins> plugins_;

    // Shows what plugins contribute, when this window is the one the Plugin
    // Host shows them in: the first window built.
    std::unique_ptr<PluginUiAdapter> pluginUi_;

    // Separator between plugin actions (top) and management actions (bottom).
    QAction* pluginMenuSeparator_ = nullptr;

    // Active merge controllers (one per merged tab).
    std::vector<std::unique_ptr<MergeController>> mergeControllers_;

    // Command palette (Ctrl+Shift+P / Cmd+Shift+P)
    CommandPalette* commandPalette_ = nullptr;
};

#endif
