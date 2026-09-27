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

#include <optional>
#include <utility>

#include <QStringView>

// What a double-click selects in either Presentation: the word under it. A
// word is a run of letters, numbers and connector punctuation (the underscore
// among them); every other character separates words, and a double-click on
// one selects nothing (#546).
//
// The word holding the character at position in text: from its first
// character up to, not including, the character after its last. None when
// that character separates words, or when position lies outside text.
std::optional<std::pair<int, int>> wordAt( QStringView text, int position );
