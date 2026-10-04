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

#include "teamfolder.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTemporaryFile>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "datalocation.h"
#include "groupexchange.h"
#include "log.h"
#include "teamfoldergit.h"

namespace logsquirl::teamfolder {

using logsquirl::valuenames::NamingGroup;

struct SyncOutcome {
    enum class Result {
        // Reached the repository and brought the clone up to date.
        Synced,
        // Could not reach the repository; the clone is as the last sync left it.
        Offline,
        // Cannot be used as it is.
        Failed
    };

    Result result = Result::Failed;
    // The step that failed, and what Git wrote then; or LogSquirl's own
    // reason, for a step that is not Git's.
    SyncStep failedStep = SyncStep::None;
    QString message;
    QList<TeamGroup<PredefinedFilterSet>> filterGroups;
    QList<TeamGroup<HighlighterSet>> highlighterGroups;
    QList<TeamGroup<NamingGroup>> namingGroups;
    QList<SkippedFile> skippedFiles;

    // Set when a publish was asked for.
    std::optional<PublishOutcome> published;
    // The server refused a push: nothing more can be published.
    bool refused = false;
    QString refusedReason;
    // Committed here and not on the server yet.
    bool hasPending = false;
};

// Records that a step of the sync failed with what Git wrote; a Git that did
// not even start fails the sync at starting it, whatever the step.
void failAt( SyncOutcome& outcome, SyncStep step, const GitResult& run )
{
    outcome.failedStep = run.started ? step : SyncStep::StartGit;
    outcome.message = run.message();
}

std::atomic<int>& groupFileReads()
{
    static std::atomic<int> count{ 0 };
    return count;
}

namespace {

const QString RemoteName = QStringLiteral( "origin" );

// The folder of the clone the groups live in, or nothing when the subfolder
// would lead out of the clone.
std::optional<QString> groupFolder( const QString& cloneDirectory, const QString& subfolder )
{
    const auto cleaned = QDir::cleanPath( subfolder.trimmed() );
    if ( cleaned.isEmpty() || cleaned == QStringLiteral( "." ) ) {
        return cloneDirectory;
    }
    if ( QDir::isAbsolutePath( cleaned ) || cleaned == QStringLiteral( ".." )
         || cleaned.startsWith( QStringLiteral( "../" ) ) ) {
        return std::nullopt;
    }
    return QDir( cloneDirectory ).filePath( cleaned );
}

// The id a group with the Default Filter Group's id takes in the Team Folder.
// The Default group is every user's own; a Team group cannot be it. The id
// follows from the file, so it stays the same from one sync to the next.
QString idForDefaultGroupIn( const QString& file )
{
    static const QUuid space( QStringLiteral( "6f1f4a3e-2f7c-4a5e-9d1c-5b0b6c7e8a90" ) );
    return QUuid::createUuidV5( space, file ).toString( QUuid::Id128 );
}

// What a file holds, as a revision: the same content is the same revision.
QString revisionOfFile( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return {};
    }
    return QString::fromLatin1(
        QCryptographicHash::hash( file.readAll(), QCryptographicHash::Sha1 ).toHex() );
}

void skipDuplicate( SyncOutcome& outcome, const QString& file, const QString& groupName )
{
    const auto reason
        = TeamFolder::tr( "Another file holds the group %1 already." ).arg( groupName );
    outcome.skippedFiles.append( { file, reason } );
    LOG_WARNING << "Team Folder skips " << file << ": " << reason;
}

// Adds the groups of one file to the Team groups of their kind. `ids` holds
// the groups known already: a group whose id is taken is skipped.
template <typename Group>
void addGroupsOfFile( const QList<Group>& groups, const QFileInfo& info, QSet<QString>& ids,
                      SyncOutcome& outcome, QList<TeamGroup<Group>>& teamGroups )
{
    const auto file = info.fileName();
    for ( const auto& group : groups ) {
        if ( ids.contains( group.id() ) ) {
            skipDuplicate( outcome, file, group.name() );
            continue;
        }
        ids.insert( group.id() );
        teamGroups.append( { group, file, revisionOfFile( info.absoluteFilePath() ) } );
    }
}

// The groups of one file, of whichever kind it holds, and what is wrong when
// it holds none.
struct GroupsOfFile {
    groupexchange::ReadError error = groupexchange::ReadError::None;
    QList<PredefinedFilterSet> filterGroups;
    QList<HighlighterSet> highlighterSets;
    QList<NamingGroup> namingGroups;
};

// Opens the file once and reads it as what its kind entry says: a Naming
// Group file by that entry alone, a file without one as a Filter Group or,
// failing that, a Highlighter Set file.
GroupsOfFile readGroupsOfFile( const QString& path )
{
    using namespace logsquirl::groupexchange;

    QSettings settings{ path, QSettings::IniFormat };
    GroupsOfFile read;
    if ( declaresKind( settings ) ) {
        auto valueNames = readGroups<NamingGroup>( settings );
        read.error = valueNames.error;
        read.namingGroups = std::move( valueNames.groups );
        return read;
    }

    auto filters = readGroups<PredefinedFilterSet>( settings );
    if ( filters.error == ReadError::None || filters.error == ReadError::Unreadable ) {
        read.error = filters.error;
        read.filterGroups = std::move( filters.groups );
        return read;
    }
    auto highlighters = readGroups<HighlighterSet>( settings );
    // A file that is neither holds no group of any kind.
    read.error = highlighters.error == ReadError::None ? ReadError::None : ReadError::NoGroups;
    read.highlighterSets = std::move( highlighters.groups );
    return read;
}

// Why a file of the folder is skipped.
QString skipReason( groupexchange::ReadError error )
{
    using groupexchange::ReadError;
    switch ( error ) {
    case ReadError::Unreadable:
        return TeamFolder::tr( "The file cannot be read." );
    case ReadError::OtherKind:
        return TeamFolder::tr( "The file holds a kind of group this version does not know." );
    case ReadError::NewerVersion:
        return TeamFolder::tr( "The file was written by a newer version of LogSquirl." );
    case ReadError::None:
    case ReadError::NoGroups:
        break;
    }
    return TeamFolder::tr( "The file holds no Filter Group, Highlighter Set or Naming Group." );
}

// Reads one file of the folder into the outcome. `ids` holds the groups known
// already: a group whose id is taken is skipped, the first file by name wins.
void readGroupFile( const QFileInfo& info, QSet<QString>& ids, SyncOutcome& outcome )
{
    groupFileReads().fetch_add( 1, std::memory_order_relaxed );
    const auto file = info.fileName();
    const auto read = readGroupsOfFile( info.absoluteFilePath() );
    if ( read.error != groupexchange::ReadError::None ) {
        const auto reason = skipReason( read.error );
        outcome.skippedFiles.append( { file, reason } );
        LOG_WARNING << "Team Folder skips " << file << ": " << reason;
        return;
    }

    QList<PredefinedFilterSet> filterGroups;
    for ( auto group : read.filterGroups ) {
        if ( group.id() == defaultFilterSetId() ) {
            group = group.withId( idForDefaultGroupIn( file ) );
        }
        filterGroups.append( group );
    }
    addGroupsOfFile( filterGroups, info, ids, outcome, outcome.filterGroups );
    addGroupsOfFile( read.highlighterSets, info, ids, outcome, outcome.highlighterGroups );
    addGroupsOfFile( read.namingGroups, info, ids, outcome, outcome.namingGroups );
}

void readGroups( const QString& folder, SyncOutcome& outcome )
{
    const QDir dir( folder );
    if ( !dir.exists() ) {
        return;
    }

    QSet<QString> ids;
    const auto files = dir.entryInfoList( { QStringLiteral( "*.conf" ) }, QDir::Files, QDir::Name );
    for ( const auto& info : files ) {
        readGroupFile( info, ids, outcome );
    }
}

// The groups of the folder as a sync knows them: read once, looked up by the
// steps that follow, and brought up to date where the files change -- after an
// integrate as a whole, after a commit for the one file it touched -- so that
// a request never sees a stale revision.
class GroupIndex {
public:
    explicit GroupIndex( QString folder )
        : folder_( std::move( folder ) )
    {
    }

    void reload()
    {
        groups_ = {};
        readGroups( folder_, groups_ );
    }

    // Reads one file again, after a commit wrote or removed it.
    void reloadFile( const QString& file )
    {
        groups_.filterGroups.removeIf(
            [ &file ]( const auto& known ) { return known.file == file; } );
        groups_.highlighterGroups.removeIf(
            [ &file ]( const auto& known ) { return known.file == file; } );
        groups_.namingGroups.removeIf(
            [ &file ]( const auto& known ) { return known.file == file; } );
        groups_.skippedFiles.removeIf(
            [ &file ]( const auto& known ) { return known.file == file; } );

        QSet<QString> ids;
        for ( const auto& known : std::as_const( groups_.filterGroups ) ) {
            ids.insert( known.group.id() );
        }
        for ( const auto& known : std::as_const( groups_.highlighterGroups ) ) {
            ids.insert( known.group.id() );
        }
        for ( const auto& known : std::as_const( groups_.namingGroups ) ) {
            ids.insert( known.group.id() );
        }
        const QFileInfo info( QDir( folder_ ).filePath( file ) );
        if ( info.isFile() ) {
            readGroupFile( info, ids, groups_ );
        }
    }

    const SyncOutcome& groups() const
    {
        return groups_;
    }

private:
    QString folder_;
    SyncOutcome groups_;
};

QStringList args( std::initializer_list<const char*> list )
{
    QStringList result;
    for ( const auto* item : list ) {
        result.append( QString::fromUtf8( item ) );
    }
    return result;
}

bool hasCommit( const Git& git, const QString& clone )
{
    return git.run( args( { "rev-parse", "--verify", "--quiet", "HEAD" } ), clone ).succeeded;
}

QString currentBranch( const Git& git, const QString& clone )
{
    const auto branch = git.run( args( { "symbolic-ref", "--quiet", "--short", "HEAD" } ), clone );
    return branch.succeeded ? branch.output.trimmed() : QString{};
}

bool remoteBranchExists( const Git& git, const QString& clone, const QString& branch )
{
    return !branch.isEmpty()
           && git.run( { QStringLiteral( "rev-parse" ), QStringLiteral( "--verify" ),
                         QStringLiteral( "--quiet" ),
                         QStringLiteral( "refs/remotes/" ) + RemoteName + QLatin1Char( '/' )
                             + branch },
                       clone )
                  .succeeded;
}

// How many commits a range holds, -1 when Git cannot tell.
int countCommits( const Git& git, const QString& clone, const QString& range )
{
    const auto counted
        = git.run( { QStringLiteral( "rev-list" ), QStringLiteral( "--count" ), range }, clone );
    return counted.succeeded ? counted.output.trimmed().toInt() : -1;
}

// Whether commits are here that the server does not have.
bool hasUnpushedCommits( const Git& git, const QString& clone )
{
    if ( !hasCommit( git, clone ) ) {
        return false;
    }
    const auto branch = currentBranch( git, clone );
    if ( !remoteBranchExists( git, clone, branch ) ) {
        return true;
    }
    return countCommits( git, clone,
                         RemoteName + QLatin1Char( '/' ) + branch + QStringLiteral( "..HEAD" ) )
           != 0;
}

// Brings the clone's working tree to what the repository's default branch
// holds, and puts changes committed here that were not pushed yet on top of
// it: a change of this user's wins over the same lines of the server's. Nothing
// to do for an empty repository. Git's run that failed, when one did.
std::optional<GitResult> integrate( const Git& git, const QString& clone )
{
    const QStringList defaultBranch{ QStringLiteral( "symbolic-ref" ), QStringLiteral( "--quiet" ),
                                     QStringLiteral( "--short" ),
                                     QStringLiteral( "refs/remotes/origin/HEAD" ) };
    auto head = git.run( defaultBranch, clone );
    if ( !head.succeeded ) {
        // A clone of a repository that was empty then knows no default branch;
        // ask the repository again, it may have one by now.
        git.run( { QStringLiteral( "remote" ), QStringLiteral( "set-head" ), RemoteName,
                   QStringLiteral( "--auto" ) },
                 clone );
        head = git.run( defaultBranch, clone );
    }
    if ( !head.succeeded ) {
        // Still none: the repository has no branch, so holds nothing yet.
        return std::nullopt;
    }

    const auto remoteBranch = head.output.trimmed();
    const auto localBranch = remoteBranch.mid( RemoteName.size() + 1 );

    GitResult update;
    if ( !hasCommit( git, clone ) ) {
        // Nothing checked out yet: the repository was empty when cloned.
        update = git.run( { QStringLiteral( "checkout" ), QStringLiteral( "--quiet" ),
                            QStringLiteral( "-B" ), localBranch, remoteBranch },
                          clone );
    }
    else {
        const auto ahead = countCommits( git, clone, remoteBranch + QStringLiteral( "..HEAD" ) );
        const auto behind = countCommits( git, clone, QStringLiteral( "HEAD.." ) + remoteBranch );
        if ( ahead == 0 ) {
            update = git.run( { QStringLiteral( "merge" ), QStringLiteral( "--ff-only" ),
                                QStringLiteral( "--quiet" ), remoteBranch },
                              clone );
        }
        else if ( behind == 0 ) {
            // Only ahead: a change waiting to be pushed.
            return std::nullopt;
        }
        else {
            update = git.run( { QStringLiteral( "rebase" ), QStringLiteral( "--quiet" ),
                                QStringLiteral( "-X" ), QStringLiteral( "theirs" ), remoteBranch },
                              clone );
            if ( !update.succeeded ) {
                git.run( args( { "rebase", "--abort" } ), clone );
            }
        }
    }
    if ( !update.succeeded ) {
        return update;
    }
    return std::nullopt;
}

struct PushResult {
    enum class Kind {
        Pushed,
        // The branch moved on the server since the last fetch.
        Rejected,
        // The server does not let this user push.
        Refused,
        // The server could not be reached, or Git failed for another reason.
        Unreachable
    };
    Kind kind = Kind::Pushed;
    QString message;
};

PushResult pushHead( const Git& git, const QString& clone )
{
    // --porcelain reports what became of each ref on a line of standard
    // output of a fixed form: "!<TAB>from:to<TAB>[reason] (why)".
    const auto pushed
        = git.run( { QStringLiteral( "push" ), QStringLiteral( "--porcelain" ),
                     QStringLiteral( "--set-upstream" ), RemoteName, QStringLiteral( "HEAD" ) },
                   clone );
    if ( pushed.succeeded ) {
        return {};
    }

    // What Git says when it cannot even try (network, credentials) names the
    // repository's URL and so may hold any number: it never classifies, with
    // one exception. An HTTP 403 prints nothing on standard output, only
    // Git's fixed sentence on standard error (Git runs untranslated), and
    // that sentence, not a bare number, says the server lets this user not
    // write (ADR 0008). Every other failure stays "unreachable" so that a
    // network error never discards a local commit. Otherwise only the status
    // of the ref classifies.
    PushResult result{ PushResult::Kind::Unreachable, pushed.message() };
    if ( pushed.error.contains( QStringLiteral( "The requested URL returned error: 403" ) ) ) {
        result.kind = PushResult::Kind::Refused;
        return result;
    }
    const auto lines = pushed.output.split( QLatin1Char( '\n' ) );
    for ( const auto& line : lines ) {
        if ( !line.startsWith( QLatin1Char( '!' ) ) ) {
            continue;
        }
        const auto status = line.section( QLatin1Char( '\t' ), 2 );
        if ( status.startsWith( QStringLiteral( "[rejected]" ) ) ) {
            // The branch moved on since the last fetch.
            result.kind = PushResult::Kind::Rejected;
        }
        else if ( status.startsWith( QStringLiteral( "[remote rejected]" ) ) ) {
            // The server's rules or hooks said no.
            result.kind = PushResult::Kind::Refused;
            break;
        }
    }
    return result;
}

QString kindWord( groupexchange::GroupKind kind )
{
    switch ( kind ) {
    case groupexchange::GroupKind::Filter:
        return QStringLiteral( "filter group" );
    case groupexchange::GroupKind::Highlighter:
        return QStringLiteral( "highlighter set" );
    case groupexchange::GroupKind::ValueNames:
        break;
    }
    return QStringLiteral( "naming group" );
}

// What the commit of a change says: the action and the group, in the
// repository's own language -- it is the team's history, not LogSquirl's UI.
QString commitMessage( const PublishRequest& request )
{
    const auto kind = kindWord( request.kind );
    switch ( request.action ) {
    case GroupAction::Add:
        return QStringLiteral( "Add %1 \"%2\"" ).arg( kind, request.name );
    case GroupAction::Rename:
        return QStringLiteral( "Rename %1 \"%2\" to \"%3\"" )
            .arg( kind, request.previousName, request.name );
    case GroupAction::Delete:
        return QStringLiteral( "Delete %1 \"%2\"" ).arg( kind, request.name );
    case GroupAction::Change:
        break;
    }
    return QStringLiteral( "Change %1 \"%2\"" ).arg( kind, request.name );
}

// A Team group as the folder holds it now.
struct FoundGroup {
    QString file;
    QString revision;
    AnyGroup group;
};

template <typename Group>
const TeamGroup<Group>* findById( const QList<TeamGroup<Group>>& groups, const QString& id )
{
    const auto found = std::find_if( groups.cbegin(), groups.cend(), [ &id ]( const auto& group ) {
        return group.group.id() == id;
    } );
    return found == groups.cend() ? nullptr : &*found;
}

std::optional<FoundGroup> findGroup( const GroupIndex& index, const PublishRequest& request )
{
    const auto& known = index.groups();
    FoundGroup found;
    const auto take = [ &found ]( const auto* group ) {
        if ( group != nullptr ) {
            found.file = group->file;
            found.revision = group->revision;
        }
        return group != nullptr;
    };
    switch ( request.kind ) {
    case groupexchange::GroupKind::Filter:
        if ( const auto* group = findById( known.filterGroups, request.id ); take( group ) ) {
            found.group = group->group;
            return found;
        }
        break;
    case groupexchange::GroupKind::Highlighter:
        if ( const auto* group = findById( known.highlighterGroups, request.id ); take( group ) ) {
            found.group = group->group;
            return found;
        }
        break;
    case groupexchange::GroupKind::ValueNames:
        if ( const auto* group = findById( known.namingGroups, request.id ); take( group ) ) {
            found.group = group->group;
            return found;
        }
        break;
    }
    return std::nullopt;
}

// The file a group lives in: the one it was read from, or for a group the
// folder does not know a free one named after it.
QString fileFor( const QString& folder, const GroupIndex& index, const PublishRequest& request )
{
    if ( const auto found = findGroup( index, request ) ) {
        return found->file;
    }

    for ( int number = 1;; ++number ) {
        const auto name = number == 1
                              ? request.name
                              : QStringLiteral( "%1 (%2)" ).arg( request.name ).arg( number );
        const auto file = groupexchange::suggestedFileName( name, request.kind );
        if ( !QFileInfo::exists( QDir( folder ).filePath( file ) ) ) {
            return file;
        }
    }
}

// The answer to a request that met a version of the group it did not know:
// the version that is in the folder now.
PublishResult conflictResult( const GroupIndex& index, const PublishRequest& request )
{
    PublishResult result;
    result.status = PublishStatus::Conflict;
    result.request = request;
    if ( const auto found = findGroup( index, request ) ) {
        result.file = found->file;
        result.theirs = found->group;
    }
    return result;
}

// Whether someone else changed the group since the user loaded it: the file
// has another revision than the one remembered, or is gone.
std::optional<PublishResult> conflictOf( const GroupIndex& index, const PublishRequest& request )
{
    if ( !request.baseRevision || request.baseRevision->isEmpty() || request.overwrite ) {
        return std::nullopt;
    }
    const auto found = findGroup( index, request );
    if ( found && found->revision == *request.baseRevision ) {
        return std::nullopt;
    }
    return conflictResult( index, request );
}

// Writes the group into the clone and commits that one file. Whether it was
// done, and Git's message when not.
PublishResult commitRequestToClone( const Git& git, const QString& clone, const QString& folder,
                                    const GroupIndex& index, const PublishRequest& request )
{
    PublishResult result;
    result.request = request;

    if ( request.action == GroupAction::Delete ) {
        const auto found = findGroup( index, request );
        if ( !found ) {
            // Already gone: nothing to delete.
            result.status = PublishStatus::Published;
            return result;
        }
        result.file = found->file;
        const auto relative
            = QDir( clone ).relativeFilePath( QDir( folder ).filePath( found->file ) );
        const auto removed = git.run( { QStringLiteral( "rm" ), QStringLiteral( "--quiet" ),
                                        QStringLiteral( "--" ), relative },
                                      clone );
        if ( !removed.succeeded ) {
            result.message = removed.message();
            return result;
        }
        const auto committed = git.run( { QStringLiteral( "commit" ), QStringLiteral( "--quiet" ),
                                          QStringLiteral( "-m" ), commitMessage( request ),
                                          QStringLiteral( "--" ), relative },
                                        clone );
        if ( !committed.succeeded ) {
            result.message = committed.message();
            return result;
        }
        result.status = PublishStatus::Published;
        return result;
    }

    result.file = fileFor( folder, index, request );
    const auto path = QDir( folder ).filePath( result.file );
    QDir().mkpath( QFileInfo( path ).absolutePath() );
    if ( !request.writeTo( path ) ) {
        result.message = TeamFolder::tr( "The group could not be written to %1." ).arg( path );
        return result;
    }

    const auto relative = QDir( clone ).relativeFilePath( path );
    const auto staged
        = git.run( { QStringLiteral( "add" ), QStringLiteral( "--" ), relative }, clone );
    if ( !staged.succeeded ) {
        result.message = staged.message();
        return result;
    }
    const auto changed = git.run( { QStringLiteral( "status" ), QStringLiteral( "--porcelain" ),
                                    QStringLiteral( "--" ), relative },
                                  clone );
    if ( changed.succeeded && changed.output.trimmed().isEmpty() ) {
        // The group is in the folder as it is: nothing to commit.
        result.status = PublishStatus::Published;
        return result;
    }

    const auto committed = git.run( { QStringLiteral( "commit" ), QStringLiteral( "--quiet" ),
                                      QStringLiteral( "-m" ), commitMessage( request ),
                                      QStringLiteral( "--" ), relative },
                                    clone );
    if ( !committed.succeeded ) {
        result.message = committed.message();
        return result;
    }
    result.status = PublishStatus::Published;
    return result;
}

// The same, and the index takes in the file the commit changed.
PublishResult commitRequest( const Git& git, const QString& clone, const QString& folder,
                             GroupIndex& index, const PublishRequest& request )
{
    auto result = commitRequestToClone( git, clone, folder, index, request );
    if ( !result.file.isEmpty() ) {
        index.reloadFile( result.file );
    }
    return result;
}

// Takes back what was committed here and cannot be pushed.
void discardUnpushed( const Git& git, const QString& clone )
{
    const auto branch = currentBranch( git, clone );
    if ( remoteBranchExists( git, clone, branch ) ) {
        git.run( { QStringLiteral( "reset" ), QStringLiteral( "--hard" ),
                   QStringLiteral( "--quiet" ), RemoteName + QLatin1Char( '/' ) + branch },
                 clone );
    }
    else {
        // The server holds nothing to go back to: start over.
        QDir( clone ).removeRecursively();
    }
}

// A sync that stops before it could commit anything still answers every
// request it was given: with the reason it stopped.
void failRequests( SyncOutcome& outcome, const QList<PublishRequest>& requests )
{
    if ( requests.isEmpty() ) {
        return;
    }
    PublishOutcome published;
    for ( const auto& request : requests ) {
        PublishResult failed;
        failed.status = PublishStatus::Failed;
        failed.message = outcome.message;
        failed.request = request;
        published.results.append( failed );
    }
    outcome.published = published;
}

// Adds a result to the answers of the sync, in place of the one of the same
// group when there is one.
void addResult( SyncOutcome& outcome, const PublishResult& result )
{
    if ( !outcome.published ) {
        outcome.published = PublishOutcome{};
    }
    for ( auto& known : outcome.published->results ) {
        if ( known.request.kind == result.request.kind && known.request.id == result.request.id ) {
            known = result;
            return;
        }
    }
    outcome.published->results.append( result );
}

// The group of the file at a revision of the clone, as a request; nothing for
// a file that holds no group.
std::optional<PublishRequest> requestFromRevision( const Git& git, const QString& clone,
                                                   const QString& revision, const QString& path,
                                                   GroupAction action )
{
    const auto shown
        = git.run( { QStringLiteral( "show" ), revision + QLatin1Char( ':' ) + path }, clone );
    QTemporaryFile file;
    if ( !shown.succeeded || !file.open() ) {
        return std::nullopt;
    }
    file.write( shown.output.toUtf8() );
    file.flush();

    const auto read = readGroupsOfFile( file.fileName() );
    const auto requestFor = [ action ]( groupexchange::GroupKind kind, const auto& group ) {
        return action == GroupAction::Delete
                   ? PublishRequest::forDeletion( kind, group.id(), group.name() )
                   : PublishRequest::forGroup( group, action );
    };
    if ( !read.filterGroups.isEmpty() ) {
        return requestFor( groupexchange::GroupKind::Filter, read.filterGroups.first() );
    }
    if ( !read.highlighterSets.isEmpty() ) {
        return requestFor( groupexchange::GroupKind::Highlighter, read.highlighterSets.first() );
    }
    if ( !read.namingGroups.isEmpty() ) {
        return requestFor( groupexchange::GroupKind::ValueNames, read.namingGroups.first() );
    }
    return std::nullopt;
}

// The files a range of commits changed, with what became of them.
QMap<QString, QChar> changedFiles( const Git& git, const QString& clone, const QString& from,
                                   const QString& to )
{
    QMap<QString, QChar> changed;
    const auto diff = git.run( { QStringLiteral( "diff" ), QStringLiteral( "--name-status" ),
                                 QStringLiteral( "--no-renames" ), from, to },
                               clone );
    const auto lines = diff.output.split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
    for ( const auto& line : lines ) {
        const auto parts = line.split( QLatin1Char( '\t' ) );
        if ( parts.size() == 2 ) {
            changed.insert( parts[ 1 ], parts[ 0 ].at( 0 ) );
        }
    }
    return changed;
}

// What became of the changes committed here and not pushed yet, when the
// server changed the same files meanwhile.
struct SetAside {
    // Changes of files the server did not touch: to commit again.
    QList<PublishRequest> recommit;
    // Changes of files it did touch: the user decides, nothing is overwritten.
    QList<PublishResult> conflicts;
};

// Before the clone takes over what the server holds, checks whether a change
// committed here (offline, or just before a push was rejected) touches a file
// the server changed too. If so, the local changes are taken back into
// requests -- the clone goes to the server's state -- so that neither side's
// version wins unasked.
SetAside setAsideOverlaps( const Git& git, const QString& clone, const QString& folder )
{
    const auto head
        = git.run( { QStringLiteral( "symbolic-ref" ), QStringLiteral( "--quiet" ),
                     QStringLiteral( "--short" ), QStringLiteral( "refs/remotes/origin/HEAD" ) },
                   clone );
    if ( !head.succeeded || !hasCommit( git, clone ) ) {
        return {};
    }
    const auto remoteBranch = head.output.trimmed();
    if ( countCommits( git, clone, remoteBranch + QStringLiteral( "..HEAD" ) ) <= 0
         || countCommits( git, clone, QStringLiteral( "HEAD.." ) + remoteBranch ) <= 0 ) {
        return {};
    }
    const auto base = git.run(
        { QStringLiteral( "merge-base" ), QStringLiteral( "HEAD" ), remoteBranch }, clone );
    if ( !base.succeeded ) {
        return {};
    }
    const auto baseRevision = base.output.trimmed();
    const auto mine = changedFiles( git, clone, baseRevision, QStringLiteral( "HEAD" ) );
    const auto theirs = changedFiles( git, clone, baseRevision, remoteBranch );
    const bool overlaps
        = std::any_of( mine.keyBegin(), mine.keyEnd(),
                       [ &theirs ]( const QString& path ) { return theirs.contains( path ); } );
    if ( !overlaps ) {
        return {};
    }

    struct Taken {
        PublishRequest request;
        bool overlapping;
    };
    QList<Taken> taken;
    for ( auto change = mine.cbegin(); change != mine.cend(); ++change ) {
        const auto removed = change.value() == QLatin1Char( 'D' );
        const auto action = removed                                ? GroupAction::Delete
                            : change.value() == QLatin1Char( 'A' ) ? GroupAction::Add
                                                                   : GroupAction::Change;
        const auto request = requestFromRevision(
            git, clone, removed ? baseRevision : QStringLiteral( "HEAD" ), change.key(), action );
        if ( request ) {
            taken.append( { *request, theirs.contains( change.key() ) } );
        }
        else {
            LOG_WARNING << "Team Folder cannot take back the change to " << change.key();
        }
    }

    const auto reset = git.run( { QStringLiteral( "reset" ), QStringLiteral( "--hard" ),
                                  QStringLiteral( "--quiet" ), remoteBranch },
                                clone );
    if ( !reset.succeeded ) {
        return {};
    }
    SetAside result;
    // The files just changed under the reset: the theirs of the conflicts are
    // read from them once.
    GroupIndex index( folder );
    if ( std::any_of( taken.cbegin(), taken.cend(),
                      []( const Taken& item ) { return item.overlapping; } ) ) {
        index.reload();
    }
    for ( const auto& item : taken ) {
        if ( item.overlapping ) {
            result.conflicts.append( conflictResult( index, item.request ) );
        }
        else {
            result.recommit.append( item.request );
        }
    }
    return result;
}

std::shared_ptr<SyncOutcome> runSync( const QString& clone, const QString& gitProgram,
                                      const TeamFolderPolicy& policy, const Git::StopFlag& stop,
                                      const QList<PublishRequest>& requests, bool writable,
                                      const QString& readOnlyReason )
{
    auto outcome = std::make_shared<SyncOutcome>();
    const Git git( gitProgram, stop );
    const auto url = policy.repositoryUrl.trimmed();

    const auto folder = groupFolder( clone, policy.subfolder );
    if ( !folder ) {
        outcome->failedStep = SyncStep::Subfolder;
        outcome->message = TeamFolder::tr( "The subfolder %1 does not lie inside the repository." )
                               .arg( policy.subfolder );
        failRequests( *outcome, requests );
        return outcome;
    }

    auto haveClone = QFileInfo( QDir( clone ).filePath( QStringLiteral( ".git" ) ) ).isDir();
    if ( haveClone ) {
        const auto origin = git.run( { QStringLiteral( "config" ), QStringLiteral( "--get" ),
                                       QStringLiteral( "remote.origin.url" ) },
                                     clone );
        if ( !origin.started ) {
            failAt( *outcome, SyncStep::Clone, origin );
            failRequests( *outcome, requests );
            return outcome;
        }
        // A clone of another repository: the user pointed the Team Folder
        // elsewhere. The folder is LogSquirl's own and holds nothing else.
        haveClone = origin.output.trimmed() == url;
    }

    // Whether the repository could be reached in this sync.
    bool reachable = true;
    if ( !haveClone ) {
        QDir( clone ).removeRecursively();
        QDir().mkpath( QFileInfo( clone ).absolutePath() );
        LOG_INFO << "Team Folder clones " << url;
        const auto cloned = git.run( { QStringLiteral( "clone" ), QStringLiteral( "--quiet" ),
                                       QStringLiteral( "--" ), url, clone } );
        if ( !cloned.succeeded ) {
            QDir( clone ).removeRecursively();
            failAt( *outcome, SyncStep::Clone, cloned );
            failRequests( *outcome, requests );
            return outcome;
        }
    }
    else {
        const auto fetched = git.run( { QStringLiteral( "fetch" ), QStringLiteral( "--quiet" ),
                                        QStringLiteral( "--prune" ), RemoteName },
                                      clone );
        if ( !fetched.succeeded ) {
            // Git runs but the repository cannot be reached: go on with the
            // groups of the last sync.
            reachable = false;
            outcome->result = SyncOutcome::Result::Offline;
            failAt( *outcome, SyncStep::Pull, fetched );
        }
    }

    // What is to be committed: what was asked for, and changes of an earlier
    // sync that the server's changes do not touch.
    auto todo = requests;
    QList<PublishResult> conflicts;
    if ( reachable ) {
        // Changes committed offline meet what others pushed meanwhile.
        auto aside = setAsideOverlaps( git, clone, *folder );
        todo.append( aside.recommit );
        conflicts = aside.conflicts;
        if ( const auto failed = integrate( git, clone ) ) {
            failAt( *outcome, SyncStep::Merge, *failed );
            failRequests( *outcome, todo );
            for ( const auto& conflict : std::as_const( conflicts ) ) {
                addResult( *outcome, conflict );
            }
            readGroups( *folder, *outcome );
            outcome->hasPending = hasUnpushedCommits( git, clone );
            return outcome;
        }
        outcome->result = SyncOutcome::Result::Synced;
    }

    // The groups are read once here; the requests look them up in the index.
    GroupIndex index( *folder );
    index.reload();
    bool groupsChanged = false;

    // Changes to publish are committed one by one, offline as well.
    bool committedSomething = false;
    if ( !todo.isEmpty() || !conflicts.isEmpty() ) {
        PublishOutcome published;
        published.results = conflicts;
        for ( const auto& request : std::as_const( todo ) ) {
            if ( !writable ) {
                PublishResult refused;
                refused.status = PublishStatus::Refused;
                refused.message = readOnlyReason;
                refused.request = request;
                published.results.append( refused );
                continue;
            }
            // After a sync, the group can be compared with what the user
            // loaded. Offline there is nothing newer to compare with.
            if ( reachable ) {
                if ( auto conflict = conflictOf( index, request ) ) {
                    published.results.append( *conflict );
                    continue;
                }
            }
            auto result = commitRequest( git, clone, *folder, index, request );
            groupsChanged = true;
            committedSomething = committedSomething || result.status == PublishStatus::Published;
            published.results.append( result );
        }
        outcome->published = published;
    }

    // Everything committed here goes to the server: this change, and earlier
    // ones that could not be pushed.
    if ( reachable && ( committedSomething || hasUnpushedCommits( git, clone ) ) ) {
        auto pushed = pushHead( git, clone );
        if ( pushed.kind == PushResult::Kind::Rejected ) {
            // Someone pushed in between: sync once more and try again.
            const auto fetched = git.run( { QStringLiteral( "fetch" ), QStringLiteral( "--quiet" ),
                                            QStringLiteral( "--prune" ), RemoteName },
                                          clone );
            if ( fetched.succeeded ) {
                // What was committed meanwhile may touch what the others just
                // pushed: that is asked about, not overwritten.
                const auto aside = setAsideOverlaps( git, clone, *folder );
                const auto integrated = integrate( git, clone );
                index.reload();
                groupsChanged = true;
                if ( !integrated ) {
                    for ( const auto& conflict : aside.conflicts ) {
                        addResult( *outcome, conflict );
                    }
                    for ( const auto& request : aside.recommit ) {
                        const auto again = commitRequest( git, clone, *folder, index, request );
                        addResult( *outcome, again );
                    }
                    pushed = pushHead( git, clone );
                }
            }
        }

        switch ( pushed.kind ) {
        case PushResult::Kind::Pushed:
            break;
        case PushResult::Kind::Rejected:
        case PushResult::Kind::Unreachable:
            outcome->result = SyncOutcome::Result::Offline;
            outcome->failedStep = SyncStep::Push;
            outcome->message = pushed.message;
            break;
        case PushResult::Kind::Refused:
            outcome->refused = true;
            outcome->refusedReason = pushed.message;
            discardUnpushed( git, clone );
            groupsChanged = true;
            break;
        }

        if ( auto& published = outcome->published; published ) {
            for ( auto& result : published->results ) {
                if ( result.status != PublishStatus::Published ) {
                    continue;
                }
                if ( outcome->refused ) {
                    result.status = PublishStatus::Refused;
                    result.message = pushed.message;
                }
                else if ( outcome->result == SyncOutcome::Result::Offline ) {
                    result.status = PublishStatus::Pending;
                    result.message = pushed.message;
                }
            }
        }
    }
    else if ( auto& published = outcome->published; !reachable && published ) {
        for ( auto& result : published->results ) {
            if ( result.status == PublishStatus::Published ) {
                result.status = PublishStatus::Pending;
                result.message = outcome->message;
            }
        }
    }

    outcome->hasPending = !outcome->refused && hasUnpushedCommits( git, clone );
    if ( groupsChanged ) {
        index.reload();
    }
    outcome->filterGroups = index.groups().filterGroups;
    outcome->highlighterGroups = index.groups().highlighterGroups;
    outcome->namingGroups = index.groups().namingGroups;
    outcome->skippedFiles = index.groups().skippedFiles;
    return outcome;
}

bool sameContent( const PredefinedFilterSet& a, const PredefinedFilterSet& b )
{
    const auto& x = a.filters();
    const auto& y = b.filters();
    return a.name() == b.name()
           && std::equal( x.cbegin(), x.cend(), y.cbegin(), y.cend(),
                          []( const PredefinedFilter& p, const PredefinedFilter& q ) {
                              return p.name == q.name && p.pattern == q.pattern
                                     && p.useRegex == q.useRegex;
                          } );
}

bool sameContent( const HighlighterSet& a, const HighlighterSet& b )
{
    return a.sameAs( b );
}

bool sameContent( const NamingGroup& a, const NamingGroup& b )
{
    return a.sameAs( b );
}

template <typename Group>
QHash<QString, QString> revisionsOf( const QList<TeamGroup<Group>>& groups )
{
    QHash<QString, QString> revisions;
    for ( const auto& group : groups ) {
        revisions.insert( group.group.id(), group.revision );
    }
    return revisions;
}

template <typename Group>
QStringList namesOf( const QList<TeamGroup<Group>>& groups )
{
    QStringList names;
    for ( const auto& group : groups ) {
        names.append( group.group.name() );
    }
    return names;
}

template <typename Group>
QList<Group> groupsOf( const QList<TeamGroup<Group>>& teamGroups )
{
    QList<Group> groups;
    groups.reserve( teamGroups.size() );
    for ( const auto& group : teamGroups ) {
        groups.append( group.group );
    }
    return groups;
}

template <typename Group>
TeamGroupChanges changesBetween( const QList<TeamGroup<Group>>& before,
                                 const QList<TeamGroup<Group>>& after )
{
    QHash<QString, const TeamGroup<Group>*> old;
    for ( const auto& group : before ) {
        old.insert( group.group.id(), &group );
    }

    TeamGroupChanges changes;
    for ( const auto& group : after ) {
        const auto id = group.group.id();
        const auto found = old.constFind( id );
        if ( found == old.constEnd() ) {
            changes.added.append( id );
            continue;
        }
        if ( !sameContent( ( *found )->group, group.group ) ) {
            changes.changed.append( id );
        }
        old.erase( found );
    }
    for ( const auto& group : before ) {
        if ( old.contains( group.group.id() ) ) {
            changes.removed.append( group.group.id() );
        }
    }
    return changes;
}

template <typename Group>
void sortByName( QList<TeamGroup<Group>>& groups )
{
    QCollator collator;
    collator.setCaseSensitivity( Qt::CaseInsensitive );
    collator.setNumericMode( true );
    std::stable_sort( groups.begin(), groups.end(), [ &collator ]( const auto& a, const auto& b ) {
        return collator.compare( a.group.name(), b.group.name() ) < 0;
    } );
}

} // namespace

} // namespace logsquirl::teamfolder

using namespace logsquirl::teamfolder;

TeamFolder::TeamFolder( QString cloneDirectory, QString gitProgram, QObject* parent )
    : QObject( parent )
    , cloneDirectory_( std::move( cloneDirectory ) )
    , gitProgram_( std::move( gitProgram ) )
{
    qRegisterMetaType<TeamGroupChanges>( "logsquirl::teamfolder::TeamGroupChanges" );
    qRegisterMetaType<PublishOutcome>( "logsquirl::teamfolder::PublishOutcome" );

    syncTimer_.setInterval( SyncInterval );
    connect( &syncTimer_, &QTimer::timeout, this, &TeamFolder::sync );
    connect( &running_, &QFutureWatcherBase::finished, this, &TeamFolder::takeOutcome );
}

TeamFolder::~TeamFolder()
{
    if ( stopRunning_ ) {
        stopRunning_->store( true );
    }
    running_.waitForFinished();
}

QString TeamFolder::defaultCloneDirectory()
{
    const auto dataDir = DataLocation::current().dataDirectory();
    if ( dataDir.isEmpty() ) {
        return {};
    }
    return dataDir + QStringLiteral( "/teamfolder" );
}

void TeamFolder::setUp( const TeamFolderPolicy& policy )
{
    if ( policy == policy_ && ( state_ != State::Off || !policy.isActive() ) ) {
        return;
    }
    policy_ = policy;
    ++setUpGeneration_;
    queuedRequests_.clear();
    writable_ = true;
    publishError_.clear();
    readOnlyReason_.clear();
    hasPending_ = false;
    // A sync for the earlier set-up is no longer wanted.
    if ( stopRunning_ ) {
        stopRunning_->store( true );
    }

    if ( !policy_.isActive() || cloneDirectory_.isEmpty() ) {
        syncTimer_.stop();
        syncAgain_ = false;
        skippedFiles_.clear();
        setGroups( {}, {}, {} );
        setState( State::Off );
        return;
    }

    readLastSynced();
    LOG_INFO << "Team Folder set up for " << policy_.repositoryUrl;
    setState( State::NotSynced );
    syncTimer_.start();
    sync();
}

void TeamFolder::sync()
{
    if ( state_ == State::Off ) {
        return;
    }
    if ( syncing_ ) {
        syncAgain_ = true;
        return;
    }
    startSync();
}

void TeamFolder::publish( QList<PublishRequest> requests )
{
    if ( requests.isEmpty() ) {
        return;
    }
    if ( state_ == State::Off ) {
        PublishOutcome outcome;
        for ( const auto& request : requests ) {
            PublishResult failed;
            failed.status = PublishStatus::Failed;
            failed.message = tr( "The Team Folder is off." );
            failed.request = request;
            outcome.results.append( failed );
        }
        Q_EMIT publishFinished( outcome );
        return;
    }
    queuedRequests_.append( std::move( requests ) );
    if ( syncing_ ) {
        syncAgain_ = true;
        return;
    }
    startSync();
}

void TeamFolder::resolveConflict( const PublishRequest& request, ConflictChoice answer )
{
    switch ( answer ) {
    case ConflictChoice::KeepMine: {
        auto again = request;
        again.overwrite = true;
        publish( { again } );
        break;
    }
    case ConflictChoice::TakeTheirs:
        // Their version is what the sync brought.
        break;
    case ConflictChoice::SaveAsCopy: {
        if ( request.group ) {
            std::visit(
                [ this ]( const auto& group ) {
                    using Group = std::decay_t<decltype( group )>;
                    publish( { PublishRequest::forGroup(
                        copyOfGroup( group, namesOf( teamGroupsOf<Group>() ) ),
                        GroupAction::Add ) } );
                },
                *request.group );
        }
        break;
    }
    }
}

QString TeamFolder::filterGroupRevision( const QString& id ) const
{
    return filterGroupRevisions().value( id );
}

QString TeamFolder::highlighterGroupRevision( const QString& id ) const
{
    return highlighterGroupRevisions().value( id );
}

QString TeamFolder::namingGroupRevision( const QString& id ) const
{
    return namingGroupRevisions().value( id );
}

QHash<QString, QString> TeamFolder::filterGroupRevisions() const
{
    return revisionsOf( filterGroups_ );
}

QHash<QString, QString> TeamFolder::highlighterGroupRevisions() const
{
    return revisionsOf( highlighterGroups_ );
}

QHash<QString, QString> TeamFolder::namingGroupRevisions() const
{
    return revisionsOf( namingGroups_ );
}

bool TeamFolder::isWritable() const
{
    return writable_;
}

QString TeamFolder::readOnlyReason() const
{
    return readOnlyReason_;
}

bool TeamFolder::hasPendingChanges() const
{
    return hasPending_;
}

void TeamFolder::startSync()
{
    syncAgain_ = false;
    syncing_ = true;
    runningGeneration_ = setUpGeneration_;
    stopRunning_ = std::make_shared<std::atomic_bool>( false );
    auto requests = std::exchange( queuedRequests_, {} );
    running_.setFuture( QtConcurrent::run(
        [ clone = cloneDirectory_, git = gitProgram_, policy = policy_, stop = stopRunning_,
          requests = std::move( requests ), writable = writable_, reason = readOnlyReason_ ] {
            return runSync( clone, git, policy, stop, requests, writable, reason );
        } ) );
    Q_EMIT stateChanged();
}

void TeamFolder::takeOutcome()
{
    syncing_ = false;
    const auto outcome = running_.result();
    const bool current = runningGeneration_ == setUpGeneration_;

    if ( current && outcome ) {
        skippedFiles_ = outcome->skippedFiles;
        hasPending_ = outcome->hasPending;
        if ( outcome->refused ) {
            writable_ = false;
            readOnlyReason_ = outcome->refusedReason;
        }
        switch ( outcome->result ) {
        case SyncOutcome::Result::Synced:
            lastSynced_ = QDateTime::currentDateTimeUtc();
            writeLastSynced();
            setGroups( outcome->filterGroups, outcome->highlighterGroups, outcome->namingGroups );
            setState( State::Synced );
            break;
        case SyncOutcome::Result::Offline:
            setGroups( outcome->filterGroups, outcome->highlighterGroups, outcome->namingGroups );
            setState( State::NotSynced, outcome->failedStep, outcome->message );
            break;
        case SyncOutcome::Result::Failed:
            setGroups( outcome->filterGroups, outcome->highlighterGroups, outcome->namingGroups );
            setState( State::Error, outcome->failedStep, outcome->message );
            break;
        }
    }

    if ( state_ != State::Off && ( syncAgain_ || !current ) ) {
        startSync();
    }
    else {
        Q_EMIT stateChanged();
    }
    if ( const auto& published = outcome ? outcome->published : std::optional<PublishOutcome>{};
         current && published ) {
        publishError_.clear();
        for ( const auto& result : published->results ) {
            if ( result.status == PublishStatus::Failed ) {
                publishError_ = result.message;
            }
        }
        if ( !publishError_.isEmpty() ) {
            LOG_WARNING << "Team Folder could not publish: " << publishError_;
            Q_EMIT stateChanged();
        }
        Q_EMIT publishFinished( *published );
    }
    if ( current ) {
        Q_EMIT syncFinished();
    }
}

void TeamFolder::setGroups( QList<TeamGroup<PredefinedFilterSet>> filterGroups,
                            QList<TeamGroup<HighlighterSet>> highlighterGroups,
                            QList<TeamGroup<NamingGroup>> namingGroups )
{
    sortByName( filterGroups );
    sortByName( highlighterGroups );
    sortByName( namingGroups );

    const auto filterChanges = changesBetween( filterGroups_, filterGroups );
    const auto highlighterChanges = changesBetween( highlighterGroups_, highlighterGroups );
    const auto namingGroupChanges = changesBetween( namingGroups_, namingGroups );
    filterGroups_ = std::move( filterGroups );
    highlighterGroups_ = std::move( highlighterGroups );
    namingGroups_ = std::move( namingGroups );
    if ( !filterChanges.isEmpty() ) {
        Q_EMIT groupsChanged( filterChanges );
    }
    if ( !highlighterChanges.isEmpty() ) {
        Q_EMIT highlighterGroupsChanged( highlighterChanges );
    }
    if ( !namingGroupChanges.isEmpty() ) {
        Q_EMIT namingGroupsChanged( namingGroupChanges );
    }
}

namespace {

// The step that failed as the log names it: in English, whatever language the
// status is shown in, like the rest of the log.
const char* logDescriptionOf( SyncStep step )
{
    switch ( step ) {
    case SyncStep::None:
        return "sync failed";
    case SyncStep::StartGit:
        return "Git could not be started";
    case SyncStep::Subfolder:
        return "the subfolder lies outside the repository";
    case SyncStep::Clone:
        return "clone failed";
    case SyncStep::Pull:
        return "pull failed";
    case SyncStep::Merge:
        return "merge failed";
    case SyncStep::Push:
        return "push failed";
    case SyncStep::PushRefused:
        return "push refused";
    }
    return "sync failed";
}

} // namespace

void TeamFolder::setState( State state, SyncStep failedStep, const QString& message )
{
    // The subfolder is LogSquirl's own refusal: its reason is a remark, and
    // there is no output of Git's.
    const bool fromGit = failedStep != SyncStep::Subfolder;
    if ( state == State::Error || failedStep != SyncStep::None ) {
        // Git's output as it is; for the subfolder, the subfolder itself
        // rather than the translated reason. Why Git could not be started was
        // logged where it failed to (teamfoldergit.cpp). Nothing after the
        // step when there is nothing to add.
        QString detail;
        if ( failedStep == SyncStep::Subfolder ) {
            detail = policy_.subfolder;
        }
        else if ( failedStep != SyncStep::StartGit ) {
            detail = message;
        }
        if ( detail.isEmpty() ) {
            LOG_WARNING << "Team Folder: " << logDescriptionOf( failedStep );
        }
        else {
            LOG_WARNING << "Team Folder: " << logDescriptionOf( failedStep ) << ": " << detail;
        }
    }
    state_ = state;
    failedStep_ = failedStep;
    gitOutput_ = fromGit ? message : QString{};
    failureReason_ = fromGit ? QString{} : message;
    Q_EMIT stateChanged();
}

TeamFolder::State TeamFolder::state() const
{
    return state_;
}

bool TeamFolder::showsRefusedPush() const
{
    return failedStep_ == SyncStep::None && !writable_;
}

SyncStep TeamFolder::failedStep() const
{
    return showsRefusedPush() ? SyncStep::PushRefused : failedStep_;
}

QString TeamFolder::gitOutput() const
{
    return showsRefusedPush() ? readOnlyReason_ : gitOutput_;
}

bool TeamFolder::isSyncing() const
{
    return syncing_;
}

QString TeamFolder::summary() const
{
    if ( syncing_ ) {
        return tr( "Team Folder syncing…" );
    }
    switch ( state_ ) {
    case State::Off:
        return tr( "Team Folder off" );
    case State::NotSynced:
        return tr( "Team Folder not synced" );
    case State::Synced:
        return tr( "Team Folder synced" );
    case State::Error:
        return tr( "Team Folder error" );
    }
    return {};
}

QString TeamFolder::heading() const
{
    if ( syncing_ ) {
        return tr( "Syncing…" );
    }
    if ( const auto step = failedStep(); step != SyncStep::None ) {
        return headingOf( step );
    }
    switch ( state_ ) {
    case State::Off:
        return tr( "Off" );
    case State::NotSynced:
        return tr( "Not synced" );
    case State::Synced:
        return tr( "Synced" );
    case State::Error:
        return tr( "Error" );
    }
    return {};
}

QString TeamFolder::headingOf( SyncStep step )
{
    switch ( step ) {
    case SyncStep::None:
        return {};
    case SyncStep::StartGit:
        return tr( "Git could not be started" );
    case SyncStep::Subfolder:
        return tr( "The subfolder lies outside the repository" );
    case SyncStep::Clone:
        return tr( "Clone failed" );
    case SyncStep::Pull:
        return tr( "Pull failed" );
    case SyncStep::Merge:
        return tr( "Merge failed" );
    case SyncStep::Push:
        return tr( "Push failed" );
    case SyncStep::PushRefused:
        return tr( "Push refused" );
    }
    return {};
}

QStringList TeamFolder::remarks() const
{
    QStringList lines;
    if ( !failureReason_.isEmpty() ) {
        lines.append( failureReason_ );
    }
    if ( !publishError_.isEmpty() ) {
        lines.append( tr( "Not published: %1" ).arg( publishError_ ) );
    }
    if ( !writable_ ) {
        // The server's reason is Git's output of the refused push, unless
        // another step failed since.
        lines.append( failedStep() == SyncStep::PushRefused
                          ? tr( "The Team groups are read-only." )
                          : tr( "The Team groups are read-only: %1" ).arg( readOnlyReason_ ) );
    }
    for ( const auto& skipped : skippedFiles_ ) {
        lines.append( tr( "Skipped %1: %2" ).arg( skipped.file, skipped.reason ) );
    }
    return lines;
}

QString TeamFolder::details() const
{
    QStringList lines;
    if ( const auto output = gitOutput(); !output.isEmpty() ) {
        lines.append( output );
    }
    lines.append( remarks() );
    return lines.join( QLatin1Char( '\n' ) );
}

QList<SkippedFile> TeamFolder::skippedFiles() const
{
    return skippedFiles_;
}

QList<PredefinedFilterSet> TeamFolder::filterGroups() const
{
    return groupsOf( filterGroups_ );
}

QList<HighlighterSet> TeamFolder::highlighterGroups() const
{
    return groupsOf( highlighterGroups_ );
}

QList<NamingGroup> TeamFolder::namingGroups() const
{
    return groupsOf( namingGroups_ );
}

namespace {

const QString RecordUrlKey = QStringLiteral( "repositoryUrl" );
const QString RecordSubfolderKey = QStringLiteral( "subfolder" );
const QString RecordLastSyncedKey = QStringLiteral( "lastSynced" );

} // namespace

QDateTime TeamFolder::lastSynced() const
{
    return lastSynced_;
}

QString TeamFolder::cloneDirectory() const
{
    return cloneDirectory_;
}

bool TeamFolder::hasClone() const
{
    return !cloneDirectory_.isEmpty()
           && QFileInfo( QDir( cloneDirectory_ ).filePath( QStringLiteral( ".git" ) ) ).isDir();
}

QString TeamFolder::syncRecordFile() const
{
    return cloneDirectory_ + QStringLiteral( "-sync.ini" );
}

void TeamFolder::readLastSynced()
{
    QSettings record( syncRecordFile(), QSettings::IniFormat );
    const auto url = policy_.repositoryUrl.trimmed();
    if ( record.value( RecordUrlKey ).toString() == url
         && record.value( RecordSubfolderKey ).toString() == policy_.subfolder ) {
        lastSynced_ = QDateTime::fromString( record.value( RecordLastSyncedKey ).toString(),
                                             Qt::ISODateWithMs );
        return;
    }
    // Another repository or subfolder: never synced, also after a restart
    // that goes back to the earlier one.
    lastSynced_ = {};
    writeLastSynced();
}

void TeamFolder::writeLastSynced() const
{
    QDir().mkpath( QFileInfo( syncRecordFile() ).absolutePath() );
    QSettings record( syncRecordFile(), QSettings::IniFormat );
    record.setValue( RecordUrlKey, policy_.repositoryUrl.trimmed() );
    record.setValue( RecordSubfolderKey, policy_.subfolder );
    if ( lastSynced_.isValid() ) {
        record.setValue( RecordLastSyncedKey, lastSynced_.toUTC().toString( Qt::ISODateWithMs ) );
    }
    else {
        record.remove( RecordLastSyncedKey );
    }
}

FailureHint TeamFolder::failureHint() const
{
    return failureHintOf( failedStep(), gitOutput() );
}

QString TeamFolder::hintOf( FailureHint hint )
{
    switch ( hint ) {
    case FailureHint::None:
        return {};
    case FailureHint::SignIn:
        return tr( "Sign-in failed. Make sure Git can sign in to this server outside LogSquirl: "
                   "with an SSH key added to your account for an SSH URL, or with stored "
                   "credentials or a token for an HTTPS URL." );
    case FailureHint::SsoAuthorization:
        return tr( "The organization requires SSO authorization. Authorize your SSH key or "
                   "token for the organization in your account settings on the server, or use "
                   "an HTTPS URL." );
    case FailureHint::RepositoryNotFound:
        return tr( "Repository not found. Check the Repository URL, and that your account may "
                   "read the repository." );
    case FailureHint::ServerUnreachable:
        return tr( "Server unreachable. Check the host name in the Repository URL, your network "
                   "connection and your proxy settings." );
    case FailureHint::GitMissing:
        return tr( "Git is not installed. Install Git, make sure the git program is on the "
                   "PATH, and press Sync Now." );
    }
    return {};
}

namespace logsquirl::teamfolder {

namespace {

// Git's fixed sentences (LC_ALL=C) of each common failure, each a whole line.
// Where a line holds a URL or a host, it sits where Git puts it, so the words
// of a sentence inside a URL or a path never make a line match.
struct HintSentences {
    FailureHint hint;
    QList<QRegularExpression> lines;
};

const QList<HintSentences>& hintSentences()
{
    static const QList<HintSentences> sentences = [] {
        const auto line = []( const char* pattern ) {
            return QRegularExpression( QStringLiteral( "^%1$" ).arg( QLatin1String( pattern ) ) );
        };
        // In the order they are looked for: SSO before the sign-in, which a
        // server may report alongside it.
        return QList<HintSentences>{
            { FailureHint::SsoAuthorization,
              { line( "(?:ERROR: |remote: )?The \\S+ organization has enabled or enforced SAML "
                      "SSO\\..*" ) } },
            { FailureHint::SignIn,
              { line( "\\S+: Permission denied \\([a-z-]+(?:,[a-z-]+)*\\)\\." ),
                line( "fatal: Authentication failed for '[^']+'" ),
                line( "fatal: could not read (?:Username|Password) for '[^']+': .+" ) } },
            { FailureHint::RepositoryNotFound,
              { line( "(?:ERROR|remote): Repository not found\\." ),
                line( "fatal: repository '[^']+' not found" ),
                line( "fatal: '[^']+' does not appear to be a git repository" ) } },
            { FailureHint::ServerUnreachable,
              { line( "ssh: Could not resolve hostname \\S+: .+" ),
                line( "ssh: connect to host \\S+ port \\d+: .+" ),
                line( "fatal: unable to access '[^']+': (?:Could not resolve (?:host|proxy): "
                      "|Failed to connect to |Connection timed out|Operation timed out).*" ) } },
        };
    }();
    return sentences;
}

} // namespace

FailureHint failureHintOf( SyncStep step, const QString& gitOutput )
{
    switch ( step ) {
    case SyncStep::None:
    case SyncStep::Subfolder:
    // The 403 of a refused push has its own rule (ADR-0008).
    case SyncStep::PushRefused:
        return FailureHint::None;
    case SyncStep::StartGit:
        return FailureHint::GitMissing;
    case SyncStep::Clone:
    case SyncStep::Pull:
    case SyncStep::Merge:
    case SyncStep::Push:
        break;
    }

    auto lines = gitOutput.split( QLatin1Char( '\n' ) );
    for ( auto& line : lines ) {
        if ( line.endsWith( QLatin1Char( '\r' ) ) ) {
            line.chop( 1 );
        }
    }
    for ( const auto& sentences : hintSentences() ) {
        for ( const auto& sentence : sentences.lines ) {
            for ( const auto& line : std::as_const( lines ) ) {
                if ( sentence.match( line ).hasMatch() ) {
                    return sentences.hint;
                }
            }
        }
    }
    return FailureHint::None;
}

namespace {

std::optional<QString> revisionOf( const QHash<QString, QString>& revisions, const QString& id )
{
    const auto found = revisions.constFind( id );
    return found == revisions.constEnd() ? std::nullopt : std::optional<QString>( *found );
}

template <typename Group>
QList<PublishRequest> requestsBetween( const QList<Group>& before, const QList<Group>& after,
                                       const QHash<QString, QString>& revisions )
{
    QList<PublishRequest> requests;
    for ( const auto& group : after ) {
        const auto was
            = std::find_if( before.cbegin(), before.cend(),
                            [ &group ]( const auto& other ) { return other.id() == group.id(); } );
        if ( was == before.cend() ) {
            requests.append( PublishRequest::forGroup( group, GroupAction::Add ) );
        }
        else if ( was->name() != group.name() ) {
            requests.append( PublishRequest::forGroup( group, GroupAction::Rename, was->name() ) );
            requests.back().baseRevision = revisionOf( revisions, group.id() );
        }
        else if ( !sameContent( *was, group ) ) {
            requests.append( PublishRequest::forGroup( group, GroupAction::Change ) );
            requests.back().baseRevision = revisionOf( revisions, group.id() );
        }
    }
    for ( const auto& group : before ) {
        const auto stays
            = std::any_of( after.cbegin(), after.cend(),
                           [ &group ]( const auto& other ) { return other.id() == group.id(); } );
        if ( !stays ) {
            requests.append( PublishRequest::forDeletion( groupexchange::GroupTraits<Group>::kind,
                                                          group.id(), group.name() ) );
        }
    }
    return requests;
}

} // namespace

template <typename Group>
QList<PublishRequest> requestsForChanges( const QList<Group>& before, const QList<Group>& after,
                                          const QHash<QString, QString>& revisions )
{
    return requestsBetween( before, after, revisions );
}

PublishRequest PublishRequest::forDeletion( groupexchange::GroupKind kind, const QString& id,
                                            const QString& name )
{
    PublishRequest request;
    request.kind = kind;
    request.action = GroupAction::Delete;
    request.id = id;
    request.name = name;
    return request;
}

template <typename Group>
Group copyOfGroup( const Group& group, const QStringList& takenNames )
{
    const auto name = groupexchange::firstFreeName( group.name(), takenNames );
    // A fresh id, of the form the kind of group gives a new group.
    auto copy = [ & ] {
        if constexpr ( std::is_same_v<Group, NamingGroup> ) {
            return group.withId( NamingGroup::createNewGroup( name ).id() );
        }
        else {
            return group.withId( Group::createNewSet( name ).id() );
        }
    }();
    copy.setName( name );
    return copy;
}

bool PublishRequest::writeTo( const QString& file ) const
{
    return group
           && std::visit(
               [ &file ]( const auto& kept ) { return groupexchange::writeGroup( file, kept ); },
               *group );
}

// Every kind of group, for the templates above.
template QList<PublishRequest> requestsForChanges( const QList<PredefinedFilterSet>&,
                                                   const QList<PredefinedFilterSet>&,
                                                   const QHash<QString, QString>& );
template QList<PublishRequest> requestsForChanges( const QList<HighlighterSet>&,
                                                   const QList<HighlighterSet>&,
                                                   const QHash<QString, QString>& );
template QList<PublishRequest> requestsForChanges( const QList<NamingGroup>&,
                                                   const QList<NamingGroup>&,
                                                   const QHash<QString, QString>& );
template PredefinedFilterSet copyOfGroup( const PredefinedFilterSet&, const QStringList& );
template HighlighterSet copyOfGroup( const HighlighterSet&, const QStringList& );
template NamingGroup copyOfGroup( const NamingGroup&, const QStringList& );

} // namespace logsquirl::teamfolder
