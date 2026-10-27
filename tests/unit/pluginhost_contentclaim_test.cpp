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

// A converter plugin claims a file by its first bytes, asked on a worker
// thread of the Plugin Host's in plugin-ID order, when no converter's
// extensions match the file's (#861).

#include <catch2/catch_test_macros.hpp>

#include "claim_probe.h"
#include "convert_probe.h"
#include "logsquirl_plugin_api.h"
#include "plugincatalog.h"
#include "pluginhost.h"
#include "test_utils.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <memory>
#include <optional>
#include <thread>

using logsquirl::plugins::ConversionEnd;
using logsquirl::plugins::ConversionOutcome;
using logsquirl::plugins::ConversionReport;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

/// Writes content into a new file at path.
void writeFile( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( content );
}

/// Turns the event loop for ms milliseconds.
void turnFor( int ms )
{
    QEventLoop loop;
    QTimer::singleShot( ms, &loop, &QEventLoop::quit );
    loop.exec();
}

/// What a test heard of a content check; written on the host's thread only.
struct ClaimHeard {
    int reports = 0;
    QString claimedBy;
    QThread* reportedOn = nullptr;

    std::function<void( const QString& )> report()
    {
        return [ this ]( const QString& pluginId ) {
            ++reports;
            claimedBy = pluginId;
            reportedOn = QThread::currentThread();
        };
    }
};

/// Opens a claim probe's gate from a thread of its own after a while, while
/// the test's thread waits inside a call that waits for the check.
class GateOpener {
public:
    // Resolved here: Catch's assertions are not for other threads.
    explicit GateOpener( ClaimProbe& probe )
        : thread_( [ openGate = probe.fn<void ( * )()>( "logsquirl_probe_open_gate" ) ] {
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
            openGate();
        } )
    {
    }

    ~GateOpener()
    {
        thread_.join();
    }

    GateOpener( const GateOpener& ) = delete;
    GateOpener& operator=( const GateOpener& ) = delete;

private:
    std::thread thread_;
};

} // namespace

SCENARIO( "A converter plugin claims a file by its content through the Plugin Host",
          "[pluginhost][plugins][conversion]" )
{
    GIVEN( "A Plugin Host with two claim probes loaded, A's plugin id the smaller" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installClaimProbeA( pluginRoot.path() );
        installClaimProbeB( pluginRoot.path() );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        auto host = std::make_unique<PluginHost>( catalog );
        // B first: the order a file is offered in is the plugin ids', not the
        // order the plugins loaded in.
        REQUIRE( host->loadPlugin( ClaimProbeBId ).isEmpty() );
        REQUIRE( host->loadPlugin( ClaimProbeAId ).isEmpty() );

        ClaimProbe probeA( QStringLiteral( LOGSQUIRL_CLAIM_PROBE_A_PATH ) );
        ClaimProbe probeB( QStringLiteral( LOGSQUIRL_CLAIM_PROBE_B_PATH ) );

        QTemporaryDir files;
        REQUIRE( files.isValid() );
        const auto claimed = files.filePath( "capture.log" );
        writeFile( claimed, ClaimMagic + "\none\ntwo\n" );
        const auto extensionless = files.filePath( "capture" );
        writeFile( extensionless, ClaimMagic + "\none\n" );
        const auto plain = files.filePath( "plain.log" );
        writeFile( plain, "one\ntwo\n" );

        ClaimHeard heard;

        WHEN( "A file that starts with the magic both claim is checked" )
        {
            const auto id = host->checkContent( claimed, heard.report() );
            REQUIRE( id != 0 );
            REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );

            THEN( "The plugin with the smaller id claims it, asked on a worker thread with the "
                  "file's absolute path and head, and the caller hears it on the host's thread" )
            {
                REQUIRE( heard.claimedBy == ClaimProbeAId );
                REQUIRE( heard.reportedOn == host->thread() );
                REQUIRE( probeA.claimsCalls() == 1 );
                REQUIRE( probeA.claimsOnInitThread() == 0 );
                REQUIRE( probeA.lastHeadLength()
                         == static_cast<std::size_t>( QFileInfo( claimed ).size() ) );
                REQUIRE( probeA.lastPath() == QFileInfo( claimed ).absoluteFilePath() );
                // The first that claims it is the last asked.
                REQUIRE( probeB.claimsCalls() == 0 );
            }
        }

        WHEN( "A file without an extension that starts with the magic is checked" )
        {
            REQUIRE( host->checkContent( extensionless, heard.report() ) != 0 );

            THEN( "It is claimed all the same" )
            {
                REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );
                REQUIRE( heard.claimedBy == ClaimProbeAId );
            }
        }

        WHEN( "A file without the magic is checked" )
        {
            REQUIRE( host->checkContent( plain, heard.report() ) != 0 );

            THEN( "Both are asked, neither claims it, and the caller hears no plugin" )
            {
                REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );
                REQUIRE( heard.claimedBy.isEmpty() );
                REQUIRE( probeA.claimsCalls() == 1 );
                REQUIRE( probeB.claimsCalls() == 1 );
            }
        }

        WHEN( "Only the plugin with the larger id claims the file" )
        {
            probeA.setMagic( "SOMETHINGELSE" );
            REQUIRE( host->checkContent( claimed, heard.report() ) != 0 );

            THEN( "It is asked after the other, and claims it" )
            {
                REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );
                REQUIRE( heard.claimedBy == ClaimProbeBId );
                REQUIRE( probeA.claimsCalls() == 1 );
                REQUIRE( probeB.claimsCalls() == 1 );
            }
        }

        WHEN( "A file longer than the head is checked" )
        {
            const auto longer = files.filePath( "longer.dat" );
            writeFile( longer, ClaimMagic + QByteArray( 10000, 'x' ) );
            REQUIRE( host->checkContent( longer, heard.report() ) != 0 );

            THEN( "The plugin is handed the head only" )
            {
                REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );
                REQUIRE( heard.claimedBy == ClaimProbeAId );
                REQUIRE( probeA.lastHeadLength() == LOGSQUIRL_CONVERTER_CLAIMS_HEAD_LENGTH );
            }
        }

        WHEN( "An empty file is checked" )
        {
            const auto empty = files.filePath( "empty.log" );
            writeFile( empty, {} );
            REQUIRE( host->checkContent( empty, heard.report() ) != 0 );

            THEN( "The plugins are asked with a head of no bytes, and claim nothing" )
            {
                REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );
                REQUIRE( heard.claimedBy.isEmpty() );
                REQUIRE( probeA.claimsCalls() == 1 );
                REQUIRE( probeA.lastHeadLength() == 0 );
            }
        }

        WHEN( "A file that cannot be read is checked" )
        {
            REQUIRE( host->checkContent( files.filePath( "missing.log" ), heard.report() ) != 0 );

            THEN( "No plugin is asked, and none claims it" )
            {
                REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );
                REQUIRE( heard.claimedBy.isEmpty() );
                REQUIRE( probeA.claimsCalls() == 0 );
                REQUIRE( probeB.claimsCalls() == 0 );
            }
        }

        WHEN( "The host is asked which converter handles an extension" )
        {
            THEN( "Of two that list it, the one with the smaller id; of one, that one" )
            {
                REQUIRE( host->converterForExtension( "clmboth" ) == ClaimProbeAId );
                REQUIRE( host->converterForExtension( "clm" ) == ClaimProbeBId );
                REQUIRE( host->converterForExtension( "log" ).isEmpty() );
            }
        }

        WHEN( "A check is under way and the plugin it asks holds" )
        {
            probeA.closeGate();
            // Counts how often the event loop turned meanwhile.
            int ticks = 0;
            QTimer ticker;
            QObject::connect( &ticker, &QTimer::timeout, [ &ticks ] { ++ticks; } );
            ticker.start( 1 );

            const auto id = host->checkContent( claimed, heard.report() );
            REQUIRE( id != 0 );
            REQUIRE( waitUiState( [ & ] { return probeA.claimsRunning() == 1; }, 10000 ) );
            turnFor( 50 );

            THEN( "This thread turns while the plugin holds, and nothing is reported yet" )
            {
                REQUIRE( ticks > 0 );
                REQUIRE( heard.reports == 0 );

                probeA.openGate();
                REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );
                REQUIRE( heard.claimedBy == ClaimProbeAId );
            }

            AND_WHEN( "That plugin is unloaded meanwhile" )
            {
                const GateOpener opener( probeA );
                host->unloadPlugin( ClaimProbeAId );

                THEN( "The unload waited for the check to return before it shut the plugin "
                      "down, the plugin's claim does not count, and the next plugin claims it" )
                {
                    REQUIRE( probeA.claimsRunning() == 0 );
                    REQUIRE( probeA.shutdownCalls() == 1 );
                    REQUIRE( probeA.shutdownsWhileClaiming() == 0 );
                    REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );
                    REQUIRE( heard.claimedBy == ClaimProbeBId );
                    REQUIRE( probeA.claimsCalls() == 1 );
                    REQUIRE( probeA.claimsWhileShutDown() == 0 );
                }
            }

            AND_WHEN( "The plugin it asks next is unloaded meanwhile" )
            {
                probeA.setMagic( "SOMETHINGELSE" );
                host->unloadPlugin( ClaimProbeBId );

                THEN( "That unload does not wait for the check, and that plugin is not asked" )
                {
                    REQUIRE( probeA.claimsRunning() == 1 );
                    REQUIRE( probeB.shutdownCalls() == 1 );

                    probeA.openGate();
                    REQUIRE( waitUiState( [ & ] { return heard.reports == 1; }, 10000 ) );
                    REQUIRE( heard.claimedBy.isEmpty() );
                    REQUIRE( probeB.claimsCalls() == 0 );
                }
            }

            AND_WHEN( "The check is cancelled" )
            {
                // Not claimed by A, B would be asked next.
                probeA.setMagic( "SOMETHINGELSE" );
                host->cancelContentCheck( id );
                probeA.openGate();

                THEN( "Its caller hears nothing, and no other plugin is asked" )
                {
                    REQUIRE( waitUiState( [ & ] { return probeA.claimsRunning() == 0; }, 10000 ) );
                    turnFor( 100 );
                    REQUIRE( heard.reports == 0 );
                    REQUIRE( probeB.claimsCalls() == 0 );
                }
            }

            AND_WHEN( "The Plugin Host goes meanwhile" )
            {
                {
                    const GateOpener opener( probeA );
                    host.reset();
                }

                THEN( "It waited for the check before it shut each plugin down, called none "
                      "after, and reports nothing" )
                {
                    REQUIRE( probeA.claimsRunning() == 0 );
                    REQUIRE( probeA.shutdownCalls() == 1 );
                    REQUIRE( probeA.shutdownsWhileClaiming() == 0 );
                    REQUIRE( probeA.claimsWhileShutDown() == 0 );
                    REQUIRE( probeB.claimsRunning() == 0 );
                    REQUIRE( probeB.shutdownCalls() == 1 );
                    REQUIRE( probeB.shutdownsWhileClaiming() == 0 );
                    REQUIRE( probeB.claimsWhileShutDown() == 0 );
                    turnFor( 50 );
                    REQUIRE( heard.reports == 0 );
                }
            }
        }

        WHEN( "Two files are checked at the same time" )
        {
            probeA.closeGate();
            ClaimHeard second;
            REQUIRE( host->checkContent( claimed, heard.report() ) != 0 );
            REQUIRE( host->checkContent( extensionless, second.report() ) != 0 );

            THEN( "The plugin is asked about both at once" )
            {
                REQUIRE( waitUiState( [ & ] { return probeA.claimsRunning() == 2; }, 10000 ) );
                probeA.openGate();
                REQUIRE( waitUiState( [ & ] { return heard.reports == 1 && second.reports == 1; },
                                      10000 ) );
                REQUIRE( heard.claimedBy == ClaimProbeAId );
                REQUIRE( second.claimedBy == ClaimProbeAId );
            }
        }

        if ( host ) {
            host->unloadAll();
        }
    }
}

SCENARIO( "A converter that does not claim by content converts by its extension as before",
          "[pluginhost][plugins][conversion]" )
{
    GIVEN( "A Plugin Host with a convert probe built against the 26.12 plugin header" )
    {
        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installConvertProbe( pluginRoot.path(), ConvertProbeId, QStringLiteral( "Convert Probe" ),
                             QStringLiteral( "ui" ),
                             QStringLiteral( LOGSQUIRL_CONVERT_PROBE_API2612_PATH ) );

        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        PluginHost host( catalog );
        REQUIRE( host.loadPlugin( ConvertProbeId ).isEmpty() );
        ConvertProbe probe( QStringLiteral( LOGSQUIRL_CONVERT_PROBE_API2612_PATH ) );
        probe.openGate();

        QTemporaryDir files;
        REQUIRE( files.isValid() );
        const auto input = files.filePath( "capture.cvx" );
        writeFile( input, "one\n" );

        WHEN( "A file is checked by its content" )
        {
            ClaimHeard heard;
            const auto id = host.checkContent( input, heard.report() );

            THEN( "No check starts, and nothing is reported" )
            {
                REQUIRE( id == 0 );
                turnFor( 50 );
                REQUIRE( heard.reports == 0 );
            }
        }

        WHEN( "A file with its extension is converted" )
        {
            std::optional<ConversionEnd> end;
            REQUIRE( host.converterForExtension( "cvx" ) == ConvertProbeId );
            REQUIRE( host.startConversion(
                         ConvertProbeId, input, files.filePath( "capture.cvx.txt" ),
                         ConversionReport{
                             .started = {},
                             .progress = {},
                             .finished = [ &end ]( const ConversionEnd& ended ) { end = ended; } } )
                     != 0 );

            THEN( "It converts as before" )
            {
                REQUIRE( waitUiState( [ & ] { return end.has_value(); }, 10000 ) );
                REQUIRE( end->outcome == ConversionOutcome::Succeeded );
            }
        }

        host.unloadAll();
    }
}
