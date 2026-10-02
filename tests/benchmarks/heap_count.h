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

#ifndef LOGSQUIRL_BENCHMARK_HEAP_COUNT_H
#define LOGSQUIRL_BENCHMARK_HEAP_COUNT_H

// The heap counts of the benchmarks' fixed-work mode (#673), built into every
// benchmark binary when CMake is configured with
// LOGSQUIRL_BENCHMARK_HEAP_COUNTS=ON (only .github/scripts/instruction-counts.sh
// does, on Linux with glibc). heap_count.c says how they are counted.
//
// instruction_count.h opens the window right before Callgrind starts counting
// a benchmark's measured code and closes it right after Callgrind has stopped,
// so both count the same run.

#ifdef __cplusplus
extern "C" {
#endif

// Starts counting from zero: allocations, and bytes held above what is held now
// (never below it; heap_count.c says why).
void logsquirl_benchmark_heap_start( void );

// Stops counting and appends "<allocations>\t<peak heap bytes>\t<label>\n" to
// the file LOGSQUIRL_BENCHMARK_HEAP_FILE names, if it names one.
void logsquirl_benchmark_heap_finish( const char* label );

#ifdef __cplusplus
}
#endif

#endif // LOGSQUIRL_BENCHMARK_HEAP_COUNT_H
