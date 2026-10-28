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

#ifndef LOGSQUIRL_TESTS_CLAIM_PROBE_H
#define LOGSQUIRL_TESTS_CLAIM_PROBE_H

// The claim probe plugins of tests/unit/plugins/claimprobe.cpp, as the host
// and UI tests of a converter that claims a file by its content drive them
// (#861): probe A, which converts ".clmboth" files by their extension, and
// probe B, whose id comes after A's, which converts ".clm" and ".clmboth"
// files. Both claim every file that starts with their magic, "LSQCLAIM" until
// a test sets another, and hold their check while their gate is closed.

#include "convert_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QLibrary>
#include <QString>

#include <cstddef>

inline const auto ClaimProbeAId = QStringLiteral( "io.github.logsquirl.test.claim-probe-a" );
inline const auto ClaimProbeBId = QStringLiteral( "io.github.logsquirl.test.claim-probe-b" );

/// The magic both probes claim a file by until a test sets another.
inline const QByteArray ClaimMagic = QByteArrayLiteral( "LSQCLAIM" );

/// Installs claim probe A into root.
inline void installClaimProbeA( const QString& root )
{
    installConvertProbe( root, ClaimProbeAId, QStringLiteral( "Claim Probe A" ),
                         QStringLiteral( "converter" ),
                         QStringLiteral( LOGSQUIRL_CLAIM_PROBE_A_PATH ) );
}

/// Installs claim probe B into root.
inline void installClaimProbeB( const QString& root )
{
    installConvertProbe( root, ClaimProbeBId, QStringLiteral( "Claim Probe B" ),
                         QStringLiteral( "converter" ),
                         QStringLiteral( LOGSQUIRL_CLAIM_PROBE_B_PATH ) );
}

/// The exported functions of a claim probe the tests drive behind the host's back.
struct ClaimProbe {
    explicit ClaimProbe( const QString& path )
        : library( path )
    {
        REQUIRE( library.load() );
        reset();
    }

    ~ClaimProbe()
    {
        // Nothing of the probe's is held at the gate once the test is over.
        openGate();
    }

    ClaimProbe( const ClaimProbe& ) = delete;
    ClaimProbe& operator=( const ClaimProbe& ) = delete;

    /// Claims, from now on, the files that start with value.
    void setMagic( const QByteArray& value )
    {
        fn<void ( * )( const char*, std::size_t )>( "logsquirl_probe_set_magic" )(
            value.constData(), static_cast<std::size_t>( value.size() ) );
    }
    /// Holds every check until the gate opens.
    void closeGate()
    {
        fn<void ( * )()>( "logsquirl_probe_close_gate" )();
    }
    void openGate()
    {
        fn<void ( * )()>( "logsquirl_probe_open_gate" )();
    }
    int claimsCalls()
    {
        return fn<int ( * )()>( "logsquirl_probe_claims_calls" )();
    }
    /// How many checks are under way right now.
    int claimsRunning()
    {
        return fn<int ( * )()>( "logsquirl_probe_claims_running" )();
    }
    /// How many checks ran on the thread the probe was initialised on.
    int claimsOnInitThread()
    {
        return fn<int ( * )()>( "logsquirl_probe_claims_on_init_thread" )();
    }
    /// How many checks began while the probe was shut down.
    int claimsWhileShutDown()
    {
        return fn<int ( * )()>( "logsquirl_probe_claims_while_shut_down" )();
    }
    /// How often the probe was shut down while a check was under way.
    int shutdownsWhileClaiming()
    {
        return fn<int ( * )()>( "logsquirl_probe_shutdowns_while_claiming" )();
    }
    int shutdownCalls()
    {
        return fn<int ( * )()>( "logsquirl_probe_shutdown_calls" )();
    }
    int conversions()
    {
        return fn<int ( * )()>( "logsquirl_probe_conversions" )();
    }
    std::size_t lastHeadLength()
    {
        return fn<std::size_t ( * )()>( "logsquirl_probe_last_head_length" )();
    }
    QString lastPath()
    {
        return QString::fromUtf8( fn<const char* ( * )()>( "logsquirl_probe_last_path" )() );
    }
    /// Forgets what the probe counted, opens its gate and claims ClaimMagic again.
    void reset()
    {
        fn<void ( * )()>( "logsquirl_probe_reset" )();
    }

    template <typename Fn>
    Fn fn( const char* name )
    {
        auto* resolved = reinterpret_cast<Fn>( library.resolve( name ) );
        REQUIRE( resolved != nullptr );
        return resolved;
    }

    QLibrary library;
};

#endif // LOGSQUIRL_TESTS_CLAIM_PROBE_H
