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

#include "stdinpump.h"

#include "log.h"
#include "streamwriter.h"

#include <QtGlobal>

#include <array>
#include <chrono>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

namespace logsquirl::plugins {

namespace {

// How often a waiting reader looks at the stop flag.
constexpr int StopCheckMilliseconds = 100;

enum class ReadResult { Data, Closed, Timeout };

#ifdef Q_OS_WIN

ReadResult readSome( int fd, char* buffer, size_t size, size_t& received )
{
    const auto handle = reinterpret_cast<HANDLE>( _get_osfhandle( fd ) );
    if ( handle == INVALID_HANDLE_VALUE ) {
        return ReadResult::Closed;
    }

    // A pipe can be asked whether anything is waiting; a file or the console
    // is read directly.
    if ( GetFileType( handle ) == FILE_TYPE_PIPE ) {
        DWORD available = 0;
        if ( !PeekNamedPipe( handle, nullptr, 0, nullptr, &available, nullptr ) ) {
            return ReadResult::Closed;
        }
        if ( available == 0 ) {
            Sleep( 20 );
            return ReadResult::Timeout;
        }
    }

    DWORD count = 0;
    if ( !ReadFile( handle, buffer, static_cast<DWORD>( size ), &count, nullptr ) || count == 0 ) {
        return ReadResult::Closed;
    }
    received = count;
    return ReadResult::Data;
}

#else

ReadResult readSome( int fd, char* buffer, size_t size, size_t& received )
{
    pollfd descriptor{ fd, POLLIN, 0 };
    const auto ready = ::poll( &descriptor, 1, StopCheckMilliseconds );
    if ( ready < 0 ) {
        return errno == EINTR ? ReadResult::Timeout : ReadResult::Closed;
    }
    if ( ready == 0 ) {
        return ReadResult::Timeout;
    }

    const auto count = ::read( fd, buffer, size );
    if ( count < 0 ) {
        return errno == EINTR || errno == EAGAIN ? ReadResult::Timeout : ReadResult::Closed;
    }
    if ( count == 0 ) {
        return ReadResult::Closed;
    }
    received = static_cast<size_t>( count );
    return ReadResult::Data;
}

#endif

} // namespace

bool isTerminal( int fd )
{
#ifdef Q_OS_WIN
    return _isatty( fd ) != 0;
#else
    return ::isatty( fd ) != 0;
#endif
}

StdinPump::StdinPump( int fd, StreamWriter& writer, std::function<void()> onClosed )
    : fd_( fd )
    , writer_( writer )
    , onClosed_( std::move( onClosed ) )
    , thread_( [ this ] { run(); } )
{
}

StdinPump::~StdinPump()
{
    stop_ = true;
    thread_.join();
}

void StdinPump::run()
{
    std::array<char, 64 * 1024> buffer{};
    while ( !stop_ ) {
        size_t received = 0;
        const auto result = readSome( fd_, buffer.data(), buffer.size(), received );
        if ( result == ReadResult::Timeout ) {
            continue;
        }
        if ( result == ReadResult::Closed ) {
            break;
        }
        writer_.pushBytes( buffer.data(), received );
    }

    if ( stop_ ) {
        return;
    }

    LOG_INFO << "Standard input closed";
    writer_.signalEos();
    if ( onClosed_ ) {
        onClosed_();
    }
}

} // namespace logsquirl::plugins
