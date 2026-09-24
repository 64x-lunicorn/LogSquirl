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

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "groupexchange.h"
#include "teamfolder.h"
#include "teamfoldergit.h"

using logsquirl::teamfolder::Git;
using logsquirl::teamfolder::TeamGroupChanges;
using namespace logsquirl::groupexchange;

namespace {

constexpr int SyncTimeoutMs = 60'000;

// Sets the environment every Git of this process runs in for as long as it
// lives, and puts back what was there.
class IsolatedGitEnvironment {
public:
    IsolatedGitEnvironment()
    {
#ifdef Q_OS_WIN
        const QByteArray nullDevice = "NUL";
#else
        const QByteArray nullDevice = "/dev/null";
#endif
        set( "GIT_CONFIG_GLOBAL", nullDevice );
        set( "GIT_CONFIG_NOSYSTEM", "1" );
        set( "GIT_TERMINAL_PROMPT", "0" );
        set( "GIT_AUTHOR_NAME", "Team Folder Test" );
        set( "GIT_AUTHOR_EMAIL", "team-folder-test@example.invalid" );
        set( "GIT_COMMITTER_NAME", "Team Folder Test" );
        set( "GIT_COMMITTER_EMAIL", "team-folder-test@example.invalid" );
    }

    ~IsolatedGitEnvironment()
    {
        for ( const auto& [ name, value ] : previous_ ) {
            if ( value.has_value() ) {
                qputenv( name.c_str(), *value );
            }
            else {
                qunsetenv( name.c_str() );
            }
        }
    }

    IsolatedGitEnvironment( const IsolatedGitEnvironment& ) = delete;
    IsolatedGitEnvironment& operator=( const IsolatedGitEnvironment& ) = delete;

private:
    void set( const char* name, const QByteArray& value )
    {
        previous_.emplace( name, qEnvironmentVariableIsSet( name )
                                     ? std::optional<QByteArray>( qgetenv( name ) )
                                     : std::nullopt );
        qputenv( name, value );
    }

    std::map<std::string, std::optional<QByteArray>> previous_;
};

bool gitInstalled()
{
    return !QStandardPaths::findExecutable( QStringLiteral( "git" ) ).isEmpty();
}

// Whether the Team Folder is done syncing, waiting for it as long as needed.
bool settled( const TeamFolder& folder )
{
    return QTest::qWaitFor( [ &folder ] { return !folder.isSyncing(); }, SyncTimeoutMs );
}

void syncNow( TeamFolder& folder )
{
    REQUIRE( settled( folder ) );
    folder.sync();
    REQUIRE( settled( folder ) );
}

TeamFolderPolicy policyFor( const QString& url, const QString& subfolder = {} )
{
    return TeamFolderPolicy{ .enabled = true, .repositoryUrl = url, .subfolder = subfolder };
}

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
    CHECK( folder.message().contains( "Git could not be started" ) );
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
    CHECK( bob->message().isEmpty() );
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
    CHECK_FALSE( bob->message().isEmpty() );
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
    CHECK_FALSE( folder.message().isEmpty() );
    CHECK_FALSE( folder.message().contains( "Git could not be started" ) );
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
    CHECK_FALSE( bob->message().isEmpty() );
    CHECK( namesOf( bob->filterGroups() ) == QStringList{ "Network" } );

    // And comes back.
    REQUIRE( QDir().rename( team.serverPath() + ".gone", team.serverPath() ) );
    syncNow( *bob );
    CHECK( bob->state() == TeamFolder::State::Synced );
    CHECK( bob->message().isEmpty() );
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

#ifndef Q_OS_WIN

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
    const auto wrapper = QDir( team.root() ).filePath( "git-wrapper.sh" );
    {
        QFile script( wrapper );
        REQUIRE( script.open( QIODevice::WriteOnly ) );
        const auto marker = QDir( team.root() ).filePath( "raced" );
        const auto carolClone = team.cloneOf( "carol" );
        script.write( QStringLiteral( "#!/bin/sh\n"
                                      "if [ \"$1\" = push ] && [ ! -e '%1' ]; then\n"
                                      "  touch '%1'\n"
                                      "  cp '%2/Base_filter.conf' '%2/Raced_filter.conf'\n"
                                      "  '%3' -C '%2' add Raced_filter.conf\n"
                                      "  '%3' -C '%2' commit -q -m 'Raced'\n"
                                      "  '%3' -C '%2' push -q origin HEAD\n"
                                      "fi\n"
                                      "exec '%3' \"$@\"\n" )
                          .arg( marker, carolClone, gitPath )
                          .toUtf8() );
        script.setPermissions( script.permissions() | QFileDevice::ExeUser );
    }

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

#endif

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

    // A removed group is not asked for.
    CHECK( requestsForChanges( before, { network } ).isEmpty() );
}
