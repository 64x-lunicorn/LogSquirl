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

#include <QString>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <memory>

// The one place in LogSquirl that starts Git (ADR-0008). It runs the
// installed `git` program as a process, so that the user's own configuration
// and authentication apply unchanged; LogSquirl links no Git library and never
// sees a credential. Only the Team Folder uses it.
//
// A run blocks until Git has finished, so it is made off the main thread: the
// Team Folder syncs on a worker thread. It can be stopped from any thread
// through the stop flag it was given.
namespace logsquirl::teamfolder {

struct GitResult {
    // Whether Git could be started at all. When it could not, Git is missing
    // (or not executable) and error says why.
    bool started = false;
    // Whether it ran to the end and exited with 0.
    bool succeeded = false;
    QString output;
    // What Git printed on its error stream: its own message, shown to the
    // user unchanged when a run fails.
    QString error;

    // Git's message, or what kept Git from running, trimmed.
    QString message() const;
};

class Git {
public:
    using StopFlag = std::shared_ptr<std::atomic_bool>;

    // How long a run that talks to the server may take before it is given up;
    // a local one is given the same, a server may be slow to answer.
    static constexpr std::chrono::minutes RunTimeout{ 3 };

    // program is what is started: "git" to look it up on the PATH, the way a
    // terminal does. A run stops early once stop is set.
    explicit Git( QString program, StopFlag stop = {} );

    // Runs git with these arguments in workingDirectory (any directory, when
    // empty). Git never asks on a terminal: a run that would have to fails with
    // Git's message instead of waiting for an answer nobody can give.
    GitResult run( const QStringList& arguments, const QString& workingDirectory = {} ) const;

private:
    QString program_;
    StopFlag stop_;
};

} // namespace logsquirl::teamfolder
