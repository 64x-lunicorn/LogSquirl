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

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace logsquirl::plugins {

class StreamWriter;

/** The positional command line arguments, split into "-" and file names. */
struct PositionalArguments {
    /** True when "-" was among them: read standard input. */
    bool readStdin = false;
    /** Everything else, in order. */
    std::vector<QString> files;
};

// Header-only: the command line parser of logsquirl_grep shares this header
// and does not link the plugin layer.
inline PositionalArguments splitPositionalArguments( const QStringList& arguments )
{
    PositionalArguments result;
    for ( const auto& argument : arguments ) {
        if ( argument == QLatin1String( "-" ) ) {
            result.readStdin = true;
        }
        else {
            result.files.push_back( argument );
        }
    }
    return result;
}

/** True when the file descriptor is attached to a terminal. */
bool isTerminal( int fd );

/**
 * Copies what arrives on a file descriptor (standard input) into a
 * StreamWriter, byte for byte, on a background thread.
 *
 * When the writing end closes, the writer is told the stream is complete and
 * the callback runs -- on the background thread. Destroying the pump stops
 * the thread; it does not wait for the writing end to close.
 */
class StdinPump {
public:
    StdinPump( int fd, StreamWriter& writer, std::function<void()> onClosed );
    ~StdinPump();

    StdinPump( const StdinPump& ) = delete;
    StdinPump& operator=( const StdinPump& ) = delete;

private:
    void run();

    int fd_;
    StreamWriter& writer_;
    std::function<void()> onClosed_;
    std::atomic_bool stop_ = false;
    std::thread thread_;
};

} // namespace logsquirl::plugins
