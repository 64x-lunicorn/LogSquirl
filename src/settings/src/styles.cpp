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

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPalette>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTextStream>
#include <qcolor.h>

#include "configuration.h"
#include "log.h"
#include "styles.h"

QStringList StyleManager::availableStyles()
{
    QStringList styles;
    styles << LightKey;
    styles << DarkStyleKey;
    styles << HighContrastKey;

    std::sort( styles.begin(), styles.end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.compare( rhs, Qt::CaseInsensitive ) < 0;
    } );

    return styles;
}

QString StyleManager::defaultPlatformStyle()
{
    return LightKey;
}

void StyleManager::applyStyle( const QString& style )
{
    LOG_INFO << "Setting style to " << style;

    const bool isDark = ( style == DarkStyleKey );
    const bool isHighContrast = ( style == HighContrastKey );

    // ---------------------------------------------------------------
    // 1.  Dark themes
    // ---------------------------------------------------------------
    if ( isDark ) {
        const auto palette = Configuration::get().darkPalette();

        QPalette darkPalette;
        darkPalette.setColor( QPalette::Window, QColor( palette.at( "Window" ) ) );
        darkPalette.setColor( QPalette::WindowText, QColor( palette.at( "WindowText" ) ) );
        darkPalette.setColor( QPalette::Base, QColor( palette.at( "Base" ) ) );
        darkPalette.setColor( QPalette::AlternateBase, QColor( palette.at( "AlternateBase" ) ) );
        darkPalette.setColor( QPalette::ToolTipBase, QColor( palette.at( "ToolTipBase" ) ) );
        darkPalette.setColor( QPalette::ToolTipText, QColor( palette.at( "ToolTipText" ) ) );
        darkPalette.setColor( QPalette::Text, QColor( palette.at( "Text" ) ) );
        darkPalette.setColor( QPalette::Button, QColor( palette.at( "Button" ) ) );
        darkPalette.setColor( QPalette::ButtonText, QColor( palette.at( "ButtonText" ) ) );
        darkPalette.setColor( QPalette::Link, QColor( palette.at( "Link" ) ) );
        darkPalette.setColor( QPalette::Highlight, QColor( palette.at( "Highlight" ) ) );
        darkPalette.setColor( QPalette::HighlightedText,
                              QColor( palette.at( "HighlightedText" ) ) );

        darkPalette.setColor( QPalette::Active, QPalette::Button,
                              QColor( palette.at( "ActiveButton" ) ) );
        darkPalette.setColor( QPalette::Disabled, QPalette::ButtonText,
                              QColor( palette.at( "DisabledButtonText" ) ) );
        darkPalette.setColor( QPalette::Disabled, QPalette::WindowText,
                              QColor( palette.at( "DisabledWindowText" ) ) );
        darkPalette.setColor( QPalette::Disabled, QPalette::Text,
                              QColor( palette.at( "DisabledText" ) ) );
        darkPalette.setColor( QPalette::Disabled, QPalette::Light,
                              QColor( palette.at( "DisabledLight" ) ) );

        const auto textColor = QColor( palette.at( "Text" ) );
        darkPalette.setColor( QPalette::PlaceholderText,
                              QColor( textColor.red(), textColor.green(), textColor.blue(),
                                      128 ) );

        qApp->setStyle( QStyleFactory::create( FusionEngine ) );

        // Tell the platform to use a dark title bar / window chrome.
        qApp->styleHints()->setColorScheme( Qt::ColorScheme::Dark );

        qApp->setPalette( darkPalette );
        qApp->setStyleSheet( loadThemeQss( QStringLiteral( "dark.qss" ) ) );
    }
    // ---------------------------------------------------------------
    // 2.  High Contrast (accessibility)
    // ---------------------------------------------------------------
    else if ( isHighContrast ) {
        qApp->setStyle( QStyleFactory::create( FusionEngine ) );

        // Tell the platform to use a dark title bar / window chrome.
        qApp->styleHints()->setColorScheme( Qt::ColorScheme::Dark );

        QPalette hcPalette;
        hcPalette.setColor( QPalette::Window, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::WindowText, QColor( "#FFFFFF" ) );
        hcPalette.setColor( QPalette::Base, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::AlternateBase, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::ToolTipBase, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::ToolTipText, QColor( "#FFFF00" ) );
        hcPalette.setColor( QPalette::Text, QColor( "#FFFFFF" ) );
        hcPalette.setColor( QPalette::Button, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::ButtonText, QColor( "#FFFFFF" ) );
        hcPalette.setColor( QPalette::Link, QColor( "#00FFFF" ) );
        hcPalette.setColor( QPalette::Highlight, QColor( "#FFFF00" ) );
        hcPalette.setColor( QPalette::HighlightedText, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::Disabled, QPalette::ButtonText, QColor( "#A6A6A6" ) );
        hcPalette.setColor( QPalette::Disabled, QPalette::WindowText, QColor( "#A6A6A6" ) );
        hcPalette.setColor( QPalette::Disabled, QPalette::Text, QColor( "#A6A6A6" ) );
        hcPalette.setColor( QPalette::PlaceholderText, QColor( "#808080" ) );

        qApp->setPalette( hcPalette );
        qApp->setStyleSheet( loadThemeQss( QStringLiteral( "high-contrast.qss" ) ) );
    }
    // ---------------------------------------------------------------
    // 3.  Light
    // ---------------------------------------------------------------
    else if ( style == LightKey ) {
        qApp->setStyle( QStyleFactory::create( FusionEngine ) );

        // Tell the platform to use a light title bar / window chrome.
        qApp->styleHints()->setColorScheme( Qt::ColorScheme::Light );

        QPalette lightPalette;
        lightPalette.setColor( QPalette::Window, QColor( "#F8F9FA" ) );
        lightPalette.setColor( QPalette::WindowText, QColor( "#212529" ) );
        lightPalette.setColor( QPalette::Base, QColor( "#FFFFFF" ) );
        lightPalette.setColor( QPalette::AlternateBase, QColor( "#F8F9FA" ) );
        lightPalette.setColor( QPalette::ToolTipBase, QColor( "#343A40" ) );
        lightPalette.setColor( QPalette::ToolTipText, QColor( "#F8F9FA" ) );
        lightPalette.setColor( QPalette::Text, QColor( "#212529" ) );
        lightPalette.setColor( QPalette::Button, QColor( "#E9ECEF" ) );
        lightPalette.setColor( QPalette::ButtonText, QColor( "#212529" ) );
        lightPalette.setColor( QPalette::Link, QColor( "#0056B3" ) );
        lightPalette.setColor( QPalette::Highlight, QColor( "#0056B3" ) );
        lightPalette.setColor( QPalette::HighlightedText, QColor( "#FFFFFF" ) );
        lightPalette.setColor( QPalette::Active, QPalette::Button, QColor( "#DEE2E6" ) );
        lightPalette.setColor( QPalette::Disabled, QPalette::ButtonText, QColor( "#868E96" ) );
        lightPalette.setColor( QPalette::Disabled, QPalette::WindowText, QColor( "#868E96" ) );
        lightPalette.setColor( QPalette::Disabled, QPalette::Text, QColor( "#868E96" ) );
        lightPalette.setColor( QPalette::PlaceholderText, QColor( "#868E96" ) );
        lightPalette.setColor( QPalette::Mid, QColor( "#ADB5BD" ) );
        lightPalette.setColor( QPalette::Shadow, QColor( "#868E96" ) );
        lightPalette.setColor( QPalette::Light, QColor( "#FFFFFF" ) );
        lightPalette.setColor( QPalette::Midlight, QColor( "#E9ECEF" ) );

        qApp->setPalette( lightPalette );
        qApp->setStyleSheet( loadThemeQss( QStringLiteral( "fusion-light.qss" ) ) );
    }
    // ---------------------------------------------------------------
    // 4.  Platform-native themes (macOS, Vista, Windows, …)
    //     Load the fusion-light QSS as a sensible baseline so toggle
    //     buttons and tab close buttons remain usable.
    // ---------------------------------------------------------------
    else {
        qApp->setStyle( style );
        qApp->setStyleSheet( loadThemeQss( QStringLiteral( "fusion-light.qss" ) ) );
    }
}

QString StyleManager::loadThemeQss( const QString& themeFileName )
{
    // 1. Check user-specific theme directory first
    const QString userThemeDir
        = QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation )
          + QStringLiteral( "/themes/" );
    const QString userPath = userThemeDir + themeFileName;

    if ( QFile::exists( userPath ) ) {
        QFile file( userPath );
        if ( file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
            LOG_INFO << "Loading user theme from " << userPath;
            QTextStream stream( &file );
            return stream.readAll();
        }
    }

    // 2. Fall back to embedded Qt resource
    const QString resourcePath = QStringLiteral( ":/themes/" ) + themeFileName;
    QFile resFile( resourcePath );
    if ( resFile.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        LOG_INFO << "Loading built-in theme from " << resourcePath;
        QTextStream stream( &resFile );
        return stream.readAll();
    }

    LOG_WARNING << "Theme file not found: " << themeFileName;
    return {};
}