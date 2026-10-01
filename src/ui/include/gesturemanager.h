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

#ifndef LOGSQUIRL_GESTUREMANAGER_H
#define LOGSQUIRL_GESTUREMANAGER_H

// Creates the QApplication's gesture manager now, on the GUI thread.
//
// Qt creates it the first time a widget grabs a gesture, which every scroll
// area does when it is built. QApplication::notify reads it for every event
// it delivers, on whatever thread the receiver lives in -- the File Watcher's
// poll thread included. A gesture manager created after that thread has
// started is written while that thread may read it, and TSan reports the race
// (#698). Created before any other thread starts, it is only read from then
// on.
//
// Call it right after the QApplication is constructed, before anything starts
// a thread that receives events.
void createGestureManager();

#endif
