/*
 * Copyright (C) 2011 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LOGSQUIRL_PERSISTABLE_H
#define LOGSQUIRL_PERSISTABLE_H

#include <mutex>
#include <stdexcept>
#include <type_traits>

#include "log.h"
#include "persistentinfo.h"

class QSettings;

// A setting persisted in the settings store, held once per process in memory.
//
// get() is the read path: it returns the in-memory copy, read from the
// settings store the first time any accessor is used, and never again by
// get() itself. Building and styling a tab, opening or restoring a Log File
// and anything else done once per tab or per Log File reads through get(), so
// a Session of N tabs does not sync the settings store N times (on macOS each
// sync goes through the system preferences daemon) (#301).
//
// getSynced() syncs the settings store and reads it again, so it picks up
// what another LogSquirl instance saved in the meantime. The places that
// still need that today call it once per user action or per start, never per
// tab:
//   - at startup: Configuration in main(), the Saved Searches and the Session
//     info when the Session is built, the recent files, favorites and
//     Highlighter Sets when a main window is built;
//   - before every write (read, change, save), so that saving does not drop
//     what another instance saved: the Session info when a window is added,
//     saved or closed, the recent files, Saved Searches, tab names, tab
//     groups, Predefined Filters and Highlighter Sets;
//   - where a list is shown to pick from: the tab context menu, the tab
//     group, Predefined Filters and Highlighter Sets dialogs, the Filters
//     panel, the welcome dashboard, the version checker.
// A write through getSynced() leaves the in-memory copy as it was saved, so a
// later get() sees it; Qt writes the change to the settings store on its own
// shortly after save().
template <typename T, typename SettingsType = app_settings>
class Persistable {

public:
    static T& get()
    {
        auto& held = heldPersistable();
        std::call_once( held.read, [ &held ] { held.persistable.retrieve(); } );
        return held.persistable;
    }

    static T& getSynced()
    {
        auto& held = heldPersistable();
        auto readNow = false;
        std::call_once( held.read, [ &held, &readNow ] {
            held.persistable.retrieve();
            readNow = true;
        } );
        if ( !readNow ) {
            held.persistable.retrieve();
        }
        return held.persistable;
    }

    void save() const
    {
        auto& settings = PersistentInfo::getSettings( SettingsType{} );
        static_cast<const T&>( *this ).saveToStorage( settings );
    }

private:
    struct HeldPersistable {
        T persistable;
        std::once_flag read;
    };

    static HeldPersistable& heldPersistable()
    {
        static HeldPersistable held;
        return held;
    }

    void retrieve()
    {
        auto& settings = PersistentInfo::getSettings( SettingsType{} );

        settings.sync();
        static_cast<T&>( *this ).retrieveFromStorage( settings );
    }
};

#endif
