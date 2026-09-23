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

#include <cstdlib>

#include <QByteArray>

#include <mimalloc.h>

// Everything that links the utils library links mimalloc, and must see the
// define that turns on its process initialization and the crash handler's
// memory telemetry (#282).
TEST_CASE( "mimalloc is linked with the define that enables it", "[allocator]" )
{
#ifdef LOGSQUIRL_USE_MIMALLOC
    SUCCEED( "LOGSQUIRL_USE_MIMALLOC is defined" );
#else
    FAIL( "mimalloc is linked, but LOGSQUIRL_USE_MIMALLOC is not defined" );
#endif
}

TEST_CASE( "mimalloc is a current 2.x release", "[allocator]" )
{
    // Since 2.2 mi_version() reads major * 10000 + minor * 100 + patch, so
    // 2.5.0 is 20500 (2.1.7 still read 217).
    CHECK( mi_version() >= 20500 );
    CHECK( mi_version() < 30000 );
}

TEST_CASE( "mimalloc serves malloc only with the process-wide override", "[allocator]" )
{
#ifdef LOGSQUIRL_MIMALLOC_OVERRIDE
    const bool overridden = true;
#else
    const bool overridden = false;
#endif

    void* const block = std::malloc( 64 );
    REQUIRE( block != nullptr );
    CHECK( mi_is_in_heap_region( block ) == overridden );
    std::free( block );

    // Qt allocates through the C runtime from its own library.
    QByteArray bytes( 4096, 'x' );
    CHECK( mi_is_in_heap_region( bytes.constData() ) == overridden );

    // Under AddressSanitizer mimalloc hands its allocations to the sanitizer,
    // so they are not in a mimalloc heap region.
#if defined( __SANITIZE_ADDRESS__ )
    const bool addressSanitizer = true;
#elif defined( __has_feature )
    const bool addressSanitizer = __has_feature( address_sanitizer );
#else
    const bool addressSanitizer = false;
#endif
    void* const projectBlock = mi_malloc( 64 );
    CHECK( mi_is_in_heap_region( projectBlock ) != addressSanitizer );
    mi_free( projectBlock );
}
