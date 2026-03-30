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

#include "thememanager.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include "log.h"

namespace {

// Singleton storage for the active palette
ThemePalette& activePalette()
{
    static ThemePalette palette = ThemePalette::darkTheme();
    return palette;
}

// Detect whether the OS prefers a dark color scheme (Qt 6.5+)
bool systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
#else
    // Fallback: check if the default window background is dark
    const auto bg = QPalette().color( QPalette::Window );
    return bg.lightnessF() < 0.5;
#endif
}

// Resolve a theme name to a concrete ThemePalette
ThemePalette resolveTheme( const QString& theme )
{
    if ( theme == ThemeManager::LightThemeKey ) {
        return ThemePalette::lightTheme();
    }
    if ( theme == ThemeManager::SystemThemeKey ) {
        return systemPrefersDark() ? ThemePalette::darkTheme() : ThemePalette::lightTheme();
    }
    // Default: dark
    return ThemePalette::darkTheme();
}

// Keep track of the user's theme choice for system-change re-application
QString& currentThemeChoice()
{
    static QString choice = ThemeManager::SystemThemeKey;
    return choice;
}

} // anonymous namespace

QStringList ThemeManager::availableThemes()
{
    return { DarkThemeKey, LightThemeKey, SystemThemeKey };
}

QString ThemeManager::defaultTheme()
{
    return SystemThemeKey;
}

void ThemeManager::applyTheme( const QString& theme )
{
    // Re-entrancy guard: setColorScheme() below can fire colorSchemeChanged,
    // which triggers connectToSystemThemeChanges() → applyTheme() again.
    // A recursive call would double-delete the QStyleSheetStyle proxy.
    static bool applying = false;
    if ( applying ) {
        return;
    }
    applying = true;

    LOG_INFO << "Applying theme: " << theme;

    currentThemeChoice() = theme;

    const auto palette = resolveTheme( theme );
    activePalette() = palette;

    // Use Fusion as the base — it fully respects custom QPalette.
    // Only create a new style instance when the current style is not Fusion,
    // because recreating the style while widgets are visible deletes the old
    // style object and can crash event handlers that still reference it.
    if ( qApp->style()->objectName().compare( "fusion", Qt::CaseInsensitive ) != 0 ) {
        qApp->setStyle( QStyleFactory::create( "Fusion" ) );
    }

    // Build the complete new state before touching any Qt global.
    const auto qPal = palette.toQPalette();
    const auto qss = palette.toStyleSheet();

    // Apply palette + stylesheet. Do NOT clear the stylesheet first:
    // clearing triggers a full QStyleSheetStyle proxy teardown+rebuild cycle
    // while widgets still hold cached pointers, causing crashes.
    // Instead, just set the new palette and then replace the stylesheet in
    // one shot — Qt swaps the proxy atomically.
    qApp->setPalette( qPal );
    qApp->setStyleSheet( qss );

    // Update the title bar to match the theme
#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
    if ( theme == SystemThemeKey ) {
        // Let Qt follow the OS natively — don't force a color scheme
        QGuiApplication::styleHints()->setColorScheme( Qt::ColorScheme::Unknown );
    }
    else {
        QGuiApplication::styleHints()->setColorScheme(
            palette.isDark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light );
    }
#endif

    applying = false;
}

const ThemePalette& ThemeManager::currentPalette()
{
    return activePalette();
}

void ThemeManager::connectToSystemThemeChanges()
{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
    QObject::connect( QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                      qApp, []( [[maybe_unused]] Qt::ColorScheme scheme ) {
                          // Only re-apply when user chose "System"
                          if ( currentThemeChoice() == ThemeManager::SystemThemeKey ) {
                              ThemeManager::applyTheme( ThemeManager::SystemThemeKey );
                          }
                      } );
#endif
}
