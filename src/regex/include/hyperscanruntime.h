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

#ifndef LOGSQUIRL_HYPERSCAN_RUNTIME_H
#define LOGSQUIRL_HYPERSCAN_RUNTIME_H

#include <optional>

// Which build of the Hyperscan library a Search runs on (#281).
//
// Vectorscan on Linux picks its SIMD code per function at run time (its fat
// runtime relies on ELF ifunc). MSVC has no such mechanism, so the Windows
// release, which must run on every x64 CPU, ships Hyperscan twice as DLLs:
// hs.dll built for SSE4.2 and hs_avx2.dll built with /arch:AVX2. Hyperscan is
// delay-loaded, and the first call into it loads the DLL chosen here. Every
// other build links one Hyperscan or Vectorscan statically.
enum class HyperscanRuntime {
    // Linked into the executable; it chooses its code paths itself, if at all.
    Linked,
    // The Windows DLL for CPUs without AVX2.
    Sse42,
    // The Windows DLL built with AVX2.
    Avx2,
};

// AVX2 where the CPU supports it, unless disabled.
HyperscanRuntime chooseHyperscanRuntime( bool cpuSupportsAvx2, bool avx2Disabled );

// Whether this build chooses between the Hyperscan DLLs at run time.
bool isHyperscanRuntimeChosenAtRunTime();

// Whether the CPU and the operating system can run the AVX2 build: AVX2,
// BMI1 and BMI2 (which /arch:AVX2 code may use), and the OS saves the YMM
// state. Always false where the runtime is not chosen at run time.
bool cpuSupportsHyperscanAvx2();

// Whether the environment variable LOGSQUIRL_HYPERSCAN_DISABLE_AVX2 is set to a
// value other than 0: the runtime for CPUs without AVX2 is used then, to test
// that path on a CPU with AVX2.
bool isHyperscanAvx2Disabled();

// The runtime this process loads: chooseHyperscanRuntime() for this CPU and
// environment where it is chosen at run time, Linked otherwise.
HyperscanRuntime selectedHyperscanRuntime();

// The runtime that is loaded now: where it is chosen at run time, the DLL that
// is loaded, std::nullopt before the first call into Hyperscan; Linked
// otherwise.
std::optional<HyperscanRuntime> loadedHyperscanRuntime();

const char* hyperscanRuntimeName( HyperscanRuntime runtime );

#ifdef LOGSQUIRL_HYPERSCAN_RUNTIME_DISPATCH
// Loads the DLL of selectedHyperscanRuntime() from the directory of the module
// this code is linked into, the SSE4.2 one if the AVX2 one cannot be loaded.
// Returns the module handle as void* (HMODULE), nullptr if neither loads.
void* loadHyperscanLibrary();
#endif

#endif
