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

#include <QtGlobal>

// The cap on the text work done per Log Line on the UI thread (#826): a Log
// Line longer than this is not matched by the Highlighters nor by Quick Find,
// not parsed by Format Recognition nor by the Table View's field extraction,
// and not read for its Timestamp. It reads as not matching. One crafted Log
// Line past it could otherwise hold the views for seconds at every paint: a
// pattern with several ".*" segments backtracks through it up to PCRE2's step
// limit per match. The Named Value lookup caps at the same length, with a
// constant of its own, as its library links nothing of this one.
inline constexpr qsizetype MaxUiThreadLineLength = 1'000'000;
