/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <QApplication>
#include <QMetaType>
#include <QtConcurrent>

#include <configuration.h>
#include <highlighterset.h>
#include <linetypes.h>
#include <logfiltereddataworker.h>
#include <persistentinfo.h>
#include <searchsession.h>

#include <logger.h>

#include <tbb/global_control.h>

#include <thread>

const bool PersistentInfo::ForcePortable = true;

class TestRunner : public QObject {
    Q_OBJECT

public:
    TestRunner( int argc, char** argv )
        : argc_( argc )
        , argv_( argv )
    {
    }

    int result()
    {
        return result_;
    }

public Q_SLOTS:
    void process()
    {
        result_ = Catch::Session().run( argc_, argv_ );
        Q_EMIT finished( result_ );
    }

Q_SIGNALS:
    void finished( int );

private:
    int argc_;
    char** argv_;

    int result_;
};

#include "qtests_main.moc"

int main( int argc, char* argv[] )
{
    // Unlike the app's own main() (src/app/main.cpp), nothing here otherwise
    // guarantees a second TBB thread. On a CPU-constrained CI runner where
    // TBB's ambient concurrency is 1, a search/index flow graph has no worker
    // thread free to make progress whenever its driving thread is busy
    // elsewhere (e.g. polling for buffer space), and can stall indefinitely.
    // Kept alive for the rest of main() so the constraint doesn't revert
    // before the tests run.
    const auto ambientConcurrency
        = tbb::global_control::active_value( tbb::global_control::max_allowed_parallelism );
    tbb::global_control ensureWorkerThread( tbb::global_control::max_allowed_parallelism,
                                            std::max( ambientConcurrency, size_t{ 2 } ) );

    QApplication a( argc, argv );

    logging::enableLogging();

    // Diagnostic for #85's "search superseded by a later one" CI flake on
    // ubuntu_noble specifically: this is the one figure the earlier
    // floor-of-2 fix (f1e50506) never actually logged, so there is no way
    // to tell from a CI run whether TBB saw this container as having 1
    // logical thread (the case that fix targets) or something else. Remove
    // once that investigation concludes.
    LOG_INFO << "qtests_main: ambient TBB concurrency " << ambientConcurrency
             << ", std::thread::hardware_concurrency() " << std::thread::hardware_concurrency();

    qRegisterMetaType<LinesCount>( "LinesCount" );
    qRegisterMetaType<LineNumber>( "LineNumber" );
    qRegisterMetaType<LineLength>( "LineLength" );
    qRegisterMetaType<SearchId>( "SearchId" );
    qRegisterMetaType<SearchSession::State>( "SearchSession::State" );

    auto& config = Configuration::getSynced();
    config.setSearchReadBufferSizeLines( 10 );
    config.setIndexReadBufferSizeMb( 1 );
    config.setUseSearchResultsCache( false );

    auto higthlighters = HighlighterSetCollection::getSynced();

#if defined( Q_OS_WIN ) || defined( Q_OS_MAC )
    config.setPollingEnabled( true );
    config.setPollIntervalMs( 1000 );
#else
    config.setPollingEnabled( false );
#endif

    config.setNativeFileWatchEnabled( true );

    // Disable confirmation dialogs so tests don't block on modal QMessageBox.
    // Must persist to storage because other tests call getSynced() which reloads
    // all values from QSettings, overwriting in-memory-only changes.
    config.setConfirmTabClose( false );
    config.save();

    QThreadPool::globalInstance()->reserveThread();

    TestRunner* runner = new TestRunner( argc, argv );

    runner->process();
    return runner->result();
}
