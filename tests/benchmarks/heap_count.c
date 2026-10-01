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

// The heap counts of the benchmarks' fixed-work mode (#673): how many heap
// blocks one run of a benchmark's measured code allocates, and the most heap it
// holds at once above what was held when it started, all threads together.
//
// Counted in the process itself, during the same Callgrind run that counts the
// instructions (instruction-counts.sh), so they cost no second run: Callgrind,
// unlike Valgrind's heap tools, keeps the program's own allocator, and none of
// those tools can count between two points of a run, which a benchmark's
// measured code is. Like an instruction count, they count work, not time, and
// repeat when the work does; threads that allocate while they wait for each
// other can make them vary between runs as they make the instructions vary.
//
// Two allocators serve a benchmark:
// - glibc's malloc, for operator new, Qt, the standard containers and the
//   libraries. This file defines malloc and the rest of its family, which an
//   executable's definitions replace for the whole process (glibc supports
//   that, "Replacing malloc" in its manual), and hands every call on to glibc's
//   own under the names glibc exports for that (__libc_malloc and so on).
// - mimalloc, for logsquirl::vector (src/utils/include/containers.h), whose
//   mi_stl_allocator calls mi_new_n and mi_free. They live in the static
//   mimalloc, so they cannot be replaced; the link wraps them instead
//   (--wrap=mi_new_n --wrap=mi_free, tests/benchmarks/CMakeLists.txt).
//
// A block's size is the size its allocator reports for it (malloc_usable_size,
// mi_usable_size), which is the size asked for rounded up to the allocator's
// granularity and does not depend on where the block lies.
//
// What is counted cannot tell a block allocated before the window from one
// allocated in it: telling them apart would need a table of the window's
// blocks, kept from inside the allocator. So:
// - Every call that hands out a block is an allocation, and that includes a
//   realloc, moved or not, also of a block allocated before the window.
// - The bytes held go up by each block handed out (by a realloc's growth) and
//   down by each block freed (by a realloc's shrinking), but never below zero,
//   the level when the window opened: freeing a block allocated before the
//   window would otherwise make room that the window's own blocks then fill
//   without raising the peak. The peak is therefore never more than the most
//   heap the window's own blocks held at once: exactly that while the window
//   frees (or shrinks) no block from before it, and possibly less when it
//   does, by at most the bytes of those blocks freed before the peak.
//
// Outside the window, and in a binary that never opens one, each call costs
// one load and a branch. Inside it, the counting is a few instructions per
// call, which Callgrind counts with the rest.

#define _GNU_SOURCE

#include "heap_count.h"

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#if !defined( __linux__ ) || !defined( __GLIBC__ )
#error "heap_count.c replaces glibc's malloc: LOGSQUIRL_BENCHMARK_HEAP_COUNTS needs Linux, glibc"
#endif

#define LOGSQUIRL_HEAP_EXPORT __attribute__( ( visibility( "default" ) ) )

// glibc's allocator under the names it exports for a replacement to call.
extern void* __libc_malloc( size_t size );
extern void __libc_free( void* block );
extern void* __libc_calloc( size_t count, size_t size );
extern void* __libc_realloc( void* block, size_t size );
extern void* __libc_memalign( size_t alignment, size_t size );
extern void* __libc_valloc( size_t size );
extern void* __libc_pvalloc( size_t size );

// mimalloc's, as the --wrap options of the link name them.
extern void* __real_mi_new_n( size_t count, size_t size );
extern void __real_mi_free( void* block );
extern size_t mi_usable_size( const void* block );

static atomic_bool counting;
static atomic_ullong allocations;
static atomic_llong held;
static atomic_llong peak;

static bool isCounting( void )
{
    return atomic_load_explicit( &counting, memory_order_relaxed );
}

// The bytes held change by bytes, but never fall below zero (see the top of
// the file).
static void noteBytes( long long bytes )
{
    if ( bytes < 0 ) {
        long long before = atomic_load_explicit( &held, memory_order_relaxed );
        long long after;
        do {
            after = before + bytes > 0 ? before + bytes : 0;
        } while ( !atomic_compare_exchange_weak_explicit(
            &held, &before, after, memory_order_relaxed, memory_order_relaxed ) );
        return;
    }
    const long long now = atomic_fetch_add_explicit( &held, bytes, memory_order_relaxed ) + bytes;
    long long highest = atomic_load_explicit( &peak, memory_order_relaxed );
    while ( now > highest
            && !atomic_compare_exchange_weak_explicit( &peak, &highest, now, memory_order_relaxed,
                                                       memory_order_relaxed ) ) {
    }
}

static void noteBlock( long long bytes )
{
    atomic_fetch_add_explicit( &allocations, 1, memory_order_relaxed );
    noteBytes( bytes );
}

static void* allocated( void* block )
{
    if ( block != NULL && isCounting() ) {
        noteBlock( (long long)malloc_usable_size( block ) );
    }
    return block;
}

// ---------------------------------------------------------------------------
// glibc's malloc family
// ---------------------------------------------------------------------------

LOGSQUIRL_HEAP_EXPORT void* malloc( size_t size )
{
    return allocated( __libc_malloc( size ) );
}

LOGSQUIRL_HEAP_EXPORT void free( void* block )
{
    if ( block != NULL && isCounting() ) {
        noteBytes( -(long long)malloc_usable_size( block ) );
    }
    __libc_free( block );
}

LOGSQUIRL_HEAP_EXPORT void* calloc( size_t count, size_t size )
{
    return allocated( __libc_calloc( count, size ) );
}

// A realloc that hands out a block, moved or not, is an allocation: growing a
// buffer one realloc at a time costs one each. That includes a block allocated
// before the window, which this cannot tell apart.
LOGSQUIRL_HEAP_EXPORT void* realloc( void* block, size_t size )
{
    if ( !isCounting() ) {
        return __libc_realloc( block, size );
    }
    const long long before = block != NULL ? (long long)malloc_usable_size( block ) : 0;
    void* const result = __libc_realloc( block, size );
    if ( result != NULL ) {
        noteBlock( (long long)malloc_usable_size( result ) - before );
    }
    else if ( block != NULL && size == 0 ) {
        // glibc frees the block and returns null.
        noteBytes( -before );
    }
    return result;
}

LOGSQUIRL_HEAP_EXPORT void* reallocarray( void* block, size_t count, size_t size )
{
    size_t bytes = 0;
    if ( __builtin_mul_overflow( count, size, &bytes ) ) {
        errno = ENOMEM;
        return NULL;
    }
    return realloc( block, bytes );
}

LOGSQUIRL_HEAP_EXPORT void* memalign( size_t alignment, size_t size )
{
    return allocated( __libc_memalign( alignment, size ) );
}

LOGSQUIRL_HEAP_EXPORT void* aligned_alloc( size_t alignment, size_t size )
{
    return allocated( __libc_memalign( alignment, size ) );
}

LOGSQUIRL_HEAP_EXPORT int posix_memalign( void** result, size_t alignment, size_t size )
{
    if ( alignment == 0 || alignment % sizeof( void* ) != 0
         || ( alignment & ( alignment - 1 ) ) != 0 ) {
        return EINVAL;
    }
    void* const block = allocated( __libc_memalign( alignment, size ) );
    if ( block == NULL ) {
        return ENOMEM;
    }
    *result = block;
    return 0;
}

LOGSQUIRL_HEAP_EXPORT void* valloc( size_t size )
{
    return allocated( __libc_valloc( size ) );
}

LOGSQUIRL_HEAP_EXPORT void* pvalloc( size_t size )
{
    return allocated( __libc_pvalloc( size ) );
}

// ---------------------------------------------------------------------------
// mimalloc, for logsquirl::vector
// ---------------------------------------------------------------------------

// mi_new_n throws std::bad_alloc when it cannot allocate; this file is
// compiled with -fexceptions so that the exception passes through.
LOGSQUIRL_HEAP_EXPORT void* __wrap_mi_new_n( size_t count, size_t size )
{
    void* const block = __real_mi_new_n( count, size );
    if ( block != NULL && isCounting() ) {
        noteBlock( (long long)mi_usable_size( block ) );
    }
    return block;
}

LOGSQUIRL_HEAP_EXPORT void __wrap_mi_free( void* block )
{
    if ( block != NULL && isCounting() ) {
        noteBytes( -(long long)mi_usable_size( block ) );
    }
    __real_mi_free( block );
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

void logsquirl_benchmark_heap_start( void )
{
    atomic_store( &allocations, 0 );
    atomic_store( &held, 0 );
    atomic_store( &peak, 0 );
    atomic_store( &counting, true );
}

void logsquirl_benchmark_heap_finish( const char* label )
{
    atomic_store( &counting, false );
    const unsigned long long blocks = atomic_load( &allocations );
    const long long highest = atomic_load( &peak );

    const char* const path = getenv( "LOGSQUIRL_BENCHMARK_HEAP_FILE" );
    if ( path == NULL || *path == '\0' ) {
        return;
    }
    // One record per line, appended: a binary that relaunches itself has two
    // processes, and instruction-counts.py adds up a label's records.
    const int file = open( path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644 );
    if ( file < 0 ) {
        perror( "LOGSQUIRL_BENCHMARK_HEAP_FILE" );
        return;
    }
    dprintf( file, "%llu\t%lld\t%s\n", blocks, highest, label );
    close( file );
}
