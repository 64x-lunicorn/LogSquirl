/*
 * Copyright (C) 2023 -- 2024 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <QString>
#include <algorithm>
#include <cstddef>
#include <qchar.h>
#include <qglobal.h>
#include <utility>

#include <QStringView>

#include "containers.h"
#include "linetypes.h"

// A line broken into the rows it occupies on screen.
//
// The wrapped rows are kept as offsets into the one QString this object owns,
// never as views into a buffer someone else owns. A WrappedString can therefore
// be copied, moved and outlive the string it was built from, and every view it
// hands out points into its own storage.
class WrappedString {
public:
    using WrappedStringPart = QStringView;

    static WrappedStringPart makeWrappedStringPart( const QString& lineText, LineColumn firstCol,
                                                    LineLength length )
    {
        return QStringView( lineText ).mid( firstCol.get(), length.get() );
    }

    explicit WrappedString( QString longLine, LineLength visibleColumns )
        : unwrappedLine_( std::move( longLine ) )
    {
        const auto columns = visibleColumns.get();

        if ( unwrappedLine_.isEmpty() ) {
            wrappedLines_.push_back( Fragment{ 0, 0 } );
            return;
        }

        WrappedStringPart lineToWrap( unwrappedLine_ );
        qsizetype consumed = 0;

        // A non-positive column count would never consume anything: treat the
        // whole line as one row rather than looping forever.
        while ( columns > 0 && lineToWrap.size() > columns ) {
            const WrappedStringPart stringToWrap = lineToWrap.left( columns );
            const auto lastSpaceIt = std::find_if( stringToWrap.rbegin(), stringToWrap.rend(),
                                                   []( QChar c ) { return c.isSpace(); } );

            const qsizetype taken = ( lastSpaceIt == stringToWrap.rend() )
                                        ? columns
                                        : std::distance( stringToWrap.begin(), lastSpaceIt.base() );

            wrappedLines_.push_back( Fragment{ consumed, taken } );
            consumed += taken;
            lineToWrap = lineToWrap.mid( taken );
        }

        if ( lineToWrap.size() > 0 ) {
            wrappedLines_.push_back( Fragment{ consumed, lineToWrap.size() } );
        }
    }

    size_t wrappedLinesCount() const
    {
        return wrappedLines_.size();
    }

    logsquirl::vector<WrappedStringPart> mid( LineColumn start, LineLength length ) const
    {
        logsquirl::vector<WrappedStringPart> resultChunks;
        if ( wrappedLines_.size() == 1 ) {
            const auto& wrappedLine = wrappedLines_.front();
            const auto len = std::min( length.get(), wrappedLine.length - start.get() );
            resultChunks.push_back( part( wrappedLine, start.get(), ( len > 0 ? len : 0 ) ) );
            return resultChunks;
        }

        size_t wrappedLineIndex = 0;
        auto positionInWrappedLine = start.get();
        while ( positionInWrappedLine > wrappedLines_[ wrappedLineIndex ].length ) {
            positionInWrappedLine -= wrappedLines_[ wrappedLineIndex ].length;
            wrappedLineIndex++;
            if ( wrappedLineIndex >= wrappedLines_.size() ) {
                return resultChunks;
            }
        }

        auto chunkLength = length.get();
        while ( positionInWrappedLine + chunkLength > wrappedLines_[ wrappedLineIndex ].length ) {
            resultChunks.push_back(
                part( wrappedLines_[ wrappedLineIndex ], positionInWrappedLine ) );
            wrappedLineIndex++;
            positionInWrappedLine = 0;
            chunkLength -= resultChunks.back().size();
            if ( wrappedLineIndex >= wrappedLines_.size() ) {
                return resultChunks;
            }
        }

        if ( chunkLength > 0 ) {
            const auto& wrappedLine = wrappedLines_[ wrappedLineIndex ];
            const auto len = std::min( chunkLength, wrappedLine.length - positionInWrappedLine );
            resultChunks.push_back(
                part( wrappedLine, positionInWrappedLine, ( len > 0 ? len : 0 ) ) );
        }

        return resultChunks;
    }

    bool isEmpty() const
    {
        return unwrappedLine_.isEmpty();
    }

    WrappedStringPart unwrappedLine() const
    {
        return WrappedStringPart{ unwrappedLine_ };
    }

    WrappedStringPart wrappedLine( size_t index ) const
    {
        const auto& fragment = wrappedLines_[ index ];
        return QStringView( unwrappedLine_ ).mid( fragment.start, fragment.length );
    }

    // Number of display columns of the wrapped row at index.
    qsizetype wrappedLineLength( size_t index ) const
    {
        return wrappedLines_[ index ].length;
    }

private:
    // A wrapped row, as a slice of unwrappedLine_.
    struct Fragment {
        qsizetype start = 0;
        qsizetype length = 0;
    };

    WrappedStringPart part( const Fragment& fragment, qsizetype offset ) const
    {
        return QStringView( unwrappedLine_ )
            .mid( fragment.start + offset, fragment.length - offset );
    }

    WrappedStringPart part( const Fragment& fragment, qsizetype offset, qsizetype length ) const
    {
        return QStringView( unwrappedLine_ ).mid( fragment.start + offset, length );
    }

    logsquirl::vector<Fragment> wrappedLines_;
    QString unwrappedLine_;
};
