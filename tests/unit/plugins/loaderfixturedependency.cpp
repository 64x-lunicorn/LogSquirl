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

// A library that a plugin of the Plugin Loader test imports and ships next to
// itself, as a plugin ships a Qt module the application does not (Windows only).
// The test places it where only the plugin's own directory can find it.

extern "C" {

__declspec( dllexport ) int logsquirl_fixture_dependency_init_result( void )
{
    return 0;
}

} // extern "C"
