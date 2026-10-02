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

#ifndef LOGSQUIRL_BENCHMARK_INSTRUCTION_COUNT_H
#define LOGSQUIRL_BENCHMARK_INSTRUCTION_COUNT_H

// The fixed-work mode of the Catch2 benchmarks (#671): every benchmark file
// includes this header instead of <catch2/benchmark/catch_benchmark.hpp>.
//
// Normally BENCHMARK and BENCHMARK_ADVANCED are Catch2's own. Catch2 picks how
// often it runs the measured code from the clock, so the work a run does
// differs from run to run; that is fine for a time, but not for an instruction
// count. With LOGSQUIRL_BENCHMARK_COUNT_INSTRUCTIONS=1 in the environment, and
// the binary run under Callgrind with --instr-atstart=no, each benchmark
// instead runs the measured code exactly once, and Callgrind counts only that
// run: instrumentation starts and the counters are zeroed where Catch2 would
// start its clock, and a dump named "<test case> / <benchmark>" is written
// where it would stop it. The setup of a BENCHMARK_ADVANCED (everything outside
// meter.measure) is not counted, just as Catch2 does not time it.
//
// In a build with LOGSQUIRL_BENCHMARK_HEAP_COUNTS (heap_count.h, #673), the
// same run also counts the allocations and the peak heap of the measured code:
// the heap window opens right before Callgrind starts and closes right after
// it stops, so the two count the same code, and the window's own instructions
// are not counted.
//
// .github/scripts/instruction-counts.sh runs every benchmark binary that way
// and .github/scripts/instruction-counts.py reads the dumps; BUILD.md,
// "Instruction counts", shows how to reproduce a count by hand.
//
// The client requests come from Valgrind's own header, which only a Linux
// build machine with Valgrind installed has; elsewhere the mode reports that
// it cannot count and fails the benchmark.

#include <catch2/benchmark/catch_benchmark.hpp>

#include <QtGlobal>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#if defined( __has_include )
#if __has_include( <valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#define LOGSQUIRL_BENCHMARK_HAS_CALLGRIND 1
#endif
#endif

#ifdef LOGSQUIRL_BENCHMARK_HEAP_COUNTS
#include "heap_count.h"
#endif

namespace logsquirl_benchmark {

// Whether this run counts instructions instead of timing.
inline bool countsInstructions()
{
    static const bool counts
        = qEnvironmentVariableIntValue( "LOGSQUIRL_BENCHMARK_COUNT_INSTRUCTIONS" ) == 1;
    return counts;
}

// Stands where Catch2's clock stands: Chronometer::measure calls start()
// right before the measured code and finish() right after it.
class InstructionCounter final : public Catch::Benchmark::Detail::ChronometerConcept {
public:
    explicit InstructionCounter( std::string label )
        : label_( std::move( label ) )
    {
    }

    void start() override
    {
        // Worker threads that the setup or the previous benchmark kept busy
        // spin for a while before they sleep; spinning into the count, they
        // added up to 30 % to a small benchmark in some runs (#671). Uncounted,
        // they get the time to fall asleep first.
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
#ifdef LOGSQUIRL_BENCHMARK_HEAP_COUNTS
        logsquirl_benchmark_heap_start();
#endif
#ifdef LOGSQUIRL_BENCHMARK_HAS_CALLGRIND
        CALLGRIND_START_INSTRUMENTATION;
        CALLGRIND_ZERO_STATS;
#endif
    }

    void finish() override
    {
#ifdef LOGSQUIRL_BENCHMARK_HAS_CALLGRIND
        CALLGRIND_DUMP_STATS_AT( label_.c_str() );
        CALLGRIND_STOP_INSTRUMENTATION;
#endif
#ifdef LOGSQUIRL_BENCHMARK_HEAP_COUNTS
        logsquirl_benchmark_heap_finish( label_.c_str() );
#endif
    }

private:
    std::string label_;
};

// What BENCHMARK and BENCHMARK_ADVANCED declare: Catch2's Benchmark, or in
// the fixed-work mode exactly one run of the measured code, counted.
class Benchmark {
public:
    explicit Benchmark( std::string name )
        : name_( std::move( name ) )
    {
    }

    template <typename Fun>
    Benchmark& operator=( Fun fun )
    {
        if ( !countsInstructions() ) {
            Catch::Benchmark::Benchmark timed{ std::move( name_ ) };
            timed = std::move( fun );
            return *this;
        }
        if ( Catch::getCurrentContext().getConfig()->skipBenchmarks() ) {
            return *this;
        }
        checkCanCount();

        InstructionCounter counter{ Catch::getResultCapture().getCurrentTestName() + " / "
                                    + name_ };
        const Catch::Benchmark::Detail::BenchmarkFunction function{ std::move( fun ) };
        function( Catch::Benchmark::Chronometer{ counter, 1 } );
        return *this;
    }

    explicit operator bool() const
    {
        return true;
    }

private:
    static void checkCanCount()
    {
#ifdef LOGSQUIRL_BENCHMARK_HAS_CALLGRIND
        if ( RUNNING_ON_VALGRIND == 0 ) {
            throw std::runtime_error( "LOGSQUIRL_BENCHMARK_COUNT_INSTRUCTIONS=1 counts only under "
                                      "valgrind --tool=callgrind --instr-atstart=no" );
        }
#else
        throw std::runtime_error( "LOGSQUIRL_BENCHMARK_COUNT_INSTRUCTIONS=1 needs a build "
                                  "with Valgrind's headers (valgrind/callgrind.h)" );
#endif
    }

    std::string name_;
};

} // namespace logsquirl_benchmark

// The same shape as Catch2's own macros (catch_benchmark.hpp), with the
// Benchmark above in place of Catch::Benchmark::Benchmark.
#undef BENCHMARK
#undef BENCHMARK_ADVANCED

#define LOGSQUIRL_INTERNAL_BENCHMARK( BenchmarkName, name, benchmarkIndex )                        \
    if ( ::logsquirl_benchmark::Benchmark BenchmarkName{ name } )                                  \
    BenchmarkName = [ & ]( int benchmarkIndex )

#define LOGSQUIRL_INTERNAL_BENCHMARK_ADVANCED( BenchmarkName, name )                               \
    if ( ::logsquirl_benchmark::Benchmark BenchmarkName{ name } )                                  \
    BenchmarkName = [ & ]

#define BENCHMARK( ... )                                                                           \
    LOGSQUIRL_INTERNAL_BENCHMARK( INTERNAL_CATCH_UNIQUE_NAME( LOGSQUIRL_BENCHMARK_ ),              \
                                  INTERNAL_CATCH_GET_1_ARG( __VA_ARGS__, , ),                      \
                                  INTERNAL_CATCH_GET_2_ARG( __VA_ARGS__, , ) )

#define BENCHMARK_ADVANCED( name )                                                                 \
    LOGSQUIRL_INTERNAL_BENCHMARK_ADVANCED( INTERNAL_CATCH_UNIQUE_NAME( LOGSQUIRL_BENCHMARK_ ),     \
                                           name )

#endif // LOGSQUIRL_BENCHMARK_INSTRUCTION_COUNT_H
