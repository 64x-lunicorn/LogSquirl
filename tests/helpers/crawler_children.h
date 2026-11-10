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

#ifndef CRAWLER_CHILDREN_H
#define CRAWLER_CHILDREN_H

// The child widgets of a tab's CrawlerWidget a test drives, found as a user
// finds them -- by what they are, here their object names
// (CrawlerWidget::ChildName) -- rather than through its private members
// (#945). What the CrawlerWidget is, it says through its State.

#include <catch2/catch_test_macros.hpp>

#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>

#include "chartpanel.h"
#include "crawlerwidget.h"
#include "filteredview.h"
#include "logmainview.h"
#include "logpresentation.h"
#include "logtableview.h"
#include "searchlinewidget.h"

namespace crawler_children {
// The child of crawler named name; a test fails without one.
template <class Child>
Child& named( const CrawlerWidget& crawler, const char* name )
{
    auto* child = crawler.findChild<Child*>( QString::fromLatin1( name ) );
    REQUIRE( child != nullptr );
    return *child;
}
} // namespace crawler_children

inline SearchLineWidget& searchLineOf( const CrawlerWidget& crawler )
{
    return crawler_children::named<SearchLineWidget>( crawler,
                                                      CrawlerWidget::ChildName::SearchLine );
}

inline LogMainView& textViewOf( const CrawlerWidget& crawler )
{
    return crawler_children::named<LogMainView>( crawler, CrawlerWidget::ChildName::TextView );
}

inline LogTableView& tableViewOf( const CrawlerWidget& crawler )
{
    return crawler_children::named<LogTableView>( crawler, CrawlerWidget::ChildName::TableView );
}

// The button that shows the Table View, or the Text View again.
inline QToolButton& tableViewToggleOf( const CrawlerWidget& crawler )
{
    return crawler_children::named<QToolButton>( crawler,
                                                 CrawlerWidget::ChildName::TableViewToggle );
}

// The Presentation the upper pane shows: the Text View or the Table View.
inline LogPresentation* shownPresentationOf( const CrawlerWidget& crawler )
{
    return dynamic_cast<LogPresentation*>(
        crawler_children::named<QStackedWidget>( crawler, CrawlerWidget::ChildName::Presentations )
            .currentWidget() );
}

// The tabs of the Filtered Views, one for each Search.
inline QTabWidget& filteredViewTabsOf( const CrawlerWidget& crawler )
{
    return crawler_children::named<QTabWidget>( crawler,
                                                CrawlerWidget::ChildName::FilteredViewTabs );
}

// The Filtered View in tab index, in front or not.
inline FilteredView* filteredViewOf( const CrawlerWidget& crawler, int index )
{
    return qobject_cast<FilteredView*>( filteredViewTabsOf( crawler ).widget( index ) );
}

// The Filtered View in front, as the State says: the current Search's.
inline FilteredView* frontFilteredViewOf( const CrawlerWidget& crawler )
{
    return filteredViewOf( crawler, crawler.state().frontFilteredView );
}

inline ChartPanel& chartPanelOf( const CrawlerWidget& crawler )
{
    return crawler_children::named<ChartPanel>( crawler, CrawlerWidget::ChildName::ChartPanel );
}

#endif
