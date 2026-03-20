/*
 * Copyright (C) 2020 Anton Filimonov and other contributors
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

#ifndef LOGSQUIRL_STYLES
#define LOGSQUIRL_STYLES

#include <QStringList>
#include <QLatin1String>

struct StyleManager {

    static constexpr QLatin1String DarkStyleKey = QLatin1String( "Dark", 4 );
    static constexpr QLatin1String DarkWindowsStyleKey = QLatin1String( "Windows Dark", 12 );
    static constexpr QLatin1String VistaKey = QLatin1String( "WindowsVista", 12 );
    static constexpr QLatin1String FusionKey = QLatin1String( "Fusion", 6 );
    static constexpr QLatin1String WindowsKey = QLatin1String( "Windows", 7 );
    static constexpr QLatin1String MacintoshKey = QLatin1String( "macintosh", 9 );

    static constexpr QLatin1String Gtk2Key = QLatin1String( "Gtk2", 4 );
    static constexpr QLatin1String Bb10Key = QLatin1String( "bb10", 4 );

    static QStringList availableStyles();
    static QString defaultPlatformStyle();

    static void applyStyle( const QString& style );
};

#endif
