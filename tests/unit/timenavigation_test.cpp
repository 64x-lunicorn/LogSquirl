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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "fake_log_data.h"
#include "logformatcatalog.h"
#include "logformatdefinition.h"
#include "logformatparser.h"
#include "timenavigation.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThreadPool>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr int LineCount = 300;

// Line i is written at 12:00:00 plus ten seconds a line, in the spdlog Log
// Format.
QStringList timedLines()
{
    QStringList lines;
    for ( int i = 0; i < LineCount; ++i ) {
        const auto seconds = i * 10;
        lines.append( QStringLiteral( "[2026-01-01 12:%1:%2.000] [nav] [info] line %3" )
                          .arg( seconds / 60, 2, 10, QChar( '0' ) )
                          .arg( seconds % 60, 2, 10, QChar( '0' ) )
                          .arg( i ) );
    }
    return lines;
}

QStringList untimedLines()
{
    QStringList lines;
    for ( int i = 0; i < LineCount; ++i ) {
        lines.append( QStringLiteral( "no time on line %1" ).arg( i ) );
    }
    return lines;
}

std::shared_ptr<const LogFormatDefinition> timedFormat()
{
    LogFormatCatalog catalog;
    catalog.rebuild();
    const auto format = catalog.allFormats().value( QStringLiteral( "spdlog_log" ) );
    REQUIRE( format );
    return format;
}

std::shared_ptr<const LogFormatDefinition> untimedFormat()
{
    const auto formats = LogFormatParser::parseJsonString( R"({
        "untimed_log": {
            "title": "Untimed",
            "regex": { "std": { "pattern": "^(?<body>.*)$" } },
            "sample": [ { "line": "anything" } ]
        }
    })" );
    REQUIRE( formats.size() == 1 );
    return std::make_shared<const LogFormatDefinition>( formats[ 0 ] );
}

// Log Lines whose reads wait while the gate is closed, so that a lookup is
// still running when a test cancels it.
class GatedLogData : public FakeLogData {
public:
    explicit GatedLogData( QStringList lines )
        : FakeLogData( std::move( lines ) )
    {
    }

    std::atomic<bool> open{ true };

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        while ( !open.load() ) {
            std::this_thread::sleep_for( 1ms );
        }
        return FakeLogData::doGetLineString( line );
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return doGetLineString( line );
    }
};

// Keeps what comes out of the Time Navigation.
struct RecordingSink {
    std::vector<LineNumber> shown;
    std::vector<std::pair<LineNumber, LineNumber>> limits;
    QStringList statuses;
    std::vector<int> windowsChosen;

    TimeNavigation::Sink sink()
    {
        return { [ this ]( LineNumber line ) { shown.push_back( line ); },
                 [ this ]( LineNumber start, LineNumber end ) {
                     limits.emplace_back( start, end );
                 },
                 [ this ]( const QString& text ) { statuses.append( text ); },
                 [ this ]( int minutes ) { windowsChosen.push_back( minutes ); } };
    }
};

// Answers the prompts from a script, in order, and keeps what was asked and
// told. A step can do something first, as a user would while a dialog is open.
struct ScriptedPrompt {
    struct Step {
        std::optional<QString> text;
        std::optional<int> number;
        std::function<void()> before;
    };

    std::deque<Step> steps;
    QStringList asked;
    std::vector<std::pair<QString, QString>> told;
    std::vector<int> offeredNumbers;

    ScriptedPrompt& text( const QString& answer, std::function<void()> before = {} )
    {
        steps.push_back( { answer, std::nullopt, std::move( before ) } );
        return *this;
    }
    ScriptedPrompt& number( int answer )
    {
        steps.push_back( { std::nullopt, answer, {} } );
        return *this;
    }
    ScriptedPrompt& cancel()
    {
        steps.push_back( {} );
        return *this;
    }

    // A prompt the script has no answer for is cancelled: a REQUIRE thrown
    // out of a lookup's callback would end the test run.
    Step next()
    {
        if ( steps.empty() ) {
            return {};
        }
        auto step = std::move( steps.front() );
        steps.pop_front();
        if ( step.before ) {
            step.before();
        }
        return step;
    }

    QStringList toldTexts() const
    {
        QStringList texts;
        for ( const auto& [ title, text ] : told ) {
            texts.append( text );
        }
        return texts;
    }

    TimeNavigation::Prompt prompt()
    {
        TimeNavigation::Prompt prompt;
        prompt.askText = [ this ]( const QString&, const QString& label ) {
            asked.append( label );
            return next().text;
        };
        prompt.askNumber = [ this ]( const QString&, const QString& label, int value, int, int ) {
            asked.append( label );
            offeredNumbers.push_back( value );
            return next().number;
        };
        prompt.tell = [ this ]( const QString& title, const QString& text ) {
            told.emplace_back( title, text );
        };
        return prompt;
    }
};

struct Navigation {
    Navigation()
    {
        navigation = std::make_unique<TimeNavigation>(
            [ this ] {
                ++sourcesTaken;
                return TimeNavigation::Source{ logData, format, QDate( 2026, 1, 1 ) };
            },
            sink.sink(), prompt.prompt() );
    }

    // A worker still reading when a check failed waits for its gate: the
    // Time Navigation waits for it when destroyed, so every gate opens first.
    ~Navigation()
    {
        for ( const auto& data : gatedData ) {
            data->open = true;
        }
        navigation.reset();
    }

    Navigation( const Navigation& ) = delete;
    Navigation& operator=( const Navigation& ) = delete;
    Navigation( Navigation&& ) = delete;
    Navigation& operator=( Navigation&& ) = delete;

    std::shared_ptr<GatedLogData> gated( QStringList lines )
    {
        gatedData.push_back( std::make_shared<GatedLogData>( std::move( lines ) ) );
        return gatedData.back();
    }

    std::vector<std::shared_ptr<GatedLogData>> gatedData;
    std::shared_ptr<GatedLogData> logData = gated( timedLines() );
    std::shared_ptr<const LogFormatDefinition> format = timedFormat();
    int sourcesTaken = 0;
    RecordingSink sink;
    ScriptedPrompt prompt;
    std::unique_ptr<TimeNavigation> navigation;

    bool isLookingUp() const
    {
        return navigation && navigation->isLookingUp();
    }

    // Until no lookup runs, the ones a prompt started included.
    void settle()
    {
        QElapsedTimer timer;
        timer.start();
        while ( isLookingUp() && timer.elapsed() < 10'000 ) {
            QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
        }
        REQUIRE_FALSE( isLookingUp() );
    }

    // Until every worker has returned and what it reported was delivered.
    void drain()
    {
        for ( const auto& data : gatedData ) {
            data->open = true;
        }
        QThreadPool::globalInstance()->waitForDone( 10'000 );
        for ( int i = 0; i < 5; ++i ) {
            QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
        }
    }
};

using Limits = std::pair<LineNumber, LineNumber>;

} // namespace

TEST_CASE( "Go to timestamp goes to the first Log Line at or after the time",
           "[ui][timenavigation]" )
{
    Navigation nav;
    nav.prompt.text( "12:10:00" );
    nav.navigation->goToTimestamp( 0_lnum );
    nav.settle();

    CHECK( nav.sink.shown == std::vector{ 60_lnum } );
    CHECK( nav.prompt.told.empty() );
    CHECK( nav.sink.statuses.last().isEmpty() );
}

TEST_CASE( "Go to timestamp tells the positions that carry a message", "[ui][timenavigation]" )
{
    Navigation nav;

    SECTION( "Before the first Timestamp it goes to the first Log Line" )
    {
        nav.prompt.text( "11:00:00" );
        nav.navigation->goToTimestamp( 100_lnum );
        nav.settle();
        CHECK( nav.sink.shown == std::vector{ 0_lnum } );
        REQUIRE( nav.prompt.told.size() == 1 );
        CHECK( nav.prompt.told[ 0 ].first == "Go to timestamp" );
        CHECK( nav.prompt.told[ 0 ].second.contains( "before the first timestamp" ) );
    }

    SECTION( "After the last Timestamp it goes to the last Log Line" )
    {
        nav.prompt.text( "13:00:00" );
        nav.navigation->goToTimestamp( 0_lnum );
        nav.settle();
        CHECK( nav.sink.shown == std::vector{ LineNumber( LineCount - 1 ) } );
        REQUIRE( nav.prompt.told.size() == 1 );
        CHECK( nav.prompt.told[ 0 ].second.contains( "after the last timestamp" ) );
    }

    SECTION( "With no Timestamp left it goes to the first Log Line" )
    {
        // The Log File loses its Timestamps while the time is asked for: the
        // lookup reads what the source holds after the prompt.
        nav.prompt.text( "12:10:00", [ & ] { nav.logData = nav.gated( untimedLines() ); } );
        nav.navigation->goToTimestamp( 0_lnum );
        nav.settle();
        CHECK( nav.sink.shown == std::vector{ 0_lnum } );
        REQUIRE( nav.prompt.told.size() == 1 );
        CHECK( nav.prompt.told[ 0 ].second
               == "No Log Line has a timestamp this Log Format can read." );
    }
}

TEST_CASE( "Go to timestamp stops at an answer that is no time", "[ui][timenavigation]" )
{
    Navigation nav;

    SECTION( "Text that is no time is said" )
    {
        nav.prompt.text( "  not a time " );
        nav.navigation->goToTimestamp( 0_lnum );
        nav.settle();
        REQUIRE( nav.prompt.told.size() == 1 );
        CHECK( nav.prompt.told[ 0 ].second.startsWith( "\"not a time\" is not a time." ) );
    }

    SECTION( "A cancelled prompt or an empty answer does nothing" )
    {
        const bool cancelled = GENERATE( true, false );
        if ( cancelled ) {
            nav.prompt.cancel();
        }
        else {
            nav.prompt.text( "   " );
        }
        nav.navigation->goToTimestamp( 0_lnum );
        nav.settle();
        CHECK( nav.prompt.told.empty() );
    }
    CHECK( nav.sink.shown.empty() );
}

TEST_CASE( "No Timestamp near the current Log Line is said before anything is asked",
           "[ui][timenavigation]" )
{
    Navigation nav;
    nav.logData = nav.gated( untimedLines() );

    const auto entry = GENERATE( size_t{ 0 }, size_t{ 1 }, size_t{ 2 } );
    const auto title = std::array{ "Go to timestamp", "Set search limits to time range",
                                   "Set search limits around current line" }[ entry ];
    switch ( entry ) {
    case 0:
        nav.navigation->goToTimestamp( 10_lnum );
        break;
    case 1:
        nav.navigation->setSearchLimitsToTimeRange( 10_lnum );
        break;
    default:
        nav.navigation->setSearchLimitsAroundLine( 10_lnum, [] { return 5; } );
        break;
    }
    nav.settle();

    CHECK( nav.prompt.asked.isEmpty() );
    REQUIRE( nav.prompt.told.size() == 1 );
    CHECK( nav.prompt.told[ 0 ].first == title );
    CHECK( nav.prompt.told[ 0 ].second == "No Log Line near the current one has a timestamp." );
}

TEST_CASE( "Search Limits to a time range come out as each of the seven outcomes",
           "[ui][timenavigation]" )
{
    Navigation nav;
    const auto rangeGives = [ & ]( const QString& start, const QString& end ) {
        nav.prompt.text( start ).text( end );
        nav.navigation->setSearchLimitsToTimeRange( 0_lnum );
        nav.settle();
        CHECK( nav.prompt.asked.size() == 2 );
    };
    const auto toldOnly = [ & ]( const char* text ) {
        CHECK( nav.sink.limits.empty() );
        REQUIRE( nav.prompt.told.size() == 1 );
        CHECK( nav.prompt.told[ 0 ].first == "Set search limits by time" );
        CHECK( nav.prompt.told[ 0 ].second.contains( text ) );
    };

    SECTION( "Limits" )
    {
        rangeGives( "12:10:00", "12:20:00" );
        CHECK( nav.sink.limits == std::vector{ Limits{ 60_lnum, 120_lnum } } );
        CHECK( nav.prompt.told.empty() );
    }

    SECTION( "Before the Log File" )
    {
        rangeGives( "11:00:00", "11:10:00" );
        toldOnly( "before the first timestamp" );
    }

    SECTION( "After the Log File" )
    {
        rangeGives( "13:00:00", "13:10:00" );
        toldOnly( "after the last timestamp" );
    }

    SECTION( "No Timestamps" )
    {
        // Taken from the source after the prompts, not before them.
        nav.prompt.text( "12:10:00" ).text( "12:20:00", [ & ] {
            nav.logData = nav.gated( untimedLines() );
        } );
        nav.navigation->setSearchLimitsToTimeRange( 0_lnum );
        nav.settle();
        toldOnly( "No Log Line has a timestamp this Log Format can read." );
    }

    SECTION( "The end not after the start" )
    {
        rangeGives( "12:20:00", "12:10:00" );
        toldOnly( "The end is not after the start." );
    }

    SECTION( "No Log Line in the range" )
    {
        rangeGives( "12:00:01", "12:00:05" );
        toldOnly( "No Log Line has a timestamp in the time range." );
    }

    SECTION( "Cancelled" )
    {
        // The lookup of the range reads slowly, and the Log File is reloaded
        // meanwhile: nothing comes of it.
        nav.prompt.text( "12:10:00" ).text( "12:20:00", [ & ] { nav.logData->open = false; } );
        nav.navigation->setSearchLimitsToTimeRange( 0_lnum );
        QElapsedTimer timer;
        timer.start();
        while ( nav.prompt.asked.size() < 2 && timer.elapsed() < 10'000 ) {
            QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
        }
        REQUIRE( nav.navigation->isLookingUp() );
        nav.navigation->reloaded();
        nav.drain();
        CHECK( nav.sink.limits.empty() );
        CHECK( nav.prompt.told.empty() );
        CHECK( nav.sink.statuses.last().isEmpty() );
    }
}

TEST_CASE( "A time range that is no time is said", "[ui][timenavigation]" )
{
    Navigation nav;
    nav.prompt.text( "12:10:00" ).text( "later" );
    nav.navigation->setSearchLimitsToTimeRange( 0_lnum );
    nav.settle();
    CHECK( nav.sink.limits.empty() );
    REQUIRE( nav.prompt.told.size() == 1 );
    CHECK( nav.prompt.told[ 0 ].second.startsWith( "\"later\" is not a time." ) );
}

TEST_CASE( "Search Limits around a Log Line take the minutes before and after it",
           "[ui][timenavigation]" )
{
    Navigation nav;

    SECTION( "Other minutes are handed out to be remembered" )
    {
        nav.prompt.number( 2 );
        // Line 150 is at 12:25:00.
        nav.navigation->setSearchLimitsAroundLine( 150_lnum, [] { return 5; } );
        nav.settle();
        CHECK( nav.prompt.offeredNumbers == std::vector{ 5 } );
        // Two minutes are twelve lines, each way.
        CHECK( nav.sink.limits == std::vector{ Limits{ 138_lnum, 162_lnum } } );
        CHECK( nav.sink.windowsChosen == std::vector{ 2 } );
    }

    SECTION( "The same minutes are not handed out" )
    {
        nav.prompt.number( 5 );
        nav.navigation->setSearchLimitsAroundLine( 150_lnum, [] { return 5; } );
        nav.settle();
        CHECK( nav.sink.limits == std::vector{ Limits{ 120_lnum, 180_lnum } } );
        CHECK( nav.sink.windowsChosen.empty() );
    }

    SECTION( "A cancelled prompt does nothing" )
    {
        nav.prompt.cancel();
        nav.navigation->setSearchLimitsAroundLine( 150_lnum, [] { return 5; } );
        nav.settle();
        CHECK( nav.sink.limits.empty() );
        CHECK( nav.sink.windowsChosen.empty() );
    }
}

TEST_CASE( "The source is taken again after a prompt", "[ui][timenavigation]" )
{
    Navigation nav;
    const auto dropFormat = [ & ] { nav.format.reset(); };

    SECTION( "Go to timestamp" )
    {
        nav.prompt.text( "12:10:00", dropFormat );
        nav.navigation->goToTimestamp( 0_lnum );
    }
    SECTION( "Search Limits to a time range" )
    {
        nav.prompt.text( "12:10:00", dropFormat ).text( "12:20:00" );
        nav.navigation->setSearchLimitsToTimeRange( 0_lnum );
    }
    SECTION( "Search Limits around a Log Line" )
    {
        // Forgotten after the lookup of the Timestamp near the line was
        // started, before the minutes are asked for.
        nav.prompt.steps.push_back( { std::nullopt, 2, dropFormat } );
        nav.navigation->setSearchLimitsAroundLine( 150_lnum, [] { return 5; } );
    }
    const auto takenBefore = nav.sourcesTaken;
    nav.settle();

    // Whatever the Log Format was before the prompt, none is read after it.
    CHECK( nav.sourcesTaken > takenBefore );
    CHECK_FALSE( nav.navigation->isLookingUp() );
    CHECK( nav.sink.shown.empty() );
    CHECK( nav.sink.limits.empty() );
    CHECK( nav.prompt.told.empty() );
}

TEST_CASE( "Each event that makes a lookup stale cancels it", "[ui][timenavigation]" )
{
    Navigation nav;
    // The lookup of the Timestamp near the line waits until the event came.
    nav.logData->open = false;
    nav.navigation->goToTimestamp( 0_lnum );
    REQUIRE( nav.navigation->isLookingUp() );
    CHECK( nav.sink.statuses == QStringList{ "Looking up the time..." } );

    const auto event = GENERATE( 0, 1, 2, 3, 4 );
    switch ( event ) {
    case 0:
        nav.navigation->reloaded();
        break;
    case 1:
        nav.navigation->loaded( false );
        break;
    case 2:
        nav.navigation->truncated();
        break;
    case 3:
        nav.navigation->formatChanged();
        break;
    default:
        nav.navigation->formatReset();
        break;
    }
    CHECK_FALSE( nav.navigation->isLookingUp() );
    CHECK( nav.sink.statuses == QStringList{ "Looking up the time...", "" } );

    nav.drain();
    // The lookup reported nothing: no prompt followed it.
    CHECK( nav.prompt.asked.isEmpty() );
    CHECK( nav.prompt.told.empty() );
}

TEST_CASE( "A load that only appended leaves a lookup running", "[ui][timenavigation]" )
{
    Navigation nav;
    nav.logData->open = false;
    nav.prompt.text( "12:10:00" );
    nav.navigation->goToTimestamp( 0_lnum );
    nav.navigation->loaded( true );
    CHECK( nav.navigation->isLookingUp() );

    nav.logData->open = true;
    nav.settle();
    CHECK( nav.prompt.asked.size() == 1 );
    CHECK( nav.sink.shown == std::vector{ 60_lnum } );
}

TEST_CASE( "Time navigation says why it is not available", "[ui][timenavigation]" )
{
    Navigation nav;
    CHECK( nav.navigation->goToTimestampUnavailableReason().isEmpty() );
    CHECK( nav.navigation->searchLimitsByTimeUnavailableReason().isEmpty() );

    SECTION( "Without a Log Format" )
    {
        nav.format.reset();
        CHECK( nav.navigation->goToTimestampUnavailableReason()
               == "Go to timestamp needs a Log Format: none was recognized for this Log File." );
        CHECK( nav.navigation->searchLimitsByTimeUnavailableReason().startsWith(
            "Search limits by time need a Log Format" ) );
    }

    SECTION( "With a Log Format without a timestamp field" )
    {
        nav.format = untimedFormat();
        CHECK( nav.navigation->goToTimestampUnavailableReason().contains( "\"Untimed\"" ) );
        CHECK( nav.navigation->searchLimitsByTimeUnavailableReason().contains( "\"Untimed\"" ) );
    }

    // Nothing is asked or looked up then.
    nav.navigation->goToTimestamp( 0_lnum );
    nav.navigation->setSearchLimitsToTimeRange( 0_lnum );
    nav.navigation->setSearchLimitsAroundLine( 0_lnum, [] { return 5; } );
    CHECK_FALSE( nav.navigation->isLookingUp() );
    CHECK( nav.sink.statuses.isEmpty() );
}

TEST_CASE( "The minutes around a Log Line are those remembered when the prompt opens",
           "[ui][timenavigation]" )
{
    Navigation nav;
    int remembered = 5;
    nav.prompt.number( 7 );
    nav.navigation->setSearchLimitsAroundLine( 150_lnum, [ & ] { return remembered; } );
    // Another tab remembers other minutes while the Timestamp is looked up.
    remembered = 7;
    nav.settle();
    CHECK( nav.prompt.offeredNumbers == std::vector{ 7 } );
    CHECK( nav.sink.windowsChosen.empty() );
}

TEST_CASE( "A lookup that lands among Timestamps out of time order says so",
           "[ui][timenavigation]" )
{
    // Around line 60 (12:10:00) two Log Lines are swapped.
    auto lines = timedLines();
    lines.swapItemsAt( 56, 64 );
    Navigation nav;
    nav.logData = nav.gated( lines );

    SECTION( "Go to timestamp" )
    {
        nav.prompt.text( "12:10:00" );
        nav.navigation->goToTimestamp( 0_lnum );
        nav.settle();
        CHECK( nav.sink.shown.size() == 1 );
    }
    SECTION( "Search Limits" )
    {
        nav.prompt.text( "12:10:00" ).text( "12:20:00" );
        nav.navigation->setSearchLimitsToTimeRange( 0_lnum );
        nav.settle();
        CHECK( nav.sink.limits.size() == 1 );
    }
    CHECK( nav.sink.statuses.last() == TimeNavigation::notInTimeOrderNotice() );
    CHECK( nav.prompt.told.empty() );
}

TEST_CASE( "A Time Navigation destroyed while a prompt is open goes no further",
           "[ui][timenavigation]" )
{
    Navigation nav;
    // The tab is closed while the dialog is open.
    const auto close = [ & ] { nav.navigation.reset(); };

    SECTION( "Go to timestamp" )
    {
        nav.prompt.text( "12:10:00", close );
        nav.navigation->goToTimestamp( 0_lnum );
    }
    SECTION( "Search Limits to a time range" )
    {
        nav.prompt.text( "12:10:00", close ).text( "12:20:00" );
        nav.navigation->setSearchLimitsToTimeRange( 0_lnum );
    }
    SECTION( "Search Limits around a Log Line" )
    {
        nav.prompt.steps.push_back( { std::nullopt, 2, close } );
        nav.navigation->setSearchLimitsAroundLine( 150_lnum, [] { return 5; } );
    }
    QElapsedTimer timer;
    timer.start();
    while ( nav.navigation && timer.elapsed() < 10'000 ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    }
    REQUIRE_FALSE( nav.navigation );
    nav.drain();

    CHECK( nav.prompt.asked.size() == 1 );
    CHECK( nav.prompt.told.empty() );
    CHECK( nav.sink.shown.empty() );
    CHECK( nav.sink.limits.empty() );
    CHECK( nav.sink.windowsChosen.empty() );
    // Only the lookup of the Timestamp near the line said anything.
    CHECK( nav.sink.statuses == QStringList{ "Looking up the time...", "" } );
}
