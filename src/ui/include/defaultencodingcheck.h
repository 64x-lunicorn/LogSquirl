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

#ifndef LOGSQUIRL_DEFAULTENCODINGCHECK_H
#define LOGSQUIRL_DEFAULTENCODINGCHECK_H

#include "configuration.h"
#include "log.h"
#include "textencoding.h"

// The settings library does not know which encodings exist (that lives in
// logdata), so the check runs here, right after the Configuration is loaded
// (#488). An unknown default Encoding MIB is rewritten to -1 ("Auto"), saved,
// and reported once in the log. Returns true when it was reset.
inline bool resetUnknownDefaultEncoding( Configuration& config )
{
    const auto mib = config.defaultEncodingMib();
    if ( mib < 0 || TextEncoding::forMib( mib ) != nullptr ) {
        return false;
    }

    LOG_WARNING << "Unknown default encoding MIB " << mib << " in the settings, reset to Auto";
    config.setDefaultEncodingMib( -1 );
    config.save();
    return true;
}

#endif
