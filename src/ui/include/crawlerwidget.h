/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014, 2015 Nicolas Bonnefon
 * and other contributors
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

#ifndef CRAWLERWIDGET_H
#define CRAWLERWIDGET_H

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "chartpanel.h"
#include "colorlabelsmanager.h"
#include "filteredview.h"
#include "iconloader.h"
#include "linetypes.h"
#include "loadingstatus.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logmainview.h"
#include "logpresentation.h"
#include "overview.h"
#include "predefinedfilters.h"
#include "signalmux.h"
#include "viewinterface.h"
#include "viewset.h"

#include "logformatdefinition.h"
#include "settingspolicies.h"

class LogFormatCatalog;
class LogTableView;
class InfoLine;
class QuickFindPattern;
class SavedSearches;
class QStandardItemModel;
class QCompleter;
class OverviewWidget;

// Implements the central widget of the application.
// It includes both windows, the search line, the info
// lines and various buttons.
class CrawlerWidget : public QSplitter,
                      public QuickFindMuxSelectorInterface,
                      public ViewInterface,
                      public MuxableDocumentInterface {
    Q_OBJECT

public:
    CrawlerWidget( QWidget* parent = nullptr );

    // Get the line number of the first line displayed.
    LineNumber getTopLine() const;

    // Get the selected text as a string (from the main window)
    QString getSelectedText() const;
    // True for partial selection
    bool isPartialSelection() const;

    // Display the QFB at the bottom, remembering where the focus was
    void displayQuickFindBar( QuickFindMux::QFDirection direction );

    // Instructs the widget to select all the text in the window the user
    // is interacting with
    void selectAll();

    std::optional<int> encodingMib() const;

    // Get the text description of the encoding effectively used,
    // suitable to display to the user.
    QString encodingText() const;

    // Returns whether follow is enabled in this crawler
    bool isFollowEnabled() const;

    bool isTextWrapEnabled() const;

    // The Policies this Log File's views show and search under, as last
    // handed down by the Session. They are held -- the Watch Policy here,
    // the others by the View Set -- so that a widget can be given what it
    // needs instead of reaching for the settings itself: every view is
    // handed what it needs from these (#184, #242), and this widget's own
    // settings follow (#185).
    const DecorationPolicy& decorationPolicy() const;
    const PresentationPolicy& presentationPolicy() const;
    const QuickFindPolicy& quickFindPolicy() const;
    const WatchPolicy& watchPolicy() const;

    void registerShortcuts();

public Q_SLOTS:
    // Stop the asynchoronous loading of the file if one is in progress
    // The file is identified by the view attached to it.
    void stopLoading();
    // Reload the displayed file
    void reload();
    // Set the encoding
    void setEncoding( std::optional<int> mib );

    void focusSearchEdit();
    void goToLine();

    // Instructs the widget to reconfigure itself because Config() has changed.
    void applyConfiguration();

    // Paints every view of this Log File again with the Highlighter Sets now
    // active. A Highlighter Set is user data, not a setting, so nothing is
    // read from the Configuration.
    void applyHighlighterSetChange();

    // Makes every view of this Log File read the Log Lines it shows again,
    // after the Log File's Decoding Policy was replaced.
    void applyDecodingPolicyChange();

public:
    template <class T>
    struct access_by;

protected:
    // Implementation of the ViewInterface functions
    void doSetData( std::shared_ptr<LogData> logData,
                    std::shared_ptr<LogFilteredData> filteredData ) override;
    void doSetQuickFindPattern( std::shared_ptr<QuickFindPattern> qfp ) override;
    void doSetSavedSearches( SavedSearches* savedSearches ) override;
    void doSetFormatRecognition( const RecognitionPolicy& policy,
                                 std::shared_ptr<const LogFormatCatalog> catalog ) override;
    void doSetRecognitionPolicy( const RecognitionPolicy& policy ) override;
    void doSetDecorationPolicy( const DecorationPolicy& policy ) override;
    void doSetPresentationPolicy( const PresentationPolicy& policy ) override;
    void doSetQuickFindPolicy( const QuickFindPolicy& policy ) override;
    void doSetWatchPolicy( const WatchPolicy& policy ) override;
    void doSetFileAccessPolicy( const FileAccessPolicy& policy ) override;
    void doApplyHighlighterSetChange() override;
    void doSetViewContext( const QString& viewContext ) override;
    std::shared_ptr<const ViewContextInterface> doGetViewContext( void ) const override;

    // Implementation of the mux selector interface
    // (for dispatching QuickFind to the right widget)
    SearchableWidgetInterface* doGetActiveSearchable() const override;
    std::vector<QObject*> doGetAllSearchables() const override;

    // Implementation of the MuxableDocumentInterface
    void doSendAllStateSignals() override;

Q_SIGNALS:
    // Sent to signal the client load has progressed,
    // passing the completion percentage.
    void loadingProgressed( int progress );
    // Sent to the client when the loading has finished
    // whether successful or not.
    void loadingFinished( LoadingStatus status );
    // Sent when follow mode is enabled/disabled
    void followSet( bool checked );
    // Sent when text wrap mode is enabled/disabled
    void textWrapSet( bool checked );
    // Sent up to the MainWindow to enable/disable the follow mode
    void followModeChanged( bool follow );
    // Sent up when the current line number is updated
    void newSelection( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                       LineLength nSymbols );
    // Sent up when user wants to save new predefined filter from current search
    void saveCurrentSearchAsPredefinedFilter( QString newFilter );

    void sendToScratchpad( QString );
    void replaceDataInScratchpad( QString );

    // "auto-refresh" check has been changed
    void searchRefreshChanged( bool isRefreshing );
    // "ignore case" check has been changed
    void matchCaseChanged( bool matchCase );

    // Sent when the data status (whether new not seen data are
    // available) has changed
    void dataStatusChanged( DataStatus status );

    // Sent up when the current filtered view has been changed
    void filteredViewChanged();

public Q_SLOTS:
    // Apply a list of predefined filters as the current search pattern.
    void setSearchPatternFromPredefinedFilters( const QList<PredefinedFilter>& filters );

    // Start a new search using the current search line content.
    void startNewSearch();

private Q_SLOTS:
    // Stop the currently ongoing search (if one exists)
    void stopSearch();
    void loadIcons();
    // QuickFind is being entered, save the focus for incremental qf.
    void enteringQuickFind();
    // QuickFind is being closed.
    void exitingQuickFind();
    // Called when new data must be displayed in the filtered window.
    void updateFilteredView( SearchSession::State state );
    // Called when a new line has been selected in the filtered view,
    // to instruct the main view to jump to the matching line.
    void jumpToMatchingLine( LineNumber logLine, LinesCount nLines, LineColumn startCol,
                             LineLength nSymbols );
    // Called when the Presentation shown is on a new Log Line; the
    // Presentations not shown follow it.
    void updateLineNumberHandler( LineNumber line, LinesCount nLines, LineColumn startCol,
                                  LineLength nSymbols );
    // Mark Log Lines from a Presentation or a Filtered View.
    void markLinesFromMain( const logsquirl::vector<LineNumber>& lines );

    // failure describes a Failed load, which the user is offered to report.
    void loadingFinishedHandler( LoadingStatus status, const QString& failure );
    // Manages the info lines to inform the user the file has changed. A
    // failure to check the file is offered to be reported.
    void fileChangedHandler( MonitoredFileStatus status, const QString& failure );

    void searchForward();
    void searchBackward();

    // Called when the checkbox for search auto-refresh is changed
    void searchRefreshChangedHandler( bool isRefreshing );

    // Called when the checkbox for case sensitivity is changed
    void matchCaseChangedHandler( bool shouldMatchCase );

    // Called when the checkbox for boolean combining is changed
    void booleanCombiningChangedHandler( bool shouldCombine );

    // Called when the checkbox for using regex is changed
    void useRegexpChangeHandler( bool shouldUseRegex );

    // Called when the text on the search line is modified
    void searchTextChangeHandler( QString );

    // Called when the user change the visibility combobox
    void changeFilteredViewVisibility( int index );

    // Called when the user add the string to the search
    void addToSearch( const QString& string );

    // Called when the user replaces the search with the selection string
    void replaceSearch( const QString& string );

    // Called when the user excludes selection string from search
    // works only in boolean combination mode
    void excludeFromSearch( const QString& string );

    void clearSearchHistory();
    void editSearchHistory();

    // Save current search as predefined filter
    void saveAsPredefinedFilter();

    // Search Context Menu
    void showSearchContextMenu();

    // Called when a match is hovered on in the filtered view, with its Log Line
    void mouseHoveredOverMatch( LineNumber line );

    // Called when there was activity in the views
    void activityDetected();

    void setSearchLimits( LineNumber startLine, LineNumber endLine );
    void clearSearchLimits();

    void addColorLabelToSelection( size_t label );
    void addNextColorLabelToSelection();
    void clearColorLabels();

    // Toggle between text view and table view (if a Log Format was recognized)
    void toggleTableView();

public Q_SLOTS:
    void toggleChartPanel();
    // Create chart series from the current search filter patterns and show
    // them in the chart panel.
    void showFilterFrequency();

private Q_SLOTS:

    void changeFilteredView( int tabIndex );
    void closeFilteredView( int tabIndex );
    void filteredViewDestroyed( QObject* view );

private:
    // State machine holding the state of the search, used to allow/disallow
    // auto-refresh and inform the user via the info line.
    class SearchState {
    public:
        enum State {
            NoSearch,
            Static,
            Autorefreshing,
            FileTruncated,
            TruncatedAutorefreshing,
        };

        SearchState()
        {
            state_ = NoSearch;
            autoRefreshRequested_ = false;
        }

        // Reset the state (no search active)
        void resetState();
        // The user changed auto-refresh request
        void setAutorefresh( bool refresh );
        // The file has been truncated (stops auto-refresh)
        void truncateFile();
        // The expression has been changed (stops auto-refresh)
        void changeExpression();
        // The search has been stopped (stops auto-refresh)
        void stopSearch();
        // The search has been started (enable auto-refresh)
        void startSearch();

        // Get the state in order to display the proper message
        State getState() const
        {
            return state_;
        }
        // Is auto-refresh allowed
        bool isAutorefreshAllowed() const
        {
            return ( state_ == Autorefreshing || state_ == TruncatedAutorefreshing );
        }
        bool isFileTruncated() const
        {
            return ( state_ == FileTruncated || state_ == TruncatedAutorefreshing );
        }

    private:
        State state_;
        bool autoRefreshRequested_;
    };

    // Private functions
    void setup();
    void setShortcuts();
    void replaceCurrentSearch( const QString& searchText );
    void updateSearchCombo();
    AbstractLogView* activeView() const;
    void printSearchInfoMessage( LinesCount nbMatches = 0_lcount );
    void changeDataStatus( DataStatus status );
    void updateEncoding();
    void changeTopViewSize( int32_t delta );

    QString escapeSearchPattern( const QString& searchPattern, bool isRegex = false ) const;
    QString& combinePatterns( QString& currentPattern, const QString& newPattern ) const;
    void setSearchPattern( const QString& searchPattern );

    void resetStateOnSearchPatternChanges();

    void updateColorLabels( const ColorLabelsManager::QuickHighlightersCollection& labels );

    void connectAllFilteredViewSlots( FilteredView* view );

    void saveSplitterSizes() const;

    void changeFontSize( bool increase );

    // The font Log Lines are drawn in, assembled from the settings -- no
    // kerning, fixed pitch, the antialias strategy and bold. This is the one
    // place that font is put together: no view reads it for itself, the View
    // Set hands it to every view.
    static QFont configuredFont();

    // Decide which Log Format applies to the Log File, now that it has loaded.
    // Only ever called from the load-finished path.
    void recognizeFormat();

    // Forget the recognized Log Format, back in the text view.
    void resetLogFormat();

    // Show the Table View in the upper pane, or else the Text View.
    void showPresentation( bool tableView );

    // Both Presentations, the one shown and the one not.
    std::array<LogPresentation*, 2> presentations() const;

    // Connect the signals every Presentation emits to the same slots.
    template <class Presentation>
    void connectPresentation( Presentation* presentation );

    // Palette for error notification (yellow background)
    static const QPalette ErrorPalette;

    IconLoader iconLoader_;

    SavedSearches* savedSearches_ = nullptr;

    std::shared_ptr<LogData> logData_;
    std::shared_ptr<LogFilteredData> logFilteredData_;

    // Matches overview
    Overview overview_;

    std::shared_ptr<QuickFindPattern> quickFindPattern_;

    LogMainView* logMainView_ = nullptr;
    FilteredView* filteredView_ = nullptr;
    std::unordered_map<FilteredView*, std::shared_ptr<LogFilteredData>> filteredViewsData_;
    QTabWidget* tabbedFilteredView_ = nullptr;

    OverviewWidget* overviewWidget_;

    QComboBox* visibilityBox_;
    QStandardItemModel* visibilityModel_;

    QComboBox* searchLineEdit_;
    QMenu* searchLineContextMenu_;
    QCompleter* searchLineCompleter_;

    InfoLine* searchInfoLine_;

    QToolButton* clearButton_;
    QToolButton* searchButton_;
    QToolButton* keepSearchResultsButton_;
    QToolButton* stopButton_;

    QToolButton* matchCaseButton_;
    QToolButton* useRegexpButton_;
    QToolButton* inverseButton_;
    QToolButton* booleanButton_;
    QToolButton* searchRefreshButton_;

    std::map<QString, QShortcut*> shortcuts_;

    // Default palette to be remembered
    QPalette searchInfoLineDefaultPalette_;

    // Reference to the QuickFind Pattern (not owned)

    QWidget* qfSavedFocus_ = nullptr;

    // Search state (for auto-refresh and truncation)
    SearchState searchState_;

    // the current dataStatus (whether we have new, not seen, data)
    DataStatus dataStatus_ = DataStatus::OLD_DATA;

    // Last main line number received
    LineNumber currentLineNumber_;

    // Whether a selection is being passed between the Table View and the
    // Filtered View, so neither passes it back.
    bool syncingSelection_ = false;

    // Current number of matches
    LinesCount nbMatches_;

    LineNumber searchStartLine_;
    LineNumber searchEndLine_;

    // Until we have received confirmation loading is finished, we
    // should consider we are loading something.
    bool loadingInProgress_ = true;
    bool firstLoadDone_ = false;

    logsquirl::vector<LineNumber> savedMarkedLines_;

    // Current encoding setting;
    std::optional<int> encodingMib_;
    QString encodingText_;

    ColorLabelsManager colorLabelsManager_;

    ChartPanel* chartPanel_ = nullptr;

    // What Format Recognition runs on
    RecognitionPolicy recognitionPolicy_;
    std::shared_ptr<const LogFormatCatalog> logFormatCatalog_;

    // Every view of this Log File, and what all of them show alike: the
    // Decoration, Presentation and QuickFind Policies, the follow allowance,
    // the font, the Color Labels and the Search Limits.
    ViewSet viewSet_;

    // Whether this Log File may be followed, and what it was opened under.
    // The File Access Policy is read when the views are built -- the
    // Encoding it names is the one a Log File is read with by default -- so
    // a later one reaches the Log Files opened from then on, not this one.
    WatchPolicy watchPolicy_;
    FileAccessPolicy fileAccessPolicy_;

    // Whether the next load to finish is to recognize the Log Format: the
    // first load, and the one after a manual reload or a truncation.
    bool formatRecognitionPending_ = true;

    // How many times Format Recognition has run, so a test can tell.
    int formatRecognitionCount_ = 0;

    // The Log Format recognized for the Log File, if any: one of the
    // Catalog's own, kept even when the Catalog is rebuilt.
    std::shared_ptr<const LogFormatDefinition> recognizedFormat_;

    // The upper pane shows either the text view or the Table View
    QStackedWidget* mainViewStack_ = nullptr;
    LogTableView* logTableView_ = nullptr;
    QToolButton* tableViewToggle_ = nullptr;
    // The Presentation the upper pane shows: logMainView_ or logTableView_
    LogPresentation* presentation_ = nullptr;
};

#endif
