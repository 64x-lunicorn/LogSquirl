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

#include "containers.h"
#include "highlighter.h"
#include "linedecorator.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "settingspolicies.h"

#include <QColor>
#include <QStringList>

#include <optional>
#include <vector>

// The one owner of the setup both Presentations need before anything is
// painted: it builds the Line Decorator's Context, and it is the only place
// that builds one.
//
// It takes its settings as a Decoration Policy and never reads the settings
// store, so a test can drive it from a literal -- with no QSettings and no
// widget. What it is handed it keeps; what it derives from that it caches.
//
// Cached, not rebuilt per line: the main-search Highlighter and the Color
// Label Highlighters. A Highlighter compiles its regex lazily on first match
// and keeps the compiled form, so a Highlighter that outlives the repaint is
// what stops the regex being recompiled for every line or every cell drawn.
//
// Selection is deliberately not part of the Context: it is a fact about one
// line, which the caller hands the Line Decorator with each line -- in raw
// columns, as every other color source is matched.
class DecorationSetup {
public:
    // Hand over the settings that color Log Lines. Rebuilds the cached
    // main-search Highlighter, since its colors come from the Policy.
    void setPolicy( const DecorationPolicy& policy );

    // Hand over the main Search's pattern. Rebuilds the cached main-search
    // Highlighter; an empty, boolean or excluding pattern builds none, as
    // none of those colors a Log Line.
    void setSearchPattern( const RegularExpressionPattern& pattern );

    // Hand over the Color Labels: the words the user labelled, one list per
    // color slot, and the color of each slot in the same order. Rebuilds the
    // cached Color Label Highlighters. A word list longer than the colors
    // given keeps only the slots a color exists for.
    void setColorLabels( const std::vector<QStringList>& words,
                         const std::vector<HighlightColor>& colors );

    // Point at the QuickFind pattern to color matches of. Not owned: the
    // caller keeps it alive, and its matcher is read afresh for every
    // Context, so what the user is typing right now is what gets colored.
    // A null pointer means nothing is QuickFound.
    void setQuickFindPattern( const QuickFindPattern* pattern );

    // The Context the Line Decorator resolves every color source against,
    // for the active Highlighter Set and the Search Limits in force at this
    // moment, the Presentation's palette and where it shows a line's Match
    // and Mark. They are passed in rather than stored: the active set is the
    // user's current coloring, which changes without this module hearing of
    // it, the Text View's limits are known only once it knows which Log
    // Lines it is about to draw, and the palette belongs to the widget.
    LineDecorator::Context context( const HighlighterSet& highlighterSet, SearchLimits searchLimits,
                                    const LinePalette& palette,
                                    LineStatusDisplay lineStatus ) const;

private:
    // Rebuild the cached main-search Highlighter from the Policy and the
    // current pattern.
    void rebuildMainSearch();

    // Rebuild the cached Color Label Highlighters from the current words
    // and colors.
    void rebuildColorLabels();

    DecorationPolicy policy_;
    RegularExpressionPattern searchPattern_;
    std::vector<QStringList> colorLabelWords_;
    std::vector<HighlightColor> colorLabelColors_;
    const QuickFindPattern* quickFindPattern_ = nullptr;

    std::optional<Highlighter> cachedMainSearch_;
    logsquirl::vector<Highlighter> cachedColorLabels_;
};
