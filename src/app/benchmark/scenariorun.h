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

#include <vector>

#include <QString>

#include "benchmarkreport.h"
#include "benchmarkscenario.h"

class MainWindow;
class QObject;

namespace logsquirl::benchmark {

// What a scenario of the benchmark mode is handed when it starts (#666): the
// Log Files and options of the run, the report its events go into, and the
// application, set up as a user's start sets it up but with nothing of the
// user's (a Benchmark Run, CONTEXT.md).
//
// The scenario opens what it measures itself: no window is open when it
// starts, and no Session is restored.
class ScenarioRun {
public:
    virtual ~ScenarioRun() = default;

    // Where the events and results go. The runner marks the scenario's start
    // just before Scenario::start(); a scenario that first has to prepare
    // something it does not measure marks it again once it is ready.
    virtual BenchmarkReport& report() = 0;

    // The Log Files given on the command line, as absolute paths.
    virtual const std::vector<QString>& logFiles() const = 0;
    // The value of --benchmark-option name=value, or fallback when not given.
    virtual QString option( const QString& name, const QString& fallback = {} ) const = 0;

    // The directory the run keeps its settings, Session and data in, which
    // goes when the run ends. A scenario that restores a Session writes it
    // here before it restores it.
    virtual QString dataDirectory() const = 0;

    // A new, empty main window, shown, at the size of the run.
    virtual MainWindow* newWindow() = 0;
    // The windows of the Session in dataDirectory(), shown; the last of them.
    virtual MainWindow* restoreSession() = 0;

    // Lives as long as the run: a parent for what the scenario creates, and a
    // context for its connections and timers.
    virtual QObject* context() = 0;

    // Ends the run once the event loop gets to it: the report is written with
    // its peak RSS as it is now, and the application exits with 0 -- or, after
    // fail(), with 1. Only the first call counts.
    virtual void finish() = 0;
    virtual void fail( const QString& reason ) = 0;
};

} // namespace logsquirl::benchmark
