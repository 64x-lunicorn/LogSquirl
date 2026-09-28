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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// Building and styling a tab reads the tab names and tab groups from what was
// read at startup, not from the settings store again for every tab (#301).
// What the first scenario hands the tab area stands in the in-memory settings
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

// The path of a Transient Log File is gone after a restart, so neither its tab
// name nor its tab group is stored: an entry for it would never match again.
// In the running application its tab is renamed and grouped as any other; an
// Ordinary Log File's tab keeps storing both (#597).
SCENARIO( "A Transient Log File's tab is renamed and grouped without storing its path",
          "[ui][tabs]" )
{
    const bool transient = GENERATE( true, false );
    CAPTURE( transient );

    const auto path = QStringLiteral( "/tmp/tabbedcrawlerwidget_test_597.log" );
    const auto groupColor = QColor( 0x65, 0x43, 0x21 );

    // The group itself is stored, as a group made from any tab is.
    auto& groups = TabGroupInfo::getSynced();
    const auto groupId = groups.addGroup( QStringLiteral( "Group 597" ), groupColor );
    groups.save();

    TabbedCrawlerWidget tabArea;
    const auto index = tabArea.addCrawler(
        new StubCrawler, path, transient ? LogFileLifetime::Transient : LogFileLifetime::Ordinary );
    REQUIRE( tabArea.holdsTransientLogFile( index ) == transient );

    WHEN( "its tab is renamed and put in a tab group" )
    {
        tabArea.renameTab( index, QStringLiteral( "Renamed 597" ) );
        tabArea.addTabToGroup( index, groupId );

        THEN( "the tab carries the name and the group's color" )
        {
            REQUIRE( tabArea.tabText( index ) == QString::fromUtf8( "● Renamed 597" ) );
            REQUIRE( tabArea.tabBar()->tabTextColor( index ) == groupColor );
            REQUIRE( tabArea.groupOfTab( index )->id == groupId );
        }

        THEN( "the stored tab names and tab groups hold its path only for an Ordinary Log File" )
        {
            REQUIRE( TabNameMapping::getSynced().tabName( path ).isEmpty() == transient );
            REQUIRE( TabGroupInfo::getSynced().groupForTab( path ).has_value() == !transient );

            AND_THEN( "the tab keeps its name and group after the settings are read again" )
            {
                tabArea.refreshAllTabGroupAppearances();
                REQUIRE( tabArea.tabText( index ) == QString::fromUtf8( "● Renamed 597" ) );
                REQUIRE( tabArea.groupOfTab( index )->id == groupId );
            }
        }

        AND_WHEN( "it is taken out of the group and its name is reset" )
        {
            tabArea.removeTabFromGroup( index );
            tabArea.renameTab( index, {} );

            THEN( "it shows its file's name again" )
            {
                REQUIRE( tabArea.tabText( index )
                         == QStringLiteral( "tabbedcrawlerwidget_test_597.log" ) );
                REQUIRE_FALSE( tabArea.groupOfTab( index ).has_value() );
            }
        }
    }

    // Leave the settings store as it was.
    TabNameMapping::getSynced().setTabName( path, {} ).save();
    TabGroupInfo::getSynced().removeGroup( groupId ).save();
}

#include "tabbedcrawlerwidget_test.moc"
