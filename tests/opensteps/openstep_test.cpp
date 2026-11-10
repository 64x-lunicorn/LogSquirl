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

// The order of the steps that open a Log File, without a window (#949): one
// test per fix that gave a step its place.

#include <catch2/catch_test_macros.hpp>

#include "openstep.h"

namespace {

// What a window knows of an Ordinary Log File it was just asked to open,
// with the plugins loaded and nothing else learned yet.
OpenFacts justAsked()
{
    return OpenFacts{ .pluginsLoaded = true };
}

} // namespace

TEST_CASE( "A Log File asked for before the plugins loaded waits for them (#303)", "[openstep]" )
{
    auto known = justAsked();
    known.pluginsLoaded = false;
    // Whatever else is known: a converter may yet be there for it.
    known.alreadyOpen = true;
    known.extensionConverter = true;
    known.decompressAction = DecompressAction::Decompress;

    CHECK( nextOpenStep( known ) == OpenStep::WaitForPlugins );
}

TEST_CASE( "A Log File open or converted in a window shows its tab (#615)", "[openstep]" )
{
    auto known = justAsked();
    known.alreadyOpen = true;
    known.extensionConverter = true;

    CHECK( nextOpenStep( known ) == OpenStep::ShowOpenTab );

    known.lifetime = LogFileLifetime::Transient;
    CHECK( nextOpenStep( known ) == OpenStep::ShowOpenTab );
}

TEST_CASE( "A converter for the extension converts before any content claim is asked",
           "[openstep]" )
{
    auto known = justAsked();
    known.extensionConverter = true;

    CHECK( nextOpenStep( known ) == OpenStep::ConvertByExtension );

    // Not even a claim already answered goes first.
    known.contentClaim = ContentClaim::Claimed;
    CHECK( nextOpenStep( known ) == OpenStep::ConvertByExtension );
}

TEST_CASE( "A Transient Log File is never offered for a content claim (ADR 0028, #861)",
           "[openstep]" )
{
    auto known = justAsked();
    known.lifetime = LogFileLifetime::Transient;

    CHECK( nextOpenStep( known ) == OpenStep::LookForCompression );

    known.decompressAction = DecompressAction::None;
    CHECK( nextOpenStep( known ) == OpenStep::Open );

    known.decompressAction = DecompressAction::Decompress;
    CHECK( nextOpenStep( known ) == OpenStep::Decompress );
}

TEST_CASE( "A content claim is asked before the Log File is decompressed (#861)", "[openstep]" )
{
    auto known = justAsked();
    known.decompressAction = DecompressAction::Extract;

    CHECK( nextOpenStep( known ) == OpenStep::AskContentClaim );

    SECTION( "Claimed, it is converted, not decompressed" )
    {
        known.contentClaim = ContentClaim::Claimed;
        CHECK( nextOpenStep( known ) == OpenStep::ConvertByClaim );
    }
    SECTION( "Not claimed, it is decompressed then" )
    {
        known.contentClaim = ContentClaim::NotClaimed;
        CHECK( nextOpenStep( known ) == OpenStep::Decompress );
    }
}

TEST_CASE( "A Log File opened meanwhile while the claim was pending shows its tab (#861)",
           "[openstep]" )
{
    auto known = justAsked();
    known.alreadyOpen = true;

    known.contentClaim = ContentClaim::Claimed;
    CHECK( nextOpenStep( known ) == OpenStep::ShowOpenTab );

    known.contentClaim = ContentClaim::NotClaimed;
    CHECK( nextOpenStep( known ) == OpenStep::ShowOpenTab );
}

TEST_CASE( "A compressed Log File opens as it is when archives are not extracted", "[openstep]" )
{
    auto known = justAsked();
    known.contentClaim = ContentClaim::NotClaimed;
    known.extractArchives = false;

    known.decompressAction = DecompressAction::Decompress;
    CHECK( nextOpenStep( known ) == OpenStep::Open );

    known.decompressAction = DecompressAction::Extract;
    CHECK( nextOpenStep( known ) == OpenStep::Open );

    known.extractArchives = true;
    CHECK( nextOpenStep( known ) == OpenStep::Decompress );
}

TEST_CASE( "A Log File no converter takes is looked at, then opened as it is", "[openstep]" )
{
    auto known = justAsked();
    known.contentClaim = ContentClaim::NotClaimed;

    CHECK( nextOpenStep( known ) == OpenStep::LookForCompression );

    known.decompressAction = DecompressAction::None;
    CHECK( nextOpenStep( known ) == OpenStep::Open );
}
