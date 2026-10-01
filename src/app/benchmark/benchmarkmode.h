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

#include <memory>

#include <QTemporaryDir>

#include "processclock.h"

class LogSquirlApp;
struct CliParameters;

namespace logsquirl::benchmark {

class BenchmarkRun;

// The benchmark mode of the application (#666): `logsquirl --benchmark
// <scenario> <Log File>...` starts as a user's start does, runs the scenario,
// writes its report as JSON and exits. BUILD.md, "Benchmark mode", documents
// the command line and the report.
//
// It is a Benchmark Run (CONTEXT.md): it reads and writes nothing of the
// user's. Its settings, Session, Log Formats, plugins, themes and crash dumps
// live in a temporary directory that goes when it ends; it does not load the
// plugins, does not check for a new version and uses no Index Cache, and a
// single-instance lock of its own keeps it apart from a LogSquirl the user is
// running. main() asks it at the few places a user's start does something
// else.
class BenchmarkMode {
public:
    // Whether the command line asks for the benchmark mode. Read from the raw
    // arguments, before the application object exists: the single-instance
    // lock is taken when it is made.
    static bool requested( int argc, char* argv[] );

    // What has to be in place before the application object exists: the
    // single-instance lock's name and Qt's test mode for the standard
    // locations.
    static void prepareProcess();

    // Sets up the run after the command line is read and before anything reads
    // a setting: the temporary directory and the settings in it. Returns none,
    // having said why on standard error, when the run cannot start -- an
    // unknown scenario, which lists the known ones, or a malformed option.
    static std::unique_ptr<BenchmarkMode> prepare( const CliParameters& parameters,
                                                   Clock::time_point mainEntered );

    ~BenchmarkMode();

    BenchmarkMode( const BenchmarkMode& ) = delete;
    BenchmarkMode& operator=( const BenchmarkMode& ) = delete;

    // Starts the scenario once the event loop runs. main() calls it where a
    // user's start opens its windows.
    void start( LogSquirlApp& app );

    // After the event loop has ended: removes the temporary directory and
    // returns the exit code of the run.
    int end( int eventLoopExitCode );

private:
    BenchmarkMode( const CliParameters& parameters, Clock::time_point mainEntered );

    std::unique_ptr<QTemporaryDir> dataDirectory_;
    std::unique_ptr<BenchmarkRun> run_;
};

} // namespace logsquirl::benchmark
