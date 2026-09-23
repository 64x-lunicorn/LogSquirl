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

#include <array>

#include <QColor>
#include <QString>

// A ready-made text and background color pair for a Highlighter. Every
// preset stays readable on its own background and stands out from the Log
// Lines of every built-in Theme, so it needs no Theme of its own.
struct HighlighterColorPreset {
    const char* name;
    QColor foreColor;
    QColor backColor;

    QString displayName() const;
};

inline constexpr int HighlighterColorPresetCount = 20;
inline constexpr int SoftHighlighterColorPresetCount = 12;

// The soft presets first (pastel backgrounds, dark text), then the strong
// ones (saturated backgrounds, white text).
const std::array<HighlighterColorPreset, HighlighterColorPresetCount>& highlighterColorPresets();

// The colors to offer as the color dialog's basic colors: the hues of the
// presets in tonal scales, plus neutrals. In the order QColorDialog takes
// them, column by column.
const std::array<QColor, 48>& highlighterDialogColors();

// Replaces the basic colors of QColorDialog with highlighterDialogColors().
void installHighlighterDialogColors();
