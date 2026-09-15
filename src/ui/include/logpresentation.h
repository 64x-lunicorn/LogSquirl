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

#include "linetypes.h"

class QFont;
class QPoint;
class QString;

// A Presentation, as whoever coordinates a Log File's panes asks and tells it
// things: the Text View or the Table View. See ADR 0003.
//
// This is half of what a Presentation is. The other half is the set of
// signals the Text View emits (newSelection, markLines, addToSearch,
// excludeFromSearch, replaceSearch, changeSearchLimits, clearSearchLimits,
// addColorLabel, clearColorLabels, sendSelectionToScratchpad,
// replaceScratchpadWithSelection, ...): every Presentation emits the same
// ones, with the same arguments and meaning, and the coordinator connects
// each Presentation to the same slots. Nothing but convention keeps the two
// halves in step, so a signal added to one Presentation is added to all.
//
// Everything a Presentation hands out, or is handed, is a Log Line, never a
// position in its own widget.
class LogPresentation {
public:
    // The selected text, as the Presentation shows it.
    virtual QString selectedText() const = 0;

    // The Log Line shown at pos, in the coordinates of the Presentation's
    // viewport; nothing where no Log Line is shown.
    virtual OptionalLineNumber logLineAt( const QPoint& pos ) const = 0;

    // Select the Log Line and bring it into view.
    virtual void showLogLine( LineNumber line ) = 0;
    // Select nSymbols characters from startCol of the Log Line, and bring it
    // into view. A Presentation that cannot select characters there selects
    // the whole Log Line.
    virtual void showLogLinePortion( LineNumber line, LinesCount nLines, LineColumn startCol,
                                     LineLength nSymbols ) = 0;

    // Repaint after Marks, Matches, Highlighters or Color Labels changed.
    virtual void updateDecorations() = 0;

    virtual void updateFont( const QFont& font ) = 0;

    // Save the selected Log Lines to filename, behind a progress dialog.
    // Nothing is saved without a selection.
    virtual void saveSelectedTo( const QString& filename ) = 0;

protected:
    // A Presentation is a widget, owned and destroyed as one; never through
    // this interface.
    ~LogPresentation() = default;
};
