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

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include <QString>
#include <QStringList>

#include "containers.h"
#include "linetypes.h"

class QMenu;
class QWidget;

// The context menu of a Presentation: the one place deciding which entries it
// offers, in which order, and when each is enabled. The Text View and the
// Table View both build theirs here, as does the Filtered View, which is drawn
// like the Text View.
//
// The menu is built from what the Presentation reports when it opens, and
// each entry calls back into the Presentation it was opened on.
class PresentationMenu {
public:
    // What the Presentation reports as its context menu opens.
    struct Report {
        // The Log Lines selected, whole or in part.
        logsquirl::vector<LineNumber> selectedLogLines;
        // Whether the selection is text within one Log Line, rather than
        // whole Log Lines.
        bool textWithinLogLine = false;
        // The selected text, as the Presentation shows it. Only looked at when
        // one Log Line or text within one is selected, so it need not be
        // reported for more.
        QString selectedText;
        // The Log Line under the cursor, if there is one.
        OptionalLineNumber logLineUnderCursor;
        // Whether any selected Log Line is not Marked.
        bool hasUnmarkedLogLines = false;
        // The words of each Color Label.
        std::vector<QStringList> colorLabels;
        // Whether a selection start has been set (see Entries).
        bool selectionStartSet = false;
        // Whether the Presentation is drawn like the Text View (the Text View
        // or the Filtered View). There Find next and Find previous have the
        // shortcuts * and /, and the Search Limits are set only while one
        // whole Log Line is selected; elsewhere they are set whenever there is
        // a Log Line under the cursor.
        bool drawnLikeTextView = false;
    };

    // What each entry does, on the Presentation the menu was opened on.
    struct Entries {
        std::function<void()> highlightersChange;
        std::function<void( size_t )> addColorLabel;
        std::function<void()> clearColorLabels;
        std::function<void()> mark;
        std::function<void()> copy;
        std::function<void()> copyWithLineNumbers;
        std::function<void()> sendToScratchpad;
        std::function<void()> replaceScratchpad;
        // Find the next or previous Log Line matching the selected text.
        std::function<void()> findNext;
        std::function<void()> findPrevious;
        std::function<void()> replaceSearch;
        std::function<void()> addToSearch;
        std::function<void()> excludeFromSearch;
        // Set the Search Limits to start or end at a Log Line.
        std::function<void( LineNumber )> setSearchStart;
        std::function<void( LineNumber )> setSearchEnd;
        std::function<void()> clearSearchLimits;
        // Only for a Presentation that selects a range of Log Lines from a
        // start to an end; left empty, the menu offers neither entry.
        std::function<void()> setSelectionStart;
        std::function<void()> setSelectionEnd;
        std::function<void()> saveSplitterPosition;
        std::function<void()> saveToFile;
        std::function<void()> saveSelectedToFile;
    };

    // The context menu for what report says, owned by the caller and a child
    // of parent.
    static std::unique_ptr<QMenu> create( QWidget* parent, const Report& report,
                                          const Entries& entries );
};
