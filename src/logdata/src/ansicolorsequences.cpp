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

#include "ansicolorsequences.h"

#include <QChar>
#include <QRegularExpression>
#include <QString>

namespace {

constexpr char16_t Escape = u'\x1B';

// Every ANSI color sequence starts with the escape character.
constexpr char AnsiColorSequencePattern[] = "\\x1B\\[([0-9]{1,4}((;|:)[0-9]{1,3})*)?[mK]";

// Built on first use; C++ guarantees the initialisation runs once even when
// several threads get here at the same time. optimize() compiles (and JITs)
// the pattern right away, so no match races to compile it later: Qt compiles
// under the expression's own mutex, and every match after that only reads the
// compiled pattern, with a JIT stack per thread.
const QRegularExpression& ansiColorSequences()
{
    static const QRegularExpression pattern = [] {
        QRegularExpression compiled( QLatin1StringView( AnsiColorSequencePattern ),
                                     QRegularExpression::CaseInsensitiveOption );
        compiled.optimize();
        return compiled;
    }();
    return pattern;
}

} // namespace

void removeAnsiColorSequences( QString& text )
{
    if ( !text.contains( QChar( Escape ) ) ) {
        return;
    }
    text.remove( ansiColorSequences() );
}
