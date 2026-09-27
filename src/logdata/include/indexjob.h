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

#ifndef LOGSQUIRL_INDEXJOB_H
#define LOGSQUIRL_INDEXJOB_H

#include <variant>

#include <QString>

class TextEncoding;

// The index jobs as values, apart from the worker that runs them: the log
// data queues them and the job rule weighs them without knowing how a Log
// File is indexed (#549).

// What asked for a Log File to be indexed in full, which decides how closely
// an Index the Index Cache hands out is checked against it (#337).
enum class FullIndexRequest {
    // A Log File is opened, or one being followed changed in the bytes it was
    // indexed from. A cached Index is checked by its header and tail, which
    // costs the same however large the Log File is.
    Automatic,
    // The user asked for the Log File to be read again. A cached Index is
    // then checked by the digest of every byte it was built from, so that a
    // Log File rewritten in place with the same size is noticed even where
    // its modification time is coarse or written late (#337).
    ExplicitReload,
};

// The index jobs the log data hands its worker, as values. Which one waits
// while another runs is the job rule's (waitingIndexJob(), logdataoperation.h).

// Attaching a Log File: its name is taken and it is indexed in full.
struct AttachJob {
    QString fileName;
    // From the File Access Policy the log data was built with; negative
    // means "detect it rather than force one".
    int defaultEncodingMib = -1;
    // Handed over by a reload that arrived while the Attach was waiting, and
    // indexed under instead of the default Encoding.
    const TextEncoding* forcedEncoding = nullptr;
};

// Indexing the Log File again in full. What asked for it is carried through
// to the run, which checks a cached Index the more closely the more the user
// asked for the Log File to be read again (#337).
struct FullReindexJob {
    FullIndexRequest request = FullIndexRequest::Automatic;
    const TextEncoding* forcedEncoding = nullptr;
};

// Indexing the Log Lines added since the end of the Log File as indexed.
struct PartialReindexJob {};

// Checking the Log File for changes on disk: growth, truncation or
// replacement.
struct CheckForChangesJob {};

// An index job, or nothing.
using IndexJob = std::variant<std::monostate, AttachJob, FullReindexJob, PartialReindexJob,
                              CheckForChangesJob>;

#endif // LOGSQUIRL_INDEXJOB_H
