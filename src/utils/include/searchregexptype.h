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

#ifndef LOGSQUIRL_SEARCH_REGEXP_TYPE_H
#define LOGSQUIRL_SEARCH_REGEXP_TYPE_H

// How the text of a Search or QuickFind pattern is read: as an extended
// regular expression, as a wildcard expression, or as a fixed string.
//
// This lives here rather than in the settings library that persists the
// user's choice, so that a Settings Policy can name it without linking
// that library -- exactly like RegexpEngine beside it. Whoever reads a
// pattern takes the type as a parameter and reads no setting itself.
enum class SearchRegexpType {
    ExtendedRegexp,
    Wildcard,
    FixedString,
};

#endif
