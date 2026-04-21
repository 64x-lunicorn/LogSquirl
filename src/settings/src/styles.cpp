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
#include <QPalette>
#include <QStyleFactory>
#include <qcolor.h>

#include "configuration.h"
#include "log.h"
#include "styles.h"

QStringList StyleManager::availableStyles()
{
    QStringList styles;
#ifdef Q_OS_WIN
    styles << VistaKey;
    styles << WindowsKey;
    styles << FusionKey;
#else
    styles << QStyleFactory::keys();
#endif

    auto removedStyles = std::remove_if( styles.begin(), styles.end(), []( const QString& style ) {
        return style.startsWith( Gtk2Key, Qt::CaseInsensitive )
               || style.startsWith( Bb10Key, Qt::CaseInsensitive );
    } );

    styles.erase( removedStyles, styles.end() );

    styles << DarkStyleKey;

#ifndef Q_OS_MACOS
    styles << DarkWindowsStyleKey;
#endif

    std::sort( styles.begin(), styles.end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.compare( rhs, Qt::CaseInsensitive ) < 0;
    } );

    return styles;
}

QString StyleManager::defaultPlatformStyle()
{
#if defined( Q_OS_WIN )
    return VistaKey;
#elif defined( Q_OS_MACOS )
    return MacintoshKey;
#else
    return FusionKey;
#endif
}

void StyleManager::applyStyle( const QString& style )
{
    LOG_INFO << "Setting style to " << style;

    const bool isDark
        = ( style == DarkStyleKey || style == DarkWindowsStyleKey );

    // ---------------------------------------------------------------
    // Common QSS applied to EVERY theme so checkable toolbar buttons
    // (filter bar, follow, wrap, …) always show their checked state
    // and tab close buttons have enough room.
    // ---------------------------------------------------------------
    QString commonQss = QStringLiteral(
        // Checked / toggled tool-buttons — visible on all themes
        "QToolButton:checked {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 3px;"
        "}"
    ).arg( isDark ? "#2a82da" : "#B8D4F0",
           isDark ? "#2a82da" : "#7AADE8" );

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

        if ( style == DarkWindowsStyleKey ) {
            qApp->setStyle( QStyleFactory::create( WindowsKey ) );
        }
        else {
            qApp->setStyle( QStyleFactory::create( FusionKey ) );
        }

        qApp->setPalette( darkPalette );

        // Dark-mode QSS
        QString darkQss = QStringLiteral(
            // --- Buttons ---
            "QPushButton {"
            "  border: 1px solid #555555;"
            "  border-radius: 3px;"
            "  padding: 4px 12px;"
            "  background-color: #404040;"
            "}"
            "QPushButton:hover { background-color: #4a4a4a; }"
            "QPushButton:pressed { background-color: #353535; }"
            "QPushButton:disabled { color: #A0A0A0; background-color: #383838; }"

            // --- Tool buttons (toolbar) ---
            "QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 3px; }"
            "QToolButton:hover { background-color: #4a4a4a; border-color: #555555; }"
            "QToolButton:pressed { background-color: #353535; }"

            // --- Tab bar ---
            "QTabBar::tab {"
            "  padding: 6px 14px;"
            "  border: 1px solid #555555;"
            "  border-bottom: none;"
            "  border-top-left-radius: 3px;"
            "  border-top-right-radius: 3px;"
            "  margin-right: 2px;"
            "  background-color: #303030;"
            "}"
            "QTabBar::tab:selected {"
            "  background-color: #282828;"
            "  border-bottom: 2px solid #2a82da;"
            "}"
            "QTabBar::tab:hover:!selected { background-color: #3a3a3a; }"

            // --- Tab close button: red ---
            "QTabBar::close-button {"
            "  border: none; border-radius: 3px; padding: 2px;"
            "}"
            "QTabBar::close-button:hover {"
            "  background-color: #C42B1C;"
            "}"

            // --- Scroll bars ---
            "QScrollBar:vertical {"
            "  background: #2a2a2a; width: 12px; margin: 0; border: none;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: #555555; min-height: 24px; border-radius: 4px; margin: 2px;"
            "}"
            "QScrollBar::handle:vertical:hover { background: #777777; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar:horizontal {"
            "  background: #2a2a2a; height: 12px; margin: 0; border: none;"
            "}"
            "QScrollBar::handle:horizontal {"
            "  background: #555555; min-width: 24px; border-radius: 4px; margin: 2px;"
            "}"
            "QScrollBar::handle:horizontal:hover { background: #777777; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"

            // --- Tooltips ---
            "QToolTip {"
            "  background-color: #2a82da; color: #FFFFFF;"
            "  border: 1px solid #1a72ca; padding: 4px;"
            "  border-radius: 3px;"
            "}"

            // --- Menu ---
            "QMenu { background-color: #353535; border: 1px solid #555555; padding: 4px 0; }"
            "QMenu::item { padding: 5px 24px; }"
            "QMenu::item:selected { background-color: #2a82da; }"
            "QMenu::separator { height: 1px; background: #555555; margin: 4px 8px; }"

            // --- Group boxes ---
            "QGroupBox { border: 1px solid #555555; border-radius: 4px;"
            "  margin-top: 8px; padding-top: 10px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"

            // --- Dock widget title ---
            "QDockWidget::title { background-color: #303030; padding: 4px; }"

            // --- Status bar ---
            "QStatusBar { border-top: 1px solid #555555; }"
            "QStatusBar::item { border: none; }"
        );

        qApp->setStyleSheet( commonQss + darkQss );
    }
    // ---------------------------------------------------------------
    // 2.  Light Fusion
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

        QString fusionQss = QStringLiteral(
            // --- Buttons ---
            "QPushButton {"
            "  border: 1px solid #B0B0B0;"
            "  border-radius: 3px;"
            "  padding: 4px 12px;"
            "  background-color: #E4E4E4;"
            "}"
            "QPushButton:hover { background-color: #D8D8D8; }"
            "QPushButton:pressed { background-color: #C8C8C8; }"
            "QPushButton:disabled { color: #A0A0A0; }"

            // --- Tool buttons ---
            "QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 3px; }"
            "QToolButton:hover { background-color: #D0D0D0; border-color: #B0B0B0; }"
            "QToolButton:pressed { background-color: #C0C0C0; }"

            // --- Tab bar ---
            "QTabBar::tab {"
            "  padding: 6px 14px;"
            "  border: 1px solid #C0C0C0;"
            "  border-bottom: none;"
            "  border-top-left-radius: 3px;"
            "  border-top-right-radius: 3px;"
            "  margin-right: 2px;"
            "  background-color: #E8E8E8;"
            "}"
            "QTabBar::tab:selected {"
            "  background-color: #FFFFFF;"
            "  border-bottom: 2px solid #3080E8;"
            "}"
            "QTabBar::tab:hover:!selected { background-color: #DCDCDC; }"

            // --- Tab close button ---
            "QTabBar::close-button {"
            "  border: none; border-radius: 3px; padding: 2px;"
            "}"
            "QTabBar::close-button:hover {"
            "  background-color: #E0A0A0;"
            "}"

            // --- Scroll bars ---
            "QScrollBar:vertical {"
            "  background: #F0F0F0; width: 12px; margin: 0; border: none;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: #C0C0C0; min-height: 24px; border-radius: 4px; margin: 2px;"
            "}"
            "QScrollBar::handle:vertical:hover { background: #A0A0A0; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar:horizontal {"
            "  background: #F0F0F0; height: 12px; margin: 0; border: none;"
            "}"
            "QScrollBar::handle:horizontal {"
            "  background: #C0C0C0; min-width: 24px; border-radius: 4px; margin: 2px;"
            "}"
            "QScrollBar::handle:horizontal:hover { background: #A0A0A0; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"

            // --- Tooltips ---
            "QToolTip {"
            "  background-color: #FFFFDC; color: #1C1C1C;"
            "  border: 1px solid #C0C0C0; padding: 4px;"
            "  border-radius: 3px;"
            "}"

            // --- Menu ---
            "QMenu { background-color: #FFFFFF; border: 1px solid #C0C0C0; padding: 4px 0; }"
            "QMenu::item { padding: 5px 24px; }"
            "QMenu::item:selected { background-color: #3080E8; color: #FFFFFF; }"
            "QMenu::separator { height: 1px; background: #D0D0D0; margin: 4px 8px; }"

            // --- Group boxes ---
            "QGroupBox { border: 1px solid #C0C0C0; border-radius: 4px;"
            "  margin-top: 8px; padding-top: 10px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"

            // --- Dock widget title ---
            "QDockWidget::title { background-color: #E8E8E8; padding: 4px; }"

            // --- Status bar ---
            "QStatusBar { border-top: 1px solid #C0C0C0; }"
            "QStatusBar::item { border: none; }"
        );

        qApp->setStyleSheet( commonQss + fusionQss );
    }
    // ---------------------------------------------------------------
    // 3.  Platform-native themes (macOS, Vista, Windows, …)
    //     Only apply the common checked-state QSS so toggle buttons
    //     and tab close buttons remain usable.
    // ---------------------------------------------------------------
    else {
        qApp->setStyle( style );
        qApp->setStyleSheet( commonQss );
    }
}