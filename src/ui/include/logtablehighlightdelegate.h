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
#include "logformattablemodel.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "tableviewstate.h"

#include <QPainter>
#include <QStyledItemDelegate>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Delegate that applies highlighter-set and search-pattern coloring to table view cells.
// Also paints portion (in-cell text) selections and hover highlights.
//
// What it paints beyond the Highlighter Set, the main search pattern and the
// Decoration Policy -- the current Search, the Search Limits, the Color
// Labels, the QuickFind pattern, the Row under the mouse cursor and the
// in-cell selection -- it reads from the Table View's state when it paints,
// and is never told of (#561).
class LogTableHighlightDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    // Horizontal padding applied on each side of a cell's text, both when
    // painting it and when hit-testing a click against it. The two must
    // agree, so both read this single constant.
    static constexpr int HorizontalTextPadding = 4;

    // Paints from state, which the Table View holds and changes, and which
    // must outlive the delegate; each Row shows the Log Line rows maps it onto.
    LogTableHighlightDelegate( const TableViewState& state, std::shared_ptr<const RowMapping> rows,
                               QObject* parent = nullptr )
        : QStyledItemDelegate( parent )
        , state_( state )
        , rows_( std::move( rows ) )
    {
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
        // over the row-level Qt selection state: it is the one the Table View
        // holds, and it needs the rest of the row to keep showing Highlighter
        // colour around it.
        const auto inCell = state_.selection.inCell();
        const bool hasPortionOnRow = inCell.has_value() && index.row() == inCell->row;
        const bool isSelectedAsWhole = ( opt.state & QStyle::State_Selected ) && !hasPortionOnRow;

        // The Row's base: alternating Rows and the Row under the mouse
        // cursor are tinted. What the Row then shows over it is the Line
        // Decorator's decision.
        auto linePalette = LinePalette::fromPalette( opt.palette );
        if ( opt.features & QStyleOptionViewItem::Alternate ) {
            linePalette.base = linePalette.base.darker( 105 );
        }
        if ( state_.hoverRow >= 0 && index.row() == state_.hoverRow ) {
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
        const auto selectionSpan
            = hasPortionOnRow ? selectionSpanFor( *inCell, index, cellText, opt ) : std::nullopt;
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

        // The first caret whose prefix is wider than the click, found by
        // bisecting: prefix advances only grow, so a long cell measures a
        // handful of prefixes rather than every one of them.
        const int textLen = static_cast<int>( cellText.size() );
        const auto prefixAdvance
            = [ & ]( int length ) { return fm.horizontalAdvance( cellText.left( length ) ); };
        int low = 1;
        int high = textLen + 1;
        while ( low < high ) {
            const int middle = low + ( high - low ) / 2;
            if ( relativeX < prefixAdvance( middle ) ) {
                high = middle;
            }
            else {
                low = middle + 1;
            }
        }
        if ( low > textLen ) {
            return textLen;
        }

        // Before or after this character, whichever edge is closer
        const int charRight = prefixAdvance( low );
        const int charLeft = prefixAdvance( low - 1 );
        return ( relativeX - charLeft < charRight - relativeX ) ? low - 1 : low;
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
    // active Highlighter Set, the Table View's state and the Theme's colors
    // of the Color Labels are read then, so that switching sets or Themes
    // re-colors the table without this delegate being told. The collection
    // keeps that set compiled, so every copy of it shares the compiled
    // expression.
    const LineDecorator& decoratorFor( PaintPassState& pass, const LinePalette& linePalette ) const
    {
        for ( const auto& decorator : pass.decorators ) {
            if ( isSamePalette( decorator.palette, linePalette ) ) {
                return decorator.lineDecorator;
            }
        }

        if ( !pass.context.has_value() ) {
            takeColorLabels();
            decorationSetup_.setQuickFindPattern( state_.quickFindPattern.get() );
            pass.context = decorationSetup_.context(
                HighlighterSetCollection::get().currentActiveSet(), state_.searchLimits(),
                linePalette, LineStatusDisplay::AsBackground );
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
        const auto lineType = rows_->lineType( state_.currentSearch, lineNumber );
        auto verdict = lineDecorator.verdictFor( LogLine{ lineNumber, rawLine }, lineType,
                                                 isSelectedAsWhole );
        return pass.rows
            .insert_or_assign( index.row(),
                               PaintPassState::Row{ lineNumber, lineType, isSelectedAsWhole,
                                                    std::move( verdict ) } )
            .first->second;
    }

    // The Color Labels as the Table View's state and the Theme have them now.
    // The Decoration Setup rebuilds its Highlighters when handed them, so it
    // is handed them only when they changed.
    void takeColorLabels() const
    {
        auto colors = colorLabelColors();
        const auto sameColors = std::equal(
            colors.begin(), colors.end(), colorLabelColors_.begin(), colorLabelColors_.end(),
            []( const HighlightColor& lhs, const HighlightColor& rhs ) {
                return lhs.foreColor == rhs.foreColor && lhs.backColor == rhs.backColor;
            } );
        if ( sameColors && state_.colorLabelWords == colorLabelWords_ ) {
            return;
        }
        colorLabelWords_ = state_.colorLabelWords;
        colorLabelColors_ = std::move( colors );
        decorationSetup_.setColorLabels( colorLabelWords_, colorLabelColors_ );
    }

    // The portion (in-cell text) selection decorate() should overlay on
    // top of everything else for this cell -- the dragged range within
    // this specific cell, already in the cell text's own coordinate space,
    // so it needs no translating. The row-level Qt selection is handled
    // separately in paint() before this is ever reached.
    static std::optional<HighlightedMatch>
    selectionSpanFor( const TableViewSelection::InCell& inCell, const QModelIndex& index,
                      const QString& cellText, const QStyleOptionViewItem& opt )
    {
        const auto startChar = std::min( inCell.startChar, inCell.endChar );
        const auto endChar = std::max( inCell.startChar, inCell.endChar );
        if ( index.column() != inCell.column || startChar >= endChar ) {
            return std::nullopt;
        }

        const auto lo = std::min( startChar, static_cast<int>( cellText.size() ) );
        const auto hi = std::min( endChar, static_cast<int>( cellText.size() ) );
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
        const auto& spans = decoration.spans();
        for ( auto span = spans.begin(); span != spans.end(); ++span ) {
            // A span covering the whole text shares it rather than copying.
            const auto spanText = cellText.mid( static_cast<qsizetype>( span->startColumn().get() ),
                                                static_cast<qsizetype>( span->size().get() ) );
            // Measuring a span shapes its text as much as drawing it does
            // (#462), so it is measured only when something needs its width:
            // a background of its own, or a span after it. Most cells are one
            // span on the line colours, and are not measured at all.
            const bool hasOwnBackground = span->backColor() != decoration.lineColors().backColor;
            const bool isLast = std::next( span ) == spans.end();
            const auto spanWidth
                = ( hasOwnBackground || !isLast ) ? fm.horizontalAdvance( spanText ) : 0;
            if ( hasOwnBackground ) {
                // The cell is already filled with the line colours.
                painter->fillRect( x, cellY, spanWidth, cellH, span->backColor() );
            }
            painter->setPen( span->foreColor() );
            painter->drawText( x, baseline, spanText );
            x += spanWidth;
        }
    }

    // The Table View's: read, never copied.
    const TableViewState& state_;
    std::shared_ptr<const RowMapping> rows_;

    // The one module that builds the Line Decorator's Context; it holds the
    // Decoration Policy, the main search pattern and the Color Labels, and
    // caches the Highlighters built from them. Brought up to date with the
    // state when a paint pass begins.
    mutable DecorationSetup decorationSetup_;
    // The Color Labels the Decoration Setup was last handed.
    mutable std::vector<QStringList> colorLabelWords_;
    mutable std::vector<HighlightColor> colorLabelColors_;

    // The paint pass open, if any.
    mutable std::optional<PaintPassState> paintPass_;
};
