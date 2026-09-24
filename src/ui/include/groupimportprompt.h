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

#include <QString>

#include "groupexchange.h"

class QWidget;

namespace logsquirl::groupexchange {

// The question both import dialogs put to the user when an imported group
// meets one of the same id or name: Replace / Keep both / Skip, the
// preselected one as the default button, and "Apply to all remaining
// conflicts".
ConflictResolver askUser( QWidget* parent, const QString& title );

// Tells the user, on screen, of a file that could not be imported: one that
// cannot be read, or that holds no group. Nothing when the import went fine.
void reportImportError( QWidget* parent, const QString& title, const QString& file,
                        const ImportResult& result );

} // namespace logsquirl::groupexchange
