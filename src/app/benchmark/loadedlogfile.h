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
#include <memory>

#include <QPointer>
#include <QString>

class CrawlerWidget;
class LogMainView;
class MainWindow;

namespace logsquirl::benchmark {

class PaintProbe;
class ScenarioRun;

// Opens the one Log File of a run in a new window and waits until it is
// loaded and shown (#668): for a scenario that measures something done on a
// loaded Log File, a Search or a QuickFind, not the open. Loaded is its
// Index finished; shown is a paint of the Text View's Viewport, after that,
// with Log Lines in it. Neither is measured: the scenario marks its start
// once it is called.
//
// The run fails when it was not given exactly one Log File, or when the load
// does not succeed.
class LoadedLogFile {
public:
    using Loaded = std::function<void( MainWindow& window, CrawlerWidget& crawler )>;

    LoadedLogFile( ScenarioRun& run, QString scenarioName, Loaded loaded );
    // Opens logFile, not one given to the run: a Log File the scenario wrote
    // itself, in the run's own directory (#670).
    LoadedLogFile( ScenarioRun& run, QString scenarioName, QString logFile, Loaded loaded );
    ~LoadedLogFile();

    LoadedLogFile( const LoadedLogFile& ) = delete;
    LoadedLogFile& operator=( const LoadedLogFile& ) = delete;

    // Opens the Log File; loaded is called from the event loop once it is
    // loaded and shown.
    void open();

    // The size of the Log File in bytes, and its Log Lines once loaded.
    qint64 bytes() const;
    qint64 logLineCount() const;

private:
    void opened( CrawlerWidget* crawler );
    void painted();

    ScenarioRun& run_;
    QString scenarioName_;
    // Empty: the one Log File given to the run.
    QString logFile_;
    Loaded loaded_;
    QPointer<MainWindow> window_;
    QPointer<CrawlerWidget> crawler_;
    QPointer<LogMainView> mainView_;
    std::unique_ptr<PaintProbe> probe_;
    bool indexed_ = false;
    bool called_ = false;
    qint64 logLineCount_ = 0;
};

} // namespace logsquirl::benchmark
