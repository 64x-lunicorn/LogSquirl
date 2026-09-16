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

#ifndef CHANGED_H
#define CHANGED_H

// What a writer tells the Session it has changed -- all it says: the Session
// works out what follows from it and who has to hear of it (#245).
enum class Changed {
    // The settings store was written: an Options Dialog applied, a View menu
    // toggle, a zoom.
    Settings,
    // The Highlighter Set Collection was written: a Highlighter Set edited,
    // imported, activated or deactivated, or a Color Label given another
    // color. Highlighter Sets are the user's coloring, not a setting.
    HighlighterSets,
};

#endif
