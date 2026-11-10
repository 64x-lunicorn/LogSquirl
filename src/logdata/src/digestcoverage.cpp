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

#include "digestcoverage.h"

IndexFitRequest indexFitRequest( FullIndexRequest request )
{
    switch ( request ) {
    case FullIndexRequest::Automatic:
        return IndexFitRequest::OpenOrFollow;
    case FullIndexRequest::ExplicitReload:
        return IndexFitRequest::Reload;
    }
    return IndexFitRequest::OpenOrFollow;
}

DigestCoverage digestCoverage( IndexFitRequest request, const IndexingPolicy& indexingPolicy,
                               const QDateTime& indexedModificationTime,
                               const QDateTime& modificationTime )
{
    // Following a Log File asks for no more than the header and tail of a
    // cached Index, so what a growing Log File costs per change is what it
    // was.
    if ( request == IndexFitRequest::OpenOrFollow ) {
        return DigestCoverage::HeaderAndTail;
    }

    // Fast modification detection is the user saying they would rather not
    // pay for reading a Log File again to find out that it did not change.
    // No full digest is recorded then, so there is none to check against.
    if ( indexingPolicy.fastModificationDetection ) {
        return DigestCoverage::HeaderAndTail;
    }

    // Reloading is the user asking for the Log File to be read again, so the
    // bytes the cached Index was built from are read once more and checked
    // against their full digest, whatever the modification time says. That
    // is the only way a rewrite in place with the same size is noticed where
    // the file system's modification time is coarse or written late (#337).
    // A Log File that grew is left to the header and tail: going on from the
    // cached Index reads its indexed bytes again anyway, so checking them
    // first would read them twice, and a change in a Log File that also grew
    // is the risk #277 documented.
    if ( request == IndexFitRequest::Reload ) {
        return DigestCoverage::FullUnlessGrown;
    }

    // A Check still tells a Log File that grew from its header and tail, and
    // reads one that did not end to end only when it was modified since it
    // was last indexed or checked: following a Log File that grows by small
    // appends does not read all of it for every change (#277).
    if ( indexedModificationTime.isValid() && modificationTime == indexedModificationTime ) {
        return DigestCoverage::HeaderAndTail;
    }
    return DigestCoverage::FullUnlessGrown;
}
