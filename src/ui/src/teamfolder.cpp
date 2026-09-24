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
#include <QDir>
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
                    outcome.highlighterGroups.append( { group, file } );
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
            outcome.filterGroups.append( { group, file } );
        }
    }
}

// Brings the clone's working tree to what the repository's default branch
// holds. Nothing to do for an empty repository. Git's message when it fails.
std::optional<QString> fastForward( const Git& git, const QString& clone )
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
    const auto current = git.run( { QStringLiteral( "rev-parse" ), QStringLiteral( "--verify" ),
                                    QStringLiteral( "--quiet" ), QStringLiteral( "HEAD" ) },
                                  clone );

    const auto update = current.succeeded
                            ? git.run( { QStringLiteral( "merge" ), QStringLiteral( "--ff-only" ),
                                         QStringLiteral( "--quiet" ), remoteBranch },
                                       clone )
                            // Nothing checked out yet: the repository was empty when cloned.
                            : git.run( { QStringLiteral( "checkout" ), QStringLiteral( "--quiet" ),
                                         QStringLiteral( "-B" ), localBranch, remoteBranch },
                                       clone );
    if ( !update.succeeded ) {
        return update.message();
    }
    return std::nullopt;
}

std::shared_ptr<SyncOutcome> runSync( const QString& clone, const QString& gitProgram,
                                      const TeamFolderPolicy& policy, const Git::StopFlag& stop )
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
            outcome->result = SyncOutcome::Result::Offline;
            outcome->message = fetched.message();
            readGroups( *folder, *outcome );
            return outcome;
        }
    }

    if ( const auto failed = fastForward( git, clone ) ) {
        outcome->message = *failed;
        readGroups( *folder, *outcome );
        return outcome;
    }

    outcome->result = SyncOutcome::Result::Synced;
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

void TeamFolder::startSync()
{
    syncAgain_ = false;
    syncing_ = true;
    runningGeneration_ = setUpGeneration_;
    stopRunning_ = std::make_shared<std::atomic_bool>( false );
    running_.setFuture( QtConcurrent::run(
        [ clone = cloneDirectory_, git = gitProgram_, policy = policy_, stop = stopRunning_ ] {
            return runSync( clone, git, policy, stop );
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
