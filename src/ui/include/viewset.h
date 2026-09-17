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
#include "settingspolicies.h"

#include <QFont>
#include <QPointer>
#include <QStringList>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

class FilteredView;
class LogPresentation;

// Every view of one Log File: its Presentations and its Filtered Views, those
// of kept Searches included (see CONTEXT.md). Whatever all of them must show
// alike is handed to the View Set, which holds it and hands it to every view;
// a view added later starts with all of it. So nothing else loops over a Log
// File's views to hand them state, and nothing has to wait for the views to
// be built before it is handed over.
//
// Each view keeps its own Decoration setup: a kept Search's Filtered View
// colors a Search pattern of its own, which is not the View Set's.
//
// The Presentations are held through the plain Presentation interface (ADR
// 0003). No view is owned: a Presentation outlives the View Set, and a
// Filtered View destroyed -- its tab closed -- is no longer reached.
class ViewSet {
public:
    // The words of each Color Label, one list per color slot.
    using ColorLabels = std::vector<QStringList>;
    // How many Color Labels there are: one per color slot, each with its own
    // shortcut, as many as ColorLabelsManager holds.
    static constexpr std::size_t ColorLabelCount = 9;

    // Add a view, and hand it everything held so far.
    void addPresentation( LogPresentation* presentation );
    void addFilteredView( FilteredView* view );

    // Hold a piece of state and hand it to every view.
    void setDecorationPolicy( const DecorationPolicy& policy );
    // Each view shows the line numbers the Policy says for its kind, and the
    // Presentations show the Overview as it says.
    void setPresentationPolicy( const PresentationPolicy& policy );
    // Only the Table View reads it.
    void setQuickFindPolicy( const QuickFindPolicy& policy );
    // Whether follow may be engaged at all. Until told otherwise it may.
    void setFollowAllowed( bool allowed );
    // The font Log Lines are drawn in. Until one is set, a view keeps its own.
    void setFont( const QFont& font );
    // The words of every Color Label, one list per color slot.
    void setColorLabels( const ColorLabels& labels );
    // Until they are set, a view keeps the Search Limits it was built with.
    void setSearchLimits( LineNumber startLine, LineNumber endLine );

    // Paint every view again with the Highlighter Sets now active. The Color
    // Labels are handed over again too: a view caches their colors with
    // their words.
    void applyHighlighterSetChange();

    // Register every view's shortcuts anew, the Filtered Views of kept Searches
    // included, after the settings changed. Shortcuts have no Policy: each
    // view reads them from the settings, and a view added later has its own
    // registered by whoever builds it.
    void registerShortcuts();

    // Make every view read the Log Lines it shows again, after the Log File's
    // Decoding Policy was replaced.
    void rereadLogLines();

    // What was handed to the View Set last, which a view added now starts with.
    const DecorationPolicy& decorationPolicy() const
    {
        return decorationPolicy_;
    }
    const PresentationPolicy& presentationPolicy() const
    {
        return presentationPolicy_;
    }
    const QuickFindPolicy& quickFindPolicy() const
    {
        return quickFindPolicy_;
    }
    bool isFollowAllowed() const
    {
        return followAllowed_;
    }
    // None until a font was set.
    const std::optional<QFont>& font() const
    {
        return font_;
    }
    const ColorLabels& colorLabels() const
    {
        return colorLabels_;
    }
    // The first Log Line searched and the end of the Search Limits; none until
    // they were set.
    const std::optional<std::pair<LineNumber, LineNumber>>& searchLimits() const
    {
        return searchLimits_;
    }

private:
    // Hand everything held to one view.
    void seed( LogPresentation* presentation ) const;
    void seed( FilteredView* view ) const;

    // Call fn with each Filtered View not destroyed.
    template <class Fn>
    void forEachFilteredView( Fn&& fn ) const;

    std::vector<LogPresentation*> presentations_;
    std::vector<QPointer<FilteredView>> filteredViews_;

    DecorationPolicy decorationPolicy_;
    PresentationPolicy presentationPolicy_;
    QuickFindPolicy quickFindPolicy_;
    bool followAllowed_ = true;
    std::optional<QFont> font_;
    ColorLabels colorLabels_ = ColorLabels( ColorLabelCount );
    std::optional<std::pair<LineNumber, LineNumber>> searchLimits_;
};
