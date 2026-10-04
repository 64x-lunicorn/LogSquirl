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

// The Team Folder against real Git repositories in a temporary directory: a
// bare repository as the team's server, reached through a file:// URL, and a
// Team Folder for each of two team members. Git runs with an identity of the
// test's own and without the user's global and system configuration, so the
// real ~/.gitconfig is neither read nor changed.
//
// Where no Git is installed, every case checks that the Team Folder reports
// the missing Git instead.

#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "groupexchange.h"
#include "naminggroup.h"
#include "teamfolder.h"
#include "teamfoldergit.h"
#include "teamfoldertesting.h"

using logsquirl::teamfolder::FailureHint;
using logsquirl::teamfolder::failureHintOf;
using logsquirl::teamfolder::Git;
using logsquirl::teamfolder::groupOfKind;
using logsquirl::teamfolder::SyncStep;
using logsquirl::teamfolder::TeamGroupChanges;
using logsquirl::valuenames::GroupTable;
using logsquirl::valuenames::NameRow;
using logsquirl::valuenames::NameTable;
using logsquirl::valuenames::NamingGroup;
using logsquirl::valuenames::NamingRule;
using namespace logsquirl::groupexchange;
using namespace teamfolder_testing;

namespace {

PredefinedFilterSet makeGroup( const QString& name, const QString& pattern = "ERROR" )
{
    auto group = PredefinedFilterSet::createNewSet( name );
    group.addFilter( { "Errors", pattern, true } );
    return group;
}

HighlighterSet makeSet( const QString& name, const QString& pattern = "ERROR" )
{
    auto set = HighlighterSet::createNewSet( name );
    set.addHighlighter( Highlighter( pattern, false, true, Qt::red, Qt::white ) );
    return set;
}

NamingGroup makeNamingGroup( const QString& name, const QString& ecuName = "Beispiel" )
{
    auto group = NamingGroup::createNewGroup( name );
    NamingRule rule;
    rule.name = "ECU";
    rule.pattern = "ECU (0x[0-9A-F]{2})";
    rule.groupTables = { GroupTable{ "1", "ECU" } };
    group.setRules( { rule } );
    group.setTables( { NameTable{ "ECU", { NameRow{ "0x15", ecuName } } } } );
    return group;
}

QString ecuNameOf( const NamingGroup& group )
{
    return group.tables().isEmpty() || group.tables().front().rows.isEmpty()
               ? QString()
               : group.tables().front().rows.front().name;
}

QStringList namesOf( const QList<NamingGroup>& groups )
{
    QStringList names;
    for ( const auto& group : groups ) {
        names.append( group.name() );
    }
    return names;
}

QStringList namesOf( const QList<HighlighterSet>& sets )
{
    QStringList names;
    for ( const auto& set : sets ) {
        names.append( set.name() );
    }
    return names;
}

QStringList namesOf( const QList<PredefinedFilterSet>& groups )
{
    QStringList names;
    for ( const auto& group : groups ) {
        names.append( group.name() );
    }
    return names;
}

// A team: its server, a bare repository, and a temporary place for the
// Team Folders of its members.
class Team {
public:
    Team()
    {
        REQUIRE( root_.isValid() );
        server_ = root_.filePath( "server.git" );
        const auto created = git_.run( { "init", "--quiet", "--bare", server_ }, root_.path() );
        REQUIRE( created.succeeded );
    }

    QString url() const
    {
        return QUrl::fromLocalFile( server_ ).toString();
    }

    QString serverPath() const
    {
        return server_;
    }

    // Where the Team Folder of a member keeps its clone.
    QString cloneOf( const QString& member ) const
    {
        return root_.filePath( member );
    }

    std::unique_ptr<TeamFolder> member( const QString& name, const QString& subfolder = {} ) const
    {
        auto folder = std::make_unique<TeamFolder>( cloneOf( name ) );
        folder->setUp( policyFor( url(), subfolder ) );
        REQUIRE( settled( *folder ) );
        return folder;
    }

    // What the server's default branch holds: the subject and author of its
    // last commit, and the files that commit touched.
    QStringList lastCommit() const
    {
        const auto shown = git_.run( { "--git-dir", server_, "log", "-1", "--format=%s%n%an",
                                       "--name-only", "--no-renames" },
                                     root_.path() );
        return shown.output.trimmed().split( '\n', Qt::SkipEmptyParts );
    }

    // The files the server's default branch holds.
    QStringList serverFiles() const
    {
        const auto listed = git_.run(
            { "--git-dir", server_, "ls-tree", "-r", "--name-only", "HEAD" }, root_.path() );
        return listed.output.trimmed().split( '\n', Qt::SkipEmptyParts );
    }

    QString root() const
    {
        return root_.path();
    }

    // What a member does by hand, without LogSquirl: writes a file into the
    // clone, commits and pushes it.
    void pushByHand( const QString& member, const QString& file, const QByteArray& content,
                     const QString& message = "Add a file" ) const
    {
        const QDir clone( cloneOf( member ) );
        QDir().mkpath( QFileInfo( clone.filePath( file ) ).absolutePath() );
        QFile out( clone.filePath( file ) );
        REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        out.write( content );
        out.close();
        commitAndPush( member, { "add", "--", file }, message );
    }

    // Exports a group with the Group Exchange into the clone and pushes it:
    // a #465 export dropped into the repository by hand.
    void pushGroupByHand( const QString& member, const PredefinedFilterSet& group,
                          const QString& subfolder = {} ) const
    {
        const auto file
            = QDir( subfolder ).filePath( suggestedFileName( group.name(), GroupKind::Filter ) );
        const QDir clone( cloneOf( member ) );
        QDir().mkpath( QFileInfo( clone.filePath( file ) ).absolutePath() );
        REQUIRE( writeGroup( clone.filePath( file ), group ) );
        commitAndPush( member, { "add", "--", QDir::cleanPath( file ) }, "Share a group" );
    }

    // The same for a Highlighter Set.
    void pushGroupByHand( const QString& member, const HighlighterSet& group ) const
    {
        const auto file = suggestedFileName( group.name(), GroupKind::Highlighter );
        REQUIRE( writeGroup( QDir( cloneOf( member ) ).filePath( file ), group ) );
        commitAndPush( member, { "add", "--", file }, "Share a set" );
    }

    // The same for a Naming Group of Value Names.
    void pushGroupByHand( const QString& member, const NamingGroup& group ) const
    {
        const auto file = suggestedFileName( group.name(), GroupKind::ValueNames );
        REQUIRE( writeGroup( QDir( cloneOf( member ) ).filePath( file ), group ) );
        commitAndPush( member, { "add", "--", file }, "Share a naming group" );
    }

    void removeByHand( const QString& member, const QString& file ) const
    {
        commitAndPush( member, { "rm", "--quiet", "--", file }, "Remove a group" );
    }

private:
    void commitAndPush( const QString& member, const QStringList& stage,
                        const QString& message ) const
    {
        const auto clone = cloneOf( member );
        INFO( "in " << clone.toStdString() );
        const auto staged = git_.run( stage, clone );
        INFO( staged.message().toStdString() );
        REQUIRE( staged.succeeded );
        const auto committed = git_.run( { "commit", "--quiet", "-m", message }, clone );
        INFO( committed.message().toStdString() );
        REQUIRE( committed.succeeded );
        const auto pushed = git_.run( { "push", "--quiet", "origin", "HEAD" }, clone );
        INFO( pushed.message().toStdString() );
        REQUIRE( pushed.succeeded );
    }

    QTemporaryDir root_;
    QString server_;
    Git git_{ QStringLiteral( "git" ) };
};

// Where no Git is installed, what every case checks instead.
void checkMissingGitIsReported( const QString& gitProgram = QStringLiteral( "git" ) )
{
    const QTemporaryDir root;
    REQUIRE( root.isValid() );
    TeamFolder folder( root.filePath( "clone" ), gitProgram );
    folder.setUp( policyFor( QUrl::fromLocalFile( root.filePath( "server.git" ) ).toString() ) );
    REQUIRE( settled( folder ) );

    CHECK( folder.state() == TeamFolder::State::Error );
    CHECK( folder.failedStep() == SyncStep::StartGit );
    CHECK( folder.gitOutput().contains( "Git could not be started" ) );
    CHECK( folder.filterGroups().isEmpty() );
}

} // namespace

TEST_CASE( "A Team Folder without Git reports that Git is missing", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    const QTemporaryDir nowhere;
    REQUIRE( nowhere.isValid() );
    checkMissingGitIsReported( nowhere.filePath( "no-git-here" ) );
}

TEST_CASE( "A Team Folder is off until it names a repository", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    const QTemporaryDir root;
    REQUIRE( root.isValid() );
    TeamFolder folder( root.filePath( "clone" ) );

    folder.setUp( TeamFolderPolicy{ .enabled = true } );
    CHECK( folder.state() == TeamFolder::State::Off );
    CHECK_FALSE( folder.isSyncing() );

    folder.setUp( TeamFolderPolicy{ .enabled = false, .repositoryUrl = "file:///nowhere" } );
    CHECK( folder.state() == TeamFolder::State::Off );
    CHECK_FALSE( QFileInfo::exists( root.filePath( "clone" ) ) );
}

TEST_CASE( "A group file added on one side appears on the other after a sync", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto bob = team.member( "bob" );

    // An empty repository clones and syncs, and holds no Team group.
    CHECK( alice->state() == TeamFolder::State::Synced );
    CHECK( bob->state() == TeamFolder::State::Synced );
    CHECK( bob->filterGroups().isEmpty() );

    const auto network = makeGroup( "Network", "timeout|refused" );
    team.pushGroupByHand( "alice", network );

    QSignalSpy changed( bob.get(), &TeamFolder::groupsChanged );
    syncNow( *bob );

    CHECK( bob->state() == TeamFolder::State::Synced );
    CHECK( bob->failedStep() == SyncStep::None );
    CHECK( bob->gitOutput().isEmpty() );
    CHECK( bob->heading() == "Synced" );
    const auto groups = bob->filterGroups();
    REQUIRE( groups.size() == 1 );
    CHECK( groups[ 0 ].id() == network.id() );
    CHECK( groups[ 0 ].name() == "Network" );
    REQUIRE( groups[ 0 ].filters().size() == 1 );
    CHECK( groups[ 0 ].filters()[ 0 ].pattern == "timeout|refused" );

    REQUIRE( changed.size() == 1 );
    const auto changes = changed.at( 0 ).at( 0 ).value<TeamGroupChanges>();
    CHECK( changes.added == QStringList{ network.id() } );
    CHECK( changes.changed.isEmpty() );
    CHECK( changes.removed.isEmpty() );
}

TEST_CASE( "A changed and a removed group reach the other side at the next sync", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    auto network = makeGroup( "Network" );
    const auto storage = makeGroup( "Storage" );
    team.pushGroupByHand( "alice", network );
    team.pushGroupByHand( "alice", storage );

    const auto bob = team.member( "bob" );
    REQUIRE( namesOf( bob->filterGroups() ) == QStringList{ "Network", "Storage" } );

    SECTION( "a changed group" )
    {
        network.setFilters( { { "Refused", "refused", false } } );
        team.pushGroupByHand( "alice", network );

        QSignalSpy changed( bob.get(), &TeamFolder::groupsChanged );
        syncNow( *bob );

        const auto groups = bob->filterGroups();
        REQUIRE( groups.size() == 2 );
        CHECK( groups[ 0 ].id() == network.id() );
        REQUIRE( groups[ 0 ].filters().size() == 1 );
        CHECK( groups[ 0 ].filters()[ 0 ].pattern == "refused" );

        REQUIRE( changed.size() == 1 );
        const auto changes = changed.at( 0 ).at( 0 ).value<TeamGroupChanges>();
        CHECK( changes.changed == QStringList{ network.id() } );
        CHECK( changes.added.isEmpty() );
        CHECK( changes.removed.isEmpty() );
    }

    SECTION( "a removed group" )
    {
        team.removeByHand( "alice", suggestedFileName( "Network", GroupKind::Filter ) );

        QSignalSpy changed( bob.get(), &TeamFolder::groupsChanged );
        syncNow( *bob );

        CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Storage" } );
        REQUIRE( changed.size() == 1 );
        const auto changes = changed.at( 0 ).at( 0 ).value<TeamGroupChanges>();
        CHECK( changes.removed == QStringList{ network.id() } );
    }

    SECTION( "a sync that brings nothing new changes nothing" )
    {
        QSignalSpy changed( bob.get(), &TeamFolder::groupsChanged );
        syncNow( *bob );
        CHECK( changed.isEmpty() );
        CHECK( bob->state() == TeamFolder::State::Synced );
    }
}

TEST_CASE( "A malformed file in the Team Folder is skipped and reported", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    team.pushGroupByHand( "alice", makeGroup( "Network" ) );
    team.pushByHand( "alice", "broken_filter.conf", "this is not a group\n\x01\x02\x03" );
    // Files that are no .conf file are the repository's own business.
    team.pushByHand( "alice", "README.md", "# The team's groups\n" );

    const auto bob = team.member( "bob" );

    CHECK( bob->state() == TeamFolder::State::Synced );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Network" } );
    const auto skipped = bob->skippedFiles();
    REQUIRE( skipped.size() == 1 );
    CHECK( skipped[ 0 ].file == "broken_filter.conf" );
    CHECK_FALSE( skipped[ 0 ].reason.isEmpty() );
    // Still shown beside the status.
    CHECK( bob->remarks()
           == QStringList{
               QStringLiteral( "Skipped broken_filter.conf: %1" ).arg( skipped[ 0 ].reason ) } );
}

TEST_CASE( "Team groups are sorted by name and read from the subfolder", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    team.pushGroupByHand( "alice", makeGroup( "zeta" ), "logsquirl" );
    team.pushGroupByHand( "alice", makeGroup( "Alpha" ), "logsquirl" );
    team.pushGroupByHand( "alice", makeGroup( "beta" ), "logsquirl" );
    team.pushGroupByHand( "alice", makeGroup( "Outside" ) );

    const auto bob = team.member( "bob", "logsquirl" );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Alpha", "beta", "zeta" } );

    const auto carol = team.member( "carol" );
    CHECK( namesOf( carol->filterGroups() ) == QStringList{ "Outside" } );
}

TEST_CASE( "A subfolder outside the repository is refused", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto bob = team.member( "bob", "../elsewhere" );
    CHECK( bob->state() == TeamFolder::State::Error );
    CHECK( bob->failedStep() == SyncStep::Subfolder );
    // The subfolder is LogSquirl's to refuse: there is nothing from Git, and
    // a remark names the subfolder.
    CHECK( bob->gitOutput().isEmpty() );
    CHECK( bob->remarks()
           == QStringList{ "The subfolder ../elsewhere does not lie inside the repository." } );

    // A subfolder inside it takes the remark away.
    bob->setUp( policyFor( team.url(), "logsquirl" ) );
    REQUIRE( settled( *bob ) );
    CHECK( bob->failedStep() == SyncStep::None );
    CHECK( bob->remarks().isEmpty() );
}

TEST_CASE( "A failed clone is reported with Git's message", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const QTemporaryDir root;
    REQUIRE( root.isValid() );
    TeamFolder folder( root.filePath( "clone" ) );
    folder.setUp( policyFor( QUrl::fromLocalFile( root.filePath( "missing.git" ) ).toString() ) );
    REQUIRE( settled( folder ) );

    CHECK( folder.state() == TeamFolder::State::Error );
    CHECK( folder.failedStep() == SyncStep::Clone );
    CHECK( folder.heading() == "Clone failed" );
    // Git's own output, and nothing but it: the heading is not joined to it.
    CHECK( folder.gitOutput().startsWith( "fatal: " ) );
    CHECK_FALSE( folder.gitOutput().contains( "Clone failed" ) );
    CHECK( folder.filterGroups().isEmpty() );
}

TEST_CASE( "An unreachable repository keeps the groups of the last sync", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    team.pushGroupByHand( "alice", makeGroup( "Network" ) );
    const auto bob = team.member( "bob" );
    REQUIRE( bob->state() == TeamFolder::State::Synced );

    // The server goes away.
    REQUIRE( QDir().rename( team.serverPath(), team.serverPath() + ".gone" ) );
    syncNow( *bob );

    CHECK( bob->state() == TeamFolder::State::NotSynced );
    CHECK( bob->failedStep() == SyncStep::Pull );
    CHECK( bob->heading() == "Pull failed" );
    CHECK( bob->gitOutput().startsWith( "fatal: " ) );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Network" } );

    // And comes back.
    REQUIRE( QDir().rename( team.serverPath() + ".gone", team.serverPath() ) );
    syncNow( *bob );
    CHECK( bob->state() == TeamFolder::State::Synced );
    CHECK( bob->failedStep() == SyncStep::None );
    CHECK( bob->gitOutput().isEmpty() );
    CHECK( bob->heading() == "Synced" );
}

TEST_CASE( "A clone that cannot take what was pulled reports the failed merge", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    team.pushGroupByHand( "alice", makeGroup( "Network" ) );
    const auto bob = team.member( "bob" );
    REQUIRE( bob->state() == TeamFolder::State::Synced );

    // Alice shares a second group; Bob's clone holds a file of his own,
    // untracked, where it would go. Git fetches it, and refuses to overwrite
    // the file with it.
    const auto file = suggestedFileName( "Second", GroupKind::Filter );
    team.pushGroupByHand( "alice", makeGroup( "Second" ) );
    {
        QFile mine( QDir( team.cloneOf( "bob" ) ).filePath( file ) );
        REQUIRE( mine.open( QIODevice::WriteOnly ) );
        mine.write( "Bob's own file\n" );
    }
    syncNow( *bob );

    CHECK( bob->state() == TeamFolder::State::Error );
    CHECK( bob->failedStep() == SyncStep::Merge );
    CHECK( bob->heading() == "Merge failed" );
    // Git's own output, naming the file it would not overwrite.
    CHECK( bob->gitOutput().contains( file ) );
    CHECK_FALSE( bob->gitOutput().contains( "Merge failed" ) );
    CHECK( namesOf( bob->filterGroups() ).contains( "Network" ) );

    // With the file out of the way, the next sync merges.
    REQUIRE( QFile::remove( QDir( team.cloneOf( "bob" ) ).filePath( file ) ) );
    syncNow( *bob );
    CHECK( bob->state() == TeamFolder::State::Synced );
    CHECK( bob->failedStep() == SyncStep::None );
    CHECK( bob->gitOutput().isEmpty() );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Network", "Second" } );
}

TEST_CASE( "Turning the Team Folder off or pointing it elsewhere replaces only Team groups",
           "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    team.pushGroupByHand( "alice", makeGroup( "Network" ) );

    const Team otherTeam;
    const auto dave = otherTeam.member( "dave" );
    otherTeam.pushGroupByHand( "dave", makeGroup( "Payments" ) );

    const auto personalBefore = namesOf( PredefinedFiltersCollection::getSynced().filterSets() );

    const auto bob = team.member( "bob" );
    REQUIRE( namesOf( bob->filterGroups() ) == QStringList{ "Network" } );

    SECTION( "turned off" )
    {
        QSignalSpy changed( bob.get(), &TeamFolder::groupsChanged );
        bob->setUp( TeamFolderPolicy{ .enabled = false, .repositoryUrl = team.url() } );

        CHECK( bob->state() == TeamFolder::State::Off );
        CHECK( bob->filterGroups().isEmpty() );
        REQUIRE( changed.size() == 1 );
        CHECK( changed.at( 0 ).at( 0 ).value<TeamGroupChanges>().removed.size() == 1 );

        // Off, it does not sync.
        bob->sync();
        CHECK_FALSE( bob->isSyncing() );

        // Turned on again, it has its groups back.
        bob->setUp( policyFor( team.url() ) );
        REQUIRE( settled( *bob ) );
        CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Network" } );
    }

    SECTION( "pointed at another repository" )
    {
        bob->setUp( policyFor( otherTeam.url() ) );
        REQUIRE( settled( *bob ) );
        CHECK( bob->state() == TeamFolder::State::Synced );
        CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Payments" } );
    }

    CHECK( namesOf( PredefinedFiltersCollection::getSynced().filterSets() ) == personalBefore );
}

TEST_CASE( "Team Highlighter Sets arrive next to the Filter Groups, sorted by name",
           "[teamfolder][highlighter]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    team.pushGroupByHand( "alice", makeSet( "zeta" ) );
    team.pushGroupByHand( "alice", makeSet( "Alpha" ) );
    team.pushGroupByHand( "alice", makeGroup( "Network" ) );

    const auto bob = team.member( "bob" );
    CHECK( namesOf( bob->highlighterGroups() ) == QStringList{ "Alpha", "zeta" } );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Network" } );
    // A Highlighter Set file is a group, not a malformed file.
    CHECK( bob->skippedFiles().isEmpty() );
}

TEST_CASE( "A changed and a removed Team Highlighter Set reach the other side",
           "[teamfolder][highlighter]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto levels = makeSet( "Levels" );
    team.pushGroupByHand( "alice", levels );
    team.pushGroupByHand( "alice", makeSet( "Other" ) );
    const auto bob = team.member( "bob" );

    SECTION( "changed" )
    {
        auto changedLevels = levels;
        changedLevels.addHighlighter( Highlighter( "WARN", false, true, Qt::black, Qt::yellow ) );
        team.pushGroupByHand( "alice", changedLevels );

        QSignalSpy filterChanges( bob.get(), &TeamFolder::groupsChanged );
        QSignalSpy changed( bob.get(), &TeamFolder::highlighterGroupsChanged );
        syncNow( *bob );

        REQUIRE( changed.size() == 1 );
        const auto changes = changed.at( 0 ).at( 0 ).value<TeamGroupChanges>();
        CHECK( changes.changed == QStringList{ levels.id() } );
        CHECK( changes.added.isEmpty() );
        CHECK( changes.removed.isEmpty() );
        CHECK( filterChanges.isEmpty() );
    }

    SECTION( "removed" )
    {
        team.removeByHand( "alice", suggestedFileName( "Levels", GroupKind::Highlighter ) );

        QSignalSpy changed( bob.get(), &TeamFolder::highlighterGroupsChanged );
        syncNow( *bob );

        CHECK( namesOf( bob->highlighterGroups() ) == QStringList{ "Other" } );
        REQUIRE( changed.size() == 1 );
        CHECK( changed.at( 0 ).at( 0 ).value<TeamGroupChanges>().removed
               == QStringList{ levels.id() } );
    }

    SECTION( "unchanged" )
    {
        QSignalSpy changed( bob.get(), &TeamFolder::highlighterGroupsChanged );
        syncNow( *bob );
        CHECK( changed.isEmpty() );
    }
}

namespace {

// How many ranges of a line the active Highlighters color.
int matchCount( const HighlighterSetCollection& collection, const QString& line )
{
    HighlightedMatchRanges matches;
    collection.currentActiveSet().matchLine( line, matches );
    return static_cast<int>( matches.matches().size() );
}

} // namespace

TEST_CASE( "A user activates a Team Highlighter Set for themselves, and only locally",
           "[teamfolder][highlighter]" )
{
    const auto levels = makeSet( "Levels", "ERROR" );
    HighlighterSetCollection collection;
    auto own = makeSet( "Own", "own" );
    collection.setHighlighterSets( { own } );
    collection.setTeamHighlighterSets( { levels } );

    CHECK( collection.teamHighlighterSets().size() == 1 );
    // The user's own sets are the collection's sets; Team sets are not.
    CHECK( collection.highlighterSets().size() == 1 );
    CHECK( collection.hasSet( levels.id() ) );

    CHECK( matchCount( collection, "an ERROR here" ) == 0 );
    collection.activateSet( levels.id() );
    CHECK( collection.activeSetIds() == QStringList{ levels.id() } );
    CHECK( matchCount( collection, "an ERROR here" ) == 1 );

    SECTION( "stored locally, the Team set itself never" )
    {
        const QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        const auto file = dir.filePath( "settings.ini" );
        {
            QSettings settings( file, QSettings::IniFormat );
            collection.saveToStorage( settings );
        }
        QSettings settings( file, QSettings::IniFormat );
        CHECK( settings.value( "HighlighterSetCollection/sets/size" ).toInt() == 1 );

        HighlighterSetCollection restarted;
        restarted.retrieveFromStorage( settings );
        CHECK( restarted.highlighterSets().size() == 1 );
        // The Team set has not arrived yet: its activation waits for it.
        CHECK( matchCount( restarted, "an ERROR here" ) == 0 );
        restarted.setTeamHighlighterSets( { levels } );
        CHECK( matchCount( restarted, "an ERROR here" ) == 1 );
    }

    SECTION( "a personal edit before the first sync keeps the activation" )
    {
        const QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        QSettings settings( dir.filePath( "settings.ini" ), QSettings::IniFormat );
        collection.saveToStorage( settings );
        HighlighterSetCollection restarted;
        restarted.retrieveFromStorage( settings );
        restarted.setHighlighterSets( { own, makeSet( "More", "more" ) } );
        restarted.setTeamHighlighterSets( { levels } );
        CHECK( restarted.activeSetIds() == QStringList{ levels.id() } );
    }
}

TEST_CASE( "A synced change to an active Team Highlighter Set re-colors at once",
           "[teamfolder][highlighter]" )
{
    const auto levels = makeSet( "Levels", "ERROR" );
    HighlighterSetCollection collection;
    collection.setTeamHighlighterSets( { levels } );
    collection.activateSet( levels.id() );
    REQUIRE( matchCount( collection, "WARN and ERROR" ) == 1 );

    auto changedLevels = levels;
    changedLevels.addHighlighter( Highlighter( "WARN", false, true, Qt::black, Qt::yellow ) );
    collection.setTeamHighlighterSets( { changedLevels } );

    CHECK( collection.activeSetIds() == QStringList{ levels.id() } );
    CHECK( matchCount( collection, "WARN and ERROR" ) == 2 );
}

TEST_CASE( "A synced removal of an active Team Highlighter Set deactivates it",
           "[teamfolder][highlighter]" )
{
    const auto levels = makeSet( "Levels", "ERROR" );
    const auto other = makeSet( "Other", "other" );
    HighlighterSetCollection collection;
    collection.setTeamHighlighterSets( { levels, other } );
    collection.activateSet( levels.id() );
    collection.activateSet( other.id() );

    collection.setTeamHighlighterSets( { other } );

    CHECK_FALSE( collection.hasSet( levels.id() ) );
    CHECK( collection.activeSetIds() == QStringList{ other.id() } );
    CHECK( matchCount( collection, "an ERROR here" ) == 0 );
}

// --- Publishing (#472) ---

namespace {

using logsquirl::teamfolder::GroupAction;
using logsquirl::teamfolder::PublishOutcome;
using logsquirl::teamfolder::PublishRequest;
using logsquirl::teamfolder::PublishStatus;

PublishOutcome publishAndWait( TeamFolder& folder, QList<PublishRequest> requests )
{
    REQUIRE( settled( folder ) );
    QSignalSpy finished( &folder, &TeamFolder::publishFinished );
    folder.publish( std::move( requests ) );
    REQUIRE( finished.wait( SyncTimeoutMs ) );
    REQUIRE( settled( folder ) );
    return finished.at( 0 ).at( 0 ).value<PublishOutcome>();
}

PublishOutcome publishGroup( TeamFolder& folder, const PredefinedFilterSet& group,
                             GroupAction action = GroupAction::Change,
                             const QString& previousName = {} )
{
    return publishAndWait( folder, { PublishRequest::forGroup( group, action, previousName ) } );
}

} // namespace

TEST_CASE( "A published group reaches the others, committed alone under a message naming it",
           "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto bob = team.member( "bob" );
    team.pushGroupByHand( "alice", makeGroup( "Storage" ) );
    syncNow( *alice );

    auto network = makeGroup( "Network", "refused" );
    const auto added = publishGroup( *alice, network, GroupAction::Add );
    REQUIRE( added.results.size() == 1 );
    CHECK( added.results[ 0 ].status == PublishStatus::Published );
    CHECK( alice->state() == TeamFolder::State::Synced );

    CHECK( team.lastCommit()
           == QStringList{ "Add filter group \"Network\"", "Team Folder Test",
                           "Network_filter.conf" } );

    syncNow( *bob );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Network", "Storage" } );

    SECTION( "a change" )
    {
        network.setFilters( { { "Timeout", "timeout", false } } );
        publishGroup( *alice, network );
        CHECK( team.lastCommit()
               == QStringList{ "Change filter group \"Network\"", "Team Folder Test",
                               "Network_filter.conf" } );
        syncNow( *bob );
        REQUIRE( bob->filterGroups().size() == 2 );
        CHECK( bob->filterGroups()[ 0 ].filters()[ 0 ].pattern == "timeout" );
    }

    SECTION( "a rename keeps the id and the file" )
    {
        network.setName( "Networking" );
        publishGroup( *alice, network, GroupAction::Rename, "Network" );
        CHECK( team.lastCommit()
               == QStringList{ "Rename filter group \"Network\" to \"Networking\"",
                               "Team Folder Test", "Network_filter.conf" } );
        CHECK( team.serverFiles() == QStringList{ "Network_filter.conf", "Storage_filter.conf" } );

        QSignalSpy changed( bob.get(), &TeamFolder::groupsChanged );
        syncNow( *bob );
        CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Networking", "Storage" } );
        REQUIRE( changed.size() == 1 );
        const auto changes = changed.at( 0 ).at( 0 ).value<TeamGroupChanges>();
        CHECK( changes.changed == QStringList{ network.id() } );
        CHECK( changes.added.isEmpty() );
        CHECK( changes.removed.isEmpty() );
    }

    SECTION( "a new group's file name is made unique in the folder" )
    {
        publishGroup( *alice, makeGroup( "Network", "other" ), GroupAction::Add );
        CHECK( team.serverFiles()
               == QStringList{ "Network (2)_filter.conf", "Network_filter.conf",
                               "Storage_filter.conf" } );
    }

    SECTION( "a Highlighter Set" )
    {
        const auto levels = makeSet( "Levels" );
        const auto outcome
            = publishAndWait( *alice, { PublishRequest::forGroup( levels, GroupAction::Add ) } );
        CHECK( outcome.results[ 0 ].status == PublishStatus::Published );
        CHECK( team.lastCommit()
               == QStringList{ "Add highlighter set \"Levels\"", "Team Folder Test",
                               "Levels_highlighter.conf" } );
        syncNow( *bob );
        CHECK( namesOf( bob->highlighterGroups() ) == QStringList{ "Levels" } );
    }
}

TEST_CASE( "Two members changing different groups both publish", "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    auto network = makeGroup( "Network" );
    auto storage = makeGroup( "Storage" );
    team.pushGroupByHand( "alice", network );
    team.pushGroupByHand( "alice", storage );
    syncNow( *alice );
    const auto bob = team.member( "bob" );

    network.setFilters( { { "Refused", "refused", false } } );
    storage.setFilters( { { "Full", "disk full", false } } );
    CHECK( publishGroup( *alice, network ).results[ 0 ].status == PublishStatus::Published );
    // Bob has not synced: his publish syncs first, then commits on top.
    CHECK( publishGroup( *bob, storage ).results[ 0 ].status == PublishStatus::Published );

    syncNow( *alice );
    for ( const auto* member : { alice.get(), bob.get() } ) {
        const auto groups = member->filterGroups();
        REQUIRE( groups.size() == 2 );
        CHECK( groups[ 0 ].filters()[ 0 ].pattern == "refused" );
        CHECK( groups[ 1 ].filters()[ 0 ].pattern == "disk full" );
    }
}

namespace {

#ifdef Q_OS_WIN
// The sh of Git for Windows: its bin/sh.exe, which puts the tools a script
// uses (touch, cp, sleep) on the PATH, found above the git on the PATH.
QString gitForWindowsShell()
{
    QDir directory = QFileInfo( QStandardPaths::findExecutable( QStringLiteral( "git" ) ) ).dir();
    for ( int up = 0; up < 3 && directory.cdUp(); ++up ) {
        const auto shell = directory.filePath( QStringLiteral( "bin/sh.exe" ) );
        if ( QFileInfo::exists( shell ) ) {
            return shell;
        }
    }
    return QStandardPaths::findExecutable( QStringLiteral( "sh" ) );
}
#endif

// A stand-in for the tool that runs a shell snippet first, and then the real
// one unless the snippet ended the run.
//
// Windows starts no shell script as a program. There the stand-in is a .cmd
// file that runs the script with the sh of Git for Windows, hands it its
// arguments and passes on its exit code. cmd.exe reads that command line
// first, so no argument may hold % ^ & | < or >; none of the Team Folder's
// arguments in these tests does. Killing the stand-in ends only the cmd.exe
// there: the script and what it started run on until they end by themselves.
QString wrapperGit( const QString& directory, const QString& name, const QString& snippet )
{
    const auto gitPath = QStandardPaths::findExecutable( QStringLiteral( "git" ) );
    const auto path = QDir( directory ).filePath( name );
    QFile script( path );
    REQUIRE( script.open( QIODevice::WriteOnly ) );
    script.write(
        QStringLiteral( "#!/bin/sh\n%1\nexec '%2' \"$@\"\n" ).arg( snippet, gitPath ).toUtf8() );
    script.close();
    QFile::setPermissions( path, script.permissions() | QFileDevice::ExeUser );
#ifdef Q_OS_WIN
    const auto shell = gitForWindowsShell();
    REQUIRE_FALSE( shell.isEmpty() );
    const auto batchPath
        = QDir( directory ).filePath( QFileInfo( name ).completeBaseName() + ".cmd" );
    QFile batch( batchPath );
    REQUIRE( batch.open( QIODevice::WriteOnly ) );
    // No MSYS_NO_PATHCONV here: Git reaches a file:// server through an sh of
    // its own, which needs its path turned from /C:/... into C:/... .
    batch.write( QStringLiteral( "@echo off\r\n"
                                 "\"%1\" \"%2\" %*\r\n"
                                 "exit /b %ERRORLEVEL%\r\n" )
                     .arg( QDir::toNativeSeparators( shell ), QDir::toNativeSeparators( path ) )
                     .toLocal8Bit() );
    return batchPath;
#else
    return path;
#endif
}

QString gitOutput( const QString& clone, const QStringList& arguments )
{
    return Git( QStringLiteral( "git" ) ).run( arguments, clone ).output;
}

// Stops a run once its stand-in has made this file, however slowly it started.
// It stops the run after SyncTimeoutMs anyway, and then leaves made false, so a
// stand-in that never ran cannot pass a case for the wrong reason.
std::thread stopOnceMade( const QString& file, const Git::StopFlag& stop, std::atomic_bool& made )
{
    return std::thread( [ file, stop, &made ] {
        QElapsedTimer waited;
        waited.start();
        while ( !QFileInfo::exists( file ) && waited.elapsed() < SyncTimeoutMs ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        }
        made.store( QFileInfo::exists( file ) );
        stop->store( true );
    } );
}

} // namespace

TEST_CASE( "A push rejected because the branch moved is retried once after a sync",
           "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto carol = team.member( "carol" );
    team.pushGroupByHand( "carol", makeGroup( "Base" ) );

    // A Git that lets Carol push in the moment before Alice's first push.
    const auto gitPath = QStandardPaths::findExecutable( QStringLiteral( "git" ) );
    const auto marker = QDir( team.root() ).filePath( "raced" );
    const auto carolClone = team.cloneOf( "carol" );
    const auto wrapper
        = wrapperGit( team.root(), "git-wrapper.sh",
                      QStringLiteral( "if [ \"$1\" = push ] && [ ! -e '%1' ]; then\n"
                                      "  touch '%1'\n"
                                      "  cp '%2/Base_filter.conf' '%2/Raced_filter.conf'\n"
                                      "  '%3' -C '%2' add Raced_filter.conf\n"
                                      "  '%3' -C '%2' commit -q -m 'Raced'\n"
                                      "  '%3' -C '%2' push -q origin HEAD\n"
                                      "fi" )
                          .arg( marker, carolClone, gitPath ) );

    TeamFolder alice( team.cloneOf( "alice" ), wrapper );
    alice.setUp( policyFor( team.url() ) );
    REQUIRE( settled( alice ) );

    const auto outcome = publishGroup( alice, makeGroup( "Network" ), GroupAction::Add );
    REQUIRE( outcome.results.size() == 1 );
    CHECK( outcome.results[ 0 ].status == PublishStatus::Published );
    CHECK( QFileInfo::exists( QDir( team.root() ).filePath( "raced" ) ) );
    CHECK( team.serverFiles()
           == QStringList{ "Base_filter.conf", "Network_filter.conf", "Raced_filter.conf" } );
}

TEST_CASE( "A push refused for missing rights makes the Team groups read-only",
           "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    REQUIRE( alice->isWritable() );

    const auto hook = QDir( team.serverPath() ).filePath( "hooks/pre-receive" );
    {
        QFile script( hook );
        REQUIRE( script.open( QIODevice::WriteOnly ) );
        script.write( "#!/bin/sh\necho 'you may not push here' >&2\nexit 1\n" );
        script.setPermissions( script.permissions() | QFileDevice::ExeUser );
    }

    const auto outcome = publishGroup( *alice, makeGroup( "Network" ), GroupAction::Add );
    REQUIRE( outcome.results.size() == 1 );
    CHECK( outcome.results[ 0 ].status == PublishStatus::Refused );
    CHECK( outcome.results[ 0 ].message.contains( "you may not push here" ) );
    CHECK_FALSE( alice->isWritable() );
    CHECK( alice->readOnlyReason().contains( "you may not push here" ) );
    // The step that failed and Git's output, apart; the Team groups stay
    // current, and say they are read-only.
    CHECK( alice->state() == TeamFolder::State::Synced );
    CHECK( alice->failedStep() == SyncStep::PushRefused );
    CHECK( alice->heading() == "Push refused" );
    CHECK( alice->gitOutput().contains( "you may not push here" ) );
    CHECK_FALSE( alice->gitOutput().contains( "read-only" ) );
    CHECK( alice->remarks() == QStringList{ "The Team groups are read-only." } );
    // Nothing is left pending that could never be pushed.
    CHECK_FALSE( alice->hasPendingChanges() );
    CHECK( alice->filterGroups().isEmpty() );

    // Publishing to a read-only Team Folder is refused without asking Git.
    const auto again = publishGroup( *alice, makeGroup( "Other" ), GroupAction::Add );
    CHECK( again.results[ 0 ].status == PublishStatus::Refused );
}

TEST_CASE( "Offline, a change stays pending and is pushed once the server is back",
           "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto bob = team.member( "bob" );
    team.pushGroupByHand( "alice", makeGroup( "Base" ) );
    syncNow( *alice );

    const auto away = team.serverPath() + ".away";
    REQUIRE( QDir().rename( team.serverPath(), away ) );

    const auto outcome = publishGroup( *alice, makeGroup( "Network" ), GroupAction::Add );
    REQUIRE( outcome.results.size() == 1 );
    CHECK( outcome.results[ 0 ].status == PublishStatus::Pending );
    CHECK( alice->state() == TeamFolder::State::NotSynced );
    CHECK( alice->hasPendingChanges() );
    // The user's own change shows meanwhile.
    CHECK( namesOf( alice->filterGroups() ) == QStringList{ "Base", "Network" } );
    CHECK( alice->isWritable() );

    REQUIRE( QDir().rename( away, team.serverPath() ) );
    syncNow( *alice );

    CHECK( alice->state() == TeamFolder::State::Synced );
    CHECK_FALSE( alice->hasPendingChanges() );
    syncNow( *bob );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Base", "Network" } );
}

TEST_CASE( "A dialog's edited Team groups ask to publish what was added, renamed or changed",
           "[teamfolder][publish]" )
{
    using logsquirl::teamfolder::requestsForChanges;

    const auto network = makeGroup( "Network" );
    const auto storage = makeGroup( "Storage" );
    const QList<PredefinedFilterSet> before{ network, storage };

    CHECK( requestsForChanges( before, before ).isEmpty() );

    auto renamed = network;
    renamed.setName( "Networking" );
    auto changed = storage;
    changed.setFilters( { { "Full", "disk full", false } } );
    const auto added = makeGroup( "Fresh" );
    const auto requests = requestsForChanges( before, { renamed, changed, added } );

    REQUIRE( requests.size() == 3 );
    CHECK( requests[ 0 ].action == GroupAction::Rename );
    CHECK( requests[ 0 ].previousName == "Network" );
    CHECK( requests[ 0 ].id == network.id() );
    CHECK( requests[ 1 ].action == GroupAction::Change );
    CHECK( requests[ 1 ].id == storage.id() );
    CHECK( requests[ 2 ].action == GroupAction::Add );
    CHECK( requests[ 2 ].id == added.id() );

    // A group that is no longer in the copy is asked to be deleted.
    const auto deletions = requestsForChanges( before, { network } );
    REQUIRE( deletions.size() == 1 );
    CHECK( deletions[ 0 ].action == GroupAction::Delete );
    CHECK( deletions[ 0 ].id == storage.id() );
}

// --- Conflicts (#473) ---

namespace {

using logsquirl::teamfolder::ConflictChoice;

PublishRequest requestBasedOnWhatWasLoaded( const TeamFolder& member,
                                            const PredefinedFilterSet& group )
{
    auto request = PublishRequest::forGroup( group, GroupAction::Change );
    request.baseRevision = member.filterGroupRevision( group.id() );
    return request;
}

} // namespace

TEST_CASE( "Publishing a group someone else changed meanwhile reports a conflict",
           "[teamfolder][conflict]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    auto network = makeGroup( "Network", "original" );
    auto storage = makeGroup( "Storage" );
    team.pushGroupByHand( "alice", network );
    team.pushGroupByHand( "alice", storage );
    syncNow( *alice );
    const auto bob = team.member( "bob" );

    // Both load the group; Bob's dialog remembers what it was when loaded.
    auto mine = network;
    mine.setFilters( { { "Bobs", "bobs pattern", false } } );
    const auto request = requestBasedOnWhatWasLoaded( *bob, mine );
    REQUIRE_FALSE( request.baseRevision.value_or( QString{} ).isEmpty() );

    auto theirs = network;
    theirs.setFilters( { { "Alices", "alices pattern", false } } );
    REQUIRE( publishGroup( *alice, theirs ).results[ 0 ].status == PublishStatus::Published );
    const auto serverHead = team.lastCommit();

    const auto outcome = publishAndWait( *bob, { request } );
    REQUIRE( outcome.results.size() == 1 );
    const auto& conflict = outcome.results[ 0 ];
    CHECK( conflict.status == PublishStatus::Conflict );
    const auto* theirsGroup = groupOfKind<PredefinedFilterSet>( conflict.theirs );
    REQUIRE( theirsGroup != nullptr );
    REQUIRE( theirsGroup->filters().size() == 1 );
    CHECK( theirsGroup->filters()[ 0 ].pattern == "alices pattern" );
    // Nothing was pushed or committed.
    CHECK( team.lastCommit() == serverHead );
    CHECK_FALSE( bob->hasPendingChanges() );

    SECTION( "keep mine overwrites their version" )
    {
        QSignalSpy finished( bob.get(), &TeamFolder::publishFinished );
        bob->resolveConflict( request, ConflictChoice::KeepMine );
        REQUIRE( finished.wait( SyncTimeoutMs ) );
        CHECK( finished.at( 0 ).at( 0 ).value<PublishOutcome>().results[ 0 ].status
               == PublishStatus::Published );
        REQUIRE( settled( *bob ) );

        syncNow( *alice );
        const auto groups = alice->filterGroups();
        REQUIRE( groups.size() == 2 );
        CHECK( groups[ 0 ].id() == network.id() );
        CHECK( groups[ 0 ].filters()[ 0 ].pattern == "bobs pattern" );
    }

    SECTION( "take theirs drops the local change" )
    {
        bob->resolveConflict( request, ConflictChoice::TakeTheirs );
        REQUIRE( settled( *bob ) );
        CHECK( team.lastCommit() == serverHead );
        const auto groups = bob->filterGroups();
        REQUIRE( groups.size() == 2 );
        CHECK( groups[ 0 ].filters()[ 0 ].pattern == "alices pattern" );
    }

    SECTION( "save mine as a copy adds a group and leaves theirs" )
    {
        QSignalSpy finished( bob.get(), &TeamFolder::publishFinished );
        bob->resolveConflict( request, ConflictChoice::SaveAsCopy );
        REQUIRE( finished.wait( SyncTimeoutMs ) );
        REQUIRE( settled( *bob ) );

        syncNow( *alice );
        const auto groups = alice->filterGroups();
        REQUIRE( groups.size() == 3 );
        CHECK( namesOf( groups ) == QStringList{ "Network", "Network (2)", "Storage" } );
        CHECK( groups[ 0 ].id() == network.id() );
        CHECK( groups[ 0 ].filters()[ 0 ].pattern == "alices pattern" );
        CHECK( groups[ 1 ].id() != network.id() );
        CHECK( groups[ 1 ].filters()[ 0 ].pattern == "bobs pattern" );
    }
}

TEST_CASE( "A change to a different group never triggers the question", "[teamfolder][conflict]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    auto network = makeGroup( "Network" );
    auto storage = makeGroup( "Storage" );
    team.pushGroupByHand( "alice", network );
    team.pushGroupByHand( "alice", storage );
    syncNow( *alice );
    const auto bob = team.member( "bob" );

    storage.setFilters( { { "Full", "disk full", false } } );
    const auto request = requestBasedOnWhatWasLoaded( *bob, storage );

    network.setFilters( { { "Refused", "refused", false } } );
    REQUIRE( publishGroup( *alice, network ).results[ 0 ].status == PublishStatus::Published );

    CHECK( publishAndWait( *bob, { request } ).results[ 0 ].status == PublishStatus::Published );
    // And a change nobody else touched publishes on its own base again.
    const auto again = requestBasedOnWhatWasLoaded( *bob, storage );
    CHECK( publishAndWait( *bob, { again } ).results[ 0 ].status == PublishStatus::Published );
}

TEST_CASE( "A dialog's requests carry the revisions of the groups it loaded",
           "[teamfolder][conflict]" )
{
    using logsquirl::teamfolder::requestsForChanges;

    const auto network = makeGroup( "Network" );
    auto changed = network;
    changed.setFilters( { { "Full", "disk full", false } } );
    const auto added = makeGroup( "Fresh" );

    const auto requests = requestsForChanges<PredefinedFilterSet>( { network }, { changed, added },
                                                                   { { network.id(), "abc123" } } );
    REQUIRE( requests.size() == 2 );
    CHECK( requests[ 0 ].baseRevision == std::optional<QString>( "abc123" ) );
    // A new group has no base: nobody else can have changed it.
    CHECK_FALSE( requests[ 1 ].baseRevision.has_value() );
}

// --- Share, copy and delete (#474) ---

TEST_CASE( "A group is shared as a Team copy with a fresh id and a free name",
           "[teamfolder][share]" )
{
    using logsquirl::teamfolder::copyOfGroup;

    const auto mine = makeGroup( "Network", "refused" );

    const auto copy = copyOfGroup( mine, { "Storage" } );
    CHECK( copy.id() != mine.id() );
    CHECK( copy.name() == "Network" );
    REQUIRE( copy.filters().size() == 1 );
    CHECK( copy.filters()[ 0 ].pattern == "refused" );

    // A clash takes the first free "<name> (n)".
    CHECK( copyOfGroup( mine, { "Network" } ).name() == "Network (2)" );
    CHECK( copyOfGroup( mine, { "Network", "Network (2)" } ).name() == "Network (3)" );

    const auto levels = makeSet( "Levels" );
    const auto setCopy = copyOfGroup( levels, { "Levels" } );
    CHECK( setCopy.id() != levels.id() );
    CHECK( setCopy.name() == "Levels (2)" );
}

TEST_CASE( "Sharing publishes a Team copy and leaves the personal group", "[teamfolder][share]" )
{
    using logsquirl::teamfolder::copyOfGroup;

    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto bob = team.member( "bob" );
    team.pushGroupByHand( "alice", makeGroup( "Network" ) );
    syncNow( *alice );

    const auto mine = makeGroup( "Network", "mine" );
    const auto shared = copyOfGroup( mine, { "Network" } );
    REQUIRE( publishGroup( *alice, shared, GroupAction::Add ).results[ 0 ].status
             == PublishStatus::Published );

    syncNow( *bob );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Network", "Network (2)" } );
    CHECK( bob->filterGroups()[ 1 ].id() == shared.id() );
    CHECK( shared.id() != mine.id() );
}

TEST_CASE( "A deleted Team group disappears for everyone at their next sync",
           "[teamfolder][share]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto network = makeGroup( "Network" );
    const auto levels = makeSet( "Levels" );
    team.pushGroupByHand( "alice", network );
    team.pushGroupByHand( "alice", levels );
    team.pushGroupByHand( "alice", makeGroup( "Storage" ) );
    syncNow( *alice );
    const auto bob = team.member( "bob" );

    const auto deleted = publishAndWait(
        *alice, { PublishRequest::forDeletion( logsquirl::groupexchange::GroupKind::Filter,
                                               network.id(), network.name() ),
                  PublishRequest::forDeletion( logsquirl::groupexchange::GroupKind::Highlighter,
                                               levels.id(), levels.name() ) } );
    REQUIRE( deleted.results.size() == 2 );
    CHECK( deleted.results[ 0 ].status == PublishStatus::Published );
    CHECK( deleted.results[ 1 ].status == PublishStatus::Published );
    CHECK( team.serverFiles() == QStringList{ "Storage_filter.conf" } );
    CHECK( namesOf( alice->filterGroups() ) == QStringList{ "Storage" } );

    QSignalSpy changed( bob.get(), &TeamFolder::groupsChanged );
    syncNow( *bob );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Storage" } );
    CHECK( bob->highlighterGroups().isEmpty() );
    REQUIRE( changed.size() == 1 );
    CHECK( changed.at( 0 ).at( 0 ).value<TeamGroupChanges>().removed
           == QStringList{ network.id() } );

    // Deleting a group that is already gone changes nothing.
    const auto again = publishAndWait(
        *alice, { PublishRequest::forDeletion( logsquirl::groupexchange::GroupKind::Filter,
                                               network.id(), network.name() ) } );
    CHECK( again.results[ 0 ].status == PublishStatus::Published );
}

TEST_CASE( "A network failure whose URL holds 403 is no refusal and loses no change",
           "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto wrapper
        = wrapperGit( team.root(), "git-net.sh",
                      "if [ \"$1\" = push ]; then\n"
                      "  echo \"fatal: unable to access 'https://host:8403/repo.git/': "
                      "Could not resolve host\" >&2\n"
                      "  exit 128\n"
                      "fi" );
    TeamFolder alice( team.cloneOf( "alice" ), wrapper );
    alice.setUp( policyFor( team.url() ) );
    REQUIRE( settled( alice ) );

    const auto outcome = publishGroup( alice, makeGroup( "Network" ), GroupAction::Add );
    REQUIRE( outcome.results.size() == 1 );
    CHECK( outcome.results[ 0 ].status == PublishStatus::Pending );
    CHECK( alice.isWritable() );
    CHECK( alice.hasPendingChanges() );
    CHECK( gitOutput( team.cloneOf( "alice" ), { "log", "--format=%s" } ).contains( "Network" ) );
}

TEST_CASE( "A publish reads each group file once per phase, not once per request",
           "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    constexpr int GroupCount = 50;
    constexpr int RequestCount = 10;

    const Team team;
    const auto alice = team.member( "alice" );
    QList<PredefinedFilterSet> groups;
    for ( int number = 0; number < GroupCount; ++number ) {
        groups.append(
            makeGroup( QString( "Group %1" ).arg( number, 2, 10, QLatin1Char( '0' ) ) ) );
        team.pushGroupByHand( "alice", groups.last() );
    }
    syncNow( *alice );
    REQUIRE( alice->filterGroups().size() == GroupCount );

    QStringList revisionsBefore;
    QList<PublishRequest> requests;
    for ( int number = 0; number < RequestCount; ++number ) {
        auto changed = groups[ number ];
        changed.setFilters( { { "Changed", "changed pattern", false } } );
        requests.append( requestBasedOnWhatWasLoaded( *alice, changed ) );
    }
    for ( const auto& group : groups ) {
        revisionsBefore.append( alice->filterGroupRevision( group.id() ) );
    }

    const auto before = logsquirl::teamfolder::groupFileReads().load();
    const auto outcome = publishAndWait( *alice, requests );
    const auto reads = logsquirl::teamfolder::groupFileReads().load() - before;

    REQUIRE( outcome.results.size() == RequestCount );
    for ( const auto& result : outcome.results ) {
        CHECK( result.status == PublishStatus::Published );
    }
    // The group files are read after the fetch and again at the end, and the
    // file of each request once more after its commit: never once per
    // request and group.
    CHECK( reads <= 2 * GroupCount + RequestCount );

    // What was not changed keeps its revision.
    for ( int number = RequestCount; number < GroupCount; ++number ) {
        CHECK( alice->filterGroupRevision( groups[ number ].id() ) == revisionsBefore[ number ] );
    }
    for ( int number = 0; number < RequestCount; ++number ) {
        CHECK( alice->filterGroupRevision( groups[ number ].id() ) != revisionsBefore[ number ] );
    }
}

TEST_CASE( "A push refused with HTTP 403 is a refusal: the Team groups turn read-only",
           "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto wrapper = wrapperGit( team.root(), "git-403.sh",
                                     "if [ \"$1\" = push ]; then\n"
                                     "  echo \"fatal: unable to access 'https://host/repo.git/': "
                                     "The requested URL returned error: 403\" >&2\n"
                                     "  exit 128\n"
                                     "fi" );
    TeamFolder alice( team.cloneOf( "alice" ), wrapper );
    alice.setUp( policyFor( team.url() ) );
    REQUIRE( settled( alice ) );

    const auto outcome = publishGroup( alice, makeGroup( "Network" ), GroupAction::Add );
    REQUIRE( outcome.results.size() == 1 );
    CHECK( outcome.results[ 0 ].status == PublishStatus::Refused );
    CHECK( outcome.results[ 0 ].message.contains( "error: 403" ) );
    CHECK_FALSE( alice.isWritable() );
    CHECK_FALSE( alice.hasPendingChanges() );
}

TEST_CASE( "Every Git of the Team Folder runs untranslated", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto seen = root.filePath( "seen" );
    const auto wrapper
        = wrapperGit( root.path(), "git-env.sh",
                      QStringLiteral( "echo \"$LC_ALL/$LANGUAGE\" > '%1'" ).arg( seen ) );
    qputenv( "LC_ALL", "de_DE.UTF-8" );
    qputenv( "LANGUAGE", "de" );
    Git( wrapper ).run( { "--version" } );
    qunsetenv( "LC_ALL" );
    qunsetenv( "LANGUAGE" );

    QFile file( seen );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    CHECK( file.readAll().trimmed() == "C/C" );
}

TEST_CASE( "A publish that stops before committing is answered, not lost", "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const auto request = PublishRequest::forGroup( makeGroup( "Network" ), GroupAction::Add );

    SECTION( "a subfolder outside the repository" )
    {
        const Team team;
        const auto member = team.member( "alice", "../outside" );
        const auto outcome = publishAndWait( *member, { request } );
        REQUIRE( outcome.results.size() == 1 );
        CHECK( outcome.results[ 0 ].status == PublishStatus::Failed );
        CHECK_FALSE( outcome.results[ 0 ].message.isEmpty() );
    }

    SECTION( "a clone that fails" )
    {
        const QTemporaryDir root;
        REQUIRE( root.isValid() );
        TeamFolder folder( root.filePath( "clone" ) );
        folder.setUp(
            policyFor( QUrl::fromLocalFile( root.filePath( "missing.git" ) ).toString() ) );
        const auto outcome = publishAndWait( folder, { request } );
        REQUIRE( outcome.results.size() == 1 );
        CHECK( outcome.results[ 0 ].status == PublishStatus::Failed );
        CHECK_FALSE( outcome.results[ 0 ].message.isEmpty() );
    }

    SECTION( "a Git that cannot start where a clone exists" )
    {
        const QTemporaryDir root;
        REQUIRE( root.isValid() );
        REQUIRE( QDir().mkpath( root.filePath( "clone/.git" ) ) );
        TeamFolder folder( root.filePath( "clone" ), root.filePath( "no-git-here" ) );
        folder.setUp(
            policyFor( QUrl::fromLocalFile( root.filePath( "server.git" ) ).toString() ) );
        const auto outcome = publishAndWait( folder, { request } );
        REQUIRE( outcome.results.size() == 1 );
        CHECK( outcome.results[ 0 ].status == PublishStatus::Failed );
        CHECK( outcome.results[ 0 ].message.contains( "Git could not be started" ) );
    }

    SECTION( "a sync that cannot bring the clone up to date" )
    {
        const Team team;
        const auto alice = team.member( "alice" );
        team.pushGroupByHand( "alice", makeGroup( "Base" ) );
        {
            const auto bob = team.member( "bob" );
        }
        team.pushGroupByHand( "alice", makeGroup( "More" ) );
        const auto wrapper
            = wrapperGit( team.root(), "git-merge.sh",
                          "if [ \"$1\" = merge ]; then echo 'merge broke' >&2; exit 1; fi" );
        TeamFolder folder( team.cloneOf( "bob" ), wrapper );
        folder.setUp( policyFor( team.url() ) );
        const auto outcome = publishAndWait( folder, { request } );
        REQUIRE( outcome.results.size() == 1 );
        CHECK( outcome.results[ 0 ].status == PublishStatus::Failed );
        CHECK( outcome.results[ 0 ].message.contains( "merge broke" ) );
    }
}

TEST_CASE( "A change made offline does not silently overwrite what a colleague pushed",
           "[teamfolder][conflict]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto bob = team.member( "bob" );
    const auto network = makeGroup( "Network", "original" );
    team.pushGroupByHand( "alice", network );
    syncNow( *alice );
    syncNow( *bob );

    const auto away = team.serverPath() + ".away";
    REQUIRE( QDir().rename( team.serverPath(), away ) );
    auto mine = network;
    mine.setFilters( { { "Alices", "alices offline", false } } );
    // A second, unrelated offline change stays publishable.
    const auto stored = publishAndWait(
        *alice, { PublishRequest::forGroup( mine, GroupAction::Change ),
                  PublishRequest::forGroup( makeGroup( "Other" ), GroupAction::Add ) } );
    REQUIRE( stored.results.size() == 2 );
    CHECK( stored.results[ 0 ].status == PublishStatus::Pending );
    REQUIRE( QDir().rename( away, team.serverPath() ) );

    auto theirs = network;
    theirs.setFilters( { { "Bobs", "bobs pattern", false } } );
    REQUIRE( publishGroup( *bob, theirs ).results[ 0 ].status == PublishStatus::Published );

    QSignalSpy finished( alice.get(), &TeamFolder::publishFinished );
    syncNow( *alice );
    REQUIRE( finished.size() >= 1 );
    const auto outcome = finished.at( 0 ).at( 0 ).value<PublishOutcome>();
    int conflicts = 0;
    for ( const auto& result : outcome.results ) {
        if ( result.status == PublishStatus::Conflict ) {
            ++conflicts;
            const auto* theirsGroup = groupOfKind<PredefinedFilterSet>( result.theirs );
            REQUIRE( theirsGroup != nullptr );
            CHECK( theirsGroup->filters()[ 0 ].pattern == "bobs pattern" );
            CHECK( result.request.id == network.id() );
        }
    }
    CHECK( conflicts == 1 );
    // Bob's version stands on the server and for Alice; her other change went through.
    CHECK( team.serverFiles().contains( "Other_filter.conf" ) );
    for ( const auto& group : alice->filterGroups() ) {
        if ( group.id() == network.id() ) {
            CHECK( group.filters()[ 0 ].pattern == "bobs pattern" );
        }
    }
}

TEST_CASE( "A push that is rejected and retried does not overwrite the racing change either",
           "[teamfolder][conflict]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto carol = team.member( "carol" );
    const auto network = makeGroup( "Network", "original" );
    team.pushGroupByHand( "carol", network );

    auto theirs = network;
    theirs.setFilters( { { "Carols", "carols pattern", false } } );
    const auto theirFile = QDir( team.root() ).filePath( "theirs.conf" );
    REQUIRE( writeGroup( theirFile, theirs ) );
    const auto carolClone = team.cloneOf( "carol" );
    const auto marker = QDir( team.root() ).filePath( "raced" );
    const auto gitPath = QStandardPaths::findExecutable( QStringLiteral( "git" ) );
    const auto wrapper = wrapperGit( team.root(), "git-race.sh",
                                     QStringLiteral( "if [ \"$1\" = push ] && [ ! -e '%1' ]; then\n"
                                                     "  touch '%1'\n"
                                                     "  cp '%2' '%3/Network_filter.conf'\n"
                                                     "  '%4' -C '%3' add Network_filter.conf\n"
                                                     "  '%4' -C '%3' commit -q -m 'Raced'\n"
                                                     "  '%4' -C '%3' push -q origin HEAD\n"
                                                     "fi" )
                                         .arg( marker, theirFile, carolClone, gitPath ) );

    TeamFolder alice( team.cloneOf( "alice" ), wrapper );
    alice.setUp( policyFor( team.url() ) );
    REQUIRE( settled( alice ) );

    auto mine = network;
    mine.setFilters( { { "Alices", "alices pattern", false } } );
    const auto outcome = publishGroup( alice, mine );
    REQUIRE( outcome.results.size() == 1 );
    CHECK( outcome.results[ 0 ].status == PublishStatus::Conflict );
    CHECK( QFileInfo::exists( marker ) );
    CHECK( gitOutput( team.serverPath(),
                      { "--git-dir", team.serverPath(), "log", "-1", "--format=%s" } )
               .trimmed()
           == "Raced" );
    CHECK_FALSE( alice.hasPendingChanges() );
}

TEST_CASE( "A stopped Git's index.lock is removed", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto clone = root.filePath( "clone" );
    REQUIRE( QDir().mkpath( clone + "/.git" ) );
    const auto wrapper
        = wrapperGit( root.path(), "git-lock.sh",
                      QStringLiteral( "touch '%1/.git/index.lock'\nexec sleep 10" ).arg( clone ) );
    auto stop = std::make_shared<std::atomic_bool>( false );
    std::atomic_bool made{ false };
    auto stopper = stopOnceMade( clone + "/.git/index.lock", stop, made );
    const auto result = Git( wrapper, stop ).run( { "status" }, clone );
    stopper.join();
    REQUIRE( made );

    CHECK_FALSE( result.succeeded );
    CHECK_FALSE( QFileInfo::exists( clone + "/.git/index.lock" ) );
}

TEST_CASE( "An index.lock older than the stopped run is not the run's and stays", "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto clone = root.filePath( "clone" );
    REQUIRE( QDir().mkpath( clone + "/.git" ) );
    const auto lockPath = clone + "/.git/index.lock";
    {
        QFile lock( lockPath );
        REQUIRE( lock.open( QIODevice::WriteOnly ) );
        REQUIRE( lock.setFileTime( QDateTime::currentDateTime().addSecs( -3600 ),
                                   QFileDevice::FileModificationTime ) );
    }
    const auto started = root.filePath( "started" );
    const auto wrapper = wrapperGit( root.path(), "git-sleep.sh",
                                     QStringLiteral( "touch '%1'\nexec sleep 10" ).arg( started ) );
    auto stop = std::make_shared<std::atomic_bool>( false );
    std::atomic_bool made{ false };
    auto stopper = stopOnceMade( started, stop, made );
    const auto result = Git( wrapper, stop ).run( { "status" }, clone );
    stopper.join();
    REQUIRE( made );

    CHECK_FALSE( result.succeeded );
    CHECK( QFileInfo::exists( lockPath ) );
}

// --- Naming Groups of Value Names: the third kind (#647) ---

TEST_CASE( "Team Naming Groups arrive next to the other kinds, each file read by its kind",
           "[teamfolder][valuenames]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto bap = makeNamingGroup( "BAP" );
    team.pushGroupByHand( "alice", makeNamingGroup( "zeta" ) );
    team.pushGroupByHand( "alice", bap );
    team.pushGroupByHand( "alice", makeSet( "Levels" ) );
    team.pushGroupByHand( "alice", makeGroup( "Network" ) );
    // A file naming a kind this version does not know is no group of it.
    team.pushByHand( "alice", "future.conf", "kind=charts\n[PredefinedFiltersCollection]\n" );
    // Nor is a Naming Group of a newer version.
    team.pushByHand( "alice", "newer_valuenames.conf",
                     "kind=valuenames\n[NamingGroup]\nversion=99\nid=x\nname=Newer\n" );

    const auto bob = team.member( "bob" );
    CHECK( namesOf( bob->namingGroups() ) == QStringList{ "BAP", "zeta" } );
    CHECK( namesOf( bob->highlighterGroups() ) == QStringList{ "Levels" } );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Network" } );
    REQUIRE( bob->namingGroups().size() == 2 );
    CHECK( bob->namingGroups()[ 0 ].id() == bap.id() );
    CHECK( bob->namingGroups()[ 0 ].sameAs( bap ) );
    CHECK_FALSE( bob->namingGroupRevision( bap.id() ).isEmpty() );
    const auto skipped = bob->skippedFiles();
    REQUIRE( skipped.size() == 2 );
    CHECK( skipped[ 0 ].file == "future.conf" );
    CHECK( skipped[ 0 ].reason.contains( "kind of group this version does not know" ) );
    CHECK( skipped[ 1 ].file == "newer_valuenames.conf" );
    CHECK( skipped[ 1 ].reason.contains( "newer version" ) );

    // A change shows at the next sync, as a change of the Naming Groups only.
    QSignalSpy valueNamesChanged( bob.get(), &TeamFolder::namingGroupsChanged );
    QSignalSpy filtersChanged( bob.get(), &TeamFolder::groupsChanged );
    team.pushGroupByHand( "alice", makeNamingGroup( "BAP", "Other" ).withId( bap.id() ) );
    syncNow( *bob );
    REQUIRE( valueNamesChanged.size() == 1 );
    CHECK( valueNamesChanged.at( 0 ).at( 0 ).value<TeamGroupChanges>().changed
           == QStringList{ bap.id() } );
    CHECK( filtersChanged.isEmpty() );
    CHECK( ecuNameOf( bob->namingGroups()[ 0 ] ) == "Other" );
}

TEST_CASE( "A Naming Group is published, changed and deleted for the whole team",
           "[teamfolder][valuenames][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto bob = team.member( "bob" );

    auto bap = makeNamingGroup( "BAP" );
    const auto added
        = publishAndWait( *alice, { PublishRequest::forGroup( bap, GroupAction::Add ) } );
    REQUIRE( added.results.size() == 1 );
    CHECK( added.results[ 0 ].status == PublishStatus::Published );
    CHECK(
        team.lastCommit()
        == QStringList{ "Add naming group \"BAP\"", "Team Folder Test", "BAP_valuenames.conf" } );

    syncNow( *bob );
    REQUIRE( bob->namingGroups().size() == 1 );
    CHECK( bob->namingGroups()[ 0 ].sameAs( bap ) );
    CHECK( bob->filterGroups().isEmpty() );
    CHECK( bob->highlighterGroups().isEmpty() );

    SECTION( "a change and a rename keep the file" )
    {
        auto changed = makeNamingGroup( "BAP renamed", "Changed" ).withId( bap.id() );
        publishAndWait( *alice,
                        { PublishRequest::forGroup( changed, GroupAction::Rename, "BAP" ) } );
        CHECK( team.lastCommit()
               == QStringList{ "Rename naming group \"BAP\" to \"BAP renamed\"", "Team Folder Test",
                               "BAP_valuenames.conf" } );
        syncNow( *bob );
        REQUIRE( bob->namingGroups().size() == 1 );
        CHECK( bob->namingGroups()[ 0 ].name() == "BAP renamed" );
        CHECK( ecuNameOf( bob->namingGroups()[ 0 ] ) == "Changed" );
    }

    SECTION( "a deletion" )
    {
        const auto deleted = publishAndWait(
            *alice,
            { PublishRequest::forDeletion( GroupKind::ValueNames, bap.id(), bap.name() ) } );
        CHECK( deleted.results[ 0 ].status == PublishStatus::Published );
        CHECK( team.serverFiles().isEmpty() );
        syncNow( *bob );
        CHECK( bob->namingGroups().isEmpty() );
    }

    SECTION( "a dialog's edits ask for what changed of the Naming Groups" )
    {
        auto renamed = bap;
        renamed.setName( "Renamed" );
        const auto fresh = makeNamingGroup( "Fresh" );
        // Checks are no change: they are the user's own.
        auto unchecked = bap;
        unchecked.setEnabled( false );
        CHECK( logsquirl::teamfolder::requestsForChanges<NamingGroup>( { bap }, { unchecked } )
                   .isEmpty() );

        const auto requests = logsquirl::teamfolder::requestsForChanges<NamingGroup>(
            { bap }, { renamed, fresh }, alice->namingGroupRevisions() );
        REQUIRE( requests.size() == 2 );
        CHECK( requests[ 0 ].kind == GroupKind::ValueNames );
        CHECK( requests[ 0 ].action == GroupAction::Rename );
        CHECK( requests[ 0 ].baseRevision.value_or( QString{} )
               == alice->namingGroupRevision( bap.id() ) );
        CHECK( requests[ 1 ].action == GroupAction::Add );
        CHECK( groupOfKind<NamingGroup>( requests[ 1 ].group ) != nullptr );
        const auto deletions
            = logsquirl::teamfolder::requestsForChanges<NamingGroup>( { bap }, {} );
        REQUIRE( deletions.size() == 1 );
        CHECK( deletions[ 0 ].kind == GroupKind::ValueNames );
        CHECK( deletions[ 0 ].action == GroupAction::Delete );
    }
}

TEST_CASE( "Publishing a Naming Group someone else changed meanwhile reports a conflict",
           "[teamfolder][valuenames][conflict]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    const auto alice = team.member( "alice" );
    const auto bap = makeNamingGroup( "BAP", "Original" );
    team.pushGroupByHand( "alice", bap );
    syncNow( *alice );
    const auto bob = team.member( "bob" );

    auto request = PublishRequest::forGroup( makeNamingGroup( "BAP", "Bobs" ).withId( bap.id() ),
                                             GroupAction::Change );
    request.baseRevision = bob->namingGroupRevision( bap.id() );
    REQUIRE_FALSE( request.baseRevision.value_or( QString{} ).isEmpty() );

    const auto theirs = makeNamingGroup( "BAP", "Alices" ).withId( bap.id() );
    REQUIRE( publishAndWait( *alice, { PublishRequest::forGroup( theirs, GroupAction::Change ) } )
                 .results[ 0 ]
                 .status
             == PublishStatus::Published );
    const auto serverHead = team.lastCommit();

    const auto outcome = publishAndWait( *bob, { request } );
    REQUIRE( outcome.results.size() == 1 );
    const auto& conflict = outcome.results[ 0 ];
    CHECK( conflict.status == PublishStatus::Conflict );
    CHECK( conflict.hasTheirs() );
    const auto* theirsGroup = groupOfKind<NamingGroup>( conflict.theirs );
    REQUIRE( theirsGroup != nullptr );
    CHECK( ecuNameOf( *theirsGroup ) == "Alices" );
    CHECK( groupOfKind<PredefinedFilterSet>( conflict.theirs ) == nullptr );
    CHECK( team.lastCommit() == serverHead );

    SECTION( "keep mine overwrites their version" )
    {
        QSignalSpy finished( bob.get(), &TeamFolder::publishFinished );
        bob->resolveConflict( request, ConflictChoice::KeepMine );
        REQUIRE( finished.wait( SyncTimeoutMs ) );
        REQUIRE( settled( *bob ) );
        syncNow( *alice );
        REQUIRE( alice->namingGroups().size() == 1 );
        CHECK( ecuNameOf( alice->namingGroups()[ 0 ] ) == "Bobs" );
    }

    SECTION( "save mine as a copy adds a Naming Group and leaves theirs" )
    {
        QSignalSpy finished( bob.get(), &TeamFolder::publishFinished );
        bob->resolveConflict( request, ConflictChoice::SaveAsCopy );
        REQUIRE( finished.wait( SyncTimeoutMs ) );
        REQUIRE( settled( *bob ) );
        syncNow( *alice );
        const auto groups = alice->namingGroups();
        CHECK( namesOf( groups ) == QStringList{ "BAP", "BAP (2)" } );
        REQUIRE( groups.size() == 2 );
        CHECK( ecuNameOf( groups[ 0 ] ) == "Alices" );
        CHECK( groups[ 1 ].id() != bap.id() );
        CHECK( ecuNameOf( groups[ 1 ] ) == "Bobs" );
    }
}

// The step that failed and Git's output, reported apart (#711): the status
// names the step, and Git's output is shown as Git wrote it.
TEST_CASE( "A failed step is reported apart from Git's output, which is kept as Git wrote it",
           "[teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        checkMissingGitIsReported();
        return;
    }

    const Team team;
    // Git's output of several lines, one of them indented, as Git writes the
    // message of a server.
    const QString printed = "fatal: first line of the server's message\n"
                            "    an indented second line, longer than a status label would "
                            "show on one line of its own without wrapping it\n"
                            "fatal: Could not read from remote repository.";
    const auto failOn = [ & ]( const QString& command ) {
        return wrapperGit( team.root(), "git-" + command + ".sh",
                           QStringLiteral( "if [ \"$1\" = %1 ]; then\n"
                                           "  printf '%s\\n' \"fatal: first line of the "
                                           "server's message\" \"    an indented second line, "
                                           "longer than a status label would show on one line "
                                           "of its own without wrapping it\" \"fatal: Could not "
                                           "read from remote repository.\" >&2\n"
                                           "  exit 128\n"
                                           "fi" )
                               .arg( command ) );
    };

    SECTION( "a clone" )
    {
        TeamFolder folder( team.cloneOf( "alice" ), failOn( "clone" ) );
        folder.setUp( policyFor( team.url() ) );
        REQUIRE( settled( folder ) );

        CHECK( folder.state() == TeamFolder::State::Error );
        CHECK( folder.failedStep() == SyncStep::Clone );
        CHECK( folder.heading() == "Clone failed" );
        CHECK( folder.gitOutput() == printed );
        CHECK( folder.remarks().isEmpty() );
    }

    SECTION( "a pull" )
    {
        // Alice's clone exists; only the syncs after it fail.
        team.member( "alice" );
        TeamFolder folder( team.cloneOf( "alice" ), failOn( "fetch" ) );
        folder.setUp( policyFor( team.url() ) );
        REQUIRE( settled( folder ) );

        CHECK( folder.state() == TeamFolder::State::NotSynced );
        CHECK( folder.failedStep() == SyncStep::Pull );
        CHECK( folder.heading() == "Pull failed" );
        CHECK( folder.gitOutput() == printed );
    }

    SECTION( "a push the server cannot be reached for" )
    {
        TeamFolder folder( team.cloneOf( "alice" ), failOn( "push" ) );
        folder.setUp( policyFor( team.url() ) );
        REQUIRE( settled( folder ) );
        REQUIRE( folder.failedStep() == SyncStep::None );

        const auto outcome = publishGroup( folder, makeGroup( "Network" ), GroupAction::Add );
        REQUIRE( outcome.results.size() == 1 );
        CHECK( outcome.results[ 0 ].status == PublishStatus::Pending );
        CHECK( folder.state() == TeamFolder::State::NotSynced );
        CHECK( folder.failedStep() == SyncStep::Push );
        CHECK( folder.heading() == "Push failed" );
        CHECK( folder.gitOutput() == printed );
        CHECK( folder.isWritable() );
    }
}

TEST_CASE( "A group that could not be published is still reported beside the status",
           "[teamfolder][publish]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto wrapper = wrapperGit( team.root(), "git-commit.sh",
                                     "if [ \"$1\" = commit ]; then\n"
                                     "  echo 'fatal: no commit in this test' >&2\n"
                                     "  exit 128\n"
                                     "fi" );
    TeamFolder alice( team.cloneOf( "alice" ), wrapper );
    alice.setUp( policyFor( team.url() ) );
    REQUIRE( settled( alice ) );

    const auto outcome = publishGroup( alice, makeGroup( "Network" ), GroupAction::Add );
    REQUIRE( outcome.results.size() == 1 );
    CHECK( outcome.results[ 0 ].status == PublishStatus::Failed );
    // The sync itself worked out.
    CHECK( alice.state() == TeamFolder::State::Synced );
    CHECK( alice.failedStep() == SyncStep::None );
    CHECK( alice.remarks() == QStringList{ "Not published: fatal: no commit in this test" } );
}

TEST_CASE( "Common failures are recognised in Git's recorded output", "[teamfolder][hint]" )
{
    // What servers print, recorded: these cannot be produced locally.
    SECTION( "a sign-in refused over SSH" )
    {
        CHECK( failureHintOf( SyncStep::Clone, "git@github.com: Permission denied (publickey).\n"
                                               "fatal: Could not read from remote repository.\n"
                                               "\n"
                                               "Please make sure you have the correct access "
                                               "rights\n"
                                               "and the repository exists." )
               == FailureHint::SignIn );
        CHECK( failureHintOf( SyncStep::Pull,
                              "git@git.example.com: Permission denied (publickey,password)." )
               == FailureHint::SignIn );
    }

    SECTION( "a sign-in refused over HTTPS" )
    {
        CHECK( failureHintOf( SyncStep::Clone,
                              "remote: Invalid username or token. Password authentication is not "
                              "supported for Git operations.\n"
                              "fatal: Authentication failed for "
                              "'https://github.com/example/team.git/'" )
               == FailureHint::SignIn );
        CHECK( failureHintOf( SyncStep::Pull,
                              "fatal: could not read Username for 'https://github.com': terminal "
                              "prompts disabled" )
               == FailureHint::SignIn );
    }

    SECTION( "an organization that requires SAML SSO" )
    {
        CHECK( failureHintOf( SyncStep::Clone,
                              "ERROR: The 'example' organization has enabled or enforced SAML "
                              "SSO. To access this repository, you must use the HTTPS remote "
                              "with a personal access token or SSH with an SSH key and passphrase "
                              "that has been authorized for this organization. Visit "
                              "https://docs.github.com/articles/"
                              "authenticating-to-a-github-organization-with-saml-single-sign-on/ "
                              "for more information.\n"
                              "\n"
                              "fatal: Could not read from remote repository.\n"
                              "\n"
                              "Please make sure you have the correct access rights\n"
                              "and the repository exists." )
               == FailureHint::SsoAuthorization );
        CHECK( failureHintOf( SyncStep::Pull,
                              "remote: The `example' organization has enabled or enforced SAML "
                              "SSO. To access\n"
                              "remote: this repository, you must re-authorize the OAuth "
                              "Application `Git Credential Manager`.\n"
                              "fatal: unable to access 'https://github.com/example/team.git/': "
                              "The requested URL returned error: 403" )
               == FailureHint::SsoAuthorization );
    }

    SECTION( "a repository the server does not have, or does not show" )
    {
        CHECK( failureHintOf( SyncStep::Clone, "ERROR: Repository not found.\n"
                                               "fatal: Could not read from remote repository." )
               == FailureHint::RepositoryNotFound );
        CHECK( failureHintOf( SyncStep::Clone,
                              "remote: Repository not found.\n"
                              "fatal: repository 'https://github.com/example/team.git/' not "
                              "found" )
               == FailureHint::RepositoryNotFound );
    }

    SECTION( "a server that cannot be reached over SSH" )
    {
        CHECK( failureHintOf( SyncStep::Clone,
                              "ssh: Could not resolve hostname git.example.com: nodename nor "
                              "servname provided, or not known\n"
                              "fatal: Could not read from remote repository." )
               == FailureHint::ServerUnreachable );
        CHECK( failureHintOf( SyncStep::Pull,
                              "ssh: connect to host git.example.com port 22: Connection timed "
                              "out\n"
                              "fatal: Could not read from remote repository." )
               == FailureHint::ServerUnreachable );
        CHECK( failureHintOf( SyncStep::Push,
                              "fatal: unable to access 'https://git.example.com/team.git/': "
                              "Could not resolve host: git.example.com" )
               == FailureHint::ServerUnreachable );
    }

    SECTION( "Git that cannot be started" )
    {
        // Only when there is no Git to start is it missing; one that is there
        // and does not start says why in its output.
        CHECK( failureHintOf( SyncStep::StartGit, "Git could not be started", true )
               == FailureHint::GitMissing );
        CHECK( failureHintOf( SyncStep::StartGit, "Git could not be started", false )
               == FailureHint::None );
    }

    SECTION( "nothing recognised, and a sentence that is only part of a line" )
    {
        CHECK( failureHintOf( SyncStep::Merge, "fatal: Not possible to fast-forward, aborting." )
               == FailureHint::None );
        CHECK( failureHintOf( SyncStep::None, {} ) == FailureHint::None );
        // The words inside a URL or a path are no sentence of Git's.
        CHECK( failureHintOf( SyncStep::Merge, "error: cannot lock ref "
                                               "'refs/remotes/origin/Permission denied "
                                               "(publickey).'" )
               == FailureHint::None );
        CHECK( failureHintOf( SyncStep::Pull, "fatal: the remote end hung up: 'https://host/"
                                              "Repository not found./x'" )
               == FailureHint::None );
    }

    SECTION( "the 403 of a refused push keeps its own rule" )
    {
        CHECK( failureHintOf( SyncStep::PushRefused,
                              "remote: Permission to example/team.git denied.\n"
                              "fatal: unable to access 'https://github.com/example/team.git/': "
                              "The requested URL returned error: 403" )
               == FailureHint::None );
    }

    CHECK( TeamFolder::hintOf( FailureHint::None ).isEmpty() );
    for ( const auto hint :
          { FailureHint::SignIn, FailureHint::SsoAuthorization, FailureHint::RepositoryNotFound,
            FailureHint::ServerUnreachable, FailureHint::GitMissing } ) {
        CHECK_FALSE( TeamFolder::hintOf( hint ).isEmpty() );
    }
}

TEST_CASE( "A failed sync says what to do about a common failure, with Git's output unchanged",
           "[teamfolder][hint]" )
{
    const IsolatedGitEnvironment environment;
    const QTemporaryDir root;
    REQUIRE( root.isValid() );

    SECTION( "Git is not installed" )
    {
        TeamFolder folder( root.filePath( "clone" ), root.filePath( "no-git-here" ) );
        folder.setUp(
            policyFor( QUrl::fromLocalFile( root.filePath( "server.git" ) ).toString() ) );
        REQUIRE( settled( folder ) );

        CHECK( folder.failureHint() == FailureHint::GitMissing );
    }

    SECTION( "a Git program that is there and cannot be started" )
    {
        // Neither executable nor a program: starting it fails all the same.
        QFile notExecutable( root.filePath( "git-not-executable" ) );
        REQUIRE( notExecutable.open( QIODevice::WriteOnly ) );
        notExecutable.write( "not a program\n" );
        notExecutable.close();
        REQUIRE( notExecutable.setPermissions( QFile::ReadOwner | QFile::WriteOwner ) );

        TeamFolder folder( root.filePath( "clone" ), notExecutable.fileName() );
        folder.setUp(
            policyFor( QUrl::fromLocalFile( root.filePath( "server.git" ) ).toString() ) );
        REQUIRE( settled( folder ) );

        CHECK( folder.failedStep() == SyncStep::StartGit );
        CHECK( folder.failureHint() == FailureHint::None );
    }

    if ( !gitInstalled() ) {
        return;
    }

    SECTION( "a repository that is not there" )
    {
        TeamFolder folder( root.filePath( "clone" ) );
        folder.setUp(
            policyFor( QUrl::fromLocalFile( root.filePath( "missing.git" ) ).toString() ) );
        REQUIRE( settled( folder ) );

        CHECK( folder.failedStep() == SyncStep::Clone );
        CHECK( folder.failureHint() == FailureHint::RepositoryNotFound );
        // The hint only adds to Git's output.
        CHECK( folder.gitOutput().startsWith( "fatal: " ) );
        CHECK_FALSE( folder.gitOutput().contains( TeamFolder::hintOf( folder.failureHint() ) ) );
    }

    SECTION( "a URL that holds the words of other failures names only the missing repository" )
    {
        const auto misleading
            = root.filePath( "Permission denied (publickey) organization has enabled or enforced "
                             "SAML SSO ssh Could not resolve hostname" );
        REQUIRE( QDir().mkpath( misleading ) );
        TeamFolder folder( root.filePath( "clone" ) );
        folder.setUp( policyFor(
            QUrl::fromLocalFile( QDir( misleading ).filePath( "missing.git" ) ).toString() ) );
        REQUIRE( settled( folder ) );

        CHECK( folder.failedStep() == SyncStep::Clone );
        CHECK( folder.failureHint() == FailureHint::RepositoryNotFound );
    }

    SECTION( "a server nobody answers at" )
    {
        TeamFolder folder( root.filePath( "clone" ) );
        folder.setUp( policyFor( "http://127.0.0.1:1/team.git" ) );
        REQUIRE( settled( folder ) );

        CHECK( folder.failedStep() == SyncStep::Clone );
        INFO( folder.gitOutput().toStdString() );
        CHECK( folder.failureHint() == FailureHint::ServerUnreachable );
    }

    SECTION( "an unrecognised failure gives no hint" )
    {
        const auto wrapper = wrapperGit( root.path(), "git-clone.sh",
                                         "if [ \"$1\" = clone ]; then\n"
                                         "  echo 'fatal: something nobody foresaw' >&2\n"
                                         "  exit 128\n"
                                         "fi" );
        TeamFolder folder( root.filePath( "clone" ), wrapper );
        folder.setUp(
            policyFor( QUrl::fromLocalFile( root.filePath( "missing.git" ) ).toString() ) );
        REQUIRE( settled( folder ) );

        CHECK( folder.failedStep() == SyncStep::Clone );
        CHECK( folder.gitOutput() == "fatal: something nobody foresaw" );
        CHECK( folder.failureHint() == FailureHint::None );
    }

    SECTION( "a sync that worked gives no hint" )
    {
        const Team team;
        const auto folder = team.member( "alice" );
        CHECK( folder->failureHint() == FailureHint::None );
    }
}

TEST_CASE( "The Team Folder records when it last synced, keeps it, and resets it",
           "[teamfolder][lastsynced]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        return;
    }

    const Team team;
    const auto clone = team.cloneOf( "alice" );

    // Before any sync that reached the repository: never.
    {
        TeamFolder folder( clone, QDir( team.root() ).filePath( "no-git-here" ) );
        CHECK_FALSE( folder.lastSynced().isValid() );
        folder.setUp( policyFor( team.url() ) );
        REQUIRE( settled( folder ) );
        REQUIRE( folder.state() == TeamFolder::State::Error );
        CHECK_FALSE( folder.lastSynced().isValid() );
        CHECK_FALSE( folder.hasClone() );
    }

    const auto before = QDateTime::currentDateTimeUtc().addSecs( -1 );
    auto folder = std::make_unique<TeamFolder>( clone );
    folder->setUp( policyFor( team.url() ) );
    REQUIRE( settled( *folder ) );
    REQUIRE( folder->state() == TeamFolder::State::Synced );
    const auto synced = folder->lastSynced();
    CHECK( synced.isValid() );
    CHECK( synced >= before );
    CHECK( synced <= QDateTime::currentDateTimeUtc().addSecs( 1 ) );
    CHECK( folder->hasClone() );
    CHECK( folder->cloneDirectory() == clone );

    SECTION( "a failed sync leaves the last successful time" )
    {
        REQUIRE( QDir().rename( team.serverPath(), team.serverPath() + ".gone" ) );
        syncNow( *folder );
        REQUIRE( folder->state() == TeamFolder::State::NotSynced );
        CHECK( folder->lastSynced() == synced );
        REQUIRE( QDir().rename( team.serverPath() + ".gone", team.serverPath() ) );
    }

    SECTION( "the time survives a restart" )
    {
        folder.reset();
        // A Git that cannot run cannot sync: what is shown is what was kept.
        TeamFolder restarted( clone, QDir( team.root() ).filePath( "no-git-here" ) );
        restarted.setUp( policyFor( team.url() ) );
        REQUIRE( settled( restarted ) );
        CHECK( restarted.lastSynced() == synced );
    }

    SECTION( "another Repository URL resets it" )
    {
        folder->setUp( policyFor(
            QUrl::fromLocalFile( QDir( team.root() ).filePath( "other.git" ) ).toString() ) );
        CHECK_FALSE( folder->lastSynced().isValid() );
        REQUIRE( settled( *folder ) );
        CHECK_FALSE( folder->lastSynced().isValid() );

        // Also after a restart, and on going back to the first repository.
        folder.reset();
        TeamFolder restarted( clone, QDir( team.root() ).filePath( "no-git-here" ) );
        restarted.setUp( policyFor( team.url() ) );
        REQUIRE( settled( restarted ) );
        CHECK_FALSE( restarted.lastSynced().isValid() );
    }

    SECTION( "another Subfolder resets it" )
    {
        folder->setUp( policyFor( team.url(), "groups" ) );
        CHECK_FALSE( folder->lastSynced().isValid() );
        REQUIRE( settled( *folder ) );
        // The subfolder's sync reached the repository.
        CHECK( folder->lastSynced().isValid() );
        CHECK( folder->lastSynced() >= synced );
    }

    SECTION( "turning the Team Folder off and on again keeps it" )
    {
        folder->setUp( TeamFolderPolicy{} );
        folder.reset();
        TeamFolder restarted( clone, QDir( team.root() ).filePath( "no-git-here" ) );
        restarted.setUp( policyFor( team.url() ) );
        REQUIRE( settled( restarted ) );
        CHECK( restarted.lastSynced() == synced );
    }
}
