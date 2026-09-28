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
#include <QDir>
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

// A Log File decompressed from an archive is read from a temporary path that
// changes at every start. Its tab's name and group are stored by the key it was
// added with, its archive and member, so that they name it again (#609).
SCENARIO( "A tab added with a stored key keeps its name and group by that key", "[ui][tabs]" )
{
    const auto path = QStringLiteral( "/tmp/logsquirl/app.log.gz.AbCdEf" );
    const auto key = QStringLiteral( "/logs/tabbedcrawlerwidget_test_609.log.gz!/" );
    const auto groupColor = QColor( 0x60, 0x90, 0x09 );

    auto& groups = TabGroupInfo::getSynced();
    const auto groupId = groups.addGroup( QStringLiteral( "Group 609" ), groupColor );
    groups.save();

    {
        TabbedCrawlerWidget tabArea;
        const auto index
            = tabArea.addCrawler( new StubCrawler, path, LogFileLifetime::Ordinary, key );
        tabArea.renameTab( index, QStringLiteral( "Renamed 609" ) );
        tabArea.addTabToGroup( index, groupId );

        // Stored by the key, never by the path.
        REQUIRE( TabNameMapping::getSynced().tabName( key ) == QStringLiteral( "Renamed 609" ) );
        REQUIRE( TabNameMapping::get().tabName( path ).isEmpty() );
        REQUIRE( TabGroupInfo::getSynced().groupForTab( key )->id == groupId );
        REQUIRE_FALSE( TabGroupInfo::get().groupForTab( path ).has_value() );
    }

    WHEN( "the same member is added again under another temporary path" )
    {
        TabbedCrawlerWidget tabArea;
        const auto index
            = tabArea.addCrawler( new StubCrawler, QStringLiteral( "/tmp/other/app.log.gz.GhIjKl" ),
                                  LogFileLifetime::Ordinary, key );

        THEN( "its tab has the name and the group" )
        {
            REQUIRE( tabArea.tabText( index ) == QString::fromUtf8( "● Renamed 609" ) );
            REQUIRE( tabArea.groupOfTab( index )->id == groupId );
        }

        AND_WHEN( "it is taken out of the group and its name is reset" )
        {
            tabArea.removeTabFromGroup( index );
            tabArea.renameTab( index, {} );

            THEN( "neither is stored for the key any longer" )
            {
                REQUIRE( TabNameMapping::getSynced().tabName( key ).isEmpty() );
                REQUIRE_FALSE( TabGroupInfo::getSynced().groupForTab( key ).has_value() );
                REQUIRE( tabArea.tabText( index ) == QStringLiteral( "app.log.gz.GhIjKl" ) );
            }
        }
    }

    // Leave the settings store as it was.
    TabNameMapping::getSynced().setTabName( key, {} ).save();
    TabGroupInfo::getSynced().removeGroup( groupId ).save();
}

// Standard input, a data source and a merge open a tab under a title of their
// own, not their temporary file's name. Grouping, the Manage Tab Groups dialog
// and a reset of a rename restyle the tabs: each gets back that title, never
// the file's name (#606).
SCENARIO( "A tab keeps the title it opened with", "[ui][tabs]" )
{
    const bool withToolTip = GENERATE( true, false );
    CAPTURE( withToolTip );

    const auto path = QStringLiteral( "/tmp/tabbedcrawlerwidget_test_606.log" );
    const auto otherPath = QStringLiteral( "/logs/tabbedcrawlerwidget_test_606_other.log" );
    const auto toolTip
        = withToolTip ? QStringLiteral( "Standard input\n%1" ).arg( path ) : QString{};
    const auto groupColor = QColor( 0x60, 0x60, 0x06 );

    auto& groups = TabGroupInfo::getSynced();
    const auto groupId = groups.addGroup( QStringLiteral( "Group 606" ), groupColor );
    groups.save();

    TabbedCrawlerWidget tabArea;
    tabArea.setOpeningTitle( path, QStringLiteral( "stdin" ), toolTip );
    const auto index = tabArea.addCrawler( new StubCrawler, path, LogFileLifetime::Transient );
    const auto other = tabArea.addCrawler( new StubCrawler, otherPath );

    const auto expectedToolTip = withToolTip ? toolTip : QDir::toNativeSeparators( path );
    REQUIRE( tabArea.tabText( index ) == QStringLiteral( "stdin" ) );
    REQUIRE( tabArea.tabToolTip( index ) == expectedToolTip );

    WHEN( "another tab is put in a tab group and taken out of it" )
    {
        tabArea.addTabToGroup( other, groupId );
        const auto whileGrouped = tabArea.tabText( index );
        tabArea.removeTabFromGroup( other );

        THEN( "the tab keeps its title and tooltip" )
        {
            REQUIRE( whileGrouped == QStringLiteral( "stdin" ) );
            REQUIRE( tabArea.tabText( index ) == QStringLiteral( "stdin" ) );
            REQUIRE( tabArea.tabToolTip( index ) == expectedToolTip );
        }
    }

    WHEN( "the tabs are restyled, as closing the Manage Tab Groups dialog does" )
    {
        tabArea.refreshAllTabGroupAppearances();

        THEN( "the tab keeps its title" )
        {
            REQUIRE( tabArea.tabText( index ) == QStringLiteral( "stdin" ) );
        }
    }

    WHEN( "the tab itself is put in a tab group" )
    {
        tabArea.addTabToGroup( index, groupId );

        THEN( "it shows the group before its title" )
        {
            REQUIRE( tabArea.tabText( index ) == QString::fromUtf8( "● stdin" ) );
        }
    }

    WHEN( "the tab is renamed" )
    {
        tabArea.renameTab( index, QStringLiteral( "Renamed 606" ) );

        THEN( "the rename wins over the title" )
        {
            REQUIRE( tabArea.tabText( index ) == QStringLiteral( "Renamed 606" ) );
        }

        AND_WHEN( "its name is reset" )
        {
            tabArea.renameTab( index, {} );

            THEN( "it shows its title again, not its file's name" )
            {
                REQUIRE( tabArea.tabText( index ) == QStringLiteral( "stdin" ) );
            }
        }
    }

    WHEN( "the tab is closed and its file opened again" )
    {
        tabArea.removeCrawler( index );
        const auto reopened
            = tabArea.addCrawler( new StubCrawler, path, LogFileLifetime::Transient );

        THEN( "the title went with the tab" )
        {
            REQUIRE( tabArea.tabText( reopened )
                     == QStringLiteral( "tabbedcrawlerwidget_test_606.log" ) );
            REQUIRE( tabArea.tabToolTip( reopened ) == QDir::toNativeSeparators( path ) );
        }
    }

    // Leave the settings store as it was.
    TabGroupInfo::getSynced().removeGroup( groupId ).save();
}

#include "tabbedcrawlerwidget_test.moc"
