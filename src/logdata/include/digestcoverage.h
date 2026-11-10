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

#include <QDateTime>

#include "indexedhash.h"
#include "indexjob.h"
#include "settingspolicies.h"

/// What asks whether an Index still fits its Log File, which decides how much
/// of the Log File is read to tell (digestCoverage()).
enum class IndexFitRequest {
    /// An Attach or a Full index run checks a cached Index: a Log File is
    /// opened, or one being followed changed in the bytes it was indexed
    /// from (FullIndexRequest::Automatic).
    OpenOrFollow,
    /// A Full index run checks a cached Index because the user asked for the
    /// Log File to be read again (FullIndexRequest::ExplicitReload).
    Reload,
    /// A Check of an Open Log File, after its Log File changed on disk.
    CheckForChanges,
};

/// The request a Full index run checks a cached Index for.
IndexFitRequest indexFitRequest( FullIndexRequest request );

/// The one rule that says how much of a Log File is read to tell whether its
/// Index still fits it (indexFit()), from what asks, the Indexing Policy, the
/// modification time the Index was last indexed or checked at (invalid when
/// unknown) and the Log File's modification time now. Reads no file.
DigestCoverage digestCoverage( IndexFitRequest request, const IndexingPolicy& indexingPolicy,
                               const QDateTime& indexedModificationTime,
                               const QDateTime& modificationTime );
