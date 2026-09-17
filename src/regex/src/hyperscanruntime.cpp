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

#include "hyperscanruntime.h"

#include <QString>
#include <QtGlobal>

#ifdef LOGSQUIRL_HYPERSCAN_RUNTIME_DISPATCH
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <intrin.h>

#include <array>
#include <string>
#endif

HyperscanRuntime chooseHyperscanRuntime( bool cpuSupportsAvx2, bool avx2Disabled )
{
    return cpuSupportsAvx2 && !avx2Disabled ? HyperscanRuntime::Avx2 : HyperscanRuntime::Sse42;
}

bool isHyperscanAvx2Disabled()
{
    const auto value = qEnvironmentVariable( "LOGSQUIRL_HYPERSCAN_DISABLE_AVX2" );
    return !value.isEmpty() && value != QLatin1String( "0" );
}

const char* hyperscanRuntimeName( HyperscanRuntime runtime )
{
    switch ( runtime ) {
    case HyperscanRuntime::Linked:
        return "linked";
    case HyperscanRuntime::Sse42:
        return "sse4.2";
    case HyperscanRuntime::Avx2:
        return "avx2";
    }
    return "unknown";
}

#ifdef LOGSQUIRL_HYPERSCAN_RUNTIME_DISPATCH

namespace {

// The file names the build gives the two Hyperscan DLLs (3rdparty/CMakeLists.txt).
// The import library names hs.dll, so a module without the delay-load hook
// still finds the SSE4.2 build.
constexpr auto Sse42Library = L"hs.dll";
constexpr auto Avx2Library = L"hs_avx2.dll";

std::wstring directoryOfThisModule()
{
    HMODULE module = nullptr;
    if ( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>( &directoryOfThisModule ), &module ) ) {
        return {};
    }

    std::wstring path( 32768, L'\0' );
    const auto length
        = GetModuleFileNameW( module, path.data(), static_cast<DWORD>( path.size() ) );
    path.resize( length );

    const auto separator = path.find_last_of( L"\\/" );
    return separator == std::wstring::npos ? std::wstring{} : path.substr( 0, separator + 1 );
}

HMODULE loadLibraryNextToThisModule( const wchar_t* name )
{
    const auto path = directoryOfThisModule() + name;
    return LoadLibraryExW( path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
}

} // namespace

bool isHyperscanRuntimeChosenAtRunTime()
{
    return true;
}

bool cpuSupportsHyperscanAvx2()
{
    std::array<int, 4> registers{};

    __cpuid( registers.data(), 0 );
    if ( registers[ 0 ] < 7 ) {
        return false;
    }

    __cpuid( registers.data(), 1 );
    const auto features1Ecx = static_cast<unsigned>( registers[ 2 ] );
    constexpr auto OsXsave = 1u << 27;
    constexpr auto Avx = 1u << 28;
    if ( ( features1Ecx & ( OsXsave | Avx ) ) != ( OsXsave | Avx ) ) {
        return false;
    }

    // The operating system saves the SSE (bit 1) and AVX (bit 2) state.
    constexpr auto SseAndAvxState = 0x6u;
    if ( ( _xgetbv( 0 ) & SseAndAvxState ) != SseAndAvxState ) {
        return false;
    }

    __cpuidex( registers.data(), 7, 0 );
    const auto features7Ebx = static_cast<unsigned>( registers[ 1 ] );
    constexpr auto Bmi1 = 1u << 3;
    constexpr auto Avx2 = 1u << 5;
    constexpr auto Bmi2 = 1u << 8;
    return ( features7Ebx & ( Bmi1 | Avx2 | Bmi2 ) ) == ( Bmi1 | Avx2 | Bmi2 );
}

HyperscanRuntime selectedHyperscanRuntime()
{
    static const auto runtime
        = chooseHyperscanRuntime( cpuSupportsHyperscanAvx2(), isHyperscanAvx2Disabled() );
    return runtime;
}

std::optional<HyperscanRuntime> loadedHyperscanRuntime()
{
    if ( GetModuleHandleW( Avx2Library ) != nullptr ) {
        return HyperscanRuntime::Avx2;
    }
    if ( GetModuleHandleW( Sse42Library ) != nullptr ) {
        return HyperscanRuntime::Sse42;
    }
    return std::nullopt;
}

void* loadHyperscanLibrary()
{
    if ( selectedHyperscanRuntime() == HyperscanRuntime::Avx2 ) {
        if ( const auto library = loadLibraryNextToThisModule( Avx2Library ) ) {
            return library;
        }
    }
    return loadLibraryNextToThisModule( Sse42Library );
}

#else

bool isHyperscanRuntimeChosenAtRunTime()
{
    return false;
}

bool cpuSupportsHyperscanAvx2()
{
    return false;
}

HyperscanRuntime selectedHyperscanRuntime()
{
    return HyperscanRuntime::Linked;
}

std::optional<HyperscanRuntime> loadedHyperscanRuntime()
{
    return HyperscanRuntime::Linked;
}

#endif
