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

// Why a decompression or an extraction stopped short of its end (#816).
// Whoever started it removes what was written so far; the budget is the
// one reason the user is told apart from the others, as it is theirs to
// raise.
enum class DecompressionFailure {
    None,
    // A corrupt or truncated archive, an archive that cannot be opened, or
    // an I/O error.
    Error,
    // The interrupt flag was set.
    Interrupted,
    // The next bytes would take what is written past the budget.
    BudgetExceeded
};
