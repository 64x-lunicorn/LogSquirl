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

#ifndef LOGSQUIRL_DECORATIONSETUP_H
#define LOGSQUIRL_DECORATIONSETUP_H

#include <optional>
#include <vector>

#include <QColor>
#include <QStringList>

#include "containers.h"
#include "highlighter.h"
#include "linedecorator.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "settingspolicies.h"

// The colors that show what a Log Line *is* rather than what its text says:
// whether it is a Match, a Mark, or both at once.
//
// Defined once here because every Presentation shows the same three facts,
// each in the only place it has: the Text View paints them as a gutter
// bullet, the Table View -- which has no gutter -- as the row background,
// the overview strip as a line. They have to agree, and a comment asking two
// copies to stay in step is not what keeps them agreeing.
struct LineStatusColors {
    // The color of a Log Line the current Search selected.
    static QColor match();

    // The color of a Log Line the user flagged by hand.
    static QColor mark();

    // The color of a Log Line that is a Mark and a Match at once. Its own
    // color, so that neither fact hides the other.
    static QColor markedMatch();
};

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
// Selection is deliberately not part of the Context. It arrives in display
// space, from pixel positions against the already tab-expanded text, while
// every other color source matches the raw Log Line; the caller overlays it
// afterwards, in the space it belongs to.
class DecorationSetup {
public:
    // Hand over the settings that color Log Lines. Rebuilds the cached
    // main-search Highlighter, since its colors come from the Policy.
    void setPolicy( const DecorationPolicy& policy );

    // The Policy currently in force, as the caller last handed it over.
    const DecorationPolicy& policy() const;

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
    // moment. Both are passed in rather than stored: the active set is the
    // user's current coloring, which changes without this module hearing of
    // it, and the Text View's limits are known only once it knows which Log
    // Lines it is about to draw.
    LineDecorator::Context context( const HighlighterSet& highlighterSet,
                                    SearchLimits searchLimits ) const;

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

#endif
