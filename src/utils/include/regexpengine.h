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

#ifndef LOGSQUIRL_REGEXP_ENGINE_H
#define LOGSQUIRL_REGEXP_ENGINE_H

// Which of the two matching engines a RegularExpression runs on.
//
// This lives here rather than in either of the two libraries that name it,
// so that neither has to link the other: the regex library takes the choice
// as a constructor parameter and reads no settings, while the settings
// library persists the user's choice and knows nothing about matching.
enum class RegexpEngine { Vectorscan, QRegularExpression };

#endif
