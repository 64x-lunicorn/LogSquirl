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

#include <QColor>
#include <QPalette>
#include <QString>

// Token-based color palette following the "Glass Monolith" design system.
// Each token maps to a semantic role in the UI rather than a raw color value.
struct ThemePalette {

    // Surface hierarchy (background tiers for tonal layering)
    QColor surface;
    QColor surfaceContainerLowest;
    QColor surfaceContainerLow;
    QColor surfaceContainerHigh;
    QColor surfaceContainerHighest;
    QColor surfaceBright;

    // Content colors on surfaces
    QColor onSurface;
    QColor onSurfaceVariant;

    // Primary accent
    QColor primary;
    QColor primaryContainer;
    QColor onPrimaryContainer;
    QColor primaryFixedDim;

    // Semantic: warning and error
    QColor tertiary;
    QColor error;

    // Structural
    QColor outlineVariant;

    // Inverse (tooltips, high-contrast overlays)
    QColor inverseSurface;
    QColor onInverseSurface;

    // Tint overlay
    QColor surfaceTint;

    // Link color
    QColor link;

    // Disabled state
    QColor disabledText;
    QColor disabledButtonText;

    // Whether this is a dark palette
    bool isDark = true;

    // Build a QPalette from the design tokens, mapping them to Qt palette roles.
    QPalette toQPalette() const
    {
        QPalette pal;

        pal.setColor( QPalette::Window, surfaceContainerLow );
        pal.setColor( QPalette::WindowText, onSurface );
        pal.setColor( QPalette::Base, surface );
        pal.setColor( QPalette::AlternateBase, surfaceContainerLow );
        pal.setColor( QPalette::ToolTipBase, inverseSurface );
        pal.setColor( QPalette::ToolTipText, onInverseSurface );
        pal.setColor( QPalette::Text, onSurface );
        pal.setColor( QPalette::Button, surfaceContainerHigh );
        pal.setColor( QPalette::ButtonText, onSurface );
        pal.setColor( QPalette::Link, link );
        pal.setColor( QPalette::Highlight, primary );
        pal.setColor( QPalette::HighlightedText, isDark ? surface : onPrimaryContainer );

        pal.setColor( QPalette::Active, QPalette::Button, surfaceContainerHigh );

        pal.setColor( QPalette::Disabled, QPalette::ButtonText, disabledButtonText );
        pal.setColor( QPalette::Disabled, QPalette::WindowText, disabledText );
        pal.setColor( QPalette::Disabled, QPalette::Text, disabledText );
        pal.setColor( QPalette::Disabled, QPalette::Light, surfaceContainerLow );

        // Semi-transparent placeholder so it is readable on any base
        pal.setColor( QPalette::PlaceholderText,
                      QColor( onSurface.red(), onSurface.green(), onSurface.blue(), 128 ) );

        return pal;
    }

    // Generate a supplemental QSS string that implements design-system rules
    // which cannot be expressed through QPalette alone (rounded corners,
    // gradients, "no-line" rule, selection glow bar, etc.).
    QString toStyleSheet() const
    {
        const auto bg = surface.name();
        const auto bgLow = surfaceContainerLow.name();
        const auto bgHigh = surfaceContainerHigh.name();
        const auto bgHighest = surfaceContainerHighest.name();
        const auto fg = onSurface.name();
        const auto fgVariant = onSurfaceVariant.name();
        const auto accent = primary.name();
        const auto ghost = outlineVariant.name();
        const auto tipBg = inverseSurface.name();
        const auto tipFg = onInverseSurface.name();
        const auto lowest = surfaceContainerLowest.name();
        const auto disText = disabledText.name();
        const auto iconSuffix = QString( isDark ? "_inverse" : "" );
        const auto focusBorder = QString( "rgba(%1, %2, %3, 77)" )
                                     .arg( primary.red() )
                                     .arg( primary.green() )
                                     .arg( primary.blue() );
        const auto selDimRgba = QString( "rgba(%1, %2, %3, 26)" )
                                    .arg( primaryFixedDim.red() )
                                    .arg( primaryFixedDim.green() )
                                    .arg( primaryFixedDim.blue() );
        const auto ghostRgba = QString( "rgba(%1, %2, %3, 38)" )
                                   .arg( outlineVariant.red() )
                                   .arg( outlineVariant.green() )
                                   .arg( outlineVariant.blue() );

        // Build stylesheet via concatenation to avoid the QString::arg() %1-%9
        // limit which corrupts placeholders like %10+ (Qt interprets %10 as %1
        // followed by literal '0').
        QString qss;

        // --- Global base ---
        qss += "QMainWindow, QDialog {"
               "  background-color: " + bgLow + ";"
               "}";

        // --- QToolTip ---
        qss += "QToolTip {"
               "  background-color: " + tipBg + ";"
               "  color: " + tipFg + ";"
               "  border: none;"
               "  border-radius: 6px;"
               "  padding: 4px 8px;"
               "}";

        // --- QPushButton ---
        qss += "QPushButton {"
               "  background-color: " + bgHigh + ";"
               "  color: " + fg + ";"
               "  border: none;"
               "  border-radius: 6px;"
               "  padding: 4px 12px;"
               "}"
               "QPushButton:hover {"
               "  background-color: " + bgHighest + ";"
               "}"
               "QPushButton:pressed {"
               "  background-color: " + accent + ";"
               "}"
               "QPushButton:disabled {"
               "  color: " + disText + ";"
               "}";

        // --- QLineEdit (search-as-surface) ---
        qss += "QLineEdit {"
               "  background-color: " + lowest + ";"
               "  color: " + fg + ";"
               "  border: none;"
               "  border-radius: 6px;"
               "  padding: 4px 8px;"
               "  selection-background-color: " + accent + ";"
               "}"
               "QLineEdit:focus {"
               "  border: 1px solid " + focusBorder + ";"
               "}";

        // --- QComboBox ---
        qss += "QComboBox {"
               "  background-color: " + bgHigh + ";"
               "  color: " + fg + ";"
               "  border: none;"
               "  border-radius: 6px;"
               "  padding: 4px 8px;"
               "}"
               "QComboBox::drop-down {"
               "  border: none;"
               "}"
               "QComboBox QAbstractItemView {"
               "  background-color: " + bgLow + ";"
               "  color: " + fg + ";"
               "  selection-background-color: " + accent + ";"
               "  border: none;"
               "  border-radius: 6px;"
               "}";

        // --- QTabBar ---
        qss += "QTabBar::tab {"
               "  height: 24px;"
               "  background-color: " + bg + ";"
               "  color: " + fgVariant + ";"
               "  border: none;"
               "  padding: 4px 12px;"
               "  border-top-left-radius: 6px;"
               "  border-top-right-radius: 6px;"
               "}"
               "QTabBar::tab:selected {"
               "  background-color: " + bgLow + ";"
               "  color: " + fg + ";"
               "}"
               "QTabBar::tab:hover:!selected {"
               "  background-color: " + bgHigh + ";"
               "}"
               "QTabBar::close-button {"
               "  image: url(:/images/icons8-close-window-16" + iconSuffix + ".png);"
               "  subcontrol-origin: padding;"
               "  subcontrol-position: right;"
               "  height: 12px; width: 12px;"
               "}"
               "QTabBar::close-button:hover {"
               "  image: url(:/images/icons8-close-window-hover-16" + iconSuffix + ".png);"
               "}";

        // --- QScrollBar (vertical) ---
        qss += "QScrollBar:vertical {"
               "  background-color: " + bg + ";"
               "  width: 10px;"
               "  border: none;"
               "  border-radius: 5px;"
               "}"
               "QScrollBar::handle:vertical {"
               "  background-color: " + bgHigh + ";"
               "  min-height: 20px;"
               "  border-radius: 5px;"
               "}"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
               "  height: 0px;"
               "}";

        // --- QScrollBar (horizontal) ---
        qss += "QScrollBar:horizontal {"
               "  background-color: " + bg + ";"
               "  height: 10px;"
               "  border: none;"
               "  border-radius: 5px;"
               "}"
               "QScrollBar::handle:horizontal {"
               "  background-color: " + bgHigh + ";"
               "  min-width: 20px;"
               "  border-radius: 5px;"
               "}"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
               "  width: 0px;"
               "}";

        // --- QTreeView / QListView selection ---
        qss += "QTreeView::item:selected, QListView::item:selected {"
               "  background-color: " + selDimRgba + ";"
               "  border-left: 2px solid " + accent + ";"
               "}";

        // --- QGroupBox ---
        qss += "QGroupBox {"
               "  border: none;"
               "  border-radius: 8px;"
               "  background-color: " + bg + ";"
               "  margin-top: 8px;"
               "  padding-top: 16px;"
               "}"
               "QGroupBox::title {"
               "  subcontrol-origin: margin;"
               "  left: 12px;"
               "  color: " + fgVariant + ";"
               "}";

        // --- QMenuBar ---
        qss += "QMenuBar {"
               "  background-color: " + bg + ";"
               "  color: " + fg + ";"
               "  border: none;"
               "}"
               "QMenuBar::item:selected {"
               "  background-color: " + bgHigh + ";"
               "  border-radius: 4px;"
               "}";

        // --- QMenu ---
        qss += "QMenu {"
               "  background-color: " + bgLow + ";"
               "  color: " + fg + ";"
               "  border: none;"
               "  border-radius: 8px;"
               "  padding: 4px;"
               "}"
               "QMenu::item:selected {"
               "  background-color: " + bgHigh + ";"
               "  border-radius: 4px;"
               "}"
               "QMenu::separator {"
               "  height: 1px;"
               "  background-color: " + ghostRgba + ";"
               "  margin: 4px 8px;"
               "}";

        // --- QStatusBar ---
        qss += "QStatusBar {"
               "  background-color: " + bg + ";"
               "  color: " + fgVariant + ";"
               "}";

        // --- QHeaderView ---
        qss += "QHeaderView::section {"
               "  background-color: " + bg + ";"
               "  color: " + fg + ";"
               "  border: none;"
               "  padding: 4px;"
               "}";

        // --- QCheckBox / QRadioButton indicator (dark theme fix) ---
        qss += "QTreeWidget::indicator:unchecked {"
               "  border: 1px solid " + fgVariant + ";"
               "  background: " + bgHigh + ";"
               "  border-radius: 3px;"
               "}";

        // --- QSpinBox ---
        qss += "QSpinBox {"
               "  background-color: " + lowest + ";"
               "  color: " + fg + ";"
               "  border: none;"
               "  border-radius: 6px;"
               "  padding: 2px 6px;"
               "}";

        return qss;
    }

    // Factory: dark theme from DESIGN.md "Glass Monolith" tokens
    static ThemePalette darkTheme()
    {
        ThemePalette p;
        p.isDark = true;

        p.surface                = QColor( "#131314" );
        p.surfaceContainerLowest = QColor( "#0e0e0f" );
        p.surfaceContainerLow    = QColor( "#1c1b1c" );
        p.surfaceContainerHigh   = QColor( "#282729" );
        p.surfaceContainerHighest = QColor( "#353436" );
        p.surfaceBright          = QColor( "#3b3a3c" );

        p.onSurface              = QColor( "#e5e2e3" );
        p.onSurfaceVariant       = QColor( "#c1c6d7" );

        p.primary                = QColor( "#adc6ff" );
        p.primaryContainer       = QColor( "#4b8eff" );
        p.onPrimaryContainer     = QColor( "#001a40" );
        p.primaryFixedDim        = QColor( "#adc6ff" );

        p.tertiary               = QColor( "#ffb595" );
        p.error                  = QColor( "#ffb4ab" );

        p.outlineVariant         = QColor( "#414755" );

        p.inverseSurface         = QColor( "#e5e2e3" );
        p.onInverseSurface       = QColor( "#1c1b1c" );

        p.surfaceTint            = QColor( "#adc6ff" );

        p.link                   = QColor( "#adc6ff" );
        p.disabledText           = QColor( "#808080" );
        p.disabledButtonText     = QColor( "#757575" );

        return p;
    }

    // Factory: light theme from DESIGN_Light.md
    static ThemePalette lightTheme()
    {
        ThemePalette p;
        p.isDark = false;

        p.surface                = QColor( "#f5f5f6" );
        p.surfaceContainerLowest = QColor( "#ffffff" );
        p.surfaceContainerLow    = QColor( "#ebebed" );
        p.surfaceContainerHigh   = QColor( "#dddde0" );
        p.surfaceContainerHighest = QColor( "#d0d0d3" );
        p.surfaceBright          = QColor( "#fafafa" );

        p.onSurface              = QColor( "#0b0b0c" );
        p.onSurfaceVariant       = QColor( "#8e8e93" );

        p.primary                = QColor( "#007aff" );
        p.primaryContainer       = QColor( "#d0e4ff" );
        p.onPrimaryContainer     = QColor( "#001d36" );
        p.primaryFixedDim        = QColor( "#007aff" );

        p.tertiary               = QColor( "#ff9500" );
        p.error                  = QColor( "#ff3b30" );

        p.outlineVariant         = QColor( "#c6c6c8" );

        p.inverseSurface         = QColor( "#2c2c2e" );
        p.onInverseSurface       = QColor( "#f2f2f7" );

        p.surfaceTint            = QColor( "#007aff" );

        p.link                   = QColor( "#007aff" );
        p.disabledText           = QColor( "#c7c7cc" );
        p.disabledButtonText     = QColor( "#aeaeb2" );

        return p;
    }
};
