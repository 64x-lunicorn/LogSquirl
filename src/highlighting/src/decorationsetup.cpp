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

#include "decorationsetup.h"

#include <algorithm>

void DecorationSetup::setPolicy( const DecorationPolicy& policy )
{
    policy_ = policy;
    rebuildMainSearch();
}

void DecorationSetup::setSearchPattern( const RegularExpressionPattern& pattern )
{
    searchPattern_ = pattern;
    rebuildMainSearch();
}

void DecorationSetup::setColorLabels( const std::vector<QStringList>& words,
                                      const std::vector<HighlightColor>& colors )
{
    colorLabelWords_ = words;
    colorLabelColors_ = colors;
    rebuildColorLabels();
}

void DecorationSetup::setQuickFindPattern( const QuickFindPattern* pattern )
{
    quickFindPattern_ = pattern;
}

void DecorationSetup::rebuildMainSearch()
{
    cachedMainSearch_.reset();

    // A boolean or excluding pattern selects Log Lines, it does not point at
    // the text that made them match, so there is nothing in a line to color.
    if ( !policy_.mainSearchHighlight || searchPattern_.isBoolean || searchPattern_.isExclude
         || searchPattern_.pattern.isEmpty() ) {
        return;
    }

    cachedMainSearch_ = Highlighter{};
    cachedMainSearch_->setHighlightOnlyMatch( true );
    cachedMainSearch_->setVariateColors( policy_.variateMainSearchHighlight );
    cachedMainSearch_->setPattern( searchPattern_.pattern );
    cachedMainSearch_->setIgnoreCase( !searchPattern_.isCaseSensitive );
    cachedMainSearch_->setUseRegex( !searchPattern_.isPlainText );
    cachedMainSearch_->setBackColor( policy_.mainSearchBackColor );
    cachedMainSearch_->setForeColor( Qt::black );
}

void DecorationSetup::rebuildColorLabels()
{
    cachedColorLabels_.clear();

    // Only the slots that have both a color and at least one word produce a
    // Highlighter; a slot whose color the caller did not supply is skipped
    // rather than painted in some fallback color.
    const auto slots = std::min( colorLabelWords_.size(), colorLabelColors_.size() );
    for ( size_t slot = 0; slot < slots; ++slot ) {
        const auto& color = colorLabelColors_[ slot ];
        for ( const auto& word : colorLabelWords_[ slot ] ) {
            if ( word.isEmpty() ) {
                continue;
            }

            Highlighter highlighter{ word, false, true, color.foreColor, color.backColor };
            highlighter.setUseRegex( false );
            cachedColorLabels_.push_back( std::move( highlighter ) );
        }
    }
}

LineDecorator::Context DecorationSetup::context( const HighlighterSet& highlighterSet,
                                                 SearchLimits searchLimits,
                                                 const LinePalette& palette,
                                                 LineStatusDisplay lineStatus ) const
{
    return LineDecorator::Context{
        highlighterSet,
        cachedMainSearch_,
        cachedColorLabels_,
        quickFindPattern_ ? quickFindPattern_->getMatcher() : QuickFindMatcher{},
        policy_.quickFindBackColor,
        searchLimits,
        palette,
        lineStatus,
    };
}
