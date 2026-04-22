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
#include <QTextStream>
#include <qcolor.h>

#include "configuration.h"
#include "log.h"
#include "styles.h"

QStringList StyleManager::availableStyles()
{
    QStringList styles;
    styles << FusionKey;
    styles << DarkStyleKey;
    styles << HighContrastKey;

    std::sort( styles.begin(), styles.end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.compare( rhs, Qt::CaseInsensitive ) < 0;
    } );

    return styles;
}

QString StyleManager::defaultPlatformStyle()
{
    return FusionKey;
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

        qApp->setStyle( QStyleFactory::create( FusionKey ) );

        qApp->setPalette( darkPalette );
        qApp->setStyleSheet( loadThemeQss( QStringLiteral( "dark.qss" ) ) );
    }
    // ---------------------------------------------------------------
    // 2.  High Contrast (accessibility)
    // ---------------------------------------------------------------
    else if ( isHighContrast ) {
        qApp->setStyle( QStyleFactory::create( FusionKey ) );

        QPalette hcPalette;
        hcPalette.setColor( QPalette::Window, QColor( "#FFFFFF" ) );
        hcPalette.setColor( QPalette::WindowText, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::Base, QColor( "#FFFFFF" ) );
        hcPalette.setColor( QPalette::AlternateBase, QColor( "#F0F0F0" ) );
        hcPalette.setColor( QPalette::ToolTipBase, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::ToolTipText, QColor( "#FFFFFF" ) );
        hcPalette.setColor( QPalette::Text, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::Button, QColor( "#FFFFFF" ) );
        hcPalette.setColor( QPalette::ButtonText, QColor( "#000000" ) );
        hcPalette.setColor( QPalette::Link, QColor( "#0000FF" ) );
        hcPalette.setColor( QPalette::Highlight, QColor( "#0000FF" ) );
        hcPalette.setColor( QPalette::HighlightedText, QColor( "#FFFFFF" ) );
        hcPalette.setColor( QPalette::Disabled, QPalette::ButtonText, QColor( "#808080" ) );
        hcPalette.setColor( QPalette::Disabled, QPalette::WindowText, QColor( "#808080" ) );
        hcPalette.setColor( QPalette::Disabled, QPalette::Text, QColor( "#808080" ) );
        hcPalette.setColor( QPalette::PlaceholderText, QColor( "#808080" ) );

        qApp->setPalette( hcPalette );
        qApp->setStyleSheet( loadThemeQss( QStringLiteral( "high-contrast.qss" ) ) );
    }
    // ---------------------------------------------------------------
    // 3.  Light Fusion
    // ---------------------------------------------------------------
    else if ( style == FusionKey ) {
        qApp->setStyle( QStyleFactory::create( FusionKey ) );

        QPalette lightPalette;
        lightPalette.setColor( QPalette::Window, QColor( "#F0F0F0" ) );
        lightPalette.setColor( QPalette::WindowText, QColor( "#1C1C1C" ) );
        lightPalette.setColor( QPalette::Base, QColor( "#FFFFFF" ) );
        lightPalette.setColor( QPalette::AlternateBase, QColor( "#F5F5F5" ) );
        lightPalette.setColor( QPalette::ToolTipBase, QColor( "#FFFFDC" ) );
        lightPalette.setColor( QPalette::ToolTipText, QColor( "#1C1C1C" ) );
        lightPalette.setColor( QPalette::Text, QColor( "#1C1C1C" ) );
        lightPalette.setColor( QPalette::Button, QColor( "#E4E4E4" ) );
        lightPalette.setColor( QPalette::ButtonText, QColor( "#1C1C1C" ) );
        lightPalette.setColor( QPalette::Link, QColor( "#2A6FDB" ) );
        lightPalette.setColor( QPalette::Highlight, QColor( "#3080E8" ) );
        lightPalette.setColor( QPalette::HighlightedText, QColor( "#FFFFFF" ) );
        lightPalette.setColor( QPalette::Active, QPalette::Button, QColor( "#E0E0E0" ) );
        lightPalette.setColor( QPalette::Disabled, QPalette::ButtonText, QColor( "#A0A0A0" ) );
        lightPalette.setColor( QPalette::Disabled, QPalette::WindowText, QColor( "#A0A0A0" ) );
        lightPalette.setColor( QPalette::Disabled, QPalette::Text, QColor( "#A0A0A0" ) );
        lightPalette.setColor( QPalette::PlaceholderText, QColor( "#A0A0A0" ) );
        lightPalette.setColor( QPalette::Mid, QColor( "#C0C0C0" ) );
        lightPalette.setColor( QPalette::Shadow, QColor( "#A0A0A0" ) );
        lightPalette.setColor( QPalette::Light, QColor( "#FFFFFF" ) );
        lightPalette.setColor( QPalette::Midlight, QColor( "#E8E8E8" ) );

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