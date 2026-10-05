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

using logsquirl::valuenames::Snap;

namespace {

// The character of a text drawn at a display column, given the display
// column each of its characters starts at (rawToDisplayColumns()): a column
// inside an expanded tab is the tab's, and one past the end of the text is
// its length.
qsizetype characterAtDisplayColumn( const logsquirl::vector<int>& displayColumns,
                                    int displayColumn )
{
    const auto next
        = std::upper_bound( displayColumns.begin(), displayColumns.end(), displayColumn );
    const auto character = std::distance( displayColumns.begin(), next ) - 1;
    return std::clamp<qsizetype>( character, 0,
                                  static_cast<qsizetype>( displayColumns.size() ) - 1 );
}

} // namespace

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

ShownColumns::ShownColumns( const QString& rawText, const logsquirl::valuenames::ShownLine& shown )
    : named_( true )
    , rawText_( rawText )
    , shown_( shown.hasNamedValues() ? shown : logsquirl::valuenames::ShownLine{ rawText, {} } )
{
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
        return untabify( readLine_() ).mid( portion.startColumn().get(), portion.size().get() );
    }

    const auto covered = covering( portion );
    const auto& rawColumns = rawDisplayColumns();
    // A Portion wholly past the end of the Log Line holds nothing of it.
    if ( covered.startColumn().get() >= rawColumns.back() ) {
        return {};
    }
    const auto startColumn = static_cast<int>( covered.startColumn().get() );
    const auto endColumn
        = std::min( static_cast<int>( covered.endColumn().get() ), rawColumns.back() - 1 );
    // An end on a Named Value lies on its edge, after covering(), and goes to
    // the edge of what it shows; one outside every Named Value keeps its place
    // inside its tab (#748).
    const auto start = shownColumn( LineColumn{ startColumn }, Snap::ToStart ).get();
    const auto end = shownColumn( LineColumn{ endColumn }, Snap::ToEnd ).get() + 1;
    return untabify( QString{ shown_.text() } ).mid( start, end - start );
}

std::optional<Portion> ShownColumns::namedValueAt( const FilePosition& position ) const
{
    if ( !shown_.hasNamedValues() ) {
        return std::nullopt;
    }
    const auto& columns = rawDisplayColumns();
    // A column past the end of the Log Line is on its end, which is on no
    // Named Value.
    const auto value = shown_.valueAtRaw(
        characterAtDisplayColumn( columns, static_cast<int>( position.column().get() ) ) );
    if ( value < 0 ) {
        return std::nullopt;
    }
    const auto& namedValue = shown_.namedValues()[ value ];
    return Portion{ position.line(),
                    LineColumn{ columns[ static_cast<size_t>( namedValue.start ) ] },
                    LineColumn{ columns[ static_cast<size_t>( namedValue.end() ) ] - 1 } };
}

LineColumn ShownColumns::shownColumn( LineColumn rawColumn, Snap snap ) const
{
    // On a Log Line without Named Values the text shown is the raw text.
    if ( !shown_.hasNamedValues() ) {
        return rawColumn;
    }
    const auto& rawColumns = rawDisplayColumns();
    const auto& shownColumns = shownDisplayColumns();
    const auto column = static_cast<int>( rawColumn.get() );
    if ( column >= rawColumns.back() ) {
        return LineColumn{ shownColumns.back() + column - rawColumns.back() };
    }

    const auto character = characterAtDisplayColumn( rawColumns, column );
    if ( shown_.valueAtRaw( character ) >= 0 ) {
        return snap == Snap::ToStart
                   ? LineColumn{ shownColumns[ static_cast<size_t>(
                         shown_.toShown( character, Snap::ToStart ) ) ] }
                   : LineColumn{ shownColumns[ static_cast<size_t>(
                                     shown_.toShown( character + 1, Snap::ToEnd ) ) ]
                                 - 1 };
    }
    // Converting to a character and back would put a column inside a tab on
    // the tab's edge (#747).
    const auto shownCharacter = static_cast<size_t>( shown_.toShown( character, Snap::ToStart ) );
    const auto start = shownColumns[ shownCharacter ];
    const auto width = shownColumns[ shownCharacter + 1 ] - start;
    return LineColumn{ start
                       + std::clamp( column - rawColumns[ static_cast<size_t>( character ) ], 0,
                                     std::max( width - 1, 0 ) ) };
}

LineColumn ShownColumns::rawColumn( LineColumn shownColumn, Snap snap ) const
{
    // On a Log Line without Named Values the text shown is the raw text.
    if ( !shown_.hasNamedValues() ) {
        return shownColumn;
    }
    const auto& rawColumns = rawDisplayColumns();
    const auto& shownColumns = shownDisplayColumns();
    const auto column = static_cast<int>( shownColumn.get() );
    if ( column >= shownColumns.back() ) {
        return LineColumn{ rawColumns.back() + column - shownColumns.back() };
    }

    const auto character = characterAtDisplayColumn( shownColumns, column );
    if ( shown_.valueAtShown( character ) >= 0 ) {
        return snap == Snap::ToStart ? LineColumn{ rawColumns[ static_cast<size_t>(
                                           shown_.toRaw( character, Snap::ToStart ) ) ] }
                                     : LineColumn{ rawColumns[ static_cast<size_t>( shown_.toRaw(
                                                       character + 1, Snap::ToEnd ) ) ]
                                                   - 1 };
    }
    // Converting to a character and back would put a column inside a tab on
    // the tab's edge (#747).
    const auto rawCharacter = static_cast<size_t>( shown_.toRaw( character, Snap::ToStart ) );
    const auto start = rawColumns[ rawCharacter ];
    const auto width = rawColumns[ rawCharacter + 1 ] - start;
    return LineColumn{ start
                       + std::clamp( column - shownColumns[ static_cast<size_t>( character ) ], 0,
                                     std::max( width - 1, 0 ) ) };
}

std::optional<logsquirl::valuenames::NamedValue>
ShownColumns::namedValueShownAt( LineColumn shownColumn ) const
{
    if ( !shown_.hasNamedValues() ) {
        return std::nullopt;
    }
    const auto& columns = shownDisplayColumns();
    const auto column = static_cast<int>( shownColumn.get() );
    // Past the end of the text shown is on no Named Value.
    if ( column >= columns.back() ) {
        return std::nullopt;
    }
    const auto value = shown_.valueAtShown( characterAtDisplayColumn( columns, column ) );
    if ( value < 0 ) {
        return std::nullopt;
    }
    return shown_.namedValues()[ value ];
}
