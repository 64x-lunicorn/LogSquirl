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

#include "highlighterpresets.h"

#include <QColorDialog>
#include <QCoreApplication>

namespace {
const QColor SoftText( "#1B1B1B" );
const QColor StrongText( "#FFFFFF" );
} // namespace

QString HighlighterColorPreset::displayName() const
{
    return QCoreApplication::translate( "HighlighterColorPreset", name );
}

const std::array<HighlighterColorPreset, HighlighterColorPresetCount>& highlighterColorPresets()
{
    static const std::array<HighlighterColorPreset, HighlighterColorPresetCount> presets{ {
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Red" ), SoftText, QColor( "#F4A09A" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Orange" ), SoftText, QColor( "#F9B872" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Amber" ), SoftText, QColor( "#F5D168" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Lime" ), SoftText, QColor( "#BCD97A" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Green" ), SoftText, QColor( "#8FD19E" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Teal" ), SoftText, QColor( "#7CD3C6" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Sky" ), SoftText, QColor( "#86C9EE" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Blue" ), SoftText, QColor( "#9DB8F5" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Violet" ), SoftText, QColor( "#BBA6F2" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Pink" ), SoftText, QColor( "#F2A3CF" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Sand" ), SoftText, QColor( "#D9C3A0" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Slate" ), SoftText, QColor( "#C2C8D0" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Strong Red" ), StrongText,
          QColor( "#C62828" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Strong Orange" ), StrongText,
          QColor( "#B45309" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Strong Green" ), StrongText,
          QColor( "#2E7D32" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Strong Teal" ), StrongText,
          QColor( "#00796B" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Strong Blue" ), StrongText,
          QColor( "#1D5FC7" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Strong Violet" ), StrongText,
          QColor( "#6D3FC0" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Strong Pink" ), StrongText,
          QColor( "#AD1457" ) },
        { QT_TRANSLATE_NOOP( "HighlighterColorPreset", "Strong Slate" ), StrongText,
          QColor( "#4B5563" ) },
    } };
    return presets;
}

const std::array<QColor, 48>& highlighterDialogColors()
{
    // The dialog shows its basic colors in 6 rows and 8 columns and fills
    // them column by column: each line below is one column, light to dark.
    // The first five rows are the tonal scale of a hue, with its soft preset
    // second and its strong preset fourth; the last row is neutral.
    static const std::array<QColor, 48> colors{ {
        // clang-format off
        QColor( "#FDE2E0" ), QColor( "#F4A09A" ), QColor( "#E86A62" ), QColor( "#C62828" ), QColor( "#8E1B1B" ), QColor( "#FFFFFF" ),
        QColor( "#FEE9D2" ), QColor( "#F9B872" ), QColor( "#F08C2E" ), QColor( "#B45309" ), QColor( "#7C3A06" ), QColor( "#F1F3F5" ),
        QColor( "#FDF3C9" ), QColor( "#F5D168" ), QColor( "#E7B416" ), QColor( "#A67C00" ), QColor( "#735500" ), QColor( "#DDE1E6" ),
        QColor( "#DDF2E1" ), QColor( "#8FD19E" ), QColor( "#4CAF63" ), QColor( "#2E7D32" ), QColor( "#1E5621" ), QColor( "#C2C8D0" ),
        QColor( "#D5F3EE" ), QColor( "#7CD3C6" ), QColor( "#2FA89A" ), QColor( "#00796B" ), QColor( "#00534A" ), QColor( "#8A93A0" ),
        QColor( "#DCE7FD" ), QColor( "#9DB8F5" ), QColor( "#5B8DEF" ), QColor( "#1D5FC7" ), QColor( "#15438C" ), QColor( "#4B5563" ),
        QColor( "#ECE5FC" ), QColor( "#BBA6F2" ), QColor( "#9270E0" ), QColor( "#6D3FC0" ), QColor( "#4C2A88" ), QColor( "#2B2F36" ),
        QColor( "#FCE4F1" ), QColor( "#F2A3CF" ), QColor( "#E0619F" ), QColor( "#AD1457" ), QColor( "#760D3B" ), QColor( "#1B1B1B" ),
        // clang-format on
    } };
    return colors;
}

void installHighlighterDialogColors()
{
    const auto& colors = highlighterDialogColors();
    for ( int i = 0; i < static_cast<int>( colors.size() ); ++i ) {
        QColorDialog::setStandardColor( i, colors[ static_cast<size_t>( i ) ] );
    }
}
