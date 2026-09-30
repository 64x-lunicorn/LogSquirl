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

#include <vector>

#include <QStringList>

#include "linetypes.h"
#include "settingspolicies.h"

class QFont;
struct RegularExpressionPattern;

// A view of a Log File as its View Set tells it what every view of the Log
// File shows alike: the Text View, the Table View and every Filtered View
// (see CONTEXT.md). The View Set holds each view through this, so it hands a
// Presentation and a Filtered View the same things the same way, and never
// asks which one it holds. What only a Presentation is asked or told is the
// Presentation interface's (ADR 0003).
//
// A view reads none of this from the settings.
class LogFileView {
public:
    // The settings that color Log Lines.
    virtual void setDecorationPolicy( const DecorationPolicy& policy ) = 0;
    // What the view shows and scrolls under: the line numbers each kind of
    // view shows, and whether a Presentation makes room for the Overview the
    // Presentations share.
    virtual void setPresentationPolicy( const PresentationPolicy& policy ) = 0;
    // Whether follow may be engaged at all. The Table View never engages
    // follow itself, and so ignores it.
    virtual void allowFollowMode( bool allow ) = 0;
    // Whether the Log File is followed, as the View Set, its owner, says: on,
    // the view goes to the bottom and stays there as the Log File grows. A
    // view that leaves or engages follow asks the View Set, and is handed the
    // outcome here, as every view of the Log File is (#558).
    virtual void followSet( bool follow ) = 0;
    // The font Log Lines are drawn in.
    virtual void updateFont( const QFont& font ) = 0;
    // The words of each Color Label, one list per color slot.
    virtual void setColorLabels( const std::vector<QStringList>& labels ) = 0;
    virtual void setSearchLimits( LineNumber startLine, LineNumber endLine ) = 0;
    // The pattern of the Search whose Matches the view colors.
    virtual void setSearchPattern( const RegularExpressionPattern& pattern ) = 0;

    // Read the Log Lines shown again and repaint: their text changed while
    // the Log File did not, as under a new Decoding Policy.
    virtual void rereadLogLines() = 0;
    // Repaint after Marks, Matches, Highlighters or Color Labels changed.
    virtual void updateDecorations() = 0;
    // The Value Names Collection changed: a view showing Value Names reads
    // the Log Lines it shows again; the Table View shows none (#647).
    virtual void applyValueNamesChange() = 0;
    // Register the shortcuts anew, as the settings now say: they have no
    // Policy, so the view reads them itself.
    virtual void registerShortcuts() = 0;

protected:
    // A view is a widget, owned and destroyed as one; never through this
    // interface.
    ~LogFileView() = default;
};
