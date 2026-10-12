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

#ifndef LOGSQUIRL_TESTS_CONVERT_PROBE_H
#define LOGSQUIRL_TESTS_CONVERT_PROBE_H

// The convert probe plugins of tests/unit/plugins/convertprobe.cpp, as the
// host and UI tests of a Conversion in the background drive them (#795): one
// with convert_ex, of kind "ui", and one with the plain convert only. Each
// holds after every line it converts until its gate opens, or a step lets one
// line pass.

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QString>

inline const auto ConvertProbeId = QStringLiteral( "io.github.logsquirl.test.convert-probe" );
inline const auto PlainConvertProbeId
    = QStringLiteral( "io.github.logsquirl.test.convert-probe-plain" );

/// Writes a plugin.json for a convert probe into its own subdirectory of root.
inline void installConvertProbe( const QString& root, const QString& id, const QString& name,
                                 const QString& type, const QString& library )
{
    const auto pluginDir = QDir( root ).filePath( id );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "%2",
        "version": "1.0.0",
        "type": "%3",
        "library": "%4",
        "api_version": 1
    })" )
                        .arg( id, name, type, library )
                        .toUtf8() );
}

/// Installs the probe with convert_ex, as a plugin of kind "ui", into root.
inline void installConvertProbe( const QString& root )
{
    installConvertProbe( root, ConvertProbeId, QStringLiteral( "Convert Probe" ),
                         QStringLiteral( "ui" ), QStringLiteral( LOGSQUIRL_CONVERT_PROBE_PATH ) );
}

/// Installs the probe with the plain convert only, a "converter" plugin, into root.
inline void installPlainConvertProbe( const QString& root )
{
    installConvertProbe( root, PlainConvertProbeId, QStringLiteral( "Plain Convert Probe" ),
                         QStringLiteral( "converter" ),
                         QStringLiteral( LOGSQUIRL_CONVERT_PROBE_PLAIN_PATH ) );
}

/// The exported functions of a convert probe the tests drive behind the host's back.
struct ConvertProbe {
    explicit ConvertProbe( const QString& path )
        : library( path )
    {
        REQUIRE( library.load() );
        reset();
    }

    ~ConvertProbe()
    {
        // Nothing of the probe's is held at the gate once the test is over.
        openGate();
    }

    ConvertProbe( const ConvertProbe& ) = delete;
    ConvertProbe& operator=( const ConvertProbe& ) = delete;

    /// Lets every held conversion, and every later one, run through.
    void openGate()
    {
        fn<void ( * )()>( "logsquirl_probe_open_gate" )();
    }
    /// Lets one held line pass; the conversion holds after the next.
    void step()
    {
        fn<void ( * )()>( "logsquirl_probe_step" )();
    }
    /// How many conversions are inside a convert call right now.
    int running()
    {
        return fn<int ( * )()>( "logsquirl_probe_running" )();
    }
    /// The most conversions that were inside a convert call at the same time.
    int mostRunningAtOnce()
    {
        return fn<int ( * )()>( "logsquirl_probe_most_running_at_once" )();
    }
    int conversionsStarted()
    {
        return fn<int ( * )()>( "logsquirl_probe_conversions_started" )();
    }
    int conversionsReturned()
    {
        return fn<int ( * )()>( "logsquirl_probe_conversions_returned" )();
    }
    /// How many convert_ex calls returned because is_cancelled said so.
    int cancellationsSeen()
    {
        return fn<int ( * )()>( "logsquirl_probe_cancellations_seen" )();
    }
    int shutdownCalls()
    {
        return fn<int ( * )()>( "logsquirl_probe_shutdown_calls" )();
    }
    /// The output path of the n-th convert call (0 first), or an empty string.
    QString outputPath( int n )
    {
        return QString::fromUtf8(
            fn<const char* ( * )( int )>( "logsquirl_probe_output_path" )( n ) );
    }
    /// Forgets what the probe counted, and closes its gate.
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

#endif // LOGSQUIRL_TESTS_CONVERT_PROBE_H
