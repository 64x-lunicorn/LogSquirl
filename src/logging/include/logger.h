/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LOGSQUIRL_LOGGER_H
#define LOGSQUIRL_LOGGER_H

#include <cstdint>

#include "log.h"

namespace logging {

enum class LogLevel { None, Fatal, Error, Warning, Info, Debug };

// The stream console logging writes to. A command line tool writes its result
// to stdout, so its log messages belong on stderr: stdout then carries only
// the result and can be piped into another tool (#327). The desktop
// application has no such result and keeps stdout.
enum class ConsoleStream { StdOut, StdErr };

void enableLogging( bool enableLogging = true, LogLevel logLevel = LogLevel::Info,
                    ConsoleStream consoleStream = ConsoleStream::StdOut );
void enableFileLogging( bool enableLogging = true, LogLevel logLevel = LogLevel::Info );
} // namespace logging

#endif