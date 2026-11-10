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

// Whether a Log File outlives the run of the application it was opened in. A
// Transient Log File -- the spool of standard input, the file of a merged tab,
// what a data source writes, the text pasted from the clipboard -- exists only
// while the application runs, so the Session does not save it: a restart
// neither opens a file that is gone nor reports an error for it (#570).
// Whoever opens a Log File knows which it is and says so; the Session only
// keeps it.
enum class LogFileLifetime { Ordinary, Transient };
