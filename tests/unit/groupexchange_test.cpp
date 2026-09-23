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

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

#include "groupexchange.h"

using namespace logsquirl::groupexchange;

namespace {

PredefinedFilterSet makeFilterSet( const QString& name )
{
    auto set = PredefinedFilterSet::createNewSet( name );
    set.addFilter( { "Errors", "ERROR|FATAL", true } );
    set.addFilter( { "Plain", "timeout", false } );
    return set;
}

HighlighterSet makeHighlighterSet( const QString& name )
{
    auto set = HighlighterSet::createNewSet( name );
    set.addHighlighter( Highlighter( "ERROR", false, true, Qt::red, Qt::white ) );
    set.addHighlighter( Highlighter( "WARN", true, false, Qt::black, Qt::yellow ) );
    return set;
}

} // namespace

TEST_CASE( "Group Exchange proposes a file name from the group name", "[groupexchange]" )
{
    CHECK( suggestedFileName( "Network", GroupKind::Filter ) == "Network_filter.conf" );
    CHECK( suggestedFileName( "Network", GroupKind::Highlighter ) == "Network_highlighter.conf" );
    CHECK( suggestedFileName( "Default", GroupKind::Filter ) == "Default_filter.conf" );
}

TEST_CASE( "Group Exchange replaces the characters a file name cannot hold", "[groupexchange]" )
{
    CHECK( suggestedFileName( "HTTP 4xx/5xx", GroupKind::Filter ) == "HTTP 4xx_5xx_filter.conf" );
    CHECK( suggestedFileName( R"(a/b\c:d*e?f"g<h>i|j)", GroupKind::Highlighter )
           == "a_b_c_d_e_f_g_h_i_j_highlighter.conf" );
    CHECK( suggestedFileName( "Ünïcode ok", GroupKind::Filter ) == "Ünïcode ok_filter.conf" );
}

TEST_CASE( "Group Exchange appends .conf to a typed name that lacks it", "[groupexchange]" )
{
    CHECK( withConfSuffix( "/tmp/mine" ) == "/tmp/mine.conf" );
    CHECK( withConfSuffix( "/tmp/mine.conf" ) == "/tmp/mine.conf" );
}

TEST_CASE( "Group Exchange remembers the export folder for the session only", "[groupexchange]" )
{
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    rememberExportFolder( dir.filePath( "x_filter.conf" ) );
    CHECK( QDir( exportFolder() ) == QDir( dir.path() ) );
}

TEST_CASE( "A written Filter Group file reads back as exactly that group", "[groupexchange]" )
{
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto file = dir.filePath( "one_filter.conf" );

    // The file held other groups before: they must be gone.
    {
        PredefinedFiltersCollection previous;
        previous.setFilterSets( { makeFilterSet( "Old 1" ), makeFilterSet( "Old 2" ) } );
        QSettings settings{ file, QSettings::IniFormat };
        previous.saveToStorage( settings );
    }

    const auto group = makeFilterSet( "Network" );
    REQUIRE( writeGroup( file, group ) );

    QSettings settings{ file, QSettings::IniFormat };
    PredefinedFiltersCollection read;
    read.retrieveFromStorage( settings );

    QList<PredefinedFilterSet> withoutDefault;
    for ( const auto& set : read.filterSets() ) {
        if ( set.id() != defaultFilterSetId() ) {
            withoutDefault.append( set );
        }
    }
    REQUIRE( withoutDefault.size() == 1 );
    CHECK( withoutDefault.front().id() == group.id() );
    CHECK( withoutDefault.front().name() == "Network" );
    REQUIRE( withoutDefault.front().filters().size() == 2 );
    CHECK( withoutDefault.front().filters()[ 0 ].pattern == "ERROR|FATAL" );
    CHECK( withoutDefault.front().filters()[ 1 ].useRegex == false );
}

TEST_CASE( "The Default Filter Group can be written", "[groupexchange]" )
{
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto file = dir.filePath( "Default_filter.conf" );

    QSettings ownSettings{ dir.filePath( "own.conf" ), QSettings::IniFormat };
    PredefinedFiltersCollection own;
    own.retrieveFromStorage( ownSettings );
    PredefinedFilterSet defaultSet;
    for ( const auto& set : own.filterSets() ) {
        if ( set.id() == defaultFilterSetId() ) {
            defaultSet = set;
        }
    }
    REQUIRE( defaultSet.id() == defaultFilterSetId() );
    defaultSet.addFilter( { "Mine", "abc", true } );

    REQUIRE( writeGroup( file, defaultSet ) );

    QSettings settings{ file, QSettings::IniFormat };
    PredefinedFiltersCollection read;
    read.retrieveFromStorage( settings );
    REQUIRE( read.filterSets().size() == 1 );
    CHECK( read.filterSets().front().id() == defaultFilterSetId() );
    CHECK( read.filterSets().front().filters().size() == 1 );
}

TEST_CASE( "A written Highlighter Set file holds that set and no Color Labels or active sets",
           "[groupexchange]" )
{
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto file = dir.filePath( "one_highlighter.conf" );

    const auto group = makeHighlighterSet( "Levels" );
    REQUIRE( writeGroup( file, group ) );

    {
        QSettings raw{ file, QSettings::IniFormat };
        CHECK( raw.value( "HighlighterSetCollection/active_sets" ).toStringList().isEmpty() );
        CHECK( raw.value( "HighlighterSetCollection/quick/size", 0 ).toInt() == 0 );
        for ( const auto& key : raw.allKeys() ) {
            if ( key != "HighlighterSetCollection/quick/size" ) {
                CHECK( !key.startsWith( "HighlighterSetCollection/quick/" ) );
            }
        }
        CHECK( raw.value( "HighlighterSetCollection/sets/size" ).toInt() == 1 );
    }

    QSettings settings{ file, QSettings::IniFormat };
    HighlighterSetCollection read;
    read.retrieveFromStorage( settings );
    REQUIRE( read.highlighterSets().size() == 1 );
    CHECK( read.highlighterSets().front().id() == group.id() );
    CHECK( read.highlighterSets().front().name() == "Levels" );
    CHECK( read.activeSetIds().isEmpty() );
}

TEST_CASE( "Writing a group reports a file that cannot be written", "[groupexchange]" )
{
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // A directory stands where the file should go.
    CHECK( !writeGroup( dir.path(), makeFilterSet( "X" ) ) );
}
