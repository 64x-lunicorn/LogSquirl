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

#include "processclock.h"

#include <algorithm>

#include <QtGlobal>

#if defined( Q_OS_WIN )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h, which it needs.
#include <psapi.h>
#elif defined( Q_OS_MACOS )
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>
#else
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#endif

namespace logsquirl::benchmark {

namespace {

#if defined( Q_OS_WIN )
// A FILETIME counts 100 ns intervals.
std::chrono::nanoseconds fromFileTime( const FILETIME& time )
{
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return std::chrono::nanoseconds{ static_cast<std::int64_t>( value.QuadPart ) * 100 };
}
#endif

} // namespace

std::optional<std::chrono::nanoseconds> timeSinceProcessStart()
{
#if defined( Q_OS_WIN )
    FILETIME creation;
    FILETIME exit;
    FILETIME kernel;
    FILETIME user;
    if ( !GetProcessTimes( GetCurrentProcess(), &creation, &exit, &kernel, &user ) ) {
        return std::nullopt;
    }
    FILETIME now;
    GetSystemTimePreciseAsFileTime( &now );
    return fromFileTime( now ) - fromFileTime( creation );
#elif defined( Q_OS_MACOS )
    int request[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>( getpid() ) };
    struct kinfo_proc info = {};
    size_t size = sizeof( info );
    if ( sysctl( request, 4, &info, &size, nullptr, 0 ) != 0 || size == 0 ) {
        return std::nullopt;
    }
    struct timeval now = {};
    gettimeofday( &now, nullptr );
    const auto& started = info.kp_proc.p_starttime;
    return std::chrono::seconds{ now.tv_sec - started.tv_sec }
           + std::chrono::microseconds{ now.tv_usec - started.tv_usec };
#else
    // Field 22 of /proc/self/stat is the start time in clock ticks since the
    // system booted. The second field, the command in parentheses, may hold
    // spaces, so the fields are counted from its closing parenthesis.
    std::ifstream stat( "/proc/self/stat" );
    const std::string content{ std::istreambuf_iterator<char>( stat ),
                               std::istreambuf_iterator<char>() };
    const auto commandEnd = content.rfind( ')' );
    if ( commandEnd == std::string::npos ) {
        return std::nullopt;
    }
    std::istringstream fields( content.substr( commandEnd + 1 ) );
    // After the command: field 3, the state, up to field 22.
    std::string field;
    for ( int number = 3; number <= 22; ++number ) {
        if ( !( fields >> field ) ) {
            return std::nullopt;
        }
    }
    const auto ticksPerSecond = sysconf( _SC_CLK_TCK );
    if ( ticksPerSecond <= 0 ) {
        return std::nullopt;
    }
    const auto startTicks = std::stoll( field );

    struct timespec sinceBoot = {};
    if ( clock_gettime( CLOCK_BOOTTIME, &sinceBoot ) != 0 ) {
        return std::nullopt;
    }
    const auto now
        = std::chrono::seconds{ sinceBoot.tv_sec } + std::chrono::nanoseconds{ sinceBoot.tv_nsec };
    const auto started = std::chrono::nanoseconds{ startTicks * 1'000'000'000LL / ticksPerSecond };
    return std::chrono::duration_cast<std::chrono::nanoseconds>( now ) - started;
#endif
}

std::optional<std::int64_t> peakResidentBytes()
{
#if defined( Q_OS_WIN )
    PROCESS_MEMORY_COUNTERS counters = {};
    if ( !GetProcessMemoryInfo( GetCurrentProcess(), &counters,
                                static_cast<DWORD>( sizeof( counters ) ) ) ) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>( counters.PeakWorkingSetSize );
#else
    struct rusage usage = {};
    if ( getrusage( RUSAGE_SELF, &usage ) != 0 ) {
        return std::nullopt;
    }
#if defined( Q_OS_MACOS )
    // In bytes on macOS.
    return static_cast<std::int64_t>( usage.ru_maxrss );
#else
    // In kilobytes on Linux.
    return static_cast<std::int64_t>( usage.ru_maxrss ) * 1024;
#endif
#endif
}

ProcessClock ProcessClock::measure( Clock::time_point mainEntered )
{
    const auto now = Clock::now();
    const auto sinceStart = timeSinceProcessStart();
    if ( !sinceStart ) {
        return ProcessClock{ mainEntered, std::nullopt };
    }
    const auto mainEnteredSinceStart
        = *sinceStart - std::chrono::duration_cast<std::chrono::nanoseconds>( now - mainEntered );
    // The process start is known to a clock tick on Linux; main() is not
    // placed before it.
    return ProcessClock{ mainEntered,
                         std::max( mainEnteredSinceStart, std::chrono::nanoseconds{ 0 } ) };
}

ProcessClock::ProcessClock( Clock::time_point mainEntered,
                            std::optional<std::chrono::nanoseconds> mainEnteredSinceProcessStart )
    : mainEntered_( mainEntered )
    , mainEnteredSinceProcessStart_(
          mainEnteredSinceProcessStart.value_or( std::chrono::nanoseconds{ 0 } ) )
    , knowsProcessStart_( mainEnteredSinceProcessStart.has_value() )
{
}

double ProcessClock::millisecondsSinceProcessStart( Clock::time_point moment ) const
{
    const auto sinceStart = mainEnteredSinceProcessStart_ + ( moment - mainEntered_ );
    return std::chrono::duration<double, std::milli>( sinceStart ).count();
}

double ProcessClock::mainEnteredMilliseconds() const
{
    return std::chrono::duration<double, std::milli>( mainEnteredSinceProcessStart_ ).count();
}

bool ProcessClock::knowsProcessStart() const
{
    return knowsProcessStart_;
}

} // namespace logsquirl::benchmark
