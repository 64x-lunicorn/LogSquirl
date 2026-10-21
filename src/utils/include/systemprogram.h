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

// The absolute native path of a program of the Windows system folder, from
// its path below the system root: "explorer.exe", "System32/cmd.exe". Started
// by a bare name, CreateProcess looks in the application's and the working
// directory before the system folder, so a planted copy would run (#822).
// Without a system root, the program is looked up on PATH by its file name;
// empty when it is not there.
QString systemProgramPath( const QString& relativePath, const QString& systemRoot );

// The same, from the SystemRoot the environment names.
QString systemProgramPath( const QString& relativePath );
