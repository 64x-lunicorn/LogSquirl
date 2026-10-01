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

#include "processwork.h"

#include <cstdint>

#include <QtGlobal>

#if defined( Q_OS_WIN )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace logsquirl::benchmark {

namespace {

double milliseconds( std::chrono::nanoseconds duration )
{
    return std::chrono::duration<double, std::milli>( duration ).count();
}

#if defined( Q_OS_WIN )
// A FILETIME of a duration counts 100 ns intervals.
std::chrono::nanoseconds fromFileTime( const FILETIME& time )
{
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return std::chrono::nanoseconds{ static_cast<std::int64_t>( value.QuadPart ) * 100 };
}
#endif

} // namespace

std::optional<std::chrono::nanoseconds> processCpuTime()
{
#if defined( Q_OS_WIN )
    FILETIME creation;
    FILETIME exit;
    FILETIME kernel;
    FILETIME user;
    if ( !GetProcessTimes( GetCurrentProcess(), &creation, &exit, &kernel, &user ) ) {
        return std::nullopt;
    }
    return fromFileTime( kernel ) + fromFileTime( user );
#else
    struct timespec spent = {};
    if ( clock_gettime( CLOCK_PROCESS_CPUTIME_ID, &spent ) != 0 ) {
        return std::nullopt;
    }
    return std::chrono::seconds{ spent.tv_sec } + std::chrono::nanoseconds{ spent.tv_nsec };
#endif
}

ProcessWork ProcessWork::between( Clock::time_point started, Clock::time_point ended,
                                  std::optional<std::chrono::nanoseconds> cpuStarted,
                                  std::optional<std::chrono::nanoseconds> cpuEnded )
{
    ProcessWork work;
    work.wallMs = milliseconds( ended - started );
    if ( cpuStarted && cpuEnded ) {
        work.cpuMs = milliseconds( *cpuEnded - *cpuStarted );
    }
    return work;
}

std::optional<double> ProcessWork::parallelism() const
{
    if ( !cpuMs || wallMs <= 0.0 ) {
        return std::nullopt;
    }
    return *cpuMs / wallMs;
}

QJsonObject ProcessWork::toJson() const
{
    QJsonObject json{ { "wall_ms", wallMs } };
    if ( cpuMs ) {
        json.insert( "cpu_ms", *cpuMs );
    }
    if ( const auto ratio = parallelism() ) {
        json.insert( "parallelism", *ratio );
    }
    return json;
}

ProcessWork::Moment ProcessWork::Moment::now()
{
    return Moment{ Clock::now(), processCpuTime() };
}

ProcessWork ProcessWork::Moment::until( const Moment& ended ) const
{
    return between( wall, ended.wall, cpu, ended.cpu );
}

} // namespace logsquirl::benchmark
