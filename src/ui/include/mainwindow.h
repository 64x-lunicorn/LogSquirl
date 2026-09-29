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

#include <QHash>
#include <QMainWindow>
#include <QMenu>
#include <QPair>
#include <QPointer>
#include <QProgressDialog>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTemporaryDir>

#include <QDockWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QTranslator>
#include <array>
#include <map>
#include <memory>
#include <mutex>

#include "applicationplugins.h"
#include "commandsource.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "downloader.h"
#include "filterspanel.h"
#include "iconloader.h"
#include "pathline.h"
#include "pluginuiadapter.h"
#include "quickfindmux.h"
#include "quickfindwidget.h"
#include "session.h"
#include "sessionfile.h"
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
namespace logsquirl::teamfolder {
struct PublishOutcome;
}

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

    // Writes this window's Session to the Session File at `path` (#576):
    // its Log Files in tab order with their view states, the tab in front,
    // and their tab names and groups. Says why when it cannot be written, and
    // returns whether it was.
    bool saveSessionFile( const QString& path );
    // Reads the Session File at `path` and asks for a new window to open it
    // in with sessionFileOpened(). A Log File that is missing, or open in any
    // window already, is left out, and the new window names it. When the file
    // cannot be read, or none of its Log Files can be opened, it says so here
    // and no window is asked for.
    void openSessionFile( const QString& path );
    // Opens what openSessionFile() read in this window, a new one, as a
    // restored Session opens: the tab in front loads first, a Log File from
    // an archive is decompressed again. Its tab names and groups are merged
    // into the stored ones first.
    void restoreSessionFile( const SessionFileRead& read );
    // Loads the initial file (parameter passed or from config file)
    void loadInitialFile( QString fileName, bool followFile );

    // Opens what arrives on standard input as a Log File that is followed. The
    // window keeps reading until the writing end closes or it is destroyed.
    // A window reads it once.
    void openStandardInput();

    // Opens, in a followed `stdin` tab, the spool file a secondary instance
    // writes what arrives on its standard input to, and brings the window to
    // the front (#623). The tab owns the file and removes it as it closes --
    // when the file is a spool of standard input in the temporary folder;
    // any other file is only opened. Each hand-over is a tab of its own.
    void openHandedOverStandardInput( const QString& spoolPath );

    // Runs the command line through the user's shell and opens its output as
    // a Transient Log File that is followed, titled by the command line
    // (#575). A working folder that does not exist, a spool file that cannot
    // be created or a shell that does not start is reported, and no tab
    // opens; returns whether one does. Closing the tab stops the command.
    bool openCommandOutput( const RecentCommand& command );

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
    // Asks for a command to run for its output (#575).
    void openCommandOutputDialog();
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

    // Mirrors in the follow action whether the current Log File is followed,
    // as its View Set, the owner of follow, holds it (#558).
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
    // or an error recovery when loading is finished: a failed load closes
    // its tab, and a Failed one is offered to be reported.
    void handleLoadingFinished( LoadingStatus status, const QString& failure );

    // Update quick find searchable
    void handleFilteredViewChanged();
    void showStatusMessage( QString message );

    // Close the tab with the passed index
    void closeTab( int index, ActionInitiator initiator );
    // The one path that closes tabs, one or many, whoever asks (#536). The
    // tabs that hold no Log File are left alone.
    void closeTabs( const QList<int>& indices, ActionInitiator initiator );
    // Setup the tab with current index for view
    void currentTabChanged( int index );

    // Instructs the widget to change the pattern in the QuickFind widget
    // and confirm it.
    void changeQFPattern( const QString& newPattern );

Q_SIGNALS:
    // The user turned the follow action on or off: asks the current Log
    // File's View Set, which says back what it holds.
    void followSet( bool checked );
    // Is emitted when the 'text wrap' option is enabled/disabled
    void textWrapSet( bool checked );
    // Is emitted before the QuickFind box is activated,
    // to allow crawlers to get search in the right view.
    void enteringQuickFind();
    // Emitted when the quickfind bar is closed.
    void exitingQuickFind();

    void newWindow();
    // A Session File was read, to be opened in a new window (#576).
    void sessionFileOpened( const SessionFileRead& read );
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
    // Opens a Log File in a new tab. A Transient Log File -- one the window
    // made for this run alone -- is not saved with the Session (#570), nor
    // added to the recent files (#597). A Log File a converter plugin
    // handles is opened as what the converter wrote, a Transient Log File
    // (#605).
    bool loadFile( const QString& fileName, bool followFile = false,
                   LogFileLifetime lifetime = LogFileLifetime::Ordinary );
    bool extractAndLoadFile( const QString& fileName );
    // Opens the Log Files of `window` as a restore does, and reloadSession()
    // does with the window stored in the Session (#576).
    void restoreWindow( const WindowSnapshot& window );
    // The views of the window's Log File tabs in tab order, each with its
    // view state, as the Session saves them.
    std::vector<SaveFileInfo> tabViewStates() const;
    // The File menu's "Save Session As..." and "Open Session..." (#576).
    void saveSessionAs();
    void openSession();
    // Adds the tab of a restored Log File whose archive decompressed after
    // the restore, `fileName`, where it stood among the tabs (#610).
    void openRestoredFromArchive( int deferredId, const ArchiveMember& member,
                                  const QString& fileName );
    // Shows the progress of the archive decompressing for a restore, or
    // closes it once none is.
    void showArchiveRestoreProgress( const QString& archive );
    void closeArchiveRestoreProgress();
    // Raises the window and gives it the focus, as a hand-over from another
    // instance does.
    void bringToFront();
    // The view of this Log File open in any window, or of the Log File a
    // converter plugin converted it into (#615); nullptr while neither is.
    const ViewInterface* openViewOf( const QString& fileName ) const;
    void openRemoteFile( const QUrl& url );
    // Opens the spool file of a Command Source in a followed tab and keeps
    // the source with it until the tab closes; the title and tooltip are the
    // tab's. Returns whether the tab opens.
    bool openCommandSource( std::unique_ptr<CommandSource> source, const QString& title,
                            const QString& toolTip );
    // The tooltip of a command's tab: its whole command line, its working
    // folder and its spool file.
    static QString commandToolTip( const CommandSource& source );
    // Shows how the Command Source of the tab of `spoolPath` ended.
    void showCommandSourceEnded( const QString& spoolPath, const CommandEnd& end );
    void updateTitleBar( const QString& fileName );
    // The file the recent files keep for a Log File open with this lifetime:
    // the Log File itself, the archive a decompressed Log File came from
    // (#609), the one a converted Log File was converted from (#605), or none
    // for any other Transient Log File (#597).
    QString recentFileOf( const QString& fileName, LogFileLifetime lifetime ) const;
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
    // Shows the Team Folder's Team groups and state in this window.
    void connectTeamFolder();
    void updateTeamFolderIndicator();
    // Hands the Team Highlighter Sets to the Highlighter Set collection.
    // dropUnknownActivations false: the first sync has not delivered groups.
    void applyTeamHighlighterSets( bool dropUnknownActivations = true );
    // Asks what to do with a Team group somebody else changed while the user
    // was changing it too: keep mine, take theirs, or save mine as a copy.
    void askAboutPublishConflicts( const logsquirl::teamfolder::PublishOutcome& outcome );
    // Asks about the conflicts waiting, when this window can: it has the focus
    // and no dialog is open on it. Else it looks again shortly.
    void askAboutPendingConflicts();

    /// Build the full list of commands for the command palette by
    /// collecting menu actions, plugin actions, recent files, and
    /// favorites.
    std::vector<struct CommandEntry> collectCommands();

    WindowSession session_;
    QString loadingFileName;
    // While the Session's tabs are added: each becomes current in turn, and
    // none of them is to start loading for that (#300).
    bool restoringSession_ = false;
    // While the tab brought to the front replays the state of its Log File:
    // a load under way is shown whatever its progress (#540).
    bool replayingFrontTab_ = false;

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
    // The Team Folder's state, quietly: shown only while there is a Team
    // Folder, it says synced, not synced or error, tells Git's message in its
    // tooltip and syncs when clicked. It never opens a dialog.
    QToolButton* teamFolderButton_ = nullptr;
    QAction* teamFolderButtonAction_ = nullptr;
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
    QAction* searchLimitsTimeRangeAction;
    QAction* searchLimitsAroundLineAction;
    QAction* findAction;
    QAction* clearLogAction;
    QAction* copyPathToClipboardAction;
    QAction* openContainingFolderAction;
    QAction* openInEditorAction;
    QAction* openClipboardAction;
    QAction* openUrlAction;
    QAction* openSessionAction;
    QAction* saveSessionAsAction;
    QAction* openCommandOutputAction;
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
    QAction* commandPaletteAction;
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
    // Where each Log File decompressed into tempDir_ came from, by the path
    // it is read from: the Session saves that instead (#596), and the recent
    // files, tab names and tab groups know it by that (#609).
    QHash<QString, ArchiveMember> archiveMembers_;
    // The Ordinary Log File each Log File a converter plugin wrote into
    // tempDir_ was converted from, by the path it is read from: the recent
    // files keep that instead (#605), and opening it again shows that tab
    // (#615). Dropped when the tab closes.
    QHash<QString, QString> convertedFrom_;
    // Decompresses the archives of a restored Session into tempDir_ while the
    // window is in use; declared after it, so that it is gone, and has waited
    // for the decompression under way, before tempDir_ is removed (#610).
    ArchiveMemberDecompression archiveRestores_;
    QPointer<QProgressDialog> archiveRestoreProgress_;

    bool isMaximized_ = false;
    bool isCloseFromTray_ = false;

    // Shown, and waiting for the window system to expose the window before
    // asking for the plugins to load (#303).
    bool waitingForExposure_ = false;

    std::once_flag screenChangesConnect_;

    // The application's one Plugin Catalog and Plugin Host, shared by every
    // window and loaded once, after the first window shows (#303).
    std::shared_ptr<logsquirl::plugins::ApplicationPlugins> plugins_;

    // The Command Source of each tab that has one -- standard input, a
    // command's output -- by the tab's path, its spool file (#575). It goes
    // with the tab.
    std::map<QString, std::unique_ptr<CommandSource>> commandSources_;

    // Shows what plugins contribute, when this window is the one the Plugin
    // Host shows them in: the first window built.
    std::unique_ptr<PluginUiAdapter> pluginUi_;

    // Separator between plugin actions (top) and management actions (bottom).
    QAction* pluginMenuSeparator_ = nullptr;

    // Command palette (Ctrl+Shift+P / Cmd+Shift+P)
    CommandPalette* commandPalette_ = nullptr;
};

#endif
