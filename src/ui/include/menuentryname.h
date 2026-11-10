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

#ifndef LOGSQUIRL_MENUENTRYNAME_H
#define LOGSQUIRL_MENUENTRYNAME_H

#include <QString>

// How a menu entry's text reads outside its menu: in the Command Palette and
// in the Welcome Dashboard's line of shortcut hints (#936, #937), named as the
// Shortcuts list names its commands (#891).
namespace logsquirl::menuentry {

// The text without its mnemonic marker: a single '&' is dropped, a doubled
// one is the character itself, and a mnemonic written after the text, as a
// Chinese entry writes it ("(&O)", or in full-width parentheses "（&O）"), is
// dropped with its parentheses.
QString withoutMnemonic( const QString& text );

// The name of the command of the entry: its text without the mnemonic and
// without the dots of an entry that opens a dialog ("..." or the
// one-character ellipsis).
QString commandName( const QString& text );

} // namespace logsquirl::menuentry

#endif // LOGSQUIRL_MENUENTRYNAME_H
