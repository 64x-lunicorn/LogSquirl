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

#include "regularexpressionpattern.h"
#include "searchsessionstate.h"

#include <QObject>
#include <QPointer>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

class FilteredView;
class LogFilteredData;
class OpenLogFile;
class ViewSet;

// The Kept Searches of one Log File (see CONTEXT.md): every Search it has,
// each shown in a Filtered View of its own -- the current one, and those whose
// results the user kept to start another. A Search is added, made current and
// dropped here alone. Making one current tells the Open Log File, which runs
// it, follows the Log File with it and puts the Marks on it, and hands it to
// the View Set, which hands it to every Presentation and the Overview: no
// view is left showing the Marks and Matches of another Search.
//
// Whoever shows the Filtered Views -- the Crawler Widget, one tab each -- only
// maps what the user picks to a Filtered View here, and owns none of the
// Searches.
class KeptSearches : public QObject {
    Q_OBJECT

public:
    // Builds the Filtered View a Search is shown in. The view reads the Search
    // for as long as it lives.
    using BuildView = std::function<FilteredView*( LogFilteredData* search )>;

    // The Open Log File and the View Set of the Log File, and how the
    // Filtered View of each Search is built. The View Set outlives this
    // object.
    KeptSearches( std::shared_ptr<OpenLogFile> openLogFile, ViewSet& viewSet, BuildView buildView );
    ~KeptSearches() override;

    KeptSearches( const KeptSearches& ) = delete;
    KeptSearches& operator=( const KeptSearches& ) = delete;

    // Shows the Open Log File's current Search in a Filtered View built for
    // it: the first Search of the Log File. Returns the view.
    FilteredView* showCurrentSearch();

    // Keeps the current Search with its results, and makes a new one, with no
    // pattern yet, current, shown in a Filtered View built for it. Returns
    // that view.
    FilteredView* startAnother();

    // Makes the Search shown in view current, in the Open Log File and in
    // every view of the Log File. Nothing happens for the current Search's
    // view, which keeps running, or for a view not shown here.
    void makeCurrent( FilteredView* view );

    // Drops the Search shown in view. The view goes too, deleted later, and
    // the Search once its view is gone, so the view never paints a Search
    // freed. The current Search is dropped only when another is left, which
    // is made current first: the one kept after it, else the one before.
    // Returns whether the Search was dropped.
    bool drop( FilteredView* view );

    // The Filtered View of the current Search.
    FilteredView* currentView() const;

    // How many Searches there are, the current one included.
    std::size_t count() const;

    // Requests the current Search for pattern, through the Open Log File, and
    // keeps pattern as what that Search was requested for. Returns the
    // Search's state right after the request.
    SearchSessionState requestCurrent( const RegularExpressionPattern& pattern );
    // No Search is active any longer: the current Search goes idle, requested
    // for nothing.
    void clearCurrent();

    // What each Search was last requested for, in the order they are kept,
    // and which of them is current: what the Session saves of them (#704). A
    // Search requested for nothing has an empty pattern.
    struct Requested {
        std::vector<RegularExpressionPattern> patterns;
        std::size_t current = 0;
    };
    Requested requested() const;

    // Rebuilds the Searches the Session saved, as a user builds them: the
    // first pattern is the first Search's, the Search shown already, and
    // every further one is started as another. The one at current (the first
    // when there is none at current) is made current. Each kept Search with a
    // pattern is requested beside it, before the Log File has first loaded or
    // after; the current one is left to the caller to request, with
    // requestCurrent(), as the Search Line does. One with an empty pattern is
    // not run. Only the first Search may be shown so far, and an empty list
    // rebuilds nothing. Returns the Filtered Views of the Searches, in their
    // order; restoredSearchesFinished() tells once every one with a pattern
    // has finished. One the Open Log File holds beyond the first load
    // (OpenLogFile::holdWaitingSearches(), #779) has not, until it was
    // released and ran, or is held no longer.
    std::vector<FilteredView*> restore( const Requested& saved );

Q_SIGNALS:
    // The current Search's state changed: progress, completion, a failure,
    // as the Open Log File reports it. Told queued, once the change was made,
    // and only while that Search is still current: what a Search told before
    // another was made current is dropped.
    void currentSearchUpdated( SearchSessionState state );

    // Every Search restore() ran has finished -- completed, failed, stopped
    // or found its pattern invalid -- or was dropped. Told once, queued, and
    // also when there was none to run.
    void restoredSearchesFinished();

private:
    struct Kept {
        QPointer<FilteredView> view;
        std::shared_ptr<LogFilteredData> search;
        // What it was last requested for.
        RegularExpressionPattern requested;
    };

    FilteredView* add( std::shared_ptr<LogFilteredData> search );
    std::vector<Kept>::iterator find( const FilteredView* view );
    // Tells restoredSearchesFinished() once none of the Searches restore()
    // ran is running any longer.
    void tellWhenRestoredSearchesFinished();

    // The Searches restore() ran, until they have all finished.
    std::vector<std::weak_ptr<LogFilteredData>> restoredRuns_;
    std::vector<QMetaObject::Connection> restoredRunConnections_;
    bool restoredRunsTold_ = true;

    std::shared_ptr<OpenLogFile> openLogFile_;
    ViewSet& viewSet_;
    BuildView buildView_;
    // In the order they were added.
    std::vector<Kept> searches_;
    QPointer<FilteredView> current_;
    // How many times a Search was made current: what was told before the
    // last time is not told.
    unsigned currentChanges_ = 0;
};
