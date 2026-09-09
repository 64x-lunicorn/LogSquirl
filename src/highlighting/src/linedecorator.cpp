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

// This file implements LineDecorator.

#include "linedecorator.h"

LineVerdict LineDecorator::verdictFor( const LogLine& line,
                                       AbstractLogData::LineType lineType ) const
{
    const bool isOutsideSearchLimits = !context_.searchLimits.contains( line.number() );

    std::optional<HighlightColor> wholeLineHighlight;
    if ( !isOutsideSearchLimits && !context_.highlighterSet.isEmpty() ) {
        HighlightedMatchRanges matches;
        const auto matchType = context_.highlighterSet.matchLine( line.text(), matches );
        if ( matchType == HighlighterMatchType::LineMatch ) {
            wholeLineHighlight
                = HighlightColor{ matches.front().foreColor(), matches.front().backColor() };
        }
    }

    return LineVerdict{ wholeLineHighlight, lineType, isOutsideSearchLimits };
}

Decoration LineDecorator::decorate( const QString& text, const LineVerdict& verdict,
                                    const std::optional<HighlightedMatch>& selection ) const
{
    HighlightedMatchRanges ranges;

    if ( !verdict.isOutsideSearchLimits() ) {
        if ( const auto wholeLine = verdict.wholeLineHighlight(); wholeLine.has_value() ) {
            ranges.addMatch( HighlightedMatch{ 0_lcol, LineLength{ text.size() },
                                               wholeLine->foreColor, wholeLine->backColor } );
        }

        if ( context_.mainSearch.has_value() ) {
            logsquirl::vector<HighlightedMatch> matches;
            context_.mainSearch->matchLine( text, matches );
            ranges.addMatches( matches );
        }

        for ( const auto& colorLabel : context_.colorLabels ) {
            logsquirl::vector<HighlightedMatch> matches;
            colorLabel.matchLine( text, matches );
            ranges.addMatches( matches );
        }
    }

    {
        logsquirl::vector<HighlightedMatch> quickFindMatches;
        context_.quickFind.matchLine( text, quickFindMatches, context_.quickFindColor );
        ranges.addMatches( quickFindMatches );
    }

    if ( selection.has_value() ) {
        ranges.addMatch( *selection );
    }

    return Decoration{ ranges.matches() };
}
