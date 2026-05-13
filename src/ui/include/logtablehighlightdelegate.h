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

#include "highlightedmatch.h"
#include "highlighterset.h"
#include "logfiltereddata.h"
#include "logformattablemodel.h"
#include "quickfindpattern.h"

#include <QPainter>
#include <QStyledItemDelegate>

#include <memory>
#include <vector>

// Delegate that applies highlighter-set and search-pattern coloring to table view cells.
// Also paints portion (in-cell text) selections and hover highlights.
class LogTableHighlightDelegate : public QStyledItemDelegate {
    Q_OBJECT

  public:
    explicit LogTableHighlightDelegate( QObject* parent = nullptr )
        : QStyledItemDelegate( parent )
    {
    }

    // Set the filtered data source for match/mark line type queries.
    void setFilteredData( LogFilteredData* data ) { filteredData_ = data; }

    // Set the quickfind pattern for incremental search highlighting.
    void setQuickFindPattern( std::shared_ptr<QuickFindPattern> pattern )
    {
        quickFindPattern_ = std::move( pattern );
    }

    // Set the color label words (one QStringList per color slot).
    void setColorLabelWords( const std::vector<QStringList>& words ) { colorLabelWords_ = words; }

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
    void setHoverRow( int row ) { hoverRow_ = row; }

    // Clear the hover row.
    void clearHoverRow() { hoverRow_ = -1; }

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

        // When a portion (in-cell text) selection is active on this row,
        // suppress the row-level selection highlight so the portion paint
        // remains visible.  The portion selection overlay uses the same
        // QPalette::Highlight colour, so it would be invisible if the whole
        // row were already painted with it.
        const bool hasPortionOnRow
            = ( portionRow_ >= 0 && index.row() == portionRow_ );
        const bool isSelected
            = ( opt.state & QStyle::State_Selected ) && !hasPortionOnRow;

        // --- Row-level background color ---
        auto backColor = opt.palette.color( QPalette::Base );
        auto foreColor = opt.palette.color( QPalette::Text );

        // When selected (and no portion selection on this row), skip all
        // expensive highlighter/match work — the selection colours override
        // everything and the user just wants the row to appear highlighted
        // instantly.
        if ( !isSelected ) {
            // Alternating row colour
            if ( opt.features & QStyleOptionViewItem::Alternate ) {
                backColor = backColor.darker( 105 );
            }
            // Subtle hover highlight for the row under the mouse cursor
            if ( hoverRow_ >= 0 && index.row() == hoverRow_ ) {
                backColor = backColor.darker( 108 );
            }
        }
        else {
            backColor = opt.palette.color( QPalette::Highlight );
            foreColor = opt.palette.color( QPalette::HighlightedText );
        }

        // Fill background
        painter->fillRect( opt.rect, backColor );

        // Get the cell text
        const auto cellText = index.data( Qt::DisplayRole ).toString();
        if ( cellText.isEmpty() ) {
            painter->restore();
            return;
        }

        // --- Cell-level highlighting (skip entirely for selected rows) ---
        if ( !isSelected ) {
            logsquirl::vector<HighlightedMatch> cellMatches;

            // Highlighter set matches on cell text (skip if no highlighters active)
            const auto& highlighterSet
                = HighlighterSetCollection::get().currentActiveSet();
            if ( !highlighterSet.isEmpty() ) {
                HighlightedMatchRanges highlighterRanges;
                highlighterSet.matchLine( cellText, highlighterRanges );
                for ( const auto& m : highlighterRanges.matches() ) {
                    cellMatches.push_back( m );
                }
            }

            // Quick highlighter (color label) matches
            const auto& quickHighlighters
                = HighlighterSetCollection::get().quickHighlighters();
            if ( !quickHighlighters.isEmpty() && !colorLabelWords_.empty() ) {
                for ( size_t i = 0; i < colorLabelWords_.size(); ++i ) {
                    if ( colorLabelWords_[ i ].isEmpty() ) {
                        continue;
                    }
                    const auto qhIndex = static_cast<int>( i );
                    if ( qhIndex >= quickHighlighters.size() ) {
                        break;
                    }
                    const auto& qh = quickHighlighters.at( qhIndex );
                    for ( const auto& word : colorLabelWords_[ i ] ) {
                        if ( word.isEmpty() ) {
                            continue;
                        }
                        Highlighter h(
                            word, false, true, qh.color.foreColor, qh.color.backColor );
                        h.setUseRegex( false );
                        logsquirl::vector<HighlightedMatch> qhMatches;
                        h.matchLine( cellText, qhMatches );
                        for ( auto& m : qhMatches ) {
                            cellMatches.push_back( m );
                        }
                    }
                }
            }

            // QuickFind pattern matches
            if ( quickFindPattern_ && quickFindPattern_->isActive() ) {
                logsquirl::vector<HighlightedMatch> qfMatches;
                quickFindPattern_->matchLine( cellText, qfMatches );
                for ( auto& m : qfMatches ) {
                    cellMatches.push_back( m );
                }
            }

            if ( !cellMatches.empty() ) {
                paintHighlightedText( painter, opt, cellText, cellMatches,
                                      foreColor );
                // Overlay portion selection on top of highlighted text
                paintPortionSelection( painter, opt, cellText, index );
                painter->restore();
                return;
            }
        }

        // Simple case: no highlights or selected — draw full text
        const auto textRect = opt.rect.adjusted( 4, 0, -4, 0 );
        const auto fm = painter->fontMetrics();
        // Center text vertically: offset = (cellHeight - fontHeight) / 2
        const int yOffset = ( opt.rect.height() - fm.height() ) / 2;
        const int baseline = opt.rect.top() + yOffset + fm.ascent();
        painter->setPen( foreColor );
        painter->drawText( textRect.left(), baseline, cellText );

        // Overlay portion selection on top of plain text
        paintPortionSelection( painter, opt, cellText, index );

        painter->restore();
    }

    // Return a size hint that accounts for the full text width (no clipping).
    QSize sizeHint( const QStyleOptionViewItem& option,
                    const QModelIndex& index ) const override
    {
        auto hint = QStyledItemDelegate::sizeHint( option, index );
        const auto cellText = index.data( Qt::DisplayRole ).toString();
        if ( !cellText.isEmpty() ) {
            const auto fm = option.fontMetrics;
            // 8 = 4px padding on each side
            hint.setWidth( fm.horizontalAdvance( cellText ) + 8 );
        }
        return hint;
    }

  private:
    // Paint cell text with highlight segments.
    static void paintHighlightedText( QPainter* painter,
                                      const QStyleOptionViewItem& opt,
                                      const QString& cellText,
                                      logsquirl::vector<HighlightedMatch>& cellMatches,
                                      const QColor& foreColor )
    {
        const auto textRect = opt.rect.adjusted( 4, 0, -4, 0 );
        const auto fm = painter->fontMetrics();
        const int cellY = opt.rect.top();
        const int cellH = opt.rect.height();
        // Center text vertically: offset = (cellHeight - fontHeight) / 2
        const int yOffset = ( cellH - fm.height() ) / 2;
        const int baseline = cellY + yOffset + fm.ascent();

        int x = textRect.left();

        std::sort( cellMatches.begin(), cellMatches.end(),
                   []( const auto& a, const auto& b ) {
                       return a.startColumn() < b.startColumn();
                   } );

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

    // Portion selection state (set by CrawlerWidget from mouse events)
    int portionRow_ = -1;
    int portionCol_ = -1;
    int portionStartChar_ = 0;
    int portionEndChar_ = 0;

    // Hover row (set by CrawlerWidget from mouse tracking)
    int hoverRow_ = -1;

    // Paint the portion (in-cell text) selection highlight for a cell.
    void paintPortionSelection( QPainter* painter,
                                const QStyleOptionViewItem& opt,
                                const QString& cellText,
                                const QModelIndex& index ) const
    {
        if ( portionRow_ < 0 || index.row() != portionRow_
             || index.column() != portionCol_ ) {
            return;
        }
        if ( portionStartChar_ >= portionEndChar_ ) {
            return;
        }

        const int lo = std::min( portionStartChar_, static_cast<int>( cellText.size() ) );
        const int hi = std::min( portionEndChar_, static_cast<int>( cellText.size() ) );
        if ( lo >= hi ) {
            return;
        }

        const auto fm = painter->fontMetrics();
        const auto textRect = opt.rect.adjusted( 4, 0, -4, 0 );

        // Pixel offset of the selection start and end within the cell
        const int xStart = textRect.left() + fm.horizontalAdvance( cellText.left( lo ) );
        const int xEnd = textRect.left() + fm.horizontalAdvance( cellText.left( hi ) );

        // Draw selection rectangle with system highlight color
        const auto highlightColor = opt.palette.color( QPalette::Highlight );
        const auto highlightTextColor = opt.palette.color( QPalette::HighlightedText );

        painter->fillRect( xStart, opt.rect.top(), xEnd - xStart, opt.rect.height(),
                           highlightColor );

        // Re-draw the selected text on top with the highlight text color
        const int cellH = opt.rect.height();
        const int yOffset = ( cellH - fm.height() ) / 2;
        const int baseline = opt.rect.top() + yOffset + fm.ascent();

        painter->setPen( highlightTextColor );
        painter->setClipRect( xStart, opt.rect.top(), xEnd - xStart, cellH );
        painter->drawText( textRect.left(), baseline, cellText );
        painter->setClipping( false );
    }
};
