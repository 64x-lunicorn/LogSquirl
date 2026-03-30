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
#include <QStringList>

#include "theme.h"

// Manages theme selection and application for the entire app.
// Replaces the old StyleManager with a token-based, restart-free approach.
// Three modes: "LogSquirl Dark", "LogSquirl Light", and "System" (auto-detect).
struct ThemeManager {

    static constexpr const char* DarkThemeKey = "LogSquirl Dark";
    static constexpr const char* LightThemeKey = "LogSquirl Light";
    static constexpr const char* SystemThemeKey = "System";

    // Returns the list of available theme names for the UI selector.
    static QStringList availableThemes();

    // Returns the default theme name ("System").
    static QString defaultTheme();

    // Apply the given theme immediately, without requiring an app restart.
    // Sets Fusion as the base style, builds QPalette from design tokens,
    // applies a supplemental QSS, and updates the title bar appearance.
    static void applyTheme( const QString& theme );

    // Returns the currently active ThemePalette (useful for custom painting).
    static const ThemePalette& currentPalette();

    // Connect to OS color-scheme changes for "System" mode live-tracking.
    // Call once at startup from main().
    static void connectToSystemThemeChanges();
};
