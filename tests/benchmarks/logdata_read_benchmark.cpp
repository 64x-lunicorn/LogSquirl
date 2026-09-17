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

// Micro-benchmarks for reading Log Lines from a Log File (#278): one Log Line
// at a time, as Quick Find, the Filtered View and saving do, a block of them,
// and a block's UTF-8 view, as a Search reads it -- hiding ANSI color
// sequences and showing them. One Log Line in ten is colored.
//
// Uses only what LogData offered before #278, so the same file measures both
// sides of an A/B comparison. See tests/benchmarks/README.md.

#include "logdata.h"
#include "test_policies.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryFile>
#include <QTimer>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

namespace {

constexpr int LogLineCount = 20'000;
constexpr int SingleReads = 2'000;

QByteArray logLine( int line )
{
    const auto text
        = ( line % 10 == 0 )
              ? QStringLiteral( "2026-09-17 12:34:56.%1 \x1B[31mERROR\x1B[0m [worker-%2] request "
                                "failed, retrying in a moment" )
              : QStringLiteral( "2026-09-17 12:34:56.%1 INFO  [worker-%2] request served in "
                                "twelve milliseconds" );
    return text.arg( line % 1000, 3, 10, QLatin1Char( '0' ) ).arg( line % 8 ).toUtf8() + '\n';
}

// A Log File of LogLineCount Log Lines, loaded into log data that reads it
// under the given Decoding Policy.
class LoadedLogFile {
public:
    explicit LoadedLogFile( bool hideAnsiColorSequences )
    {
        REQUIRE( file_.open() );
        for ( int line = 0; line < LogLineCount; ++line ) {
            file_.write( logLine( line ) );
        }
        file_.flush();

        auto policies = testSettingsPolicies();
        policies.decoding.hideAnsiColorSequences = hideAnsiColorSequences;
        logData_ = std::make_unique<LogData>( policies.indexing, policies.search,
                                              policies.fileAccess, policies.decoding );

        QEventLoop loop;
        QObject::connect( logData_.get(), &LogData::loadingFinished, &loop, &QEventLoop::quit );
        QTimer::singleShot( 60'000, &loop, &QEventLoop::quit );
        logData_->attachFile( file_.fileName() );
        loop.exec();
        REQUIRE( logData_->getNbLine() == LinesCount( LogLineCount ) );
    }

    const LogData& logData() const
    {
        return *logData_;
    }

private:
    QTemporaryFile file_{ "logdata_read_benchmark_XXXXXX" };
    std::unique_ptr<LogData> logData_;
};

void benchmarkReads( const LoadedLogFile& logFile, const std::string& decoding )
{
    const auto& logData = logFile.logData();

    BENCHMARK( "one Log Line at a time, " + decoding )
    {
        qsizetype characters = 0;
        for ( int line = 0; line < SingleReads; ++line ) {
            characters
                += logData.getLineString( LineNumber( static_cast<uint64_t>( line ) ) ).size();
        }
        return characters;
    };

    BENCHMARK( "a block of Log Lines, " + decoding )
    {
        return logData.getLines( 0_lnum, LinesCount( LogLineCount ) ).size();
    };

    BENCHMARK( "a block's UTF-8 view for a Search, " + decoding )
    {
        const auto rawLines = logData.getLinesRaw( 0_lnum, LinesCount( LogLineCount ) );
        return rawLines.buildUtf8View().size();
    };
}

} // namespace

TEST_CASE( "Reading Log Lines", "[logdata-read-benchmark]" )
{
    SECTION( "hiding ANSI color sequences" )
    {
        const LoadedLogFile logFile{ true };
        benchmarkReads( logFile, "hiding ANSI color sequences" );
    }

    SECTION( "showing ANSI color sequences" )
    {
        const LoadedLogFile logFile{ false };
        benchmarkReads( logFile, "showing ANSI color sequences" );
    }
}

int main( int argc, char* argv[] )
{
    QCoreApplication application( argc, argv );
    return Catch::Session().run( argc, argv );
}
