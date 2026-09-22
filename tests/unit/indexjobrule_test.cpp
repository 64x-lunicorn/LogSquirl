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

#include <catch2/catch.hpp>

#include <string>
#include <vector>

#include <QTextCodec>

#include "logdataoperation.h"
#include "overload_visitor.h"

// The job rule (#395): of the index job waiting to run and one arriving, the
// one that waits from now on. Every pair of jobs is a row.

namespace {

constexpr auto DefaultEncodingMib = 106; // UTF-8

QTextCodec* latin1()
{
    return QTextCodec::codecForName( "ISO-8859-1" );
}

QTextCodec* utf16()
{
    return QTextCodec::codecForName( "UTF-16LE" );
}

IndexJob nothing()
{
    return std::monostate{};
}

IndexJob attach()
{
    return AttachJob{ "attached.log", DefaultEncodingMib };
}

IndexJob explicitReload( QTextCodec* forcedEncoding = nullptr )
{
    return FullReindexJob{ FullIndexRequest::ExplicitReload, forcedEncoding };
}

IndexJob automaticFull()
{
    return FullReindexJob{ FullIndexRequest::Automatic };
}

IndexJob check()
{
    return CheckForChangesJob{};
}

IndexJob partial()
{
    return PartialReindexJob{};
}

std::string encodingName( QTextCodec* encoding )
{
    return encoding ? " forcing " + encoding->name().toStdString() : std::string{};
}

// What a job is, in the words of the rule, with the Encoding it forces.
std::string describe( const IndexJob& job )
{
    return std::visit( makeOverloadVisitor(
                           []( std::monostate ) -> std::string { return "nothing"; },
                           []( const AttachJob& attach ) {
                               return "Attach " + attach.fileName.toStdString() + " default "
                                      + std::to_string( attach.defaultEncodingMib )
                                      + encodingName( attach.forcedEncoding );
                           },
                           []( const FullReindexJob& full ) {
                               return std::string{ full.request == FullIndexRequest::ExplicitReload
                                                       ? "Full (explicit reload)"
                                                       : "Full (automatic)" }
                                      + encodingName( full.forcedEncoding );
                           },
                           []( const CheckForChangesJob& ) -> std::string { return "Check"; },
                           []( const PartialReindexJob& ) -> std::string { return "Partial"; } ),
                       job );
}

struct Row {
    IndexJob waiting;
    IndexJob arriving;
    IndexJob waitsAfterwards;
};

} // namespace

SCENARIO( "Of two index jobs the stronger one waits", "[logdata][jobrule]" )
{
    const std::vector<Row> rows = {
        // Nothing waiting: the job arriving waits.
        { nothing(), attach(), attach() },
        { nothing(), explicitReload( latin1() ), explicitReload( latin1() ) },
        { nothing(), automaticFull(), automaticFull() },
        { nothing(), check(), check() },
        { nothing(), partial(), partial() },

        // An Attach indexes everything anyway.
        { attach(), attach(), attach() },
        { attach(), explicitReload(), attach() },
        { attach(), automaticFull(), attach() },
        { attach(), check(), attach() },
        { attach(), partial(), attach() },
        { explicitReload(), attach(), attach() },
        { automaticFull(), attach(), attach() },
        { check(), attach(), attach() },
        { partial(), attach(), attach() },

        // ... and takes the Encoding a reload forces, whichever came first.
        { attach(), explicitReload( latin1() ),
          AttachJob{ "attached.log", DefaultEncodingMib, latin1() } },
        { explicitReload( latin1() ), attach(),
          AttachJob{ "attached.log", DefaultEncodingMib, latin1() } },
        { AttachJob{ "attached.log", DefaultEncodingMib, latin1() }, explicitReload( utf16() ),
          AttachJob{ "attached.log", DefaultEncodingMib, utf16() } },

        // An explicit reload beats an automatic Full; of two reloads the
        // later one, and its Encoding, waits.
        { explicitReload( latin1() ), explicitReload( utf16() ), explicitReload( utf16() ) },
        { explicitReload( latin1() ), explicitReload(), explicitReload() },
        { explicitReload( latin1() ), automaticFull(), explicitReload( latin1() ) },
        { automaticFull(), explicitReload( latin1() ), explicitReload( latin1() ) },
        { automaticFull(), automaticFull(), automaticFull() },

        // A Full reads everything, so it covers a Check and a Partial.
        { explicitReload( latin1() ), check(), explicitReload( latin1() ) },
        { explicitReload(), partial(), explicitReload() },
        { automaticFull(), check(), automaticFull() },
        { automaticFull(), partial(), automaticFull() },
        { check(), explicitReload( latin1() ), explicitReload( latin1() ) },
        { check(), automaticFull(), automaticFull() },
        { partial(), explicitReload(), explicitReload() },
        { partial(), automaticFull(), automaticFull() },

        // A Check covers a Partial: it could find a truncation, and when it
        // finds only growth it queues the Partial again.
        { check(), check(), check() },
        { check(), partial(), check() },
        { partial(), check(), check() },
        { partial(), partial(), partial() },
    };

    for ( const auto& row : rows ) {
        CAPTURE( describe( row.waiting ), describe( row.arriving ) );
        REQUIRE( describe( waitingIndexJob( row.waiting, row.arriving ) )
                 == describe( row.waitsAfterwards ) );
    }
}
