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

#pragma once

#include "decorationsetup.h"
#include "highlightedmatch.h"
#include "highlighterset.h"
#include "linedecorator.h"
#include "logfiltereddata.h"
#include "logformattablemodel.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"

#include <QPainter>
#include <QStyledItemDelegate>

#include <memory>
#include <optional>
#include <vector>

// Delegate that applies highlighter-set and search-pattern coloring to table view cells.
// Also paints portion (in-cell text) selections and hover highlights.
class LogTableHighlightDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    // Horizontal padding applied on each side of a cell's text, both when
    // painting it and when hit-testing a click against it. The two must
    // agree, so both read this single constant.
    static constexpr int HorizontalTextPadding = 4;

    explicit LogTableHighlightDelegate( QObject* parent = nullptr )
        : QStyledItemDelegate( parent )
    {
    }

    // Set the filtered data source for match/mark line type queries.
    void setFilteredData( LogFilteredData* data )
    {
        filteredData_ = data;
    }

    // Set which Log Line each Row shows.
    void setRowMapping( std::shared_ptr<const RowMapping> rows )
    {
        rows_ = std::move( rows );
    }

    // Set the quickfind pattern for incremental search highlighting.
    void setQuickFindPattern( std::shared_ptr<QuickFindPattern> pattern )
    {
        quickFindPattern_ = std::move( pattern );
        // The Decoration Setup does not own it: this delegate keeps it alive
        // for as long as it points at it.
        decorationSetup_.setQuickFindPattern( quickFindPattern_.get() );
    }

    // Set the color label words (one QStringList per color slot). The color
    // of each slot comes from the Highlighter Set Collection, so a change to
    // those colors reaches the cells by setting the words again.
    void setColorLabelWords( const std::vector<QStringList>& words )
    {
        decorationSetup_.setColorLabels( words, colorLabelColors() );
    }

    // Set the main search pattern for main-search highlighting.
    void setSearchPattern( const RegularExpressionPattern& pattern )
    {
        decorationSetup_.setSearchPattern( pattern );
    }

    // Hand over the settings that color Log Lines. Call it after a settings
    // change: paint() reads no setting of its own, so this is the only way a
    // changed one reaches a cell.
    void setDecorationPolicy( const DecorationPolicy& policy )
    {
        decorationSetup_.setPolicy( policy );
    }

    // Set the Search Limits: rows outside this range are shown subdued. The
    // end is the Log Line after the last one searched, as the Line Decorator
    // takes it.
    void setSearchLimits( LineNumber startLine, LineNumber endLine )
    {
        searchLimits_ = SearchLimits{ startLine, endLine };
    }

    // Set the current portion (in-cell text) selection for painting.
    void setPortionSelection( int row, int column, int startChar, int endChar )
    {
        portionRow_ = row;
        portionCol_ = column;
        portionStartChar_ = std::min( startChar, endChar );
        portionEndChar_ = std::max( startChar, endChar );
    }

    // Clear the portion selection.
    void clearPortionSelection()
    {
        portionRow_ = -1;
        portionCol_ = -1;
        portionStartChar_ = 0;
        portionEndChar_ = 0;
    }

    // Set the row currently under the mouse cursor for hover highlighting.
    void setHoverRow( int row )
    {
        hoverRow_ = row;
    }

    // Clear the hover row.
    void clearHoverRow()
    {
        hoverRow_ = -1;
    }

    // Compose the Decoration for one cell given an explicit Line Decorator
    // Context and the row-level Line Verdict (decided from the raw Log
    // Line -- a whole-line Highlighter and the Search Limits are facts
    // about the whole line, not about one field of it). A word-only
    // Highlighter's matched span, main search, Color Labels and QuickFind
    // all match the cell's own text directly, so no mapping from cell
    // column back to raw line offset is needed -- only the whole-line
    // colour and the Search Limits gate carry over from the row verdict.
    // This is the same composition rule paint() applies with its own live
    // Context; exposed here with an explicit Context so it can be tested
    // without touching the live Highlighter Set or Configuration.
    static Decoration decorationFor( const LineDecorator::Context& context,
                                     const LineVerdict& rowVerdict, LineNumber lineNumber,
                                     AbstractLogData::LineType lineType, const QString& cellText,
                                     const std::optional<HighlightedMatch>& selection
                                     = std::nullopt )
    {
        const LineDecorator lineDecorator{ context };
        const auto cellHighlighterVerdict
            = lineDecorator.verdictFor( LogLine{ lineNumber, cellText }, lineType );
        const LineVerdict cellVerdict{ rowVerdict.wholeLineHighlight(), lineType,
                                       rowVerdict.isOutsideSearchLimits(),
                                       cellHighlighterVerdict.highlighterSpans() };
        return lineDecorator.decorate( cellText, cellVerdict, selection );
    }

    // The row's foreground and background colour, decided from its Line
    // Verdict. Mirrors AbstractLogView's per-line colour derivation so the
    // Table View reads the same facts the text view's gutter bullet reads --
    // it just has nowhere but the row background to show them, having no
    // gutter of its own. Precedence, highest to lowest: Search Limits (the
    // whole row is subdued, nothing else shows), a whole-line Highlighter
    // (an explicit, deliberate user rule), then Mark/Match -- consistent
    // with the text view's bullet colours, including the distinct colour
    // for a line that is both. A Context Line is dimmed on top of whatever
    // colour was decided, exactly as the text view dims its foreground.
    struct RowColors {
        QColor foreColor;
        QColor backColor;
    };

    static RowColors rowColorsFor( const LineVerdict& rowVerdict, QColor defaultForeColor,
                                   QColor defaultBackColor, QColor disabledForeColor )
    {
        QColor foreColor = defaultForeColor;
        QColor backColor = defaultBackColor;

        if ( rowVerdict.isOutsideSearchLimits() ) {
            foreColor = disabledForeColor;
        }
        else if ( const auto wholeLine = rowVerdict.wholeLineHighlight(); wholeLine.has_value() ) {
            foreColor = wholeLine->foreColor;
            backColor = wholeLine->backColor;
        }
        else if ( rowVerdict.isMark() ) {
            backColor
                = rowVerdict.isMatch() ? LineStatusColors::markedMatch() : LineStatusColors::mark();
        }
        else if ( rowVerdict.isMatch() ) {
            backColor = LineStatusColors::match();
        }

        if ( rowVerdict.isContextLine() ) {
            foreColor.setAlpha( 128 );
        }

        return { foreColor, backColor };
    }

    void paint( QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index ) const override
    {
        if ( !index.isValid() ) {
            QStyledItemDelegate::paint( painter, option, index );
            return;
        }

        painter->save();

        // Match the text view's rendering quality
        painter->setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing );

        auto opt = option;
        initStyleOption( &opt, index );

        // A portion (in-cell text) selection on this row takes precedence
        // over the row-level Qt selection state: it is what setPortionSelection()
        // was told about most recently, and it needs the rest of the row to
        // keep showing Highlighter colour around it.
        const bool hasPortionOnRow = ( portionRow_ >= 0 && index.row() == portionRow_ );
        const bool isFullRowSelected = ( opt.state & QStyle::State_Selected ) && !hasPortionOnRow;

        if ( isFullRowSelected ) {
            // The selection colour overrides everything else in the cell
            // (it is decorate()'s highest-precedence source, and here it
            // covers the whole cell), so there is nothing to gain from
            // matching Highlighters, main search or QuickFind against it --
            // skip straight to the plain solid selection, the same
            // shortcut the text view takes for a fully selected line.
            const auto backColor = opt.palette.color( QPalette::Highlight );
            const auto foreColor = opt.palette.color( QPalette::HighlightedText );
            painter->fillRect( opt.rect, backColor );
            const auto cellText = index.data( Qt::DisplayRole ).toString();
            if ( !cellText.isEmpty() ) {
                const auto textRect
                    = opt.rect.adjusted( HorizontalTextPadding, 0, -HorizontalTextPadding, 0 );
                const auto fm = painter->fontMetrics();
                const int yOffset = ( opt.rect.height() - fm.height() ) / 2;
                const int baseline = opt.rect.top() + yOffset + fm.ascent();
                painter->setPen( foreColor );
                painter->drawText( textRect.left(), baseline, cellText );
            }
            painter->restore();
            return;
        }

        auto backColor = opt.palette.color( QPalette::Base );
        auto foreColor = opt.palette.color( QPalette::Text );

        // Alternating row colour
        if ( opt.features & QStyleOptionViewItem::Alternate ) {
            backColor = backColor.darker( 105 );
        }
        // Subtle hover highlight for the row under the mouse cursor
        if ( hoverRow_ >= 0 && index.row() == hoverRow_ ) {
            backColor = backColor.darker( 108 );
        }

        // The Line Decorator owns the colour precedence rule. The row-level
        // Line Verdict is decided from the raw Log Line -- a whole-line
        // Highlighter and the Search Limits are facts about the whole line,
        // not about one field of it -- so this needs the row's line number
        // and raw text even before we know whether this particular cell
        // has any text of its own.
        const auto lineNumber = rows_->logLineAt( index.row() );
        const auto rawLine = index.data( LogFormatTableModel::RawLineRole ).toString();
        const auto currentLineType = filteredData_ ? filteredData_->lineTypeByLine( lineNumber )
                                                   : AbstractLogData::LineTypeFlags::Plain;

        const auto context = buildDecoratorContext();
        const LineDecorator lineDecorator{ context };
        const auto rowVerdict
            = lineDecorator.verdictFor( LogLine{ lineNumber, rawLine }, currentLineType );

        // A whole-line Highlighter colours the whole row: every cell gets
        // the same background/foreground, not just the one whose text
        // happens to contain the matched word. Mark and Match colour the
        // row background the same way, since this view has no gutter to
        // draw a bullet in.
        const auto rowColors
            = rowColorsFor( rowVerdict, foreColor, backColor,
                            opt.palette.brush( QPalette::Disabled, QPalette::Text ).color() );
        foreColor = rowColors.foreColor;
        backColor = rowColors.backColor;

        painter->fillRect( opt.rect, backColor );

        const auto cellText = index.data( Qt::DisplayRole ).toString();
        if ( cellText.isEmpty() ) {
            painter->restore();
            return;
        }

        // A word-only Highlighter, main search, Color Labels and QuickFind
        // all match the cell's own text directly -- no mapping from cell
        // column back to raw line offset is needed. A portion (in-cell
        // text) selection is the highest-precedence source decorate()
        // knows about: it wins wherever it overlaps but leaves Highlighter
        // colour showing everywhere else, so a partially selected row
        // still shows it outside the selection.
        const auto selectionSpan = selectionSpanFor( index, cellText, opt, hasPortionOnRow );
        const auto decoration = decorationFor( context, rowVerdict, lineNumber, currentLineType,
                                               cellText, selectionSpan );

        if ( !decoration.spans().empty() ) {
            paintHighlightedText( painter, opt, cellText, decoration.spans(), foreColor );
        }
        else {
            const auto textRect
                = opt.rect.adjusted( HorizontalTextPadding, 0, -HorizontalTextPadding, 0 );
            const auto fm = painter->fontMetrics();
            // Center text vertically: offset = (cellHeight - fontHeight) / 2
            const int yOffset = ( opt.rect.height() - fm.height() ) / 2;
            const int baseline = opt.rect.top() + yOffset + fm.ascent();
            painter->setPen( foreColor );
            painter->drawText( textRect.left(), baseline, cellText );
        }

        painter->restore();
    }

    // The character position a click at pixelX resolves to, in a cell whose
    // left edge is at cellLeft: the caret position nearest the click, so the
    // left half of a character is before it and the right half after it.
    // 0 for an empty cell or a click left of the text.
    //
    // This is the Table View's hit test. It lives here, beside paint(),
    // rather than in the LogTableView that handles the click, because it
    // has to agree with where paint() draws each character -- both apply
    // HorizontalTextPadding -- and as a static of the delegate a test can
    // check the two against each other without standing up a LogTableView.
    static int charIndexAtX( const QString& cellText, const QFontMetrics& fm, int cellLeft,
                             int pixelX )
    {
        if ( cellText.isEmpty() ) {
            return 0;
        }

        const int relativeX = pixelX - ( cellLeft + HorizontalTextPadding );
        if ( relativeX <= 0 ) {
            return 0;
        }

        const int textLen = static_cast<int>( cellText.size() );
        for ( int i = 1; i <= textLen; ++i ) {
            const int charRight = fm.horizontalAdvance( cellText.left( i ) );
            if ( relativeX < charRight ) {
                // Before or after this character, whichever edge is closer
                const int charLeft = fm.horizontalAdvance( cellText.left( i - 1 ) );
                return ( relativeX - charLeft < charRight - relativeX ) ? i - 1 : i;
            }
        }
        return textLen;
    }

    // Return a size hint that accounts for the full text width (no clipping).
    QSize sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const override
    {
        auto hint = QStyledItemDelegate::sizeHint( option, index );
        const auto cellText = index.data( Qt::DisplayRole ).toString();
        if ( !cellText.isEmpty() ) {
            const auto fm = option.fontMetrics;
            hint.setWidth( fm.horizontalAdvance( cellText ) + 2 * HorizontalTextPadding );
        }
        return hint;
    }

private:
    // The Context the Line Decorator matches every color source against,
    // built by the one module that builds it for either Presentation. The
    // active Highlighter Set is read here, afresh for every cell, so that
    // switching sets re-colors the table without this delegate being told.
    LineDecorator::Context buildDecoratorContext() const
    {
        return decorationSetup_.context( HighlighterSetCollection::get().currentActiveSet(),
                                         searchLimits_ );
    }

    // The portion (in-cell text) selection decorate() should overlay on
    // top of everything else for this cell -- the dragged range within
    // this specific cell, already in the cell text's own coordinate space,
    // so it needs no translating. The row-level Qt selection is handled
    // separately in paint() before this is ever reached.
    std::optional<HighlightedMatch> selectionSpanFor( const QModelIndex& index,
                                                      const QString& cellText,
                                                      const QStyleOptionViewItem& opt,
                                                      bool hasPortionOnRow ) const
    {
        if ( !hasPortionOnRow || index.column() != portionCol_
             || portionStartChar_ >= portionEndChar_ ) {
            return std::nullopt;
        }

        const auto lo = std::min( portionStartChar_, static_cast<int>( cellText.size() ) );
        const auto hi = std::min( portionEndChar_, static_cast<int>( cellText.size() ) );
        if ( lo >= hi ) {
            return std::nullopt;
        }

        return HighlightedMatch{ LineColumn{ lo }, LineLength{ hi - lo },
                                 opt.palette.color( QPalette::HighlightedText ),
                                 opt.palette.color( QPalette::Highlight ) };
    }

    // Paint cell text with highlight segments. The spans are already an
    // ordered, non-overlapping Decoration, so no sorting or overlap
    // handling is needed here.
    static void paintHighlightedText( QPainter* painter, const QStyleOptionViewItem& opt,
                                      const QString& cellText,
                                      const logsquirl::vector<HighlightedMatch>& cellMatches,
                                      const QColor& foreColor )
    {
        const auto textRect
            = opt.rect.adjusted( HorizontalTextPadding, 0, -HorizontalTextPadding, 0 );
        const auto fm = painter->fontMetrics();
        const int cellY = opt.rect.top();
        const int cellH = opt.rect.height();
        // Center text vertically: offset = (cellHeight - fontHeight) / 2
        const int yOffset = ( cellH - fm.height() ) / 2;
        const int baseline = cellY + yOffset + fm.ascent();

        int x = textRect.left();

        int pos = 0;
        for ( const auto& match : cellMatches ) {
            const auto matchStart = static_cast<int>( match.startColumn().get() );
            const auto matchLen = static_cast<int>( match.size().get() );

            if ( matchStart < pos ) {
                continue;
            }

            if ( matchStart > pos ) {
                const auto before = cellText.mid( pos, matchStart - pos );
                painter->setPen( foreColor );
                painter->drawText( x, baseline, before );
                x += fm.horizontalAdvance( before );
            }

            const auto matchText = cellText.mid( matchStart, matchLen );
            const auto matchWidth = fm.horizontalAdvance( matchText );
            if ( match.backColor().isValid() ) {
                painter->fillRect( x, cellY, matchWidth, cellH, match.backColor() );
            }
            painter->setPen( match.foreColor().isValid() ? match.foreColor() : foreColor );
            painter->drawText( x, baseline, matchText );
            x += matchWidth;
            pos = matchStart + matchLen;
        }

        if ( pos < cellText.size() ) {
            const auto remaining = cellText.mid( pos );
            painter->setPen( foreColor );
            painter->drawText( x, baseline, remaining );
        }
    }

    LogFilteredData* filteredData_ = nullptr;
    std::shared_ptr<const RowMapping> rows_ = std::make_shared<OneRowPerLogLine>();
    std::shared_ptr<QuickFindPattern> quickFindPattern_;

    // The one module that builds the Line Decorator's Context; it holds the
    // Decoration Policy, the main search pattern and the Color Labels, and
    // caches the Highlighters built from them.
    DecorationSetup decorationSetup_;

    // Search Limits (set by LogTableView, mirroring what it hands the text
    // view); until then, none, so no row is subdued.
    SearchLimits searchLimits_;

    // Portion selection state (set by LogTableView from mouse events)
    int portionRow_ = -1;
    int portionCol_ = -1;
    int portionStartChar_ = 0;
    int portionEndChar_ = 0;

    // Hover row (set by LogTableView from mouse tracking)
    int hoverRow_ = -1;
};
