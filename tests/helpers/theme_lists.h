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

#ifndef LOGSQUIRL_TEST_THEME_LISTS_H
#define LOGSQUIRL_TEST_THEME_LISTS_H

#include <QLatin1String>
#include <QStringList>

#include "theme.h"

// The Themes a test walks through (#358). Whatever holds for every Theme is
// checked against Theme::builtInThemes() itself; these are for the tests that
// apply one Theme first and then switch away from it, where the list is a
// sequence rather than an enumeration.

// Every built-in Theme but applied, in Theme::builtInThemes() order: the
// switches a test makes after showing something under applied.
inline QStringList themeSwitchesFrom( QLatin1String applied )
{
    auto themes = Theme::builtInThemes();
    themes.removeAll( QString( applied ) );
    return themes;
}

// The same switches and then back to applied: a round trip, which leaves the
// Theme it started from active rather than whichever one sorted last.
inline QStringList themeRoundTripFrom( QLatin1String applied )
{
    auto themes = themeSwitchesFrom( applied );
    themes.append( QString( applied ) );
    return themes;
}

#endif
