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

#include "filewatchport.h"
#include "settingspolicies.h"

// Everything the Session needs from file watching: the File Watch Port it
// hands every Open Log File, and the Watch Policy to follow, which it hands
// over when it is built and again whenever a settings change alters it
// (#245). An Open Log File is handed only the File Watch Port: which Policy
// the watcher follows is none of its business.
class PolicyFileWatchPort : public FileWatchPort {
public:
    using FileWatchPort::FileWatchPort;

    // Follows the passed Watch Policy from now on, the files already watched
    // included.
    virtual void setWatchPolicy( const WatchPolicy& policy ) = 0;
};
