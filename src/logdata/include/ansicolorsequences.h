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

class QString;

// Removes from text what a Decoding Policy that hides ANSI color sequences
// removes from every Log Line: the color (SGR) and erase-in-line sequences.
//
// The pattern is compiled once per process, and a text without an escape
// character is left as it is without running it. Safe to call from any
// number of threads at once, so every reader of Log Lines -- one line at a
// time or a block for a Search -- shares the one compiled pattern (#278).
void removeAnsiColorSequences( QString& text );
