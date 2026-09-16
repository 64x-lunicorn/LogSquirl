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

// The Log Files the text view checks of log_view_scrolling.h run on.
//
// Shown one column wide, every character of a Log Line is one Visual Line
// whatever font the platform picks, so every Scroll Position these lead to
// follows from the text alone. Nothing here spells words out: what sits on a
// row is asked of the view's Viewport layout, not read back as text.

#include <cstddef>
#include <cstdint>

#include <QLatin1Char>
#include <QString>
#include <QStringList>

#include "linetypes.h"

namespace logviewscrolling {

// A Log Line of TallLineVisualLines characters, between Log Lines of one.
constexpr uint64_t LinesBeforeTallLine = 10;
constexpr uint64_t TallLineVisualLines = 300;
constexpr uint64_t LinesAfterTallLine = 100;
inline const LineNumber TallLine{ LinesBeforeTallLine };

// A Log Line of TallLineVisualLines Visual Lines, far taller than the Viewport.
inline QString tallLine()
{
    return QString( static_cast<qsizetype>( TallLineVisualLines ), QLatin1Char( 'x' ) );
}

// Log Lines of one Visual Line around tall: LinesBeforeTallLine above it and
// LinesAfterTallLine below, the last of them one character.
inline QStringList logLinesAround( const QString& tall )
{
    QStringList lines;
    for ( uint64_t i = 0; i < LinesBeforeTallLine; ++i ) {
        lines << QStringLiteral( "a" );
    }
    lines << tall;
    for ( uint64_t i = 1; i < LinesAfterTallLine; ++i ) {
        lines << QStringLiteral( "b" );
    }
    lines << QStringLiteral( "z" );
    return lines;
}

// A Log File with a Log Line taller than the Viewport in the middle.
inline QStringList tallLogLines()
{
    return logLinesAround( tallLine() );
}

// A Log File whose last Log Line is taller than the Viewport.
inline QStringList tallLastLogLines()
{
    QStringList lines;
    for ( uint64_t i = 0; i < LinesBeforeTallLine; ++i ) {
        lines << QStringLiteral( "a" );
    }
    lines << tallLine();
    return lines;
}

// Fewer Log Lines than the Viewport has rows, one of them taller than the Viewport.
inline QStringList fewLogLinesOneTallerThanTheViewport()
{
    return QStringList{ QStringLiteral( "a" ), tallLine(), QStringLiteral( "z" ) };
}

// Four Visual Lines: fewer than any Viewport 200 px high has rows.
inline QStringList fewerVisualLinesThanRows()
{
    return QStringList{ QStringLiteral( "a" ), QStringLiteral( "bb" ), QStringLiteral( "z" ) };
}

// The text QuickFind searches for, and the Visual Line of the tall Log Line it
// starts on when the view is one column wide.
inline const QString FoundText = QStringLiteral( "found" );
constexpr size_t FoundVisualLine = 39;

// A Log File whose tall Log Line holds FoundText, starting at FoundVisualLine.
inline QStringList quickFindLogLines()
{
    return logLinesAround(
        QString( static_cast<qsizetype>( FoundVisualLine ), QLatin1Char( 'x' ) ) + FoundText
        + QString( static_cast<qsizetype>( TallLineVisualLines )
                       - static_cast<qsizetype>( FoundVisualLine ) - FoundText.size(),
                   QLatin1Char( 'x' ) ) );
}

} // namespace logviewscrolling
