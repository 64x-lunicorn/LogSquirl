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

#ifndef LOGSQUIRL_INDEXJOBRUNNER_H
#define LOGSQUIRL_INDEXJOBRUNNER_H

#include "indexjob.h"

struct IndexingPolicy;

// Everything the operation queue needs from whoever runs the index jobs of a
// Log File: to start one, to interrupt the one in flight and to take a changed
// Indexing Policy. How a run ends is not reported through it: the log data
// hears that from the runner and tells the queue (#550).
//
// The index worker is the runner the log data hands the queue; the tests hand
// over a fake that records what it is asked to run.
class IndexJobRunner {
public:
    IndexJobRunner() = default;
    // The queue owns its runner and destroys it on shutdown.
    virtual ~IndexJobRunner() = default;

    IndexJobRunner( const IndexJobRunner& ) = delete;
    IndexJobRunner& operator=( const IndexJobRunner& ) = delete;
    IndexJobRunner( IndexJobRunner&& ) = delete;
    IndexJobRunner& operator=( IndexJobRunner&& ) = delete;

    // Starts running the index job and returns once it has started; the queue
    // starts one only once the one before is reported finished.
    virtual void run( const IndexJob& job ) = 0;

    // Replaces the Indexing Policy used by the runs requested from now on.
    virtual void setIndexingPolicy( const IndexingPolicy& indexingPolicy ) = 0;

    // Interrupts the index run in flight, if any, without waiting for it.
    virtual void interrupt() = 0;
};

#endif // LOGSQUIRL_INDEXJOBRUNNER_H
