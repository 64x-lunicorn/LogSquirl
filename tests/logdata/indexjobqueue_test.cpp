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

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "textencoding.h"

#include "indexjobrunner.h"
#include "logdataoperation.h"
#include "overload_visitor.h"

// The operation queue that applies the job rule (#550): it hands the index
// jobs of one Log File to its runner one at a time. A fake runner records what
// it is asked to do, and each test reports the running job finished by hand,
// as the log data does once the worker has sent its end: no Log File, no
// thread and no waiting.

namespace {

constexpr auto DefaultEncodingMib = 106; // UTF-8

const TextEncoding* latin1()
{
    return TextEncoding::forName( "ISO-8859-1" );
}

std::string describe( const IndexJob& job )
{
    const auto encodingName = []( const TextEncoding* encoding ) {
        return encoding ? " in " + encoding->name().toStdString() : std::string{};
    };

    return std::visit(
        makeOverloadVisitor(
            []( std::monostate ) { return std::string{ "nothing" }; },
            [ & ]( const AttachJob& attach ) {
                return "Attach " + attach.fileName.toStdString()
                       + encodingName( attach.forcedEncoding );
            },
            [ & ]( const FullReindexJob& full ) {
                return std::string{ full.request == FullIndexRequest::ExplicitReload
                                        ? "Full (explicit reload)"
                                        : "Full (automatic)" }
                       + encodingName( full.forcedEncoding );
            },
            []( const PartialReindexJob& ) { return std::string{ "Partial" }; },
            []( const CheckForChangesJob& ) { return std::string{ "Check" }; } ),
        job );
}

// Records every request of the queue in the test's journal, which outlives it.
class FakeRunner : public IndexJobRunner {
public:
    explicit FakeRunner( std::vector<std::string>& journal )
        : journal_( journal )
    {
    }

    ~FakeRunner() override
    {
        journal_.emplace_back( "destroyed" );
    }

    FakeRunner( const FakeRunner& ) = delete;
    FakeRunner& operator=( const FakeRunner& ) = delete;
    FakeRunner( FakeRunner&& ) = delete;
    FakeRunner& operator=( FakeRunner&& ) = delete;

    void run( const IndexJob& job ) override
    {
        journal_.emplace_back( "run " + describe( job ) );
    }

    void setIndexingPolicy( const IndexingPolicy& ) override
    {
        journal_.emplace_back( "policy" );
    }

    void interrupt() override
    {
        journal_.emplace_back( "interrupt" );
    }

private:
    std::vector<std::string>& journal_;
};

struct QueueWithFakeRunner {
    QueueWithFakeRunner()
    {
        queue.setRunner( std::make_unique<FakeRunner>( journal ) );
    }

    std::vector<std::string> journal;
    OperationQueue queue;
};

IndexJob attach()
{
    return AttachJob{ "attached.log", DefaultEncodingMib };
}

using Journal = std::vector<std::string>;

} // namespace

TEST_CASE( "An index job enqueued while none runs starts at once", "[indexjobqueue]" )
{
    QueueWithFakeRunner fixture;

    fixture.queue.enqueueJob( attach() );

    REQUIRE( fixture.journal == Journal{ "run Attach attached.log" } );
}

TEST_CASE( "An index job enqueued while another runs waits for it under the job rule",
           "[indexjobqueue]" )
{
    QueueWithFakeRunner fixture;
    fixture.queue.enqueueJob( attach() );

    // A Check covers a Partial: only the Check waits.
    fixture.queue.enqueueJob( PartialReindexJob{} );
    fixture.queue.enqueueJob( CheckForChangesJob{} );

    REQUIRE( fixture.journal == Journal{ "run Attach attached.log" } );

    fixture.queue.finishJobAndStartNext();

    REQUIRE( fixture.journal == Journal{ "run Attach attached.log", "run Check" } );

    // Nothing waits any more: the next index job starts at once again.
    fixture.queue.finishJobAndStartNext();
    fixture.queue.enqueueJob( PartialReindexJob{} );

    REQUIRE( fixture.journal == Journal{ "run Attach attached.log", "run Check", "run Partial" } );
    REQUIRE( fixture.queue.isPartialReindexRunning() );
}

TEST_CASE( "A reload enqueued while the finished index job is reported starts once the report "
           "returns",
           "[indexjobqueue]" )
{
    QueueWithFakeRunner fixture;
    fixture.queue.enqueueJob( attach() );

    // The log data tells its views the load finished before it reports the
    // job finished to the queue; a view that chooses another Encoding there
    // reloads while the Attach still counts as running.
    fixture.queue.enqueueJob( FullReindexJob{ FullIndexRequest::ExplicitReload, latin1() } );

    REQUIRE( fixture.journal == Journal{ "run Attach attached.log" } );

    fixture.queue.finishJobAndStartNext();

    REQUIRE( fixture.journal
             == Journal{ "run Attach attached.log", "run Full (explicit reload) in ISO-8859-1" } );
}

TEST_CASE( "Shutting the queue down drops the index job waiting", "[indexjobqueue]" )
{
    QueueWithFakeRunner fixture;
    fixture.queue.enqueueJob( attach() );
    fixture.queue.enqueueJob( CheckForChangesJob{} );

    fixture.queue.shutdown();

    REQUIRE( fixture.journal == Journal{ "run Attach attached.log", "interrupt", "destroyed" } );

    // A finish reported late starts nothing, nor does a job enqueued late.
    fixture.queue.finishJobAndStartNext();
    fixture.queue.enqueueJob( FullReindexJob{} );

    REQUIRE( fixture.journal == Journal{ "run Attach attached.log", "interrupt", "destroyed" } );
    REQUIRE_FALSE( fixture.queue.isPartialReindexRunning() );
}

TEST_CASE( "Shutting the queue down while a Partial runs leaves none running", "[indexjobqueue]" )
{
    QueueWithFakeRunner fixture;
    fixture.queue.enqueueJob( PartialReindexJob{} );
    REQUIRE( fixture.queue.isPartialReindexRunning() );

    fixture.queue.shutdown();

    REQUIRE_FALSE( fixture.queue.isPartialReindexRunning() );
    REQUIRE( fixture.journal == Journal{ "run Partial", "interrupt", "destroyed" } );
}

TEST_CASE( "The queue hands interrupts and a changed Indexing Policy to its runner",
           "[indexjobqueue]" )
{
    QueueWithFakeRunner fixture;

    fixture.queue.setIndexingPolicy( IndexingPolicy{} );
    fixture.queue.interrupt();

    REQUIRE( fixture.journal == Journal{ "policy", "interrupt" } );
}
