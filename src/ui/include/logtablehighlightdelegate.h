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
#include <unordered_map>
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
    // Line -- a whole-line Highlighter, the Search Limits and whether the
    // Row is selected as a whole are facts about the whole line, not about
    // one field of it). A word-only Highlighter's matched span, main
    // search, Color Labels and QuickFind all match the cell's own text
    // directly, so no mapping from cell column back to raw line offset is
    // needed -- only the whole-line facts carry over from the row verdict.
    // This is the same composition rule paint() applies with its own live
    // Context; exposed here with an explicit Context so it can be tested
    // without touching the live Highlighter Set or Configuration.
    static Decoration decorationFor( const LineDecorator::Context& context,
                                     const LineVerdict& rowVerdict, LineNumber lineNumber,
                                     AbstractLogData::LineType lineType, const QString& cellText,
                                     const std::optional<HighlightedMatch>& selection
                                     = std::nullopt )
    {
        return decorationFor( LineDecorator{ context }, rowVerdict, lineNumber, lineType, cellText,
                              selection );
    }

    // The same, with a Line Decorator already built from the Context, so a
    // paint pass builds it once rather than once per cell.
    static Decoration decorationFor( const LineDecorator& lineDecorator,
                                     const LineVerdict& rowVerdict, LineNumber lineNumber,
                                     AbstractLogData::LineType lineType, const QString& cellText,
                                     const std::optional<HighlightedMatch>& selection
                                     = std::nullopt )
    {
        logsquirl::vector<HighlightedMatch> cellHighlighterSpans;
        if ( !cellText.isEmpty() && !rowVerdict.isOutsideSearchLimits()
             && !rowVerdict.isSelectedAsWhole() ) {
            cellHighlighterSpans
                = lineDecorator.verdictFor( LogLine{ lineNumber, cellText }, lineType )
                      .highlighterSpans();
        }
        const LineVerdict cellVerdict{ rowVerdict.wholeLineHighlight(), lineType,
                                       rowVerdict.isOutsideSearchLimits(),
                                       std::move( cellHighlighterSpans ),
                                       rowVerdict.isSelectedAsWhole() };
        return lineDecorator.decorate( cellText, cellVerdict, selection );
    }

    // While the object paintPass() returns lives, the cells painted belong to
    // one paint pass: the Line Decorator's Context is built once for all of
    // them and each Row's Line Verdict is decided once, from its raw Log
    // Line, however many of its cells are painted. LogTableView opens one
    // around each paint event. Nothing may change what a cell shows while a
    // pass is open. Outside a pass, each cell is painted on its own.
    class PaintPass {
    public:
        explicit PaintPass( const LogTableHighlightDelegate* delegate )
            : delegate_{ delegate }
        {
            delegate_->paintPass_.emplace();
        }

        ~PaintPass()
        {
            delegate_->paintPass_.reset();
        }

        PaintPass( const PaintPass& ) = delete;
        PaintPass& operator=( const PaintPass& ) = delete;

    private:
        const LogTableHighlightDelegate* delegate_;
    };

    [[nodiscard]] PaintPass paintPass() const
    {
        return PaintPass{ this };
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
        const bool isSelectedAsWhole = ( opt.state & QStyle::State_Selected ) && !hasPortionOnRow;

        // The Row's base: alternating Rows and the Row under the mouse
        // cursor are tinted. What the Row then shows over it is the Line
        // Decorator's decision.
        auto linePalette = LinePalette::fromPalette( opt.palette );
        if ( opt.features & QStyleOptionViewItem::Alternate ) {
            linePalette.base = linePalette.base.darker( 105 );
        }
        if ( hoverRow_ >= 0 && index.row() == hoverRow_ ) {
            linePalette.base = linePalette.base.darker( 108 );
        }

        // Outside a paint pass, this cell is a pass of its own.
        std::optional<PaintPassState> ownPass;
        auto& pass = paintPass_.has_value() ? *paintPass_ : ownPass.emplace();

        const auto& lineDecorator = decoratorFor( pass, linePalette );
        const auto& row = rowFor( pass, lineDecorator, index, isSelectedAsWhole );

        // The Decoration covers the cell's whole text; its line colours also
        // fill the rest of the cell, so a whole-line Highlighter, a Mark or
        // a Match colours the whole row, as this view has no gutter.
        const auto cellText = index.data( Qt::DisplayRole ).toString();
        const auto selectionSpan = selectionSpanFor( index, cellText, opt, hasPortionOnRow );
        const auto decoration = decorationFor( lineDecorator, row.verdict, row.lineNumber,
                                               row.lineType, cellText, selectionSpan );

        painter->fillRect( opt.rect, decoration.lineColors().backColor );
        paintDecoratedText( painter, opt, cellText, decoration );

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
    // What a paint pass decided once for all the cells it paints.
    struct PaintPassState {
        // A Row's Line Verdict, with what it was decided from.
        struct Row {
            LineNumber lineNumber;
            AbstractLogData::LineType lineType;
            bool isSelectedAsWhole = false;
            LineVerdict verdict;
        };

        // A Line Decorator for each Row base the pass met: plain,
        // alternating, under the mouse cursor. They differ in the palette
        // only.
        struct Decorator {
            LinePalette palette;
            LineDecorator lineDecorator;
        };

        std::optional<LineDecorator::Context> context;
        std::vector<Decorator> decorators;
        std::unordered_map<int, Row> rows;
    };

    static bool isSamePalette( const LinePalette& lhs, const LinePalette& rhs )
    {
        return lhs.text == rhs.text && lhs.base == rhs.base && lhs.subduedText == rhs.subduedText
               && lhs.selectedText == rhs.selectedText && lhs.selection == rhs.selection;
    }

    // The Line Decorator for a Row of the given palette. The Context the
    // Line Decorator matches every color source against is built by the one
    // module that builds it for either Presentation, once per pass. The
    // active Highlighter Set is read then, so that switching sets re-colors
    // the table without this delegate being told. The collection keeps that
    // set compiled, so every copy of it shares the compiled expression.
    const LineDecorator& decoratorFor( PaintPassState& pass, const LinePalette& linePalette ) const
    {
        for ( const auto& decorator : pass.decorators ) {
            if ( isSamePalette( decorator.palette, linePalette ) ) {
                return decorator.lineDecorator;
            }
        }

        if ( !pass.context.has_value() ) {
            pass.context = decorationSetup_.context(
                HighlighterSetCollection::get().currentActiveSet(), searchLimits_, linePalette,
                LineStatusDisplay::AsBackground );
        }
        auto context = *pass.context;
        context.palette = linePalette;
        pass.decorators.push_back( { linePalette, LineDecorator{ std::move( context ) } } );
        return pass.decorators.back().lineDecorator;
    }

    // The Row of a cell, its Line Verdict decided the first time the pass
    // meets it. The row-level Line Verdict is decided from the raw Log Line
    // -- a whole-line Highlighter, the Search Limits and the selection are
    // facts about the whole line, not about one field of it. The Line
    // Verdict does not depend on the palette, so any of the pass's Line
    // Decorators decides it.
    const PaintPassState::Row& rowFor( PaintPassState& pass, const LineDecorator& lineDecorator,
                                       const QModelIndex& index, bool isSelectedAsWhole ) const
    {
        const auto found = pass.rows.find( index.row() );
        if ( found != pass.rows.end() && found->second.isSelectedAsWhole == isSelectedAsWhole ) {
            return found->second;
        }

        const auto lineNumber = rows_->logLineAt( index.row() );
        const auto rawLine = index.data( LogFormatTableModel::RawLineRole ).toString();
        const auto lineType = filteredData_ ? filteredData_->lineTypeByLine( lineNumber )
                                            : AbstractLogData::LineTypeFlags::Plain;
        auto verdict = lineDecorator.verdictFor( LogLine{ lineNumber, rawLine }, lineType,
                                                 isSelectedAsWhole );
        return pass.rows
            .insert_or_assign( index.row(),
                               PaintPassState::Row{ lineNumber, lineType, isSelectedAsWhole,
                                                    std::move( verdict ) } )
            .first->second;
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

    // Paint a cell's text as its Decoration says. The spans cover the whole
    // text in order, so each is drawn as it is, one after the other.
    static void paintDecoratedText( QPainter* painter, const QStyleOptionViewItem& opt,
                                    const QString& cellText, const Decoration& decoration )
    {
        if ( decoration.spans().empty() ) {
            return;
        }

        const auto textRect
            = opt.rect.adjusted( HorizontalTextPadding, 0, -HorizontalTextPadding, 0 );
        const auto fm = painter->fontMetrics();
        const int cellY = opt.rect.top();
        const int cellH = opt.rect.height();
        // Center text vertically: offset = (cellHeight - fontHeight) / 2
        const int yOffset = ( cellH - fm.height() ) / 2;
        const int baseline = cellY + yOffset + fm.ascent();

        int x = textRect.left();
        for ( const auto& span : decoration.spans() ) {
            // A span covering the whole text shares it rather than copying.
            const auto spanText = cellText.mid( static_cast<qsizetype>( span.startColumn().get() ),
                                                static_cast<qsizetype>( span.size().get() ) );
            const auto spanWidth = fm.horizontalAdvance( spanText );
            if ( span.backColor() != decoration.lineColors().backColor ) {
                // The cell is already filled with the line colours.
                painter->fillRect( x, cellY, spanWidth, cellH, span.backColor() );
            }
            painter->setPen( span.foreColor() );
            painter->drawText( x, baseline, spanText );
            x += spanWidth;
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

    // The paint pass open, if any.
    mutable std::optional<PaintPassState> paintPass_;
};
