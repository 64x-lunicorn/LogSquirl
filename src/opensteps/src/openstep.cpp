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

#include "openstep.h"

OpenStep nextOpenStep( const OpenFacts& known )
{
    // Whether a converter plugin opens the file is only known once the
    // plugins have loaded (#303).
    if ( !known.pluginsLoaded ) {
        return OpenStep::WaitForPlugins;
    }
    // Open or converted in any window, its tab is shown; on its way, it is
    // not opened twice (#615, #795, #861). Asked again once a content claim
    // was answered: it may have been opened meanwhile.
    if ( known.alreadyOpen ) {
        return OpenStep::ShowOpenTab;
    }
    if ( known.extensionConverter ) {
        return OpenStep::ConvertByExtension;
    }
    // Only an Ordinary Log File is offered to be claimed by its content, and
    // as it is on disk: before it is decompressed (#861, ADR 0028). A
    // Transient one is LogSquirl's own.
    if ( known.lifetime == LogFileLifetime::Ordinary ) {
        switch ( known.contentClaim ) {
        case ContentClaim::NotAsked:
            return OpenStep::AskContentClaim;
        case ContentClaim::Claimed:
            return OpenStep::ConvertByClaim;
        case ContentClaim::NotClaimed:
            break;
        }
    }
    if ( !known.decompressAction.has_value() ) {
        return OpenStep::LookForCompression;
    }
    // A compressed file or an archive opens as it is when the File Access
    // Policy does not have archives extracted.
    if ( *known.decompressAction != DecompressAction::None && known.extractArchives ) {
        return OpenStep::Decompress;
    }
    return OpenStep::Open;
}
