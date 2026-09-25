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

namespace logsquirl::versioncheck {

// The update check can be turned off for a whole installation, not only by
// the user in the Options Dialog: the Windows installer offers it as a
// component, as code signing through the SignPath Foundation asks of software
// that contacts a server on its own (#445). Unticking the component leaves
// this file beside the executable; the installer deletes it again when the
// component is ticked on a later install, and on uninstall. A package that
// should never check for updates can ship the file the same way.
//
// The file only has to exist; its content is not read. It lives in the
// installation directory rather than in the user's settings: the installer
// runs elevated, possibly as another account than the user's, and must not
// write a settings file it does not own. While it is there the update check
// stays off whatever the settings say, and the Options Dialog shows why.
inline constexpr auto UpdateCheckOffFileName = "logsquirl_no_update_check";

// Whether the installation in applicationDirectory has the update check
// turned off.
bool updateCheckTurnedOffAtInstall( const QString& applicationDirectory );

// The same for the running application: the directory of its executable.
bool updateCheckTurnedOffAtInstall();

} // namespace logsquirl::versioncheck
