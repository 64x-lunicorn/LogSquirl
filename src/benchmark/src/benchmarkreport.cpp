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

#include "benchmarkreport.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace logsquirl::benchmark {

namespace {

// The nearest-rank percentile of samples sorted ascending, which are not
// empty.
double percentile( const std::vector<double>& sorted, double percent )
{
    const auto count = static_cast<double>( sorted.size() );
    const auto rank = static_cast<std::size_t>( std::ceil( percent / 100.0 * count ) );
    const auto index = std::clamp<std::size_t>( rank, 1, sorted.size() ) - 1;
    return sorted[ index ];
}

} // namespace

Distribution Distribution::of( std::vector<double> milliseconds )
{
    Distribution distribution;
    distribution.count = milliseconds.size();
    if ( milliseconds.empty() ) {
        return distribution;
    }

    std::sort( milliseconds.begin(), milliseconds.end() );
    distribution.min = milliseconds.front();
    distribution.max = milliseconds.back();
    distribution.p50 = percentile( milliseconds, 50.0 );
    distribution.p99 = percentile( milliseconds, 99.0 );
    distribution.mean = std::accumulate( milliseconds.begin(), milliseconds.end(), 0.0 )
                        / static_cast<double>( milliseconds.size() );
    return distribution;
}

QJsonObject Distribution::toJson() const
{
    QJsonObject json{ { "count", static_cast<qint64>( count ) } };
    if ( count == 0 ) {
        return json;
    }
    json.insert( "min_ms", min );
    json.insert( "p50_ms", p50 );
    json.insert( "p99_ms", p99 );
    json.insert( "max_ms", max );
    json.insert( "mean_ms", mean );
    return json;
}

BenchmarkReport::BenchmarkReport( QString scenario, ProcessClock clock )
    : scenario_( std::move( scenario ) )
    , clock_( clock )
    , scenarioStart_( Clock::now() )
{
}

void BenchmarkReport::markScenarioStart( Clock::time_point moment )
{
    scenarioStart_ = moment;
}

void BenchmarkReport::event( const QString& name, const QJsonObject& data )
{
    eventAt( name, Clock::now(), data );
}

void BenchmarkReport::eventAt( const QString& name, Clock::time_point moment,
                               const QJsonObject& data )
{
    // Kept in the order they happened, whatever order they are reported in.
    const auto after = std::upper_bound(
        events_.begin(), events_.end(), moment,
        []( Clock::time_point value, const Event& event ) { return value < event.moment; } );
    events_.insert( after, Event{ name, moment, data } );
}

bool BenchmarkReport::hasEvent( const QString& name ) const
{
    return millisecondsSinceScenarioStart( name ).has_value();
}

std::optional<double> BenchmarkReport::millisecondsSinceScenarioStart( const QString& name ) const
{
    const auto found
        = std::find_if( events_.begin(), events_.end(),
                        [ &name ]( const Event& event ) { return event.name == name; } );
    if ( found == events_.end() ) {
        return std::nullopt;
    }
    return milliseconds( found->moment - scenarioStart_ );
}

void BenchmarkReport::setResult( const QString& key, const QJsonValue& value )
{
    results_.insert( key, value );
}

void BenchmarkReport::setOption( const QString& key, const QString& value )
{
    options_.insert( key, value );
}

void BenchmarkReport::addLogFile( const QString& path, qint64 sizeBytes )
{
    logFiles_.append( QJsonObject{ { "path", path }, { "size_bytes", sizeBytes } } );
}

void BenchmarkReport::setApplication( const QJsonObject& application )
{
    application_ = application;
}

void BenchmarkReport::setPlatform( const QJsonObject& platform )
{
    platform_ = platform;
}

void BenchmarkReport::fail( const QString& reason )
{
    if ( !failure_ ) {
        failure_ = reason;
    }
}

bool BenchmarkReport::failed() const
{
    return failure_.has_value();
}

QJsonObject BenchmarkReport::toJson( std::optional<std::int64_t> peakResidentBytes ) const
{
    QJsonObject process{
        { "start_source", clock_.knowsProcessStart() ? "os" : "main" },
        { "main_entered_ms", clock_.mainEnteredMilliseconds() },
    };
    if ( peakResidentBytes ) {
        process.insert( "peak_rss_bytes", static_cast<qint64>( *peakResidentBytes ) );
    }

    QJsonArray events;
    for ( const auto& event : events_ ) {
        QJsonObject json{
            { "name", event.name },
            { "since_process_start_ms", clock_.millisecondsSinceProcessStart( event.moment ) },
            { "since_scenario_start_ms", milliseconds( event.moment - scenarioStart_ ) },
        };
        if ( !event.data.isEmpty() ) {
            json.insert( "data", event.data );
        }
        events.append( json );
    }

    QJsonObject report{
        { "format", FormatName },
        { "format_version", FormatVersion },
        { "scenario", scenario_ },
        { "outcome", failure_ ? "failed" : "passed" },
        { "application", application_ },
        { "platform", platform_ },
        { "options", options_ },
        { "log_files", logFiles_ },
        { "process", process },
        { "scenario_started_ms", clock_.millisecondsSinceProcessStart( scenarioStart_ ) },
        { "events", events },
        { "results", results_ },
    };
    if ( failure_ ) {
        report.insert( "failure", *failure_ );
    }
    return report;
}

} // namespace logsquirl::benchmark
