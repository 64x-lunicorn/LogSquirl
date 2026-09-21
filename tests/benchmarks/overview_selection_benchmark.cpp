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

// Benchmarks for the overview with a million Matches and for extending a long
// selection line by line (#297): recomputing the overview after a Search
// changed, painting it, and Shift+Down / Shift+Up at the end of a selection of
// 100,000 Log Lines.
//
// Uses only what the Overview, its widget and the text view offered before
// #297, so the same file measures both sides of an A/B comparison (see
// README.md).

#include "generated_log_file.h"
#include "generated_log_lines.h"
#include "test_policies.h"

#include "abstractlogdata.h"
#include "abstractlogview.h"
#include "configuration.h"
#include "highlighterset.h"
#include "loadingstatus.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "overview.h"
#include "overviewwidget.h"
#include "persistentinfo.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "shortcuts.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QMouseEvent>
#include <QShortcut>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include "isolated_settings.h"

// The settings library, which the UI library links, asks every executable.
const bool PersistentInfo::ForcePortable = true;

namespace {

// Every other generated Log Line: [worker-0], [worker-2], ...
const QString EveryOtherLine = QStringLiteral( "\\[worker-[0246]\\]" );

// How many pixel rows the overview draws on.
constexpr unsigned OverviewRows = 1000;

// The size of the generated Log File: 192 MiB, about two million Log Lines of
// which a million match, or the number of MiB in
// LOGSQUIRL_BENCHMARK_LOG_FILE_MB, for a quick run.
std::uint64_t logFileBytes()
{
    constexpr std::uint64_t Mib = 1024 * 1024;
    return qEnvironmentVariableIsSet( "LOGSQUIRL_BENCHMARK_LOG_FILE_MB" )
               ? logdatabenchmark::generatedLogFileBytes()
               : 192 * Mib;
}

// A generated Log File, indexed, with a Search for every other Log Line
// completed on it; written on first use and removed at exit.
class SearchedLogFile {
public:
    SearchedLogFile()
    {
        using namespace logdatabenchmark;

        REQUIRE( directory_.isValid() );
        const auto fileName = directory_.filePath( "short_lines.log" );
        std::cerr << "Generating a Log File of " << ( logFileBytes() / ( 1024 * 1024 ) )
                  << " MiB in " << directory_.path().toStdString() << std::endl;
        bool written = false;
        writeGeneratedLogFile( fileName, LogFileShape::ShortLines, logFileBytes(), written );
        REQUIRE( written );

        auto policies = testSettingsPolicies();
        policies.search.contextLinesCount = 0;
        logData_ = std::make_unique<LogData>( policies.indexing, policies.search,
                                              policies.fileAccess, policies.decoding );
        {
            QEventLoop loop;
            QObject::connect( logData_.get(), &LogData::loadingFinished, &loop,
                              [ &loop ]( LoadingStatus ) { loop.quit(); } );
            logData_->attachFile( fileName );
            loop.exec();
        }
        REQUIRE( logData_->getNbLine().get() > 0 );

        search_ = logData_->getNewFilteredData();
        {
            QEventLoop loop;
            QObject::connect( search_.get(), &LogFilteredData::searchStateChanged, &loop,
                              [ &loop ]( const SearchSession::State& state ) {
                                  if ( state.phase != SearchSession::Phase::Running ) {
                                      loop.quit();
                                  }
                              } );
            search_->request( RegularExpressionPattern( EveryOtherLine ) );
            loop.exec();
        }
        std::cerr << search_->getNbMatches().get() << " Matches in " << logData_->getNbLine().get()
                  << " Log Lines" << std::endl;
    }

    ~SearchedLogFile()
    {
        search_.reset();
        logData_.reset();
    }

    LogData& logData() const
    {
        return *logData_;
    }
    LogFilteredData& search() const
    {
        return *search_;
    }

private:
    QTemporaryDir directory_{ QDir::tempPath() + "/logsquirl_overview_benchmark_XXXXXX" };
    std::unique_ptr<LogData> logData_;
    std::unique_ptr<LogFilteredData> search_;
};

// Owned by main(), so that every LogData is gone before the QApplication.
std::unique_ptr<SearchedLogFile>* searchedLogFileHolder = nullptr;

SearchedLogFile& searchedLogFile()
{
    if ( !*searchedLogFileHolder ) {
        *searchedLogFileHolder = std::make_unique<SearchedLogFile>();
    }
    return **searchedLogFileHolder;
}

// A million generated Log Lines held in memory, as the text view scrolling
// benchmark reads them.
class GeneratedLogData final : public AbstractLogData {
protected:
    QString doGetLineString( LineNumber line ) const override
    {
        return scrollingbenchmark::generatedLogLine( line.get() );
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return untabify( scrollingbenchmark::generatedLogLine( line.get() ) );
    }
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        logsquirl::vector<QString> lines;
        const auto end = std::min( first.get() + count.get(), doGetNbLine().get() );
        for ( auto line = first.get(); line < end; ++line ) {
            lines.push_back( scrollingbenchmark::generatedLogLine( line ) );
        }
        return lines;
    }
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first,
                                                   LinesCount count ) const override
    {
        auto lines = doGetLines( first, count );
        for ( auto& line : lines ) {
            line = untabify( std::move( line ) );
        }
        return lines;
    }
    LineNumber doGetLineNumber( LineNumber index ) const override
    {
        return index;
    }
    LinesCount doGetNbLine() const override
    {
        return LinesCount( scrollingbenchmark::LogLineCount );
    }
    LineLength doGetMaxLength() const override
    {
        return LineLength( scrollingbenchmark::LongestLogLine );
    }
    LineLength doGetLineLength( LineNumber line ) const override
    {
        return LineLength( static_cast<LineLength::UnderlyingType>(
            scrollingbenchmark::generatedLogLine( line.get() ).size() ) );
    }
    void doSetDisplayEncoding( const char* ) override {}
    QTextCodec* doGetDisplayEncoding() const override
    {
        return nullptr;
    }
    void doAttachReader() const override {}
    void doDetachReader() const override {}
};

class BenchmarkedView : public AbstractLogView {
public:
    BenchmarkedView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern )
        : AbstractLogView( logData, quickFindPattern, /* textWrap */ false )
    {
    }

    // Declared without override so that this builds on both sides of an A/B
    // comparison (see textview_scroll_benchmark.cpp).
    AbstractLogData::LineType lineType( LineNumber ) const // NOLINT
    {
        return {};
    }
};

void triggerShortcut( QWidget& view, const char* action )
{
    const auto keys = ShortcutAction::shortcutKeys( action, Configuration::get().shortcuts() );
    REQUIRE_FALSE( keys.isEmpty() );
    for ( auto* shortcut : view.findChildren<QShortcut*>() ) {
        if ( shortcut->key() == keys.first() ) {
            Q_EMIT shortcut->activated();
            return;
        }
    }
    FAIL( "no shortcut for " << action );
}

} // namespace

TEST_CASE( "The overview with a million Matches", "[overview-benchmark]" )
{
    auto& logFile = searchedLogFile();
    const auto nbLines = logFile.logData().getNbLine();

    Overview overview;
    overview.setFilteredData( &logFile.search() );
    overview.setVisible( true );
    overview.updateData( nbLines );
    overview.updateView( OverviewRows );

    BENCHMARK( "recompute after the Search changed, 1000 rows" )
    {
        overview.updateData( nbLines );
        overview.updateView( OverviewRows );
        return overview.getMatchLines()->size();
    };

    OverviewWidget widget;
    widget.setOverview( &overview );
    widget.resize( 20, static_cast<int>( OverviewRows ) );
    widget.show();
    QCoreApplication::processEvents();
    widget.repaint();

    BENCHMARK( "paint, nothing changed, 10 times" )
    {
        for ( int paint = 0; paint < 10; ++paint ) {
            widget.repaint();
        }
        return overview.getMatchLines()->size();
    };

    BENCHMARK( "10 Search ticks, each painted" )
    {
        for ( int tick = 0; tick < 10; ++tick ) {
            overview.updateData( nbLines );
            widget.repaint();
        }
        return overview.getMatchLines()->size();
    };
}

TEST_CASE( "Extending a selection of 100,000 Log Lines", "[selection-benchmark]" )
{
    constexpr std::uint64_t SelectedLines = 100'000;
    constexpr std::uint64_t FirstLine = 1000;

    GeneratedLogData logData;
    const QuickFindPattern quickFindPattern;
    BenchmarkedView view( &logData, &quickFindPattern );
    view.setFrameShape( QFrame::NoFrame );
    view.resize( 800, 600 );
    view.show();
    QCoreApplication::processEvents();
    view.setPresentationPolicy( testSettingsPolicies().presentation );
    view.updateData();
    view.registerShortcuts();

    // Log Line FirstLine selected, then a Shift+click on the Log Line
    // SelectedLines further down, shown on the top row.
    const auto lastLine = LineNumber( FirstLine + SelectedLines - 1 );
    view.selectAndDisplayLine( LineNumber( FirstLine ) );
    view.jumpToLine( lastLine );
    const QPointF onTopRow{ 200.0, 2.0 };
    QMouseEvent shiftClick( QEvent::MouseButtonPress, onTopRow,
                            view.viewport()->mapToGlobal( onTopRow ), Qt::LeftButton,
                            Qt::LeftButton, Qt::ShiftModifier );
    QCoreApplication::sendEvent( view.viewport(), &shiftClick );
    QMouseEvent release( QEvent::MouseButtonRelease, onTopRow,
                         view.viewport()->mapToGlobal( onTopRow ), Qt::LeftButton, Qt::NoButton,
                         Qt::ShiftModifier );
    QCoreApplication::sendEvent( view.viewport(), &release );
    triggerShortcut( view, ShortcutAction::LogViewSelectLinesDown );
    const auto selected = view.getSelectedText().count( QChar::LineFeed ) + 1;
    std::cerr << selected << " Log Lines selected" << std::endl;
    REQUIRE( static_cast<std::uint64_t>( selected ) > SelectedLines / 2 );

    BENCHMARK( "Shift+Down 20 times and Shift+Up 20 times" )
    {
        for ( int step = 0; step < 20; ++step ) {
            triggerShortcut( view, ShortcutAction::LogViewSelectLinesDown );
        }
        for ( int step = 0; step < 20; ++step ) {
            triggerShortcut( view, ShortcutAction::LogViewSelectLinesUp );
        }
        return view.getTopLine();
    };
}

int main( int argc, char* argv[] )
{
    // The test cases run beside a settings file of this process's own, so
    // that no test binary reads or writes the one in the build directory and
    // no case inherits what an earlier one left behind (#370).
    if ( const auto launcherExitCode = isolated_settings::relaunchWithOwnSettings( argc, argv ) ) {
        return *launcherExitCode;
    }

    // Offscreen unless a platform was asked for.
    if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) ) {
        qputenv( "QT_QPA_PLATFORM", "offscreen" );
    }
    QApplication app( argc, argv );

    // What the text view reads for its shortcuts and its painting.
    Configuration::getSynced();
    HighlighterSetCollection::getSynced();

    std::unique_ptr<SearchedLogFile> logFile;
    searchedLogFileHolder = &logFile;
    const auto result = Catch::Session().run( argc, argv );
    logFile.reset();
    searchedLogFileHolder = nullptr;
    return result;
}
