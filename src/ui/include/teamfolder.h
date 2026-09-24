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
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <memory>

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
};

Q_DECLARE_METATYPE( logsquirl::teamfolder::TeamGroupChanges )
