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
#include <optional>
#include <memory>
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
        const auto created
            = git_.run( { "init", "--quiet", "--bare", server_ }, root_.path() );
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
        const auto file = QDir( subfolder ).filePath(
            suggestedFileName( group.name(), GroupKind::Filter ) );
        const QDir clone( cloneOf( member ) );
        QDir().mkpath( QFileInfo( clone.filePath( file ) ).absolutePath() );
        REQUIRE( writeGroup( clone.filePath( file ), group ) );
        commitAndPush( member, { "add", "--", QDir::cleanPath( file ) }, "Share a group" );
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
