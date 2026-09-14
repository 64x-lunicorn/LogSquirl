/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2017 Nicolas Bonnefon
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
 * Copyright (C) 2016 -- 2021 Anton Filimonov and other contributors
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

#ifndef ABSTRACTLOGVIEW_H
#define ABSTRACTLOGVIEW_H

#include <array>
#include <cstddef>
#include <functional>
#include <qchar.h>
#include <string_view>
#include <utility>
#include <vector>

#include <QAbstractScrollArea>
#include <QBasicTimer>
#include <QColor>
#include <QEvent>
#include <QFontMetrics>

#ifdef GLOGG_PERF_MEASURE_FPS
#include "perfcounter.h"
#endif

#include "abstractlogdata.h"
#include "linessaver.h"
#include "linetypes.h"
#include "overviewwidget.h"
#include "quickfind.h"
#include "quickfindmux.h"
#include "regularexpressionpattern.h"
#include "selection.h"
#include "viewportlayout.h"
#include "viewtools.h"
#include "wrappedstring.h"

class QMenu;
class QAction;
class QShortcut;
class HighlightersMenu;

// Utility class representing a buffer for number entered on the keyboard
// The buffer keep at most 7 digits, and reset itself after a timeout.
class DigitsBuffer : public QObject {
    Q_OBJECT

public:
    // Reset the buffer.
    void reset();
    // Add a single digit to the buffer (discarded if it's not a digit),
    // the timeout timer is reset.
    void add( char character );
    // Get the content of the buffer (0 if empty) and reset it.
    LineNumber::UnderlyingType content();

    bool isEmpty() const;

protected:
    void timerEvent( QTimerEvent* event ) override;

private:
    // Duration of the timeout in milliseconds.
    static constexpr int DigitsTimeout = 2000;

    QString digits_;

    QBasicTimer timer_;
};

class Overview;

// Base class representing the log view widget.
// It can be either the top (full) or bottom (filtered) view.
class AbstractLogView : public QAbstractScrollArea, public SearchableWidgetInterface {
    Q_OBJECT

public:
    // Width of the overview strip, in pixels. Shared with whatever else
    // lays out an overview beside a view of Log Lines -- the Table View's
    // overview included -- so there is one declaration to keep in step.
    static constexpr int OverviewWidth = 27;

    // Constructor of the widget, the data set is passed.
    // The caller retains ownership of the data set.
    // The pointer to the QFP is used for colouring and QuickFind searches
    // initialTextWrap is the state this view starts in. It is a
    // constructor parameter and not a setting this view reads, because
    // that is the truth of it: the setting is the starting state for a new
    // view. Changing it afterwards goes through textWrapSet(), which is
    // what the View menu's action calls; a view that already exists is
    // never re-read from the settings.
    AbstractLogView( const AbstractLogData* newLogData, const QuickFindPattern* const quickFind,
                     bool initialTextWrap, QWidget* parent = nullptr );

    ~AbstractLogView() override;

    // rule of 5
    AbstractLogView( const AbstractLogView& ) = delete;
    AbstractLogView( AbstractLogView&& ) = delete;
    AbstractLogView& operator=( const AbstractLogView& ) = delete;
    AbstractLogView& operator=( AbstractLogView&& ) = delete;

    void updateFont( const QFont& font );

    // Refresh the widget when the data set has changed.
    void updateData();
    // Instructs the widget to update it's content geometry,
    // used when the font is changed.
    void updateDisplaySize();
    // Return the line number of the top line of the view
    LineNumber getTopLine() const;
    // Where the view stands: the Log Line at the top and which of its Visual
    // Lines is shown first.
    ScrollPosition scrollPosition() const;
    // Return the text of the current selection.
    QString getSelectedText() const;
    // True for partial selection
    bool isPartialSelection() const;
    // Instructs the widget to select the whole text.
    void selectAll();

    bool isFollowEnabled() const
    {
        return followMode_;
    }

    bool isTextWrapEnabled() const
    {
        return useTextWrap_;
    }

    void allowFollowMode( bool allow );

    void setSearchPattern( const RegularExpressionPattern& pattern );

    using QuickHighlighters = QStringList;
    void setQuickHighlighters( const std::vector<QuickHighlighters>& wordHighlighters );

    void registerShortcuts();

protected:
    void mousePressEvent( QMouseEvent* mouseEvent ) override;
    void mouseMoveEvent( QMouseEvent* mouseEvent ) override;
    void mouseReleaseEvent( QMouseEvent* ) override;
    void mouseDoubleClickEvent( QMouseEvent* mouseEvent ) override;
    void timerEvent( QTimerEvent* timerEvent ) override;
    void changeEvent( QEvent* changeEvent ) override;
    void paintEvent( QPaintEvent* paintEvent ) override;
    void resizeEvent( QResizeEvent* resizeEvent ) override;
    void scrollContentsBy( int dx, int dy ) override;
    void keyPressEvent( QKeyEvent* keyEvent ) override;
    void wheelEvent( QWheelEvent* wheelEvent ) override;
    bool event( QEvent* e ) override;

    // Must be implemented to return what LineType the line number is
    // (used for coloured bullets)
    virtual AbstractLogData::LineType lineType( LineNumber lineNumber ) const = 0;

    // Line number to display for line at the given index
    virtual LineNumber displayLineNumber( LineNumber lineNumber ) const;
    virtual LineNumber lineIndex( LineNumber lineNumber ) const;
    virtual LineNumber maxDisplayLineNumber() const;

    // The lines a QuickFind in this view searches, in Log Line numbers: a
    // copy of what the view displays, taken on the UI thread when the
    // QuickFind starts. Every Log Line unless overridden.
    virtual QuickFindLines quickFindLines() const;

    // Reads the lines a save from this view writes, by their position in the
    // view, off the UI thread: taken on the UI thread when the save starts,
    // it doesn't see what the view displays afterwards. Reads the view's data
    // unless overridden.
    virtual DisplayedLinesReader linesToSave() const;

    // Get the overview associated with this view, or NULL if there is none
    Overview* getOverview() const
    {
        return overview_;
    }
    // Set the Overview and OverviewWidget
    void setOverview( Overview* overview, OverviewWidget* overviewWidget );

    // Returns the current "position" of the view as a line number,
    // it is either the selected line or the middle of the view.
    LineNumber getViewPosition() const;

    virtual void doRegisterShortcuts();
    void registerShortcut( const std::string& action, std::function<void()> func );

Q_SIGNALS:
    // Sent up to the MainWindow to enable/disable the follow mode
    void followModeChanged( bool enabled );
    // Sent when the view wants the QuickFind widget pattern to change.
    void changeQuickFind( const QString& newPattern, QuickFindMux::QFDirection newDirection );
    // Sent when a new line has been selected by the user
    void newSelection( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                       LineLength nSymbols );
    // Sent up when quickFind wants to show a message to the user.
    void notifyQuickFind( const QFNotification& message );
    // Sent up when quickFind wants to clear the notification.
    void clearQuickFindNotification();
    // Sent when the view ask for a line to be marked
    // (click in the left margin).
    void markLines( const logsquirl::vector<LineNumber>& lines );
    // Sent up when the user wants to add the selection to the search
    void addToSearch( const QString& selection );
    // Sent up when the user wants to replace the search with the selection
    void replaceSearch( const QString& selection );
    void excludeFromSearch( const QString& selection );
    // Sent up when the mouse is hovered over a line's margin
    void mouseHoveredOverLine( LineNumber line );
    // Sent up when the mouse leaves a line's margin
    void mouseLeftHoveringZone();
    // Sent up for view initiated quickfind searches
    void searchNext();
    void searchPrevious();
    // Sent up when the user has moved within the view
    void activity();
    // Sent up when the user want to exit this view
    // (switch to the next one)
    void exitView();

    void changeSearchLimits( LineNumber startLine, LineNumber endLine );
    void clearSearchLimits();

    void saveDefaultSplitterSizes();
    void sendSelectionToScratchpad();
    void replaceScratchpadWithSelection();
    void changeFontSize( bool increase );

    void addColorLabel( size_t label );
    void addNextColorLabel();
    void clearColorLabels();
    void highlightersChange();

public Q_SLOTS:
    // Makes the widget select and display the passed line.
    // Scrolling as necessary
    void trySelectLine( LineNumber newLine );
    void selectAndDisplayLine( LineNumber line );
    void selectPortionAndDisplayLine( LineNumber line, LinesCount nLines, LineColumn startCol,
                                      LineLength nSymbols );

    // Use the current QFP to go and select the next match.
    void searchForward() override;
    // Use the current QFP to go and select the previous match.
    void searchBackward() override;

    // Use the current QFP to go and select the next match (incremental)
    void incrementallySearchForward() override;
    // Use the current QFP to go and select the previous match (incremental)
    void incrementallySearchBackward() override;
    // Stop the current incremental search (typically when user press return)
    void incrementalSearchStop() override;
    // Abort the current incremental search (typically when user press esc)
    void incrementalSearchAbort() override;

    // Signals the follow mode has been enabled.
    void followSet( bool checked );

    // Signals the text wrap mode has been enabled.
    void textWrapSet( bool checked );

    // Signal the on/off status of the overview has been changed.
    void refreshOverview();

    // Make the view jump to the specified line, regardless of it
    // being on the screen or not. (does NOT Q_EMIT followDisabled() )
    void jumpToLine( LineNumber line );

    // Configure the setting of whether to show line number margin
    void setLineNumbersVisible( bool lineNumbersVisible );

    // Force the next refresh to fully redraw the view by invalidating the cache.
    // To be used if the data might have changed.
    void forceRefresh();

    // Set the overview visibility and update viewport margins accordingly.
    void setOverviewVisible( bool visible );

    void setSearchLimits( LineNumber startLine, LineNumber endLine );

private Q_SLOTS:
    void handlePatternUpdated();
    void addToSearch();
    void replaceSearch();
    void excludeFromSearch();
    void findNextSelected();
    void findPreviousSelected();
    void copy();
    void copyWithLineNumbers();
    void markSelected();
    void saveToFile();
    void saveSelectedToFile();
    void setSearchStart();
    void setSearchEnd();
    void setSelectionStart();
    void setSelectionEnd();
    void setQuickFindResult( bool hasMatch, const Portion& selection );
    void setColorLabel( QAction* action );

private:
    // Graphic parameters
    static constexpr int HookThreshold = 300;

    // Digits buffer (for numeric keyboard entry)
    DigitsBuffer digitsBuffer_;

    // Follow mode
    bool followMode_ = false;

    // ElasticHook for follow mode
    ElasticHook followElasticHook_;

    // Whether to show line numbers or not
    bool lineNumbersVisible_ = false;

    // Pointer to the CrawlerWidget's data set
    const AbstractLogData* logData_;

    // Pointer to the Overview object
    Overview* overview_ = nullptr;

    // Pointer to the OverviewWidget, this class doesn't own it,
    // but is responsible for displaying it (for purely aesthetic
    // reasons).
    OverviewWidget* overviewWidget_ = nullptr;

    bool selectionStarted_ = false;
    // Start of the selection (characters)
    FilePosition selectionStartPos_;
    // Current end of the selection (characters)
    FilePosition selectionCurrentEndPos_;
    QBasicTimer autoScrollTimer_;

    // Hovering state
    // Last line that has been hoovered on, -1 if none
    OptionalLineNumber lastHoveredLine_;

    // Marks (left margin click)
    bool markingClickInitiated_ = false;
    OptionalLineNumber markingClickLine_;

    Selection selection_;
    RegularExpressionPattern searchPattern_;

    std::vector<QuickHighlighters> quickHighlighters_ = std::vector<QuickHighlighters>{ 9 };

    // Position of the view, those are crucial to control drawing
    // scrollPosition_ gives the position of the view; only scrolling moves it.
    // lastLineAligned_ == true draws the last Visual Line of the Log File on
    // the Viewport's last row rather than the first Visual Line on the top
    // row: the view is at the bottom Scroll Position. Scrolling updates it, so
    // Log Lines added below a view that is not following leave it as it is.
    ScrollPosition scrollPosition_;
    // The text columns scrollPosition_'s Visual Line was counted at. When the
    // view re-wraps to another width, rewrapScrollPosition() finds the same
    // character again from it.
    LineLength scrollPositionColumns_{ 0 };
    bool lastLineAligned_ = false;
    // The fraction of a Visual Line the wheel has turned but not yet scrolled.
    double wheelVisualLinesPending_ = 0;
    bool useTextWrap_;
    LineColumn firstCol_ = 0_lcol;

    // A Log Line in the Viewport, both as the Log File holds it and as it is drawn.
    struct ViewportLogLine {
        LineNumber lineNumber{ 0 };
        // The text as the Log File holds it, which the Line Decorator matches against.
        QString text;
        // The text with its tabs expanded, split into the Visual Lines it is drawn as.
        WrappedString wrapped;
        // The first of its Visual Lines in the Viewport. Past 0 only for the Log
        // Line at the top, when the Scroll Position is partway through it.
        size_t firstVisualLine = 0;
        // How many of its Visual Lines, from firstVisualLine on, are in the Viewport.
        size_t visualLineCount = 0;
    };

    // The Log Lines currently in the Viewport, with the Visual Lines they occupy.
    // Computed on demand from the Log File -- not by painting -- and cached until
    // something it depends on changes. Each Log Line is expanded and wrapped once
    // here, for hit testing and painting alike.
    struct ViewportContent {
        // What painting draws.
        logsquirl::vector<ViewportLogLine> logLines;
        // What hit testing resolves a point against.
        VisualLines visualLines;
    };

    // Everything a ViewportContent depends on. When this changes, the content
    // is rebuilt.
    struct ViewportContentKey {
        ScrollPosition scrollPosition;
        LineColumn firstColumn{ 0 };
        LinesCount totalLines{ 0 };
        int viewportWidth = -1;
        int viewportHeight = -1;
        int charWidth = -1;
        int charHeight = -1;
        bool textWrap = false;
        bool lineNumbersVisible = false;
        uint64_t generation = 0;

        bool operator==( const ViewportContentKey& ) const = default;
    };

    mutable std::optional<ViewportContent> viewportContent_;
    mutable ViewportContentKey viewportContentKey_;
    // Bumped whenever the Log File content behind the viewport may have
    // changed, so the cached content is rebuilt.
    uint64_t viewportGeneration_ = 0;

    // Everything the bottom of the Log File depends on but its Log Lines. A
    // change to those comes through updateData(), which wraps the bottom again.
    struct LogFileBottomKey {
        LinesCount totalLines{ 0 };
        int viewportWidth = -1;
        int viewportHeight = -1;
        int charWidth = -1;
        int charHeight = -1;
        bool textWrap = false;
        bool lineNumbersVisible = false;

        bool operator==( const LogFileBottomKey& ) const = default;
    };

    mutable std::optional<LogFileBottom> logFileBottom_;
    mutable LogFileBottomKey logFileBottomKey_;

    LineNumber searchStart_;
    LineNumber searchEnd_;

    OptionalLineNumber selectionStart_;

    // Text handling
    int charWidth_ = 1;
    int charHeight_ = 10;

    // Popup menu
    QMenu* popupMenu_;
    QAction* copyAction_;
    QAction* copyWithLineNumbersAction_;
    QAction* markAction_;
    QAction* sendToScratchpadAction_;
    QAction* replaceInScratchpadAction_;
    QAction* saveToFileAction_;
    QAction* saveSelectedToFileAction_;
    QAction* findNextAction_;
    QAction* findPreviousAction_;
    QAction* addToSearchAction_;
    QAction* replaceSearchAction_;
    QAction* excludeFromSearchAction_;
    QAction* setSearchStartAction_;
    QAction* setSearchEndAction_;
    QAction* clearSearchLimitAction_;
    QAction* setSelectionStartAction_;
    QAction* setSelectionEndAction_;
    QAction* saveDefaultSplitterSizesAction_;
    HighlightersMenu* highlightersMenu_;
    QMenu* colorLabelsMenu_;

    std::map<QString, QShortcut*> shortcuts_;

    // Pointer to the CrawlerWidget's QFP object
    const QuickFindPattern* const quickFindPattern_;
    // Our own QuickFind object
    QuickFind* quickFind_;

#ifdef GLOGG_PERF_MEASURE_FPS
    // Performance measurement
    PerfCounter perfCounter_;
#endif

    // Cache pixmap and associated info
    struct TextAreaCache {
        QPixmap pixmap_;
        bool invalid_;
        ScrollPosition scroll_position_;
        LineNumber last_line_;
        LineColumn first_column_;
    };
    struct PullToFollowCache {
        QPixmap pixmap_;
        LineLength nb_columns_;
    };
    TextAreaCache textAreaCache_ = { {}, true, {}, 0_lnum, 0_lcol };
    PullToFollowCache pullToFollowCache_ = { {}, 0_length };
    QFontMetrics pixmapFontMetrics_;

    // Everything the viewport layout is built from but the drawing offset,
    // which the pull-to-follow geometry derives from the rest.
    ViewportLayoutInput viewportInput() const;
    // The viewport layout, without the Visual Lines: enough to answer margins,
    // visible counts and scroll ranges, and cheap because it touches no
    // Log Line.
    ViewportLayout viewportGeometry() const;
    // The viewport layout including the Visual Lines currently in the Viewport, which is
    // what hit testing and painting need. Built from the Log File, never from
    // a paint, so it answers before the first paint has happened.
    ViewportLayout viewportLayout() const;

    const ViewportContent& viewportContent() const;
    ViewportContent buildViewportContent() const;

    LinesCount getNbVisibleLines() const;
    LineLength getNbVisibleCols() const;

    FilePosition convertCoordToFilePos( const QPoint& pos ) const;
    OptionalLineNumber convertCoordToLine( int yPos ) const;

    // The follow mode state the layout places the text and the pull-to-follow
    // bar from. Painting and hit testing both hand it to the same
    // ViewportLayout::pullToFollowGeometry() instead of computing positions
    // of their own.
    PullToFollowState pullToFollowState() const;

    // Brings the first Visual Line of line into view (see displayPosition()).
    void displayLine( LineNumber line );
    // Brings the Visual Line holding position into view. A Visual Line already
    // wholly in the Viewport leaves the view where it is; otherwise the view
    // moves to put it on the top row, going no further than the bottom.
    void displayPosition( FilePosition position );
    void moveSelection( LinesCount delta, bool isDeltaNegative );
    void moveSelectionUp();
    void moveSelectionDown();
    void jumpToStartOfLine();
    void jumpToEndOfLine();
    void jumpToRightOfScreen();
    void jumpToTop();
    void jumpToBottom();
    void selectWordAtPosition( const FilePosition& pos );

    void updateSearchLimits();

    void createMenu();

    void considerMouseHovering( int xPos, int yPos );

    LineLength maxLineLength( const logsquirl::vector<LineNumber>& lines ) const;

    // Save specified lines in range [begin, end) to a file
    void saveLinesToFile( LineNumber begin, LineNumber end );

    // Search functions (for n/N)
    using QuickFindSearchFn = void ( QuickFind::* )( Selection, QuickFindMatcher );
    void searchUsingFunction( QuickFindSearchFn searchFunction );
    // QuickFind works in Log Line numbers: these convert a selection from
    // this view's line numbers and back.
    Selection toLogLines( const Selection& selection ) const;
    Selection toViewLines( const Selection& selection ) const;
    // Whether this view displays the Log Line.
    bool displaysLogLine( LineNumber logLine ) const;
    // The Log Line this view shows at its line viewLine.
    LineNumber logLineAt( LineNumber viewLine ) const;

    void updateScrollBars();

    // Moves the view to position, brought into the Log File, and the vertical
    // scrollbar to its Log Line. Moving between Visual Lines of one Log Line
    // leaves the scrollbar where it is.
    void scrollTo( ScrollPosition position );
    // Moves the view visualLines Visual Lines down, or up when negative,
    // wrapping only the Log Lines it passes over.
    void scrollByVisualLines( int64_t visualLines );
    // A scroll step taken by a key or by selection autoscroll. As the
    // scrollbar's own steps do, a step up leaves follow mode.
    void stepVisualLines( int64_t visualLines );
    // How many Visual Lines a wheel event scrolls down (up when negative).
    int64_t wheelVisualLines( const QWheelEvent& wheelEvent );
    int64_t visualLinesPerPage() const;
    // Where the last Visual Line of the Log File sits on the Viewport's last
    // row. Wrapped backwards from the end of the Log File, no more Log Lines
    // than the Viewport has rows, when something it depends on changed.
    const LogFileBottom& logFileBottom() const;
    // The bottom Scroll Position: where follow mode and the vertical
    // scrollbar's maximum put the view, and scrolling goes no further down.
    ScrollPosition bottomScrollPosition() const;
    // How many Visual Lines line wraps into at the current width. Reads and
    // wraps that one Log Line.
    size_t visualLineCount( LineNumber line ) const;
    // How many Visual Lines line wraps into, columns wide.
    size_t visualLineCount( LineNumber line, LineLength columns ) const;
    // position, with a Visual Line a re-wrap or a change to the Log File has
    // left past the end of its Log Line brought back to that Log Line's last.
    ScrollPosition withinLogLine( ScrollPosition position ) const;
    // What every change that re-wraps the view (its width, the font, line
    // numbers) does before the layout is rebuilt: the Scroll Position keeps its
    // Log Line, and its Visual Line becomes the one holding the character that
    // was first on the top row at the width it was counted at.
    void rewrapScrollPosition();
    // The Visual Line of the Log Line holding position, at the current width.
    ScrollPosition visualLineOf( FilePosition position ) const;
    // Aligns the last Visual Line on the last row when the view is at the
    // bottom Scroll Position, and the first on the top row otherwise.
    void updateLastLineAligned();
    // What follows any move of the Scroll Position: the overview, the
    // hovered line and a repaint.
    void scrollPositionMoved();

    LineNumber verticalScrollToLineNumber( int scrollPosition ) const;
    int lineNumberToVerticalScroll( LineNumber line ) const;
    double verticalScrollMultiplicator() const;

    void drawTextArea( QPaintDevice* paintDevice );
    QPixmap drawPullToFollowBar( int width, qreal pixelRatio );

    void disableFollow();

    // Utils functions
    void updateGlobalSelection();

    void selectAndDisplayRange( FilePosition pos );
};

#endif
