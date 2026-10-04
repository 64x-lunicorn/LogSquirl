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
#include "viewinterface.h"

#include <QJsonArray>
#include <QList>
#include <QString>

#include <utility>

struct QuickFindPolicy;

// A Search of a tab as the Session keeps it: its pattern and how the Search
// Line read it when it was requested (#704). Its results are not kept: a
// restored Search runs again.
struct SavedSearch {
    QString pattern;
    bool ignoreCase = false;
    bool useRegexp = false;
    bool inverseRegexp = false;
    bool useBooleanCombination = false;

    bool operator==( const SavedSearch& ) const = default;
};

// The view state of a tab: what the Session saves for a Log File's views
// and hands back to restore them on the next start (#390).
struct ViewState {
    // The QSplitter sizes of the main and the filtered view.
    QList<int> sizes;

    // The state of the Search's button row.
    bool ignoreCase = false;
    bool autoRefresh = false;
    bool followFile = false;
    bool useRegexp = false;
    bool inverseRegexp = false;
    bool useBooleanCombination = false;

    QList<LineNumber::UnderlyingType> marks;

    // The chart series definitions, as ChartSeriesDefinition::toJson() writes
    // them, and whether the chart panel is shown.
    QJsonArray chartSeries;
    bool chartVisible = false;

    // The Log Line of the text view's Scroll Position: the one at the top of
    // its Viewport. Only the Log Line; which of its Visual Lines was first
    // depends on a width the next start need not have (#559).
    LineNumber::UnderlyingType scrollPosition = 0;

    // Every Search of the tab, in the order its Kept Searches hold them, and
    // which of them is current. A tab has at least one: a view state saved
    // without them has one empty Search, current (#704).
    QList<SavedSearch> searches{ SavedSearch{} };
    qsizetype currentSearch = 0;

    bool operator==( const ViewState& ) const = default;
};

// The text the Session saves for this view state.
QString encodeViewState( const ViewState& state );

// The view state saved as this text, in the current format (a JSON object)
// or the legacy one glogg wrote ("S400:100:IC0:AR0:FF0"). A field the text
// does not hold is cleared, except whether the Search line reads its pattern
// as a regexp: a view state saved before that was part of one falls back to
// what the QuickFind Policy says. Text that holds nothing readable gives
// defaults; decoding never fails. A saved Search it cannot read is left out;
// when none is left, the tab has one empty Search.
ViewState decodeViewState( const QString& encoded, const QuickFindPolicy& quickFindPolicy );

// A view state as the Session asks the views for it.
class ViewStateContext final : public ViewContextInterface {
public:
    explicit ViewStateContext( ViewState state )
        : state_( std::move( state ) )
    {
    }

    QString toString() const override
    {
        return encodeViewState( state_ );
    }

private:
    ViewState state_;
};
