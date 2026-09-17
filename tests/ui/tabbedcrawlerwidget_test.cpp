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

#include "tabbedcrawlerwidget.h"
#include "tabgroupinfo.h"
#include "tabnamemapping.h"

#include <QColor>
#include <QTabBar>
#include <QWidget>

#include <catch2/catch.hpp>

// Building and styling a tab reads the tab names and tab groups from what was
// read at startup, not from the settings store again for every tab (#301).
// What these scenarios hand the tab area stands in the in-memory settings
// only, never saved: a tab styled from a fresh read of the settings store
// would not find it.

namespace {

// A Log File's widget as the tab area sees it: it only reports its data status.
class StubCrawler final : public QWidget {
    Q_OBJECT

public:
    Q_SIGNAL void dataStatusChanged( DataStatus status );
};

} // namespace

SCENARIO( "A tab is named and styled from the settings read at startup", "[ui][tabs]" )
{
    const auto path = QStringLiteral( "/logs/tabbedcrawlerwidget_test_301.log" );
    const auto groupColor = QColor( 0x12, 0x34, 0x56 );

    // Read once, at startup, then changed in memory only.
    TabNameMapping::getSynced().setTabName( path, QStringLiteral( "Renamed" ) );
    auto& groups = TabGroupInfo::getSynced();
    const auto groupId = groups.addGroup( QStringLiteral( "Group 301" ), groupColor );
    groups.addTabToGroup( groupId, path );

    TabbedCrawlerWidget tabArea;

    WHEN( "a Log File's tab is added" )
    {
        auto* crawler = new StubCrawler;
        const auto index = tabArea.addCrawler( crawler, path );

        THEN( "it carries the name and the group color read at startup" )
        {
            REQUIRE( tabArea.tabText( index ) == QString::fromUtf8( "● Renamed" ) );
            REQUIRE( tabArea.tabBar()->tabTextColor( index ) == groupColor );
        }
    }

    // Leave the in-memory settings as the settings store has them.
    TabNameMapping::getSynced();
    TabGroupInfo::getSynced();
}

#include "tabbedcrawlerwidget_test.moc"
