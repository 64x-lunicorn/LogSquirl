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

#include "configuration.h"
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

    // Set the quickfind pattern for incremental search highlighting.
    void setQuickFindPattern( std::shared_ptr<QuickFindPattern> pattern )
    {
        quickFindPattern_ = std::move( pattern );
    }

    // Set the color label words (one QStringList per color slot).
    void setColorLabelWords( const std::vector<QStringList>& words )
    {
        colorLabelWords_ = words;
        rebuildColorLabelHighlighters();
    }

    // Set the main search pattern for main-search highlighting.
    void setSearchPattern( const RegularExpressionPattern& pattern )
    {
        searchPattern_ = pattern;
        rebuildMainSearchHighlighter();
    }

    // Rebuild the cached main-search Highlighter's colour/config-derived
    // properties from the current Configuration, without changing the
    // pattern itself -- call after Configuration changes (paint() does not
    // re-read Configuration on every cell the way AbstractLogView's
    // per-repaint construction does, so this is its equivalent hook).
    void refreshMainSearchHighlighter()
    {
        rebuildMainSearchHighlighter();
    }

    // Set the Search Limits: rows outside this range are shown subdued.
    void setSearchLimits( LineNumber startLine, LineNumber endLine )
    {
        searchStart_ = startLine;
        searchEnd_ = endLine;
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
            backColor = rowVerdict.isMatch() ? markedMatchRowColor() : markRowColor();
        }
        else if ( rowVerdict.isMatch() ) {
            backColor = matchRowColor();
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
        const auto lineNumber = LineNumber( static_cast<uint64_t>( index.row() ) );
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
    // Row background colours for Mark/Match, consistent with the text
    // view's gutter bullet colours (see AbstractLogView::drawing, where
    // matchBulletBrush/markBrush/markedMatchBrush are the same colours).
    static QColor matchRowColor()
    {
        return QColor{ Qt::red };
    }
    static QColor markRowColor()
    {
        return QColor{ "dodgerblue" };
    }
    static QColor markedMatchRowColor()
    {
        return QColor{ "violet" };
    }

    // Rebuild the cached main-search Highlighter (used by
    // buildDecoratorContext() below) from the current pattern and
    // Configuration. Highlighter compiles its regex lazily on first match
    // and caches the compiled form on the instance, so keeping this
    // Highlighter alive across repaints -- rebuilding it only when the
    // pattern or Configuration actually changes, not on every cell --
    // is what avoids recompiling it on every cell paint() draws.
    void rebuildMainSearchHighlighter()
    {
        cachedMainSearch_.reset();
        if ( Configuration::get().mainSearchHighlight() && !searchPattern_.isBoolean
             && !searchPattern_.isExclude && !searchPattern_.pattern.isEmpty() ) {
            cachedMainSearch_ = Highlighter{};
            cachedMainSearch_->setHighlightOnlyMatch( true );
            cachedMainSearch_->setVariateColors(
                Configuration::get().variateMainSearchHighlight() );
            cachedMainSearch_->setPattern( searchPattern_.pattern );
            cachedMainSearch_->setIgnoreCase( !searchPattern_.isCaseSensitive );
            cachedMainSearch_->setUseRegex( !searchPattern_.isPlainText );
            cachedMainSearch_->setBackColor( Configuration::get().mainSearchBackColor() );
            cachedMainSearch_->setForeColor( Qt::black );
        }
    }

    // Rebuild the cached Color Label Highlighters (see rebuildMainSearchHighlighter()
    // for why this is cached rather than rebuilt on every cell paint()).
    void rebuildColorLabelHighlighters()
    {
        cachedColorLabels_.clear();
        const auto quickHighlighters = HighlighterSetCollection::get().quickHighlighters();
        for ( size_t i = 0;
              i < colorLabelWords_.size() && static_cast<int>( i ) < quickHighlighters.size();
              ++i ) {
            if ( colorLabelWords_[ i ].isEmpty() ) {
                continue;
            }
            const auto& qh = quickHighlighters.at( static_cast<int>( i ) );
            for ( const auto& word : colorLabelWords_[ i ] ) {
                if ( word.isEmpty() ) {
                    continue;
                }
                Highlighter h( word, false, true, qh.color.foreColor, qh.color.backColor );
                h.setUseRegex( false );
                cachedColorLabels_.push_back( std::move( h ) );
            }
        }
    }

    // Build the stable context the Line Decorator matches every colour
    // source against: the active Highlighter Set, the main search pattern,
    // Color Labels, QuickFind and the Search Limits, matching how
    // AbstractLogView builds the same context for the text view. The main
    // search Highlighter and Color Label Highlighters are cached (see
    // rebuildMainSearchHighlighter()/rebuildColorLabelHighlighters()) since
    // paint() calls this once per cell.
    LineDecorator::Context buildDecoratorContext() const
    {
        return LineDecorator::Context{
            HighlighterSetCollection::get().currentActiveSet(),
            cachedMainSearch_,
            cachedColorLabels_,
            quickFindPattern_ ? quickFindPattern_->getMatcher() : QuickFindMatcher{},
            Configuration::get().qfBackColor(),
            SearchLimits{ searchStart_, searchEnd_ },
        };
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
    std::shared_ptr<QuickFindPattern> quickFindPattern_;
    std::vector<QStringList> colorLabelWords_;

    // Main search pattern and Search Limits (set by CrawlerWidget, mirroring
    // what it hands the text view)
    RegularExpressionPattern searchPattern_;
    LineNumber searchStart_{ 0_lnum };
    LineNumber searchEnd_{ 0_lnum };

    // Cached Highlighters built from searchPattern_/colorLabelWords_ (see
    // rebuildMainSearchHighlighter()/rebuildColorLabelHighlighters())
    std::optional<Highlighter> cachedMainSearch_;
    logsquirl::vector<Highlighter> cachedColorLabels_;

    // Portion selection state (set by CrawlerWidget from mouse events)
    int portionRow_ = -1;
    int portionCol_ = -1;
    int portionStartChar_ = 0;
    int portionEndChar_ = 0;

    // Hover row (set by CrawlerWidget from mouse tracking)
    int hoverRow_ = -1;
};
