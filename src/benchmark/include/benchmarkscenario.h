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

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QString>

namespace logsquirl::benchmark {

// What a scenario is handed when it starts: the application, the Log Files and
// options of the run, and the report (src/app/benchmark/scenariorun.h).
class ScenarioRun;

// One thing the benchmark mode measures, such as opening and indexing a Log
// File (#666). It is started once the application is set up, from the event
// loop, and ends the run with ScenarioRun::finish() or fail() once the events
// it waits for have happened.
//
// A new scenario is a source file of its own under src/app/benchmark/scenarios,
// which the build picks up by itself, with a ScenarioRegistration at namespace
// scope; nothing else changes.
class Scenario {
public:
    virtual ~Scenario() = default;

    virtual void start( ScenarioRun& run ) = 0;
};

using ScenarioFactory = std::function<std::unique_ptr<Scenario>()>;

struct ScenarioEntry {
    QString name;
    // One line for the list of scenarios the application prints.
    QString description;
    ScenarioFactory create;
};

// The scenarios the application knows, by name.
class ScenarioRegistry {
public:
    // The application's registry, which every ScenarioRegistration adds to.
    static ScenarioRegistry& instance();

    // Returns false, and keeps the scenario already there, when the name is
    // taken.
    bool add( const QString& name, const QString& description, ScenarioFactory create );

    const ScenarioEntry* find( const QString& name ) const;

    // Every scenario, by name.
    std::vector<const ScenarioEntry*> entries() const;

private:
    std::map<QString, ScenarioEntry> entries_;
};

// Adds a scenario to the application's registry when the program starts:
//
//     namespace {
//     const ScenarioRegistration registration{
//         "open-and-index", "Opens a Log File and waits until it is indexed",
//         [] { return std::make_unique<OpenAndIndex>(); } };
//     }
//
// Two scenarios of one name end the program at start, before anything runs.
class ScenarioRegistration {
public:
    ScenarioRegistration( const QString& name, const QString& description, ScenarioFactory create );
};

} // namespace logsquirl::benchmark
