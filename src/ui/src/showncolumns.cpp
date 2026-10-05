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

#include "showncolumns.h"

#include <algorithm>
#include <iterator>

#include "linetypes.h"

qsizetype characterAtDisplayColumn( const logsquirl::vector<int>& displayColumns,
                                    int displayColumn )
{
    const auto next
        = std::upper_bound( displayColumns.begin(), displayColumns.end(), displayColumn );
    const auto character = std::distance( displayColumns.begin(), next ) - 1;
    return std::clamp<qsizetype>( character, 0,
                                  static_cast<qsizetype>( displayColumns.size() ) - 1 );
}

ShownColumns::ShownColumns( const logsquirl::valuenames::ValueNamer* namer,
                            const std::function<QString()>& readLine )
{
    if ( namer == nullptr ) {
        readLine_ = readLine;
        return;
    }
    named_ = true;
    rawText_ = readLine();
    shown_ = logsquirl::valuenames::ShownLine{ rawText_, namer->namedValues( rawText_ ) };
}

const logsquirl::vector<int>& ShownColumns::rawDisplayColumns() const
{
    if ( !rawDisplayColumns_.has_value() ) {
        rawDisplayColumns_ = rawToDisplayColumns( rawText_ );
    }
    return *rawDisplayColumns_;
}

const logsquirl::vector<int>& ShownColumns::shownDisplayColumns() const
{
    if ( !shownDisplayColumns_.has_value() ) {
        shownDisplayColumns_ = rawToDisplayColumns( shown_.text() );
    }
    return *shownDisplayColumns_;
}

Portion ShownColumns::covering( const Portion& portion ) const
{
    if ( !portion.isValid() || !shown_.hasNamedValues() ) {
        return portion;
    }

    const auto& columns = rawDisplayColumns();
    // A Portion wholly past the end of the Log Line takes part of nothing on
    // it (#746).
    if ( portion.startColumn().get() >= columns.back() ) {
        return portion;
    }
    // The characters of the Portion, kept on the Log Line however far past its
    // end it reaches. A Log Line with a Named Value has at least one.
    const auto rawLength = static_cast<qsizetype>( columns.size() ) - 1;
    const auto last = std::min(
        characterAtDisplayColumn( columns, static_cast<int>( portion.endColumn().get() ) ),
        rawLength - 1 );
    const auto first = std::min(
        characterAtDisplayColumn( columns, static_cast<int>( portion.startColumn().get() ) ),
        last );
    const auto [ start, end ] = shown_.wholeRawRange( first, last + 1 );
    // Only an end on a Named Value moves, to the edge of it: one inside a tab
    // beside it stays where it is (#747).
    const auto startColumn = shown_.valueAtRaw( first ) >= 0
                                 ? LineColumn{ columns[ static_cast<size_t>( start ) ] }
                                 : portion.startColumn();
    const auto endColumn = shown_.valueAtRaw( last ) >= 0
                               ? LineColumn{ columns[ static_cast<size_t>( end ) ] - 1 }
                               : std::min( portion.endColumn(), LineColumn{ columns.back() - 1 } );
    return Portion{ portion.line(), startColumn, endColumn };
}

QString ShownColumns::textShown( const Portion& portion ) const
{
    if ( !portion.isValid() ) {
        return {};
    }
    if ( !named_ ) {
        if ( !readLine_ ) {
            return {};
        }
        return untabify( readLine_() )
            .mid( portion.startColumn().get(), portion.size().get() );
    }

    const auto covered = covering( portion );
    const auto& rawColumns = rawDisplayColumns();
    // A Portion wholly past the end of the Log Line holds nothing of it.
    if ( covered.startColumn().get() >= rawColumns.back() ) {
        return {};
    }
    const auto startColumn = static_cast<int>( covered.startColumn().get() );
    const auto endColumn = std::min( static_cast<int>( covered.endColumn().get() ),
                                     rawColumns.back() - 1 );
    const auto& shownColumns = shownDisplayColumns();

    // Each end is moved to its character of the raw Log Line and to that of
    // the text shown. An end on a Named Value lies on its edge, after
    // covering(); one outside every Named Value keeps its place inside its
    // character -- a tab -- cut to the width that character has in the text
    // shown, where a tab after a Named Value is narrower or wider (#748).
    const auto first = characterAtDisplayColumn( rawColumns, startColumn );
    const auto shownFirst = shown_.toShown( first, logsquirl::valuenames::Snap::ToStart );
    auto start = shownColumns[ static_cast<size_t>( shownFirst ) ];
    if ( shown_.valueAtRaw( first ) < 0 ) {
        const auto width = shownColumns[ static_cast<size_t>( shownFirst ) + 1 ] - start;
        start += std::clamp( startColumn - rawColumns[ static_cast<size_t>( first ) ], 0,
                            std::max( width - 1, 0 ) );
    }

    const auto last = characterAtDisplayColumn( rawColumns, endColumn );
    qsizetype end = 0;
    if ( shown_.valueAtRaw( last ) < 0 ) {
        const auto shownLast = shown_.toShown( last, logsquirl::valuenames::Snap::ToStart );
        const auto lastStart = shownColumns[ static_cast<size_t>( shownLast ) ];
        const auto width = shownColumns[ static_cast<size_t>( shownLast ) + 1 ] - lastStart;
        end = lastStart
              + std::min( endColumn - rawColumns[ static_cast<size_t>( last ) ] + 1, width );
    }
    else {
        end = shownColumns[ static_cast<size_t>(
            shown_.toShown( last + 1, logsquirl::valuenames::Snap::ToEnd ) ) ];
    }
    return untabify( QString{ shown_.text() } ).mid( start, end - start );
}
