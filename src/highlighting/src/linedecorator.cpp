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

#include <algorithm>

QColor LineStatusColors::match()
{
    return QColor{ Qt::red };
}

QColor LineStatusColors::mark()
{
    return QColor{ "dodgerblue" };
}

QColor LineStatusColors::markedMatch()
{
    return QColor{ "violet" };
}

LinePalette LinePalette::fromPalette( const QPalette& palette )
{
    return LinePalette{
        palette.color( QPalette::Text ),
        palette.color( QPalette::Base ),
        palette.color( QPalette::Disabled, QPalette::Text ),
        palette.color( QPalette::HighlightedText ),
        palette.color( QPalette::Highlight ),
    };
}

LineVerdict LineDecorator::verdictFor( const LogLine& line, AbstractLogData::LineType lineType,
                                       bool isSelectedAsWhole ) const
{
    const bool isOutsideSearchLimits = !context_.searchLimits.contains( line.number() );

    std::optional<HighlightColor> wholeLineHighlight;
    logsquirl::vector<HighlightedMatch> highlighterSpans;
    if ( !isOutsideSearchLimits && !isSelectedAsWhole && !context_.highlighterSet.isEmpty() ) {
        HighlightedMatchRanges matches;
        const auto matchType = context_.highlighterSet.matchLine( line.text(), matches );
        if ( matchType == HighlighterMatchType::LineMatch ) {
            wholeLineHighlight
                = HighlightColor{ matches.front().foreColor(), matches.front().backColor() };
        }
        if ( matchType != HighlighterMatchType::NoMatch ) {
            // A word-only rule can be layered on top of a whole-line match
            // that a higher-priority rule already set (HighlighterSet::
            // matchLine resolves that by priority, not by picking one kind
            // over the other), so the full match set is kept here rather
            // than only the whole-line color.
            highlighterSpans = matches.matches();
        }
    }

    return LineVerdict{ wholeLineHighlight, lineType, isOutsideSearchLimits,
                        std::move( highlighterSpans ), isSelectedAsWhole };
}

HighlightColor LineDecorator::lineColorsFor( const LineVerdict& verdict ) const
{
    const auto& palette = context_.palette;
    HighlightColor colors{ palette.text, palette.base };

    if ( verdict.isSelectedAsWhole() ) {
        colors = HighlightColor{ palette.selectedText, palette.selection };
    }
    else if ( verdict.isOutsideSearchLimits() ) {
        colors.foreColor = palette.subduedText;
    }
    else if ( const auto wholeLine = verdict.wholeLineHighlight(); wholeLine.has_value() ) {
        colors = *wholeLine;
    }
    else if ( context_.lineStatus == LineStatusDisplay::AsBackground ) {
        if ( verdict.isMark() ) {
            colors.backColor
                = verdict.isMatch() ? LineStatusColors::markedMatch() : LineStatusColors::mark();
        }
        else if ( verdict.isMatch() ) {
            colors.backColor = LineStatusColors::match();
        }
    }

    if ( verdict.isContextLine() ) {
        colors.foreColor.setAlpha( 128 );
    }

    return colors;
}

Decoration LineDecorator::decorate( const QString& text, const LineVerdict& verdict,
                                    const std::optional<HighlightedMatch>& selection ) const
{
    HighlightedMatchRanges ranges;

    if ( !verdict.isOutsideSearchLimits() && !verdict.isSelectedAsWhole() ) {
        ranges.addMatches( verdict.highlighterSpans() );

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

    if ( selection.has_value() && !verdict.isSelectedAsWhole() ) {
        ranges.addMatch( *selection );
    }

    // Fill every gap between the sources with the line's own colors, so the
    // Decoration covers the whole text and no painter decides a color. A
    // source span reaching past the end of the text is cut at it, and a
    // source color left unset falls back to the line's own.
    const auto lineColors = lineColorsFor( verdict );
    const auto textEnd = static_cast<int>( text.size() );
    const auto& sourceSpans = ranges.matches();

    logsquirl::vector<HighlightedMatch> spans;
    if ( textEnd == 0 ) {
        return Decoration{ std::move( spans ), lineColors };
    }
    spans.reserve( 2 * sourceSpans.size() + 1 );

    int covered = 0;
    const auto addSpan = [ &spans, &covered ]( int start, int end, const QColor& foreColor,
                                               const QColor& backColor ) {
        spans.emplace_back( LineColumn{ start }, LineLength{ end - start }, foreColor, backColor );
        covered = end;
    };

    for ( const auto& source : sourceSpans ) {
        const auto start = std::max( static_cast<int>( source.startColumn().get() ), covered );
        const auto end = std::min(
            static_cast<int>( source.startColumn().get() + source.size().get() ), textEnd );
        if ( start >= end ) {
            continue;
        }
        if ( start > covered ) {
            addSpan( covered, start, lineColors.foreColor, lineColors.backColor );
        }
        addSpan( start, end,
                 source.foreColor().isValid() ? source.foreColor() : lineColors.foreColor,
                 source.backColor().isValid() ? source.backColor() : lineColors.backColor );
    }
    if ( covered < textEnd ) {
        addSpan( covered, textEnd, lineColors.foreColor, lineColors.backColor );
    }

    return Decoration{ std::move( spans ), lineColors };
}

Decoration Decoration::inDisplayColumns( QStringView rawText, LineLength displayLength ) &&
{
    if ( spans_.empty() || displayLength.get() == rawText.size() ) {
        // Nothing expanded: every raw column is its own display column.
        return std::move( *this );
    }

    // Only the last span can reach the end of the text, and its end is the
    // end of the displayed text; every other end is some later span's
    // start. So the columns are mapped only as far as the last span starts,
    // which on a long line with a few early colored words is not far.
    const auto lastStart = static_cast<qsizetype>( spans_.back().startColumn().get() );
    const auto rawToDisplay = rawToDisplayColumns( rawText.left( lastStart ) );
    const auto displayColumn = [ &rawToDisplay, lastStart, displayLength ]( qsizetype raw ) {
        return raw <= lastStart ? rawToDisplay[ static_cast<size_t>( raw ) ]
                                : static_cast<int>( displayLength.get() );
    };

    for ( auto& span : spans_ ) {
        const auto rawStart = static_cast<qsizetype>( span.startColumn().get() );
        const auto displayStart = displayColumn( rawStart );
        const auto displayEnd = displayColumn( rawStart + span.size().get() );
        span = HighlightedMatch{
            LineColumn{ type_safe::narrow_cast<LineColumn::UnderlyingType>( displayStart ) },
            LineLength{
                type_safe::narrow_cast<LineLength::UnderlyingType>( displayEnd - displayStart ) },
            span.foreColor(), span.backColor()
        };
    }

    return std::move( *this );
}

HighlightedMatch inRawColumns( QStringView rawText, const HighlightedMatch& displaySpan )
{
    const auto rawToDisplay = rawToDisplayColumns( rawText );
    const auto displayStart = static_cast<int>( displaySpan.startColumn().get() );
    const auto displayEnd = displayStart + static_cast<int>( displaySpan.size().get() );

    // The raw character whose display columns contain the start, and the
    // first raw column at or after the end.
    const auto startIt = std::upper_bound( rawToDisplay.begin(), rawToDisplay.end(), displayStart );
    const auto rawStart
        = std::max<std::ptrdiff_t>( 0, std::distance( rawToDisplay.begin(), startIt ) - 1 );
    const auto endIt = std::lower_bound( rawToDisplay.begin(), rawToDisplay.end(), displayEnd );
    const auto rawEnd
        = std::min<std::ptrdiff_t>( std::distance( rawToDisplay.begin(), endIt ), rawText.size() );

    return HighlightedMatch{ LineColumn{
                                 type_safe::narrow_cast<LineColumn::UnderlyingType>( rawStart ) },
                             LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
                                 std::max<std::ptrdiff_t>( 0, rawEnd - rawStart ) ) },
                             displaySpan.foreColor(), displaySpan.backColor() };
}
