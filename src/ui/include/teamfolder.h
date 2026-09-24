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

#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>

#include "groupexchange.h"
#include "highlighterset.h"
#include "predefinedfilters.h"
#include "settingspolicies.h"

namespace logsquirl::teamfolder {

// A Team group as the Team Folder holds it: the group, and the file of the
// Team Folder it was read from, relative to the Team Folder's subfolder.
// A group is known by the id inside its file, not by the file's name.
template <typename Group>
struct TeamGroup {
    Group group;
    QString file;
    // The revision of the file: what it held when it was read. Two reads of
    // the same content have the same revision.
    QString revision;
};

// A file of the Team Folder that was skipped, and why.
struct SkippedFile {
    QString file;
    QString reason;
};

// What a sync did to the Team groups, by group id.
struct TeamGroupChanges {
    QStringList added;
    QStringList changed;
    QStringList removed;

    bool isEmpty() const
    {
        return added.isEmpty() && changed.isEmpty() && removed.isEmpty();
    }
};

enum class GroupAction { Add, Change, Rename, Delete };

// A change to one Team group, to be published: written into its file, committed
// on its own and pushed. The Team Folder finds the group's file by its id;
// a group it does not know yet gets a file of its own.
struct PublishRequest {
    groupexchange::GroupKind kind = groupexchange::GroupKind::Filter;
    GroupAction action = GroupAction::Change;
    QString id;
    // The group's name to publish, and for a Rename the name it had.
    QString name;
    QString previousName;
    // The group to publish: a Filter Group or a Highlighter Set, as kind says.
    std::optional<PredefinedFilterSet> filterGroup;
    std::optional<HighlighterSet> highlighterSet;
    // The revision of the group's file when the user started editing it. When
    // the file has another one after the sync, someone else changed the group
    // meanwhile, and the user is asked. Empty for a group that is new, and for
    // a publish that wants no such question.
    std::optional<QString> baseRevision;
    // Publish although someone else changed the group: the user's answer
    // "keep mine".
    bool overwrite = false;

    // Writes the group into the file it is given, in the Group Exchange's
    // one-group format.
    bool writeTo( const QString& file ) const;

    // Deletes the group's file: the group is gone for the whole team.
    static PublishRequest forDeletion( groupexchange::GroupKind kind, const QString& id,
                                       const QString& name );

    static PublishRequest forGroup( const PredefinedFilterSet& group, GroupAction action,
                                    const QString& previousName = {} );
    static PublishRequest forGroup( const HighlighterSet& group, GroupAction action,
                                    const QString& previousName = {} );
};

enum class PublishStatus {
    // Pushed: everyone has it at their next sync.
    Published,
    // Committed here, but the server cannot be reached: pushed at a later sync.
    Pending,
    // The server refused the push (missing rights, a protected branch).
    Refused,
    // Someone else changed the group since the user loaded it: nothing was
    // committed, and the result carries their version.
    Conflict,
    // Git could not do it.
    Failed
};

// What the user answers when publishing meets a change of someone else.
enum class ConflictChoice { KeepMine, TakeTheirs, SaveAsCopy };

struct PublishResult {
    PublishStatus status = PublishStatus::Failed;
    // Git's own message when it did not work out, empty otherwise.
    QString message;
    // The file of the group, relative to the Team Folder's subfolder.
    QString file;
    // The request this is the result of.
    PublishRequest request;
    // For a Conflict: their version of the group, when the group is still
    // there and nobody deleted it.
    std::optional<PredefinedFilterSet> theirsFilterGroup;
    std::optional<HighlighterSet> theirsHighlighterSet;
};

// The results of one publish, one for each request, in the order of the
// requests.
struct PublishOutcome {
    QList<PublishResult> results;
};

// A copy of a group under a fresh id and the first free name -- its own when
// no group in takenNames has it, else "<name> (n)". Sharing a personal group
// with the team, and copying a Team group into the personal ones, are copies.
PredefinedFilterSet copyOfGroup( const PredefinedFilterSet& group, const QStringList& takenNames );
HighlighterSet copyOfGroup( const HighlighterSet& group, const QStringList& takenNames );

// What a dialog's edited copy of the Team groups asks to publish, against the
// groups it was given: a group it added, one it renamed, one it changed, and
// one that is no longer in the copy, to be deleted.
// revisions holds the revision of each group's file when the dialog loaded it,
// by group id; a changed or renamed group carries its revision.
QList<PublishRequest> requestsForChanges( const QList<PredefinedFilterSet>& before,
                                          const QList<PredefinedFilterSet>& after,
                                          const QHash<QString, QString>& revisions = {} );
QList<PublishRequest> requestsForChanges( const QList<HighlighterSet>& before,
                                          const QList<HighlighterSet>& after,
                                          const QHash<QString, QString>& revisions = {} );

// What one sync found: the result of the worker thread, taken over on the
// main thread. Internal to the Team Folder, declared here for the watcher.
struct SyncOutcome;

} // namespace logsquirl::teamfolder

// The Team Folder (ADR-0008): a Git repository a team shares, cloned into
// LogSquirl's own data folder and kept current. It is the only part of
// LogSquirl that runs Git, and it runs the installed `git` program.
//
// Its groups -- one file each, in the Group Exchange's one-group format --
// are Team groups: they join the user's own groups in the dialogs and the
// Filters panel, and are never written into the user's settings. Turning the
// Team Folder off, or pointing it at another repository, leaves the user's
// own groups alone.
//
// A sync fetches, fast-forwards and reads the files again, on a worker
// thread: nothing it does blocks the user interface. It runs when the Team
// Folder is set up, every five minutes after that, and on sync(). What it
// ends in is the state, quiet by design: a failure is shown where the state is
// shown, with Git's own message, and never opens a dialog.
//
// Lives on the main thread; every call and signal is made there.
class TeamFolder : public QObject {
    Q_OBJECT

public:
    enum class State {
        // Turned off, or no repository named.
        Off,
        // Set up, but the Team groups are not known to be current: the first
        // sync has not finished, or the last one could not reach the
        // repository and the groups are those of the last sync that did.
        NotSynced,
        // The last sync reached the repository and the groups are current.
        Synced,
        // The Team Folder cannot be used as it is: Git is missing, the clone
        // failed, or the repository cannot be brought up to date.
        Error
    };

    static constexpr std::chrono::minutes SyncInterval{ 5 };

    // cloneDirectory is where the repository is cloned to; LogSquirl keeps
    // nothing else there, and replaces it when the repository changes.
    // gitProgram is the Git that is run, looked up on the PATH by default.
    explicit TeamFolder( QString cloneDirectory, QString gitProgram = QStringLiteral( "git" ),
                         QObject* parent = nullptr );
    // Stops a running sync and waits for it to end.
    ~TeamFolder() override;

    TeamFolder( const TeamFolder& ) = delete;
    TeamFolder& operator=( const TeamFolder& ) = delete;

    // Where the application keeps the clone: "teamfolder" in its data folder.
    static QString defaultCloneDirectory();

    // Sets the Team Folder up as the Policy says: turns it on and syncs,
    // follows a changed repository or subfolder, or turns it off. The same
    // Policy again changes nothing.
    void setUp( const TeamFolderPolicy& policy );

    // Syncs now, unless the Team Folder is off. A sync asked for while one is
    // running follows it.
    void sync();

    State state() const;
    // Why the state is what it is: Git's own message for a failure, empty
    // otherwise.
    QString message() const;
    bool isSyncing() const;

    // The files the last sync could not read as a group, and why. The other
    // files were read all the same.
    QList<logsquirl::teamfolder::SkippedFile> skippedFiles() const;

    // Publishes changed Team groups: each is committed on its own, under the
    // user's own Git identity, and pushed after a sync. A push rejected
    // because the branch moved is retried once after syncing again. Offline,
    // the changes stay pending and are pushed at a later sync. Ends in
    // publishFinished.
    void publish( QList<logsquirl::teamfolder::PublishRequest> requests );

    // Carries out the user's answer to a Conflict of a publish. Keep mine
    // publishes the request again without the question; Take theirs leaves the
    // server and the Team groups as the sync brought them; Save as copy
    // publishes the group as a new Team group with a fresh id and a free name,
    // leaving theirs. Ends in publishFinished, except for Take theirs.
    void resolveConflict( const logsquirl::teamfolder::PublishRequest& request,
                          logsquirl::teamfolder::ConflictChoice answer );

    // The revision of a Team group's file as of the last sync, and of every
    // Team group; empty for a group that is not there.
    QString filterGroupRevision( const QString& id ) const;
    QString highlighterGroupRevision( const QString& id ) const;
    QHash<QString, QString> filterGroupRevisions() const;
    QHash<QString, QString> highlighterGroupRevisions() const;

    // Whether Team groups can be changed: not when the server refused a push,
    // until the Team Folder is set up again.
    bool isWritable() const;
    // The server's message for why not.
    QString readOnlyReason() const;
    // Whether changes are committed here that the server does not have yet.
    bool hasPendingChanges() const;

    // The state in a few words, for where it is shown: "Team Folder synced".
    QString summary() const;
    // Git's message and the files skipped, one per line; empty when there is
    // nothing to say beyond the summary.
    QString details() const;

    // The Team Filter Groups, sorted alphabetically by name.
    QList<PredefinedFilterSet> filterGroups() const;
    // The Team Highlighter Sets, sorted alphabetically by name.
    QList<HighlighterSet> highlighterGroups() const;

Q_SIGNALS:
    void stateChanged();
    // The Team groups changed: a sync brought groups that were added,
    // changed or removed, or the Team Folder was turned off.
    void groupsChanged( const logsquirl::teamfolder::TeamGroupChanges& changes );
    // The same for the Team Highlighter Sets.
    void highlighterGroupsChanged( const logsquirl::teamfolder::TeamGroupChanges& changes );
    // A publish ended.
    void publishFinished( const logsquirl::teamfolder::PublishOutcome& outcome );
    // A sync ended, whatever it brought.
    void syncFinished();

private:
    void startSync();
    void takeOutcome();
    void setGroups( QList<logsquirl::teamfolder::TeamGroup<PredefinedFilterSet>> filterGroups,
                    QList<logsquirl::teamfolder::TeamGroup<HighlighterSet>> highlighterGroups );
    void setState( State state, const QString& message );

    QString cloneDirectory_;
    QString gitProgram_;
    TeamFolderPolicy policy_;

    State state_ = State::Off;
    QString message_;
    QList<logsquirl::teamfolder::SkippedFile> skippedFiles_;
    QList<logsquirl::teamfolder::TeamGroup<PredefinedFilterSet>> filterGroups_;
    QList<logsquirl::teamfolder::TeamGroup<HighlighterSet>> highlighterGroups_;

    QTimer syncTimer_;
    QFutureWatcher<std::shared_ptr<logsquirl::teamfolder::SyncOutcome>> running_;
    // Set to stop the running sync: on turning off, on a new repository and
    // on destruction.
    std::shared_ptr<std::atomic_bool> stopRunning_;
    // Counts the set-ups, so that a sync started for an earlier one is
    // dropped when it ends.
    unsigned setUpGeneration_ = 0;
    unsigned runningGeneration_ = 0;
    // From the start of a sync until its outcome is taken over.
    bool syncing_ = false;
    bool syncAgain_ = false;
    // Publishes asked for and not started yet, and those running.
    QList<logsquirl::teamfolder::PublishRequest> queuedRequests_;
    bool writable_ = true;
    QString readOnlyReason_;
    bool hasPending_ = false;
    // Git's message for the last publish that did not work out, until a
    // publish does.
    QString publishError_;
};

Q_DECLARE_METATYPE( logsquirl::teamfolder::TeamGroupChanges )
Q_DECLARE_METATYPE( logsquirl::teamfolder::PublishOutcome )
