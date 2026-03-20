/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov
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

#ifndef LOGSQUIRL_SYNCHRONIZATION_H
#define LOGSQUIRL_SYNCHRONIZATION_H

#include <mutex>
#include <shared_mutex>

using Mutex = std::shared_mutex;
using ScopedLock = std::unique_lock<Mutex>;

using RecursiveMutex = std::recursive_mutex;
using ScopedRecursiveLock = std::scoped_lock<RecursiveMutex>;

using SharedMutex = std::shared_mutex;
using SharedLock = std::shared_lock<SharedMutex>;
using UniqueLock = std::unique_lock<SharedMutex>;

#endif