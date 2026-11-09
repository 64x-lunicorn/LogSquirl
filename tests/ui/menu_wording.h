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

// How a menu entry is worded (#891): in title case, an entry that opens a
// dialog ending in three dots, never in the one-character ellipsis.

#pragma once

#include <QAction>
#include <QChar>
#include <QList>
#include <QMenu>
#include <QString>
#include <QStringList>

#include "menuentryname.h"

namespace menuwording {

/// The text of the entry without its mnemonic marker, as the application
/// reads it (#937).
inline QString withoutMnemonic( const QString& text )
{
    return logsquirl::menuentry::withoutMnemonic( text );
}

/// Whether the title has a mnemonic: a '&' not doubled.
inline bool hasMnemonic( const QString& title )
{
    for ( qsizetype i = 0; i + 1 < title.size(); ++i ) {
        if ( title.at( i ) == u'&' ) {
            if ( title.at( i + 1 ) != u'&' ) {
                return true;
            }
            ++i;
        }
    }
    return false;
}

/// The name the entry gives its action: no mnemonic, no trailing dots, as
/// the Command Palette and the Dashboard name it (#937).
inline QString actionName( const QString& text )
{
    return logsquirl::menuentry::commandName( text );
}

/// Whether the text is in title case: every word capitalized, but for the
/// articles, conjunctions and short prepositions inside it.
inline bool isTitleCase( const QString& text )
{
    static const QStringList minorWords
        = { "a",  "an", "and", "as", "at",  "but", "by",   "for", "from",
            "in", "of", "on",  "or", "the", "to",  "with", "per" };
    const auto words = actionName( text ).split( u' ', Qt::SkipEmptyParts );
    for ( qsizetype i = 0; i < words.size(); ++i ) {
        const auto& word = words.at( i );
        const auto first = word.front();
        if ( !first.isLetter() ) {
            continue;
        }
        const bool inside = i > 0 && i + 1 < words.size();
        if ( inside && minorWords.contains( word ) ) {
            continue;
        }
        if ( !first.isUpper() ) {
            return false;
        }
    }
    return true;
}

/// Whether the text holds the one-character ellipsis.
inline bool hasEllipsisCharacter( const QString& text )
{
    return text.contains( QChar( 0x2026 ) );
}

/// Whether the menu shows two separators in a row, the actions in hidden
/// left out as the menu leaves them out.
inline bool hasSeparatorsInARow( const QMenu& menu, const QList<const QAction*>& hidden = {} )
{
    bool previousWasSeparator = false;
    for ( const auto* action : menu.actions() ) {
        if ( !action->isVisible() || hidden.contains( action ) ) {
            continue;
        }
        if ( action->isSeparator() && previousWasSeparator ) {
            return true;
        }
        previousWasSeparator = action->isSeparator();
    }
    return false;
}

} // namespace menuwording
