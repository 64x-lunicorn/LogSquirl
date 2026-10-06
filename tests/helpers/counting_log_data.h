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

#ifndef COUNTING_LOG_DATA_H
#define COUNTING_LOG_DATA_H

#include <cstdint>

#include <QString>

#include "fake_log_data.h"

// A FakeLogData that counts the Log Lines read from it: every read of a Log
// Line, alone, expanded or with others, reads it with doGetLineString().
// It also counts the fetches of several Log Lines at once, expanded or not,
// for a test that counts how often a view goes to its Log File rather than how
// much it reads.
class CountingLogData : public FakeLogData {
public:
    using FakeLogData::FakeLogData;

    // Every Log Line read.
    mutable uint64_t linesRead = 0;
    // Every fetch of several Log Lines at once: one per fetch, however many
    // Log Lines it holds.
    mutable uint64_t fetches = 0;
    // The fetches that start at the first Log Line of the Log File.
    mutable uint64_t fetchesFromFirstLogLine = 0;

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        ++linesRead;
        return FakeLogData::doGetLineString( line );
    }
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        ++fetches;
        if ( first == 0_lnum ) {
            ++fetchesFromFirstLogLine;
        }
        return FakeLogData::doGetLines( first, count );
    }
};

#endif
