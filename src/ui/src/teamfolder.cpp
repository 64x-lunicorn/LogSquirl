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
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <optional>
#include <utility>

#include "groupexchange.h"
#include "log.h"
#include "teamfoldergit.h"

namespace logsquirl::teamfolder {

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
    QString message;
    QList<TeamGroup<PredefinedFilterSet>> filterGroups;
    QList<TeamGroup<HighlighterSet>> highlighterGroups;
    QList<SkippedFile> skippedFiles;

    // Set when a publish was asked for.
    std::optional<PublishOutcome> published;
    // The server refused a push: nothing more can be published.
    bool refused = false;
    QString refusedReason;
    // Committed here and not on the server yet.
    bool hasPending = false;
};

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

void readGroups( const QString& folder, SyncOutcome& outcome )
{
    using namespace logsquirl::groupexchange;

    const QDir dir( folder );
    if ( !dir.exists() ) {
        return;
    }

    QSet<QString> ids;
    const auto files = dir.entryInfoList( { QStringLiteral( "*.conf" ) }, QDir::Files, QDir::Name );
    for ( const auto& info : files ) {
        const auto file = info.fileName();
        const auto filters = readFilterGroups( info.absoluteFilePath() );
        if ( filters.error != ReadError::None ) {
            const auto highlighters = readHighlighterGroups( info.absoluteFilePath() );
            if ( highlighters.error == ReadError::None ) {
                for ( const auto& group : highlighters.groups ) {
                    if ( ids.contains( group.id() ) ) {
                        skipDuplicate( outcome, file, group.name() );
                        continue;
                    }
                    ids.insert( group.id() );
                    outcome.highlighterGroups.append(
                        { group, file, revisionOfFile( info.absoluteFilePath() ) } );
                }
            }
            else {
                const auto reason
                    = filters.error == ReadError::Unreadable
                          ? TeamFolder::tr( "The file cannot be read." )
                          : TeamFolder::tr( "The file holds no Filter Group or Highlighter Set." );
                outcome.skippedFiles.append( { file, reason } );
                LOG_WARNING << "Team Folder skips " << file << ": " << reason;
            }
            continue;
        }

        for ( auto group : filters.groups ) {
            if ( group.id() == defaultFilterSetId() ) {
                group = group.withId( idForDefaultGroupIn( file ) );
            }
            if ( ids.contains( group.id() ) ) {
                skipDuplicate( outcome, file, group.name() );
                continue;
            }
            ids.insert( group.id() );
            outcome.filterGroups.append(
                { group, file, revisionOfFile( info.absoluteFilePath() ) } );
        }
    }
}

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
// to do for an empty repository. Git's message when it fails.
std::optional<QString> integrate( const Git& git, const QString& clone )
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
        return update.message();
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
    const auto pushed
        = git.run( { QStringLiteral( "push" ), QStringLiteral( "--quiet" ),
                     QStringLiteral( "--set-upstream" ), RemoteName, QStringLiteral( "HEAD" ) },
                   clone );
    if ( pushed.succeeded ) {
        return {};
    }

    const auto message = pushed.message();
    const auto text = message.toLower();
    PushResult result{ PushResult::Kind::Unreachable, message };
    if ( text.contains( QStringLiteral( "[rejected]" ) )
         && ( text.contains( QStringLiteral( "non-fast-forward" ) )
              || text.contains( QStringLiteral( "fetch first" ) ) ) ) {
        result.kind = PushResult::Kind::Rejected;
    }
    else if ( text.contains( QStringLiteral( "[remote rejected]" ) )
              || text.contains( QStringLiteral( "denied" ) )
              || text.contains( QStringLiteral( "not allowed" ) )
              || text.contains( QStringLiteral( "403" ) )
              || text.contains( QStringLiteral( "protected branch" ) )
              || text.contains( QStringLiteral( "declined" ) ) ) {
        result.kind = PushResult::Kind::Refused;
    }
    return result;
}

QString kindWord( groupexchange::GroupKind kind )
{
    return kind == groupexchange::GroupKind::Filter ? QStringLiteral( "filter group" )
                                                    : QStringLiteral( "highlighter set" );
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
    case GroupAction::Change:
        break;
    }
    return QStringLiteral( "Change %1 \"%2\"" ).arg( kind, request.name );
}

// A Team group as the folder holds it now.
struct FoundGroup {
    QString file;
    QString revision;
    std::optional<PredefinedFilterSet> filterGroup;
    std::optional<HighlighterSet> highlighterSet;
};

std::optional<FoundGroup> findGroup( const QString& folder, const PublishRequest& request )
{
    SyncOutcome known;
    readGroups( folder, known );
    if ( request.kind == groupexchange::GroupKind::Filter ) {
        for ( const auto& group : known.filterGroups ) {
            if ( group.group.id() == request.id ) {
                return FoundGroup{ group.file, group.revision, group.group, std::nullopt };
            }
        }
    }
    else {
        for ( const auto& group : known.highlighterGroups ) {
            if ( group.group.id() == request.id ) {
                return FoundGroup{ group.file, group.revision, std::nullopt, group.group };
            }
        }
    }
    return std::nullopt;
}

// The file a group lives in: the one it was read from, or for a group the
// folder does not know a free one named after it.
QString fileFor( const QString& folder, const PublishRequest& request )
{
    if ( const auto found = findGroup( folder, request ) ) {
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

// Whether someone else changed the group since the user loaded it: the file
// has another revision than the one remembered, or is gone.
std::optional<PublishResult> conflictOf( const QString& folder, const PublishRequest& request )
{
    if ( !request.baseRevision || request.baseRevision->isEmpty() || request.overwrite ) {
        return std::nullopt;
    }
    const auto found = findGroup( folder, request );
    if ( found && found->revision == *request.baseRevision ) {
        return std::nullopt;
    }

    PublishResult result;
    result.status = PublishStatus::Conflict;
    result.request = request;
    if ( found ) {
        result.file = found->file;
        result.theirsFilterGroup = found->filterGroup;
        result.theirsHighlighterSet = found->highlighterSet;
    }
    return result;
}

// Writes the group into the clone and commits that one file. Whether it was
// done, and Git's message when not.
PublishResult commitRequest( const Git& git, const QString& clone, const QString& folder,
                             const PublishRequest& request )
{
    PublishResult result;
    result.request = request;
    result.file = fileFor( folder, request );
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
        outcome->message = TeamFolder::tr( "The subfolder %1 does not lie inside the repository." )
                               .arg( policy.subfolder );
        return outcome;
    }

    auto haveClone = QFileInfo( QDir( clone ).filePath( QStringLiteral( ".git" ) ) ).isDir();
    if ( haveClone ) {
        const auto origin = git.run( { QStringLiteral( "config" ), QStringLiteral( "--get" ),
                                       QStringLiteral( "remote.origin.url" ) },
                                     clone );
        if ( !origin.started ) {
            outcome->message = origin.message();
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
            outcome->message = cloned.message();
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
            outcome->message = fetched.message();
        }
    }

    if ( reachable ) {
        if ( const auto failed = integrate( git, clone ) ) {
            outcome->message = *failed;
            readGroups( *folder, *outcome );
            outcome->hasPending = hasUnpushedCommits( git, clone );
            return outcome;
        }
        outcome->result = SyncOutcome::Result::Synced;
    }

    // Changes to publish are committed one by one, offline as well.
    bool committedSomething = false;
    if ( !requests.isEmpty() ) {
        PublishOutcome published;
        for ( const auto& request : requests ) {
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
                if ( auto conflict = conflictOf( *folder, request ) ) {
                    published.results.append( *conflict );
                    continue;
                }
            }
            auto result = commitRequest( git, clone, *folder, request );
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
            if ( fetched.succeeded && !integrate( git, clone ) ) {
                pushed = pushHead( git, clone );
            }
        }

        switch ( pushed.kind ) {
        case PushResult::Kind::Pushed:
            break;
        case PushResult::Kind::Rejected:
        case PushResult::Kind::Unreachable:
            outcome->result = SyncOutcome::Result::Offline;
            outcome->message = pushed.message;
            break;
        case PushResult::Kind::Refused:
            outcome->refused = true;
            outcome->refusedReason = pushed.message;
            discardUnpushed( git, clone );
            break;
        }

        if ( outcome->published ) {
            for ( auto& result : outcome->published->results ) {
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
    else if ( !reachable && outcome->published ) {
        for ( auto& result : outcome->published->results ) {
            if ( result.status == PublishStatus::Published ) {
                result.status = PublishStatus::Pending;
                result.message = outcome->message;
            }
        }
    }

    outcome->hasPending = !outcome->refused && hasUnpushedCommits( git, clone );
    readGroups( *folder, *outcome );
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
    const auto dataDir = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
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
        setGroups( {}, {} );
        setState( State::Off, {} );
        return;
    }

    LOG_INFO << "Team Folder set up for " << policy_.repositoryUrl;
    setState( State::NotSynced, {} );
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
        QStringList taken;
        if ( request.kind == logsquirl::groupexchange::GroupKind::Filter ) {
            for ( const auto& group : filterGroups_ ) {
                taken.append( group.group.name() );
            }
        }
        else {
            for ( const auto& group : highlighterGroups_ ) {
                taken.append( group.group.name() );
            }
        }
        const auto name = logsquirl::groupexchange::firstFreeName( request.name, taken );
        if ( request.filterGroup ) {
            auto copy
                = request.filterGroup->withId( PredefinedFilterSet::createNewSet( name ).id() );
            copy.setName( name );
            publish( { PublishRequest::forGroup( copy, GroupAction::Add ) } );
        }
        else if ( request.highlighterSet ) {
            auto copy = request.highlighterSet->withId( HighlighterSet::createNewSet( name ).id() );
            copy.setName( name );
            publish( { PublishRequest::forGroup( copy, GroupAction::Add ) } );
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

QHash<QString, QString> TeamFolder::filterGroupRevisions() const
{
    QHash<QString, QString> revisions;
    for ( const auto& group : filterGroups_ ) {
        revisions.insert( group.group.id(), group.revision );
    }
    return revisions;
}

QHash<QString, QString> TeamFolder::highlighterGroupRevisions() const
{
    QHash<QString, QString> revisions;
    for ( const auto& group : highlighterGroups_ ) {
        revisions.insert( group.group.id(), group.revision );
    }
    return revisions;
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
            setGroups( outcome->filterGroups, outcome->highlighterGroups );
            setState( State::Synced, {} );
            break;
        case SyncOutcome::Result::Offline:
            setGroups( outcome->filterGroups, outcome->highlighterGroups );
            setState( State::NotSynced, outcome->message );
            break;
        case SyncOutcome::Result::Failed:
            setGroups( outcome->filterGroups, outcome->highlighterGroups );
            setState( State::Error, outcome->message );
            break;
        }
    }

    if ( state_ != State::Off && ( syncAgain_ || !current ) ) {
        startSync();
    }
    else {
        Q_EMIT stateChanged();
    }
    if ( current && outcome && outcome->published ) {
        publishError_.clear();
        for ( const auto& result : outcome->published->results ) {
            if ( result.status == PublishStatus::Failed ) {
                publishError_ = result.message;
            }
        }
        if ( !publishError_.isEmpty() ) {
            LOG_WARNING << "Team Folder could not publish: " << publishError_;
            Q_EMIT stateChanged();
        }
        Q_EMIT publishFinished( *outcome->published );
    }
    if ( current ) {
        Q_EMIT syncFinished();
    }
}

void TeamFolder::setGroups( QList<TeamGroup<PredefinedFilterSet>> filterGroups,
                            QList<TeamGroup<HighlighterSet>> highlighterGroups )
{
    sortByName( filterGroups );
    sortByName( highlighterGroups );

    const auto filterChanges = changesBetween( filterGroups_, filterGroups );
    const auto highlighterChanges = changesBetween( highlighterGroups_, highlighterGroups );
    filterGroups_ = std::move( filterGroups );
    highlighterGroups_ = std::move( highlighterGroups );
    if ( !filterChanges.isEmpty() ) {
        Q_EMIT groupsChanged( filterChanges );
    }
    if ( !highlighterChanges.isEmpty() ) {
        Q_EMIT highlighterGroupsChanged( highlighterChanges );
    }
}

void TeamFolder::setState( State state, const QString& message )
{
    if ( state == State::Error || !message.isEmpty() ) {
        LOG_WARNING << "Team Folder: " << message;
    }
    state_ = state;
    message_ = message;
    Q_EMIT stateChanged();
}

TeamFolder::State TeamFolder::state() const
{
    return state_;
}

QString TeamFolder::message() const
{
    return message_;
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

QString TeamFolder::details() const
{
    QStringList lines;
    if ( !message_.isEmpty() ) {
        lines.append( message_ );
    }
    if ( !publishError_.isEmpty() ) {
        lines.append( tr( "Not published: %1" ).arg( publishError_ ) );
    }
    if ( !writable_ ) {
        lines.append( tr( "The Team groups are read-only: %1" ).arg( readOnlyReason_ ) );
    }
    for ( const auto& skipped : skippedFiles_ ) {
        lines.append( tr( "Skipped %1: %2" ).arg( skipped.file, skipped.reason ) );
    }
    return lines.join( QLatin1Char( '\n' ) );
}

QList<SkippedFile> TeamFolder::skippedFiles() const
{
    return skippedFiles_;
}

QList<PredefinedFilterSet> TeamFolder::filterGroups() const
{
    QList<PredefinedFilterSet> groups;
    groups.reserve( filterGroups_.size() );
    for ( const auto& group : filterGroups_ ) {
        groups.append( group.group );
    }
    return groups;
}

QList<HighlighterSet> TeamFolder::highlighterGroups() const
{
    QList<HighlighterSet> groups;
    groups.reserve( highlighterGroups_.size() );
    for ( const auto& group : highlighterGroups_ ) {
        groups.append( group.group );
    }
    return groups;
}

namespace logsquirl::teamfolder {

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
    return requests;
}

} // namespace

QList<PublishRequest> requestsForChanges( const QList<PredefinedFilterSet>& before,
                                          const QList<PredefinedFilterSet>& after,
                                          const QHash<QString, QString>& revisions )
{
    return requestsBetween( before, after, revisions );
}

QList<PublishRequest> requestsForChanges( const QList<HighlighterSet>& before,
                                          const QList<HighlighterSet>& after,
                                          const QHash<QString, QString>& revisions )
{
    return requestsBetween( before, after, revisions );
}

PublishRequest PublishRequest::forGroup( const PredefinedFilterSet& group, GroupAction action,
                                         const QString& previousName )
{
    PublishRequest request;
    request.kind = groupexchange::GroupKind::Filter;
    request.action = action;
    request.id = group.id();
    request.name = group.name();
    request.previousName = previousName;
    request.filterGroup = group;
    return request;
}

PublishRequest PublishRequest::forGroup( const HighlighterSet& group, GroupAction action,
                                         const QString& previousName )
{
    PublishRequest request;
    request.kind = groupexchange::GroupKind::Highlighter;
    request.action = action;
    request.id = group.id();
    request.name = group.name();
    request.previousName = previousName;
    request.highlighterSet = group;
    return request;
}

bool PublishRequest::writeTo( const QString& file ) const
{
    if ( filterGroup ) {
        return groupexchange::writeGroup( file, *filterGroup );
    }
    return highlighterSet && groupexchange::writeGroup( file, *highlighterSet );
}

} // namespace logsquirl::teamfolder
