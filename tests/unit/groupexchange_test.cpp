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
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <memory>

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

// --- Import: read and merge ---

namespace {

// A resolver that always gives the same answer and records the questions.
struct FixedAnswer {
    ConflictAnswer answer;
    bool applyToAll = false;
    std::shared_ptr<QList<ConflictQuestion>> asked = std::make_shared<QList<ConflictQuestion>>();

    ConflictResolver resolver() const
    {
        return [ *this ]( const ConflictQuestion& question ) -> ConflictDecision {
            asked->append( question );
            return { answer, applyToAll };
        };
    }
};

PredefinedFilterSet filterGroup( const QString& id, const QString& name, const QString& pattern )
{
    auto set = PredefinedFilterSet::createNewSet( name ).withId( id );
    set.addFilter( { "f", pattern, true } );
    return set;
}

QString patternOf( const PredefinedFilterSet& set )
{
    return set.filters().isEmpty() ? QString() : set.filters().front().pattern;
}

QStringList names( const QList<PredefinedFilterSet>& groups )
{
    QStringList result;
    for ( const auto& group : groups ) {
        result.append( group.name() );
    }
    return result;
}

} // namespace

TEST_CASE( "The first free name is the name itself or <name> (n)", "[groupexchange]" )
{
    CHECK( firstFreeName( "Net", {} ) == "Net" );
    CHECK( firstFreeName( "Net", { "Net" } ) == "Net (2)" );
    CHECK( firstFreeName( "Net", { "Net", "Net (2)", "Net (4)" } ) == "Net (3)" );
}

TEST_CASE( "Each kind of conflict preselects its answer", "[groupexchange]" )
{
    CHECK( preselectedAnswer( ConflictKind::SameId ) == ConflictAnswer::Replace );
    CHECK( preselectedAnswer( ConflictKind::SameName ) == ConflictAnswer::KeepBoth );
}

TEST_CASE( "A group without a conflict is added without a question", "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( "a", "A", "1" ) };
    FixedAnswer fixed{ ConflictAnswer::Skip };
    ImportSession session( fixed.resolver() );

    const auto result = mergeGroups( groups, { filterGroup( "b", "B", "2" ) }, session );

    CHECK( result.added == 1 );
    CHECK( fixed.asked->isEmpty() );
    CHECK( names( groups ) == QStringList{ "A", "B" } );
}

TEST_CASE( "The same id asks with Replace preselected", "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( "a", "A", "1" ) };
    FixedAnswer fixed{ ConflictAnswer::Skip };
    ImportSession session( fixed.resolver() );

    mergeGroups( groups, { filterGroup( "a", "A renamed", "2" ) }, session );

    REQUIRE( fixed.asked->size() == 1 );
    CHECK( fixed.asked->front().kind == ConflictKind::SameId );
    CHECK( fixed.asked->front().preselected == ConflictAnswer::Replace );
    CHECK( fixed.asked->front().existingName == "A" );
    CHECK( fixed.asked->front().importedName == "A renamed" );
}

TEST_CASE( "Only the same name asks with Keep both preselected", "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( "a", "A", "1" ) };
    FixedAnswer fixed{ ConflictAnswer::Skip };
    ImportSession session( fixed.resolver() );

    mergeGroups( groups, { filterGroup( "z", "A", "2" ) }, session );

    REQUIRE( fixed.asked->size() == 1 );
    CHECK( fixed.asked->front().kind == ConflictKind::SameName );
    CHECK( fixed.asked->front().preselected == ConflictAnswer::KeepBoth );
}

TEST_CASE( "Replace keeps the position and the id of the existing group", "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( "a", "A", "1" ), filterGroup( "b", "B", "2" ),
                                       filterGroup( "c", "C", "3" ) };
    FixedAnswer fixed{ ConflictAnswer::Replace };
    ImportSession session( fixed.resolver() );

    SECTION( "by id" )
    {
        const auto result = mergeGroups( groups, { filterGroup( "b", "B2", "new" ) }, session );
        CHECK( result.replaced == 1 );
        CHECK( names( groups ) == QStringList{ "A", "B2", "C" } );
    }
    SECTION( "by name" )
    {
        const auto result = mergeGroups( groups, { filterGroup( "x", "B", "new" ) }, session );
        CHECK( result.replaced == 1 );
        CHECK( names( groups ) == QStringList{ "A", "B", "C" } );
    }
    CHECK( groups[ 1 ].id() == "b" );
    CHECK( patternOf( groups[ 1 ] ) == "new" );
    CHECK( groups.size() == 3 );
}

TEST_CASE( "Replacing a Highlighter Set keeps its id, so an active set stays active",
           "[groupexchange]" )
{
    auto existing = makeHighlighterSet( "Levels" );
    QList<HighlighterSet> groups{ existing };
    auto incoming = makeHighlighterSet( "Levels" ).withId( existing.id() );
    incoming.addHighlighter( Highlighter( "DEBUG", false, true, Qt::gray, Qt::white ) );

    FixedAnswer fixed{ ConflictAnswer::Replace };
    ImportSession session( fixed.resolver() );
    mergeGroups( groups, { incoming }, session );

    REQUIRE( groups.size() == 1 );
    CHECK( groups.front().id() == existing.id() );

    // The active sets are kept by id: the collection still finds the set.
    HighlighterSetCollection collection;
    collection.setHighlighterSets( groups );
    collection.activateSet( existing.id() );
    CHECK( collection.activeSetIds() == QStringList{ existing.id() } );
}

TEST_CASE( "Keep both adds a fresh id and the first free name", "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( "a", "A", "1" ),
                                       filterGroup( "b", "A (2)", "2" ) };
    FixedAnswer fixed{ ConflictAnswer::KeepBoth };
    ImportSession session( fixed.resolver() );

    const auto result = mergeGroups( groups, { filterGroup( "a", "A", "new" ) }, session );

    CHECK( result.added == 1 );
    REQUIRE( groups.size() == 3 );
    CHECK( groups[ 2 ].name() == "A (3)" );
    CHECK( groups[ 2 ].id() != "a" );
    CHECK( !groups[ 2 ].id().isEmpty() );
    CHECK( patternOf( groups[ 2 ] ) == "new" );
    CHECK( patternOf( groups[ 0 ] ) == "1" );
}

TEST_CASE( "Skip leaves the list unchanged", "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( "a", "A", "1" ) };
    FixedAnswer fixed{ ConflictAnswer::Skip };
    ImportSession session( fixed.resolver() );

    const auto result = mergeGroups( groups, { filterGroup( "a", "A", "2" ) }, session );

    CHECK( result.skipped == 1 );
    REQUIRE( groups.size() == 1 );
    CHECK( patternOf( groups.front() ) == "1" );
}

TEST_CASE( "Apply to all covers the rest of the import across files", "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( "a", "A", "1" ), filterGroup( "b", "B", "2" ),
                                       filterGroup( "c", "C", "3" ) };
    FixedAnswer fixed{ ConflictAnswer::Replace, true };
    ImportSession session( fixed.resolver() );

    // Two files of one import.
    mergeGroups( groups, { filterGroup( "a", "A", "x" ), filterGroup( "b", "B", "y" ) }, session );
    mergeGroups( groups, { filterGroup( "c", "C", "z" ) }, session );

    CHECK( fixed.asked->size() == 1 );
    CHECK( patternOf( groups[ 0 ] ) == "x" );
    CHECK( patternOf( groups[ 1 ] ) == "y" );
    CHECK( patternOf( groups[ 2 ] ) == "z" );

    // A new import asks again.
    ImportSession next( fixed.resolver() );
    mergeGroups( groups, { filterGroup( "a", "A", "again" ) }, next );
    CHECK( fixed.asked->size() == 2 );
}

TEST_CASE( "Without apply to all every conflict is asked", "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( "a", "A", "1" ), filterGroup( "b", "B", "2" ) };
    FixedAnswer fixed{ ConflictAnswer::Skip };
    ImportSession session( fixed.resolver() );

    mergeGroups( groups, { filterGroup( "a", "A", "x" ), filterGroup( "b", "B", "y" ) }, session );
    CHECK( fixed.asked->size() == 2 );
}

TEST_CASE( "A group with the Default id never replaces the Default group", "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( defaultFilterSetId(), "Default", "own" ) };
    const auto incoming = filterGroup( defaultFilterSetId(), "Default", "theirs" );

    SECTION( "with an answer that keeps both" )
    {
        FixedAnswer fixed{ ConflictAnswer::KeepBoth };
        ImportSession session( fixed.resolver() );
        mergeGroups( groups, { incoming }, session );

        REQUIRE( fixed.asked->size() == 1 );
        // The id is not a conflict; the name is.
        CHECK( fixed.asked->front().kind == ConflictKind::SameName );
        REQUIRE( groups.size() == 2 );
        CHECK( groups[ 0 ].id() == defaultFilterSetId() );
        CHECK( patternOf( groups[ 0 ] ) == "own" );
        CHECK( groups[ 1 ].id() != defaultFilterSetId() );
        CHECK( groups[ 1 ].name() == "Default (2)" );
        CHECK( patternOf( groups[ 1 ] ) == "theirs" );
    }
    SECTION( "even with Replace" )
    {
        FixedAnswer fixed{ ConflictAnswer::Replace };
        ImportSession session( fixed.resolver() );
        mergeGroups( groups, { incoming }, session );

        REQUIRE( fixed.asked->size() == 1 );
        CHECK( !fixed.asked->front().replaceAllowed );
        CHECK( patternOf( groups[ 0 ] ) == "own" );
        REQUIRE( groups.size() == 2 );
        CHECK( groups[ 1 ].id() != defaultFilterSetId() );
    }
}

TEST_CASE( "A Default id group without a name conflict arrives under a fresh id",
           "[groupexchange]" )
{
    QList<PredefinedFilterSet> groups{ filterGroup( defaultFilterSetId(), "Standard", "own" ) };
    FixedAnswer fixed{ ConflictAnswer::Skip };
    ImportSession session( fixed.resolver() );

    mergeGroups( groups, { filterGroup( defaultFilterSetId(), "Default", "theirs" ) }, session );

    CHECK( fixed.asked->isEmpty() );
    REQUIRE( groups.size() == 2 );
    CHECK( groups[ 1 ].id() != defaultFilterSetId() );
}

TEST_CASE( "A file of earlier versions holding many groups imports each through the rules",
           "[groupexchange]" )
{
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto file = dir.filePath( "old.conf" );
    {
        HighlighterSetCollection old;
        old.setHighlighterSets( { makeHighlighterSet( "One" ), makeHighlighterSet( "Two" ),
                                  makeHighlighterSet( "Three" ) } );
        old.activateSet( old.highlighterSets().front().id() );
        old.setQuickHighlighters( { { "Mine", { Qt::red, Qt::white }, true } } );
        QSettings settings{ file, QSettings::IniFormat };
        old.saveToStorage( settings );
    }

    QList<HighlighterSet> groups{ makeHighlighterSet( "Two" ) };
    FixedAnswer fixed{ ConflictAnswer::KeepBoth };
    ImportSession session( fixed.resolver() );
    const auto result = importFile( file, groups, session );

    CHECK( result.error == ReadError::None );
    CHECK( result.added == 3 );
    CHECK( fixed.asked->size() == 1 );
    REQUIRE( groups.size() == 4 );
    CHECK( groups[ 1 ].name() == "One" );
    CHECK( groups[ 2 ].name() == "Two (2)" );
    CHECK( groups[ 3 ].name() == "Three" );
}

TEST_CASE( "A Filter Group file imports its group and not a Default group it did not hold",
           "[groupexchange]" )
{
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto file = dir.filePath( "one_filter.conf" );
    REQUIRE( writeGroup( file, makeFilterSet( "Network" ) ) );

    QList<PredefinedFilterSet> groups{ filterGroup( defaultFilterSetId(), "Default", "own" ) };
    FixedAnswer fixed{ ConflictAnswer::Skip };
    ImportSession session( fixed.resolver() );
    const auto result = importFile( file, groups, session );

    CHECK( result.error == ReadError::None );
    CHECK( fixed.asked->isEmpty() );
    CHECK( names( groups ) == QStringList{ "Default", "Network" } );
}

TEST_CASE( "An unreadable or empty file is reported and changes nothing", "[groupexchange]" )
{
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QList<PredefinedFilterSet> filters{ filterGroup( "a", "A", "1" ) };
    QList<HighlighterSet> highlighters{ makeHighlighterSet( "H" ) };
    FixedAnswer fixed{ ConflictAnswer::Replace };
    ImportSession session( fixed.resolver() );

    SECTION( "a file that is not there" )
    {
        const auto missing = dir.filePath( "missing.conf" );
        CHECK( importFile( missing, filters, session ).error == ReadError::Unreadable );
        CHECK( importFile( missing, highlighters, session ).error == ReadError::Unreadable );
        CHECK( !QFileInfo::exists( missing ) );
    }
    SECTION( "a directory" )
    {
        CHECK( importFile( dir.path(), filters, session ).error == ReadError::Unreadable );
    }
    SECTION( "an empty file" )
    {
        const auto empty = dir.filePath( "empty.conf" );
        QFile emptyFile( empty );
        REQUIRE( emptyFile.open( QIODevice::WriteOnly ) );
        emptyFile.close();
        CHECK( importFile( empty, filters, session ).error == ReadError::NoGroups );
        CHECK( importFile( empty, highlighters, session ).error == ReadError::NoGroups );
    }
    SECTION( "a file with other settings but no groups" )
    {
        const auto other = dir.filePath( "other.conf" );
        {
            QSettings settings{ other, QSettings::IniFormat };
            settings.setValue( "General/x", 1 );
        }
        CHECK( importFile( other, filters, session ).error == ReadError::NoGroups );
        CHECK( importFile( other, highlighters, session ).error == ReadError::NoGroups );
    }

    CHECK( filters.size() == 1 );
    CHECK( highlighters.size() == 1 );
    CHECK( fixed.asked->isEmpty() );
}
