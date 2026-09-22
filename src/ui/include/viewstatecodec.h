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

#ifndef LOGSQUIRL_VIEWSTATECODEC_H
#define LOGSQUIRL_VIEWSTATECODEC_H

#include <utility>

#include <QJsonArray>
#include <QList>
#include <QString>

#include "linetypes.h"
#include "viewinterface.h"

struct QuickFindPolicy;

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

    bool operator==( const ViewState& ) const = default;
};

// The text the Session saves for this view state.
QString encodeViewState( const ViewState& state );

// The view state saved as this text, in the current format (a JSON object)
// or the legacy one glogg wrote ("S400:100:IC0:AR0:FF0"). A field the text
// does not hold is cleared, except whether the Search line reads its pattern
// as a regexp: a view state saved before that was part of one falls back to
// what the QuickFind Policy says. Text that holds nothing readable gives
// defaults; decoding never fails.
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

#endif // LOGSQUIRL_VIEWSTATECODEC_H
