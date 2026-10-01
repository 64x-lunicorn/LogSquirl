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
#include <memory>
#include <optional>
#include <qchar.h>
#include <string_view>
#include <utility>
#include <vector>

#include <QAbstractScrollArea>
#include <QBasicTimer>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>

#ifdef GLOGG_PERF_MEASURE_FPS
#include "perfcounter.h"
#endif

#include "abstractlogdata.h"
#include "decorationsetup.h"
#include "linedecorator.h"
#include "linemapping.h"
#include "linessaver.h"
#include "linetypes.h"
#include "logfileview.h"
#include "overviewwidget.h"
#include "quickfind.h"
#include "quickfindmux.h"
#include "regularexpressionpattern.h"
#include "selection.h"
#include "settingspolicies.h"
#include "shownline.h"
#include "textviewscrolling.h"
#include "viewportlayout.h"
#include "wrappedstring.h"

class QFileDialog;
class QKeyEvent;
class QMenu;
class QPainter;
class QShortcut;

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
// It can be either the top (full) or bottom (filtered) view: the two differ
// only in the LineMapping they are built with. Either is told what every view
// of its Log File shows alike through LogFileView.
class AbstractLogView : public QAbstractScrollArea,
                        public LogFileView,
                        public SearchableWidgetInterface {
    Q_OBJECT

public:
    // Width of the overview strip, in pixels. Shared with whatever else
    // lays out an overview beside a view of Log Lines -- the Table View's
    // overview included -- so there is one declaration to keep in step.
    static constexpr int OverviewWidth = 27;

    // Lets a test read what the view was handed.
    template <class T>
    struct access_by;

    // Constructor of the widget, the data set is passed.
    // The caller retains ownership of the data set.
    // The pointer to the QFP is used for colouring and QuickFind searches
    // initialTextWrap is the state this view starts in. It is a
    // constructor parameter and not a setting this view reads, because
    // that is the truth of it: the setting is the starting state for a new
    // view. Changing it afterwards goes through textWrapSet(), which is
    // what the View menu's action calls; a view that already exists is
    // never re-read from the settings.
    //
    // newLogData is read by position, as the view shows its lines; lines says
    // which Log Line each position shows. Without one, every Log Line of
    // newLogData is shown at its own position.
    AbstractLogView( const AbstractLogData* newLogData, std::unique_ptr<const LineMapping> lines,
                     const QuickFindPattern* const quickFind, bool initialTextWrap,
                     QWidget* parent = nullptr );
    AbstractLogView( const AbstractLogData* newLogData, const QuickFindPattern* const quickFind,
                     bool initialTextWrap, QWidget* parent = nullptr );

    ~AbstractLogView() override;

    // rule of 5
    AbstractLogView( const AbstractLogView& ) = delete;
    AbstractLogView( AbstractLogView&& ) = delete;
    AbstractLogView& operator=( const AbstractLogView& ) = delete;
    AbstractLogView& operator=( AbstractLogView&& ) = delete;

    void updateFont( const QFont& font ) override;

    // Refresh the widget when the data set has changed, as change says: told
    // that Log Lines were only appended, scrolling keeps what it counted for
    // the Log Lines there were.
    void updateData( LinesChange change = LinesChange::Any );
    // Instructs the widget to update it's content geometry,
    // used when the font is changed.
    void updateDisplaySize();
    // Return the position of the Log Line at the view's Scroll Position
    LineNumber getTopLine() const;
    // Where the view stands: the Log Line at the top and which of its Visual
    // Lines is shown first.
    ScrollPosition scrollPosition() const;
    // Return the text of the current selection, as the Log File holds it.
    // Text selected within a Log Line that takes in part of a Named Value
    // takes in all of its raw text (#647).
    QString getSelectedText() const;
    // The text of the current selection as the view shows it: with the
    // names of its Named Values while the view shows Value Names (Copy as
    // Shown, #647).
    QString getSelectedTextAsShown() const;
    // True for partial selection
    bool isPartialSelection() const;
    // The selected Log Lines, in order (see Selection::getLines()).
    logsquirl::vector<LineNumber> selectedLogLines() const;
    // The first at most limit of them, without looking at the others (#663).
    logsquirl::vector<LineNumber> selectedLogLines( LinesCount limit ) const;
    // The Log Lines the view shows around its current position
    // (getViewPosition()), in order: at most count of them, centered on it
    // where the view shows enough before and after it. What the Regex Lab
    // takes as its sample (#659).
    logsquirl::vector<LineNumber> logLinesAroundViewPosition( LinesCount count ) const;
    // Instructs the widget to select the whole text.
    void selectAll();
    // Saves the lines from the first to the last selected one to filename,
    // behind a progress dialog. Nothing is saved without a selection.
    void saveSelectedTo( const QString& filename );

    // Whether the view follows, as it was handed last.
    bool isFollowEnabled() const
    {
        return follow_;
    }

    bool isTextWrapEnabled() const
    {
        return scrolling_.textWrap();
    }

    // Whether the View menu's Show Value Names is on for this view, as it was
    // handed last (valueNamesShownSet()). Off until it is handed.
    bool isValueNamesShownSet() const
    {
        return valueNamesShown_;
    }
    // Whether the view shows Value Names now: the switch is on and some
    // Naming Rule of the Value Names Collection can name a value (#647).
    bool showsValueNames() const;

    // What the tooltip over pos, in Viewport coordinates, tells: the Named
    // Value there and where its name came from; empty where there is none.
    QString valueNameToolTipAt( const QPoint& pos ) const;

    void allowFollowMode( bool allow ) override;

    void setSearchPattern( const RegularExpressionPattern& pattern ) override;

    void setColorLabels( const std::vector<QStringList>& labels ) override;

    // Hand over the settings that color Log Lines. Call it after a settings
    // change: painting reads no setting of its own, so this is the only way
    // a changed one reaches the Viewport.
    void setDecorationPolicy( const DecorationPolicy& policy ) override;

    // Hand over the settings this Presentation shows and scrolls under. Call
    // it when the view is built and again after a settings change: the view
    // reads no setting of its own, so this is the only way a changed one
    // reaches it. Nothing is derived from it and kept, so a change on the
    // Presentation Axis takes effect without the Log File being opened again.
    // The main view and the Filtered View each show the line numbers the
    // Policy says for their kind as well.
    void setPresentationPolicy( const PresentationPolicy& policy ) override;

    // Where every Log Line sits in the Viewport and what sits at any point of
    // it, including the Visual Lines the Viewport holds right now. This is what
    // hit testing and painting read, so asking it is asking the view itself.
    // Built from the Log File and never from a paint, it answers before the
    // first paint has happened.
    //
    // The layout is a pure value that reads its inputs and returns answers, so
    // a caller holding one cannot move the view. The reference is to the
    // view's own copy, rebuilt only when the view has moved or its Log File
    // changed: it stays valid until the next call, so ask again rather than
    // hold it, or copy it to keep a snapshot.
    const ViewportLayout& viewportLayout() const;

    // Which Log Line each position of this view shows. Replacing it repaints
    // the view; the selection, being Log Lines, stays where it is.
    void setLineMapping( std::unique_ptr<const LineMapping> lines );
    const LineMapping& lineMapping() const;

    void registerShortcuts() override;

    // The Value Names Collection changed: the Log Lines are named again, if
    // the view shows Value Names.
    void applyValueNamesChange() override;

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
    bool viewportEvent( QEvent* e ) override;

    // Reads the lines a save from this view writes, by their position in the
    // view, off the UI thread (see LineMapping::linesToSave()).
    DisplayedLinesReader linesToSave() const;

    // The dialog Save to file and Save selected to file ask with: one that
    // offers to save With Value Names while the view shows them; none, and
    // the platform's own dialog, otherwise (#647).
    std::unique_ptr<QFileDialog> saveLinesDialog();

    // Saves the lines at positions [begin, end) to filename, behind an application
    // modal progress dialog. filename is replaced only when every line was
    // written: a cancelled or failed save leaves it as it was. withValueNames
    // saves them as shown with the Value Names of this view (#647); without,
    // or when the view shows none, as the Log File holds them.
    void saveLinesTo( const QString& filename, LineNumber begin, LineNumber end,
                      bool withValueNames = false );

    // Get the overview associated with this view, or NULL if there is none
    Overview* getOverview() const
    {
        return overview_;
    }
    // Set the Overview and OverviewWidget
    void setOverview( Overview* overview, OverviewWidget* overviewWidget );

    // Returns the current "position" of the view as a Log Line: either the
    // selected Log Line or the one in the middle of the view. None when the
    // view shows no Log Line.
    OptionalLineNumber getViewPosition() const;

    // The Log Line drawn at pos, in viewport coordinates, if any.
    OptionalLineNumber logLineAtPoint( const QPoint& pos ) const;

    // The context menu for the current selection, opened at pos in viewport
    // coordinates; built by PresentationMenu, and not yet shown.
    std::unique_ptr<QMenu> createContextMenu( const QPoint& pos );
    // What the context menu's Export as CSV... does. Empty, and the menu
    // offers no such entry, unless the view can split its Log Lines into the
    // fields of a Log Format.
    virtual std::function<void()> exportAsCsvAction();

    void registerShortcut( const std::string& action, std::function<void()> func );

Q_SIGNALS:
    // Asks the owner of follow, the View Set, to engage or leave it: the user
    // pulled past the bottom or moved away from it. The view follows only once
    // it is handed the outcome through followSet() (#558).
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
    // Makes the widget select and display the passed Log Line, scrolling as
    // necessary. A Log Line the view doesn't show selects the one shown
    // before it (or the first one shown).
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

    // The owner of follow turned it on or off.
    void followSet( bool checked ) override;

    // Signals the text wrap mode has been enabled.
    void textWrapSet( bool checked );

    // The View menu's Show Value Names switched for this view's tab (#647).
    void valueNamesShownSet( bool shown );

    // Signal the on/off status of the overview has been changed.
    void refreshOverview();

    // Make the view jump to the specified Log Line, regardless of it
    // being on the screen or not. (does NOT Q_EMIT followDisabled() )
    void jumpToLine( LineNumber line );
    // Moves the Scroll Position to the first Visual Line of logLine -- the
    // nearest one shown, when it is not -- as far as the bottom Scroll
    // Position allows: a restored Log File stands where it stood (#559).
    void showAtTop( LineNumber logLine );

    // Configure the setting of whether to show line number margin
    void setLineNumbersVisible( bool lineNumbersVisible );

    // Repaint after how the Log Lines look changed, not their text: Marks,
    // Matches, Highlighters, Color Labels. The Log Lines already read for the
    // Viewport are painted again.
    void updateDecorations() override;

    // Read the Log Lines in the Viewport again and repaint: their text may
    // have changed although the Log File's line count did not, as under
    // another Encoding.
    void rereadLogLines() override;

    // Set the overview visibility and update viewport margins accordingly.
    void setOverviewVisible( bool visible );

    void setSearchLimits( LineNumber startLine, LineNumber endLine ) override;

private Q_SLOTS:
    void handlePatternUpdated();
    void addToSearch();
    void replaceSearch();
    void excludeFromSearch();
    void findNextSelected();
    void findPreviousSelected();
    void copy();
    void copyWithLineNumbers();
    void copyAsShown();
    void markSelected();
    void saveToFile();
    void saveSelectedToFile();
    void setSelectionStart();
    void setSelectionEnd();
    void setQuickFindResult( bool hasMatch, const Portion& selection );

private:
    // Whether keyEvent is a bare digit that continues the count being typed,
    // which the view then takes before any shortcut of that key.
    bool isCountDigit( const QKeyEvent& keyEvent ) const;

    // Digits buffer (for numeric keyboard entry): the count of the next
    // command, typed as 0 followed by the number (docs/adr/0016).
    DigitsBuffer digitsBuffer_;

    // Whether the view follows, as the owner of follow handed it last.
    // Scrolling asks it through scrolledLines_ and keeps none of its own.
    bool follow_ = false;
    // Whether to show line numbers or not
    bool lineNumbersVisible_ = false;
    // Whether the View menu's Show Value Names is on for this view (#647).
    bool valueNamesShown_ = false;

    // Pointer to the CrawlerWidget's data set, read by position
    const AbstractLogData* logData_;

    // Which Log Line each position shows
    std::unique_ptr<const LineMapping> lines_;

    // Pointer to the Overview object
    Overview* overview_ = nullptr;

    // Pointer to the OverviewWidget, this class doesn't own it,
    // but is responsible for displaying it (for purely aesthetic
    // reasons).
    OverviewWidget* overviewWidget_ = nullptr;

    bool selectionStarted_ = false;
    // Start of the selection (characters), in Log Lines
    FilePosition selectionStartPos_;
    // Current end of the selection (characters)
    FilePosition selectionCurrentEndPos_;
    QBasicTimer autoScrollTimer_;

    // Hovering state
    // Last Log Line that has been hoovered on, if any
    OptionalLineNumber lastHoveredLine_;

    // Marks (left margin click)
    bool markingClickInitiated_ = false;
    OptionalLineNumber markingClickLine_;

    Selection selection_;
    // The length of the selection's text, which newSelection() reports; a
    // range extended step by step reads only the Log Lines each step adds.
    SelectedTextLength selectedTextLength_;
    RegularExpressionPattern searchPattern_;

    // The words of each Color Label, one list per color slot.
    std::vector<QStringList> colorLabelWords_ = std::vector<QStringList>{ 9 };

    // The one module that builds the Line Decorator's Context, shared with
    // the Table View: it holds the Decoration Policy, the main search
    // pattern and the Color Labels, and caches the Highlighters built from
    // them, so a repaint builds no Highlighter of its own.
    DecorationSetup decorationSetup_;

    // What scrolling reads of this view: the lines it shows, by position, and
    // its Viewport as it measures now.
    class ScrolledLines final : public ScrolledText {
    public:
        explicit ScrolledLines( const AbstractLogView& view )
            : view_( view )
        {
        }
        LinesCount lineCount() const override;
        QString lineText( LineNumber position ) const override;
        logsquirl::vector<QString> lineTexts( LineNumber first, LinesCount count ) const override;
        ScrollingViewport viewport() const override;
        bool follows() const override;

    private:
        const AbstractLogView& view_;
    };
    ScrolledLines scrolledLines_{ *this };

    // Every scrolling rule, and all the state they keep: the Scroll Position,
    // the elastic hook that pulls to follow, the bottom of the Log File, text
    // wrapping, the first column and the Presentation Policy. Follow is not
    // among them: scrolling asks this view. This view turns Qt events into
    // its calls and applies its answers (#246).
    TextViewScrolling scrolling_;

    // Everything a Log Line's Decoration depends on that can change while the
    // Log Line stays in the Viewport. The Decorations change as a whole,
    // counted by the generation; the selection changes line by line, so a
    // Log Line's own part of it is compared.
    struct DecorationKey {
        uint64_t generation = 0;
        qint64 palette = 0;
        // The palette's colors differ as the window is active or not, which
        // its cache key does not tell.
        QPalette::ColorGroup colorGroup = QPalette::Active;
        bool selectedAsWhole = false;
        bool selectedAsSingleLine = false;
        LineColumn selectionStart{ -1 };
        LineColumn selectionEnd{ -1 };
        // The Value Names the Log Line was named with: 0 for none (#647).
        uint64_t valueNames = 0;

        bool operator==( const DecorationKey& ) const = default;
    };

    // A Log Line decorated for the Viewport, kept for as long as its key holds.
    struct DecoratedLogLine {
        DecorationKey key;
        AbstractLogData::LineType lineType;
        Decoration decoration;
    };

    // A Log Line in the Viewport, both as the Log File holds it and as it is drawn.
    struct ViewportLogLine {
        // Its position in the view.
        LineNumber position{ 0 };
        // The Log Line, not its position in the view.
        LineNumber lineNumber{ 0 };
        // The text as the Log File holds it, which the Line Decorator matches against.
        QString text;
        // The colors its ANSI color sequences ask for, in the columns of text.
        // Read only while the Decoration Policy shows ANSI colors, with the
        // text, once as the Log Line enters the Viewport (#573).
        logsquirl::vector<AnsiColorSpan> ansiColors;
        // The text as shown with its Named Values, while the view shows Value
        // Names and the Log Line has any; empty otherwise (#647).
        logsquirl::valuenames::ShownLine shown;
        // The display columns each Named Value is drawn in, in order.
        logsquirl::vector<WholeRun> namedValueColumns;
        // The text shown, with its tabs expanded, split into the Visual Lines
        // it is drawn as.
        WrappedString wrapped;
        // The first of its Visual Lines in the Viewport. Past 0 only for the Log
        // Line at the top, when the Scroll Position is partway through it.
        size_t firstVisualLine = 0;
        // How many of its Visual Lines, from firstVisualLine on, are in the Viewport.
        size_t visualLineCount = 0;
        // How it was last decorated. Painting fills it in, and a Log Line that
        // stays in the Viewport as it scrolls keeps it.
        mutable std::optional<DecoratedLogLine> decorated;
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
        double charWidth = -1;
        int charHeight = -1;
        bool textWrap = false;
        bool lineNumbersVisible = false;
        // Grows with the Log File's line count, which a Filtered View's
        // Displayed Lines need not follow.
        int lineNumberAreaWidth = 0;
        uint64_t generation = 0;
        // The Value Names the Log Lines are named with: 0 for none (#647).
        uint64_t valueNames = 0;

        bool operator==( const ViewportContentKey& ) const = default;

        // Whether the Log Lines read, expanded and wrapped for other, which
        // may stand elsewhere, are still right for this: only scrolling
        // happened in between.
        bool onlyScrolledFrom( const ViewportContentKey& other ) const
        {
            auto scrolled = other;
            scrolled.scrollPosition = scrollPosition;
            scrolled.firstColumn = firstColumn;
            return scrolled == *this;
        }
    };

    mutable std::optional<ViewportContent> viewportContent_;
    mutable ViewportContentKey viewportContentKey_;
    // Counts the builds of viewportContent_, so viewportLayout_ knows when
    // the Visual Lines it holds are stale.
    mutable uint64_t viewportContentBuilds_ = 0;
    mutable std::optional<ViewportLayout> viewportLayout_;
    mutable uint64_t viewportLayoutContentBuild_ = 0;
    // Bumped whenever the Log File content behind the viewport may have
    // changed, so the cached content is rebuilt.
    uint64_t viewportGeneration_ = 0;
    // Bumped whenever the Decorations may have changed, so every Log Line in
    // the Viewport is decorated again.
    uint64_t decorationGeneration_ = 0;

    // What changed about the Log Lines in the Viewport, and so what a refresh
    // redoes. A change of the Scroll Position, the first column or the
    // Viewport's geometry is none of these: the Viewport notices those by
    // itself (see ViewportContentKey).
    enum class ViewportChange {
        // How they are decorated: Marks, Matches, the Search pattern and its
        // Search Limits, QuickFind, Highlighters, Color Labels, the selection.
        // They are painted again from the text already read.
        Decorations,
        // Their text: the Log File was reloaded or grew, the Encoding changed,
        // or each position shows another Log Line. They are read, expanded and
        // wrapped again, then painted.
        Text,
    };
    // The one place that decides what a change drops.
    void refresh( ViewportChange change );

    // The Search Limits, in Log Lines, half-open as the Line Decorator takes them.
    LineNumber searchStart_;
    LineNumber searchEnd_;

    OptionalLineNumber selectionStart_;

    // Text handling
    // The advance a column is painted with, fractional: see
    // ViewportLayoutInput::charWidthPx.
    double charWidth_ = 1;
    int charHeight_ = 10;

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
        // What the Log Lines in the pixmap were read for.
        ViewportContentKey content_key_;
    };
    struct PullToFollowCache {
        QPixmap pixmap_;
        LineLength nb_columns_;
    };
    TextAreaCache textAreaCache_ = { {}, true, {}, 0_lnum, 0_lcol, {} };
    PullToFollowCache pullToFollowCache_ = { {}, 0_length };
    // The font Log Lines are measured and painted in. Held here rather than
    // read back from the widget: Qt's stylesheet style replaces a widget's
    // font when it polishes it, and a Theme applies a stylesheet to the whole
    // application, so the widget's font is not the view's to rely on (#354).
    // pixmapFontMetrics_, charWidth_ and charHeight_ all come from this font,
    // and so does the painter of the text area -- one font, measured and
    // painted with.
    QFont logFont_;
    QFontMetrics pixmapFontMetrics_;

    // FontUtils::uniformAsciiAdvance() for the font and resolution the text
    // area was last painted with. Measuring it takes about a hundred glyph
    // advances and the shaping of a long text, too much for every paint.
    struct UniformAsciiAdvanceCache {
        QFont font;
        int logicalDpiX = 0;
        int logicalDpiY = 0;
        std::optional<qreal> advance;
    };
    std::optional<UniformAsciiAdvanceCache> uniformAsciiAdvanceCache_;
    std::optional<qreal> uniformAsciiAdvance( const QPainter& painter );

    // The viewport layout, without the Visual Lines: enough to answer margins,
    // visible counts and scroll ranges, and cheap because it touches no
    // Log Line.
    ViewportLayout viewportGeometry() const;

    ViewportContentKey currentViewportContentKey() const;
    const ViewportContent& viewportContent() const;
    // The content at the current Scroll Position. Log Lines previous holds at
    // the same positions are taken from it rather than read again.
    ViewportContent buildViewportContent( std::optional<ViewportContent> previous ) const;

    // What a Log Line's Decoration depends on now.
    DecorationKey decorationKey( LineNumber logLine ) const;

    // The Value Namer the view names Log Lines with, or none: while the switch
    // is off or no Naming Rule can name anything, no Log Line is looked at.
    // Constant time (#647).
    const logsquirl::valuenames::ValueNamer* shownValueNamer() const;
    // Which Value Names the view shows, for the keys of what it caches: 0
    // for none.
    uint64_t valueNamesKey() const;
    // The Log Line as shown, named by the namer the view shows; none while it
    // shows none. Read from the Log File.
    std::optional<logsquirl::valuenames::ShownLine> shownLineOf( LineNumber logLine ) const;
    // Drops what was named with the Value Names shown before, and the widths
    // scrolled to for them.
    void renameLogLines();
    // The selection with its portion grown to take in every Named Value it
    // takes in part of; none while there is nothing to grow (#647).
    std::optional<Selection> selectionCoveringNamedValues() const;
    // The selected text, never half a raw value, with line numbers or not.
    QString selectedText( bool lineNumbers ) const;
    // The portion grown to take in every Named Value it takes in part of.
    Portion coveringNamedValues( const Portion& portion ) const;
    // The Log Line in the Viewport at a position of the view, if it is there.
    const ViewportLogLine* viewportLogLineAt( LineNumber position ) const;
    // The widest Log Line the Viewport has shown with Value Names, in display
    // columns: the horizontal scrollbar reaches it as well as the widest raw
    // one. Reset when the Value Names change.
    mutable LineLength widestShownLine_{ 0 };
    // The width the horizontal scrollbar was last given.
    LineLength scrolledWidth_{ 0 };

    // Moves the text area pixmap by the Visual Lines the view scrolled since
    // it was painted, and paints only the rows that came into view. Only
    // without text wrapping, where a row is a Log Line, and only when nothing
    // but the Scroll Position changed for the Log Lines still in view.
    // Returns false, having changed nothing, when it cannot.
    bool scrollTextArea( ScrollPosition scrollPosition );

    LinesCount getNbVisibleLines() const;
    LineLength getNbVisibleCols() const;

    // What sits under a point, by position.
    FilePosition convertCoordToFilePos( const QPoint& pos ) const;
    OptionalLineNumber convertCoordToLine( int yPos ) const;
    // What sits under a point, in Log Lines.
    FilePosition logLineFilePosAt( const QPoint& pos ) const;
    OptionalLineNumber logLineAtY( int yPos ) const;

    // Brings the first Visual Line of logLine into view (see displayPosition()).
    void displayLine( LineNumber logLine );
    // Brings the Visual Line holding position -- a position in the view, not a
    // Log Line -- into view. A Visual Line already wholly in the Viewport leaves
    // the view where it is; otherwise the view moves to put it on the top row,
    // going no further than the bottom.
    void displayPosition( FilePosition position );
    void moveSelection( LinesCount delta, bool isDeltaNegative );
    void moveSelectionUp();
    void moveSelectionDown();
    void jumpToStartOfLine();
    void jumpToEndOfLine();
    void jumpToRightOfScreen();
    void jumpToBottom();
    void selectWordAtPosition( const FilePosition& pos );
    // Selects the nearest Mark shown after, or before, the view's position.
    void selectMark( bool after );

    void updateSearchLimits();
    // Make the Search Limits start at, or end with, the Log Line.
    void setSearchStart( LineNumber logLine );
    void setSearchEnd( LineNumber logLine );

    void considerMouseHovering( int xPos, int yPos );

    LineLength maxLineLength( const logsquirl::vector<LineNumber>& lines ) const;

    // Asks for a file, and saves the lines in range [begin, end) to it
    void saveLinesToFile( LineNumber begin, LineNumber end );

    // Search functions (for n/N)
    using QuickFindSearchFn = void ( QuickFind::* )( Selection, QuickFindMatcher );
    void searchUsingFunction( QuickFindSearchFn searchFunction );
    // The Log Line shown at the last position, if any.
    OptionalLineNumber lastShownLogLine() const;
    // The Log Line shown delta positions after (or before, when negative)
    // logLine's, kept within the Log Lines shown.
    OptionalLineNumber shownLogLineMovedBy( LineNumber logLine, int64_t delta ) const;

    // Hands the scrollbars the ranges scrolling answers, then keeps the view
    // above the bottom.
    void updateScrollBars();

    // Applies what scrolling answered: asks the owner of follow for a change
    // of follow, moves the vertical scrollbar -- scrollContentsBy() follows,
    // and scrolling knows the scrollbar only caught up -- and redraws.
    void applyScroll( const ScrollAnswer& answer );
    // What follows any move of the Scroll Position: the overview, the
    // hovered line and a repaint.
    void scrollPositionMoved();
    // Tells the overview which lines the Viewport shows.
    void updateOverviewPosition();

    // Paints the rows of the text area from firstRow up to, not including,
    // endRow -- all of them by default -- and leaves the others as they are.
    void drawTextArea( QPaintDevice* paintDevice, int firstRow = 0,
                       std::optional<int> endRow = std::nullopt );
    QPixmap drawPullToFollowBar( int width, qreal pixelRatio );

    // Asks to leave follow: a move away from the bottom by the user.
    void disableFollow();

    // Utils functions
    void updateGlobalSelection();
    // The length of the selection's text, without building it.
    LineLength selectedTextLength();

    void selectAndDisplayRange( FilePosition pos );
};

#endif
