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

// A Conversion runs in the background: the window stays in use, a progress
// dialog beside it shows how far the converter plugin is and cancels it, and
// the Transient Log File opens once it is done (#795). The convert probe of
// the unit tests holds after each line until its gate opens, so each test
// sees the Conversion under way. A converter plugin may also claim a file by
// its first bytes, asked on a worker thread when no converter's extensions
// match (#861); the claim probes of the unit tests do.

#include "applicationplugins.h"
#include "claim_probe.h"
#include "convert_probe.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "modal_answers.h"
#include "session.h"
#include "shown_widget.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QLibrary>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <catch2/catch_test_macros.hpp>
#include <kcompressiondevice.h>

#include <memory>

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

std::shared_ptr<Session> newSession()
{
    return std::make_shared<Session>( testSettingsPolicies(),
                                      std::make_shared<LogFormatCatalog>() );
}

/// The plugin config directory the host creates goes to a test location, not the user's.
struct StandardPathsInTestMode {
    StandardPathsInTestMode()
    {
        QStandardPaths::setTestModeEnabled( true );
    }
    ~StandardPathsInTestMode()
    {
        QStandardPaths::setTestModeEnabled( false );
    }
    StandardPathsInTestMode( const StandardPathsInTestMode& ) = delete;
    StandardPathsInTestMode& operator=( const StandardPathsInTestMode& ) = delete;
};

/// The texts of the window's Log File tabs.
QStringList logFileTabTexts( const MainWindow& window )
{
    QStringList texts;
    const auto* tabs = window.findChild<TabbedCrawlerWidget*>();
    if ( !tabs ) {
        return texts;
    }
    for ( const auto i : tabs->logFileTabs() ) {
        texts.append( tabs->tabText( i ) );
    }
    return texts;
}

/// Whether every Log File tab of the window holds a Transient Log File.
bool allTabsTransient( const MainWindow& window )
{
    const auto* tabs = window.findChild<TabbedCrawlerWidget*>();
    if ( !tabs ) {
        return false;
    }
    for ( const auto i : tabs->logFileTabs() ) {
        if ( !tabs->holdsTransientLogFile( i ) ) {
            return false;
        }
    }
    return true;
}

/// The progress dialogs of the window's Conversions.
QList<QProgressDialog*> conversionDialogs( const MainWindow& window )
{
    QList<QProgressDialog*> dialogs;
    for ( auto* dialog : window.findChildren<QProgressDialog*>() ) {
        if ( dialog->labelText().startsWith( QStringLiteral( "Converting " ) ) ) {
            dialogs.append( dialog );
        }
    }
    return dialogs;
}

/// The one progress dialog of a Conversion, or nullptr.
QProgressDialog* conversionDialog( const MainWindow& window )
{
    const auto dialogs = conversionDialogs( window );
    return dialogs.size() == 1 ? dialogs.front() : nullptr;
}

/// Answers Ok to every warning the window shows, and keeps what each said.
class WarningAnswerer {
public:
    WarningAnswerer()
    {
        QObject::connect( &driver_, &QTimer::timeout, [ this ] {
            if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
                if ( box->isVisible() && !box->property( "answered" ).toBool() ) {
                    box->setProperty( "answered", true );
                    warnings_.append( box->text() );
                    box->button( QMessageBox::Ok )->click();
                }
            }
        } );
        driver_.start( 10 );
    }

    const QStringList& warnings() const
    {
        return warnings_;
    }

private:
    QTimer driver_;
    QStringList warnings_;
};

/// A shown window whose convert probes have loaded, and a folder of files to convert.
struct ConvertingWindow {
    ConvertingWindow()
        : ConvertingWindow( { ConvertProbeId, PlainConvertProbeId } )
    {
    }

    /// A window with the probes enabled loaded only: the convert and claim probes.
    explicit ConvertingWindow( const QStringList& enabled )
    {
        REQUIRE( pluginRoot.isValid() );
        REQUIRE( fileDir.isValid() );
        installConvertProbe( pluginRoot.path() );
        installPlainConvertProbe( pluginRoot.path() );
        installClaimProbeA( pluginRoot.path() );
        installClaimProbeB( pluginRoot.path() );

        plugins = std::make_shared<ApplicationPlugins>( [ this, enabled ]( PluginCatalog& catalog,
                                                                           PluginHost& host ) {
            catalog.discoverPlugins( { pluginRoot.path() } );
            loadErrors = host.autoLoadPlugins( { .autoLoad = true, .enabled = enabled } ).errors;
        } );
        window = std::make_unique<MainWindow>( WindowSession{ newSession(), "Main", 0 }, plugins );
        showUntilExposed( *window );
        REQUIRE( waitUiState( [ this ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
    }

    ~ConvertingWindow()
    {
        window.reset();
        plugins.reset();
    }

    ConvertingWindow( const ConvertingWindow& ) = delete;
    ConvertingWindow& operator=( const ConvertingWindow& ) = delete;

    /// Writes content into a new file named name in the folder, and returns its path.
    QString writeFile( const QString& name, const QByteArray& content )
    {
        const auto path = fileDir.filePath( name );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( content );
        return path;
    }

    const StandardPathsInTestMode testPaths{};
    QTemporaryDir pluginRoot;
    QTemporaryDir fileDir;
    QStringList loadErrors;
    std::shared_ptr<ApplicationPlugins> plugins;
    std::unique_ptr<MainWindow> window;
};

} // namespace

SCENARIO(
    "A converter plugin converts in the background, and the Transient Log File opens when done",
    "[ui][plugins][conversion]" )
{
    GIVEN( "A window with a convert probe of kind \"ui\" that holds after its first line" )
    {
        ConvertingWindow converting;
        ConvertProbe probe( QStringLiteral( LOGSQUIRL_CONVERT_PROBE_PATH ) );
        const auto path = converting.writeFile( "capture.cvx", "one\ntwo\nthree\n" );
        auto& window = *converting.window;

        // Counts how often the event loop turned meanwhile.
        int ticks = 0;
        QTimer ticker;
        QObject::connect( &ticker, &QTimer::timeout, [ &ticks ] { ++ticks; } );
        ticker.start( 1 );

        WHEN( "the file is opened, as from the command line" )
        {
            window.loadInitialFile( path, false );
            REQUIRE( waitUiState( [ & ] { return probe.running() == 1; }, 10000 ) );

            THEN( "the window stays in use, and a non-modal progress dialog named after the file "
                  "and the plugin shows how far the converter is" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        const auto* dialog = conversionDialog( window );
                        return dialog && dialog->value() > 0;
                    },
                    10000 ) );
                REQUIRE( ticks > 0 );
                REQUIRE( logFileTabTexts( window ).isEmpty() );

                auto* dialog = conversionDialog( window );
                REQUIRE( dialog->labelText().contains( QStringLiteral( "capture.cvx" ) ) );
                REQUIRE( dialog->labelText().contains( QStringLiteral( "Convert Probe" ) ) );
                REQUIRE( dialog->windowModality() == Qt::NonModal );
                REQUIRE( dialog->maximum() > 0 );
                // One line of three is done.
                REQUIRE( dialog->value() == dialog->maximum() / 3 );
                // Only a Conversion that takes a while shows it.
                REQUIRE_FALSE( dialog->isVisible() );

                // It advances as the converter reports the next line.
                probe.step();
                REQUIRE( waitUiState(
                    [ & ] { return dialog->value() == dialog->maximum() * 2 / 3; }, 10000 ) );
                REQUIRE( logFileTabTexts( window ).isEmpty() );
            }

            AND_WHEN( "the converter runs through" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return conversionDialog( window ) != nullptr; }, 10000 ) );
                const QPointer<QProgressDialog> dialog = conversionDialog( window );
                probe.openGate();

                THEN( "the Transient Log File it wrote opens in a tab, and the dialog is gone" )
                {
                    REQUIRE( waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; },
                                          10000 ) );
                    REQUIRE( logFileTabTexts( window ).front().contains(
                        QStringLiteral( "capture.cvx.txt" ) ) );
                    REQUIRE( allTabsTransient( window ) );
                    REQUIRE( waitUiState( [ & ] { return dialog.isNull(); }, 10000 ) );
                }
            }

            AND_WHEN( "the user cancels it" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return conversionDialog( window ) != nullptr; }, 10000 ) );
                const QPointer<QProgressDialog> dialog = conversionDialog( window );
                auto* cancel = dialog->findChild<QPushButton*>();
                REQUIRE( cancel != nullptr );
                cancel->click();

                THEN( "the converter hears it and returns, no tab opens, the dialog is gone and "
                      "so is the output file" )
                {
                    REQUIRE(
                        waitUiState( [ & ] { return probe.cancellationsSeen() == 1; }, 10000 ) );
                    REQUIRE( waitUiState( [ & ] { return dialog.isNull(); }, 10000 ) );
                    REQUIRE( logFileTabTexts( window ).isEmpty() );
                    REQUIRE( waitUiState( [ & ] { return !QFile::exists( probe.outputPath( 0 ) ); },
                                          10000 ) );
                    REQUIRE( probe.conversionsReturned() == 1 );
                }
            }
        }

        WHEN( "the plugin is unloaded while it converts the file" )
        {
            window.loadInitialFile( path, false );
            REQUIRE(
                waitUiState( [ & ] { return conversionDialog( window ) != nullptr; }, 10000 ) );
            const QPointer<QProgressDialog> dialog = conversionDialog( window );
            REQUIRE( waitUiState( [ & ] { return probe.running() == 1; }, 10000 ) );
            converting.plugins->host().unloadPlugin( ConvertProbeId );

            THEN( "the Conversion was cancelled and waited for, its dialog is gone and no tab "
                  "opens" )
            {
                REQUIRE( probe.cancellationsSeen() == 1 );
                REQUIRE( probe.running() == 0 );
                REQUIRE_FALSE( converting.plugins->host().isLoaded( ConvertProbeId ) );
                REQUIRE( waitUiState( [ & ] { return dialog.isNull(); }, 10000 ) );
                REQUIRE( waitUiState( [ & ] { return !QFile::exists( probe.outputPath( 0 ) ); },
                                      10000 ) );
                REQUIRE( logFileTabTexts( window ).isEmpty() );
            }
        }

        WHEN( "the file is dropped on the window" )
        {
            probe.openGate();
            QMimeData mimeData;
            mimeData.setUrls( { QUrl::fromLocalFile( path ) } );
            // A drop reaches the widget that accepted the drag entering it.
            QDragEnterEvent enter( QPoint( 10, 10 ), Qt::CopyAction, &mimeData, Qt::LeftButton,
                                   Qt::NoModifier );
            QApplication::sendEvent( &window, &enter );
            REQUIRE( enter.isAccepted() );
            QDropEvent drop( QPointF( 10, 10 ), Qt::CopyAction, &mimeData, Qt::LeftButton,
                             Qt::NoModifier );
            QApplication::sendEvent( &window, &drop );

            THEN( "it is converted by the plugin of kind \"ui\" all the same" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; }, 10000 ) );
                REQUIRE( logFileTabTexts( window ).front().contains(
                    QStringLiteral( "capture.cvx.txt" ) ) );
            }
        }

        WHEN( "two files for the same plugin are opened" )
        {
            const auto second = converting.writeFile( "second.cvx", "four\n" );
            window.loadInitialFile( path, false );
            window.loadInitialFile( second, false );
            REQUIRE( waitUiState( [ & ] { return probe.running() == 1; }, 10000 ) );

            THEN( "they convert one after the other, each with a dialog as its turn comes" )
            {
                REQUIRE( waitUiState( [ & ] { return conversionDialogs( window ).size() == 1; },
                                      10000 ) );
                REQUIRE( probe.conversionsStarted() == 1 );

                probe.openGate();
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 2; }, 10000 ) );
                REQUIRE( probe.conversionsStarted() == 2 );
                REQUIRE(
                    waitUiState( [ & ] { return conversionDialogs( window ).isEmpty(); }, 10000 ) );
            }
        }
    }
}

SCENARIO( "A plugin with the plain convert only converts in the background and is cancelled",
          "[ui][plugins][conversion]" )
{
    GIVEN( "A window with a plain convert probe that holds after its first line" )
    {
        ConvertingWindow converting;
        ConvertProbe plain( QStringLiteral( LOGSQUIRL_CONVERT_PROBE_PLAIN_PATH ) );
        const auto path = converting.writeFile( "capture.cvxplain", "one\ntwo\n" );
        auto& window = *converting.window;

        WHEN( "the file is opened" )
        {
            window.loadInitialFile( path, false );
            REQUIRE( waitUiState( [ & ] { return plain.running() == 1; }, 10000 ) );
            REQUIRE(
                waitUiState( [ & ] { return conversionDialog( window ) != nullptr; }, 10000 ) );
            const QPointer<QProgressDialog> dialog = conversionDialog( window );

            THEN( "its dialog shows the Conversion as busy, without a share done" )
            {
                REQUIRE( dialog->labelText().contains( QStringLiteral( "Plain Convert Probe" ) ) );
                REQUIRE( dialog->minimum() == 0 );
                REQUIRE( dialog->maximum() == 0 );
            }

            AND_WHEN( "the user cancels it, and the converter later returns its result" )
            {
                auto* cancel = dialog->findChild<QPushButton*>();
                REQUIRE( cancel != nullptr );
                cancel->click();
                plain.openGate();

                THEN( "the result is discarded: no tab opens, and the output file is gone" )
                {
                    REQUIRE(
                        waitUiState( [ & ] { return plain.conversionsReturned() == 1; }, 10000 ) );
                    REQUIRE( waitUiState( [ & ] { return dialog.isNull(); }, 10000 ) );
                    REQUIRE( waitUiState( [ & ] { return !QFile::exists( plain.outputPath( 0 ) ); },
                                          10000 ) );
                    REQUIRE( logFileTabTexts( window ).isEmpty() );
                }
            }
        }
    }
}

SCENARIO( "A failed Conversion is reported and opens no tab", "[ui][plugins][conversion]" )
{
    GIVEN( "A window with a convert probe, and a file it fails to convert" )
    {
        ConvertingWindow converting;
        ConvertProbe probe( QStringLiteral( LOGSQUIRL_CONVERT_PROBE_PATH ) );
        probe.openGate();
        const auto path = converting.writeFile( "failing.cvx", "fail\n" );
        auto& window = *converting.window;
        WarningAnswerer answerer;

        WHEN( "the file is opened" )
        {
            window.loadInitialFile( path, false );

            THEN( "a warning names the plugin and the file, no tab opens -- not the raw file "
                  "either -- and the output file is gone" )
            {
                REQUIRE( waitUiState( [ & ] { return !answerer.warnings().isEmpty(); }, 10000 ) );
                REQUIRE( answerer.warnings().size() == 1 );
                REQUIRE(
                    answerer.warnings().front().contains( QStringLiteral( "Convert Probe" ) ) );
                REQUIRE( answerer.warnings().front().contains( QStringLiteral( "failing.cvx" ) ) );
                REQUIRE( logFileTabTexts( window ).isEmpty() );
                REQUIRE( waitUiState( [ & ] { return !QFile::exists( probe.outputPath( 0 ) ); },
                                      10000 ) );
                REQUIRE( conversionDialogs( window ).isEmpty() );
            }
        }
    }
}

SCENARIO( "Without a converter that claims by content, a file opens at once as before",
          "[ui][plugins][conversion]" )
{
    GIVEN( "A window whose convert probes claim files by their extension only" )
    {
        ConvertingWindow converting;
        const auto path = converting.writeFile( "plain.log", "one\ntwo\n" );
        auto& window = *converting.window;

        WHEN( "a file no extension matches is opened" )
        {
            window.loadInitialFile( path, false );

            THEN( "its tab is there when the call returns, as the file was" )
            {
                REQUIRE( logFileTabTexts( window )
                         == QStringList{ QStringLiteral( "plain.log" ) } );
                REQUIRE_FALSE( allTabsTransient( window ) );
            }
        }
    }
}

SCENARIO( "A converter plugin claims a file by its content when it is opened",
          "[ui][plugins][conversion]" )
{
    GIVEN( "A window with two claim probes, A's plugin id the smaller" )
    {
        ConvertingWindow converting( { ClaimProbeAId, ClaimProbeBId } );
        ClaimProbe probeA( QStringLiteral( LOGSQUIRL_CLAIM_PROBE_A_PATH ) );
        ClaimProbe probeB( QStringLiteral( LOGSQUIRL_CLAIM_PROBE_B_PATH ) );
        auto& window = *converting.window;

        WHEN( "a .log file that starts with the magic both claim is opened" )
        {
            const auto path = converting.writeFile( "capture.log", ClaimMagic + "\none\n" );
            window.loadInitialFile( path, false );

            THEN( "no tab is there when the call returns; the plugin with the smaller id, asked "
                  "off the UI thread, converts it, and what it wrote opens" )
            {
                REQUIRE( logFileTabTexts( window ).isEmpty() );
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; }, 10000 ) );
                REQUIRE( logFileTabTexts( window ).front().contains(
                    QStringLiteral( "capture.log.txt" ) ) );
                REQUIRE( allTabsTransient( window ) );
                REQUIRE( probeA.conversions() == 1 );
                REQUIRE( probeB.conversions() == 0 );
                // Once: what the Conversion wrote is not offered again.
                REQUIRE( probeA.claimsCalls() == 1 );
                REQUIRE( probeA.claimsOnInitThread() == 0 );
                // The first that claims it is the last asked.
                REQUIRE( probeB.claimsCalls() == 0 );
            }
        }

        WHEN( "a file without an extension that starts with the magic is opened" )
        {
            const auto path = converting.writeFile( "capture", ClaimMagic + "\none\n" );
            window.loadInitialFile( path, false );

            THEN( "it is converted all the same" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; }, 10000 ) );
                REQUIRE(
                    logFileTabTexts( window ).front().contains( QStringLiteral( "capture.txt" ) ) );
                REQUIRE( probeA.conversions() == 1 );
            }
        }

        WHEN( "a file without the magic is opened" )
        {
            const auto path = converting.writeFile( "plain.log", "one\ntwo\n" );
            window.loadInitialFile( path, false );

            THEN( "both are asked, and it opens as text once they said no" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; }, 10000 ) );
                REQUIRE( logFileTabTexts( window )
                         == QStringList{ QStringLiteral( "plain.log" ) } );
                REQUIRE_FALSE( allTabsTransient( window ) );
                REQUIRE( probeA.claimsCalls() == 1 );
                REQUIRE( probeB.claimsCalls() == 1 );
                REQUIRE( probeA.conversions() == 0 );
                REQUIRE( probeB.conversions() == 0 );
            }
        }

        WHEN( "a file one plugin's extensions match starts with the magic the other claims" )
        {
            const auto path = converting.writeFile( "capture.clm", ClaimMagic + "\none\n" );
            window.loadInitialFile( path, false );

            THEN( "the extension wins: that plugin converts it, though the other's id is "
                  "smaller, and no plugin is asked about the content" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; }, 10000 ) );
                REQUIRE( probeB.conversions() == 1 );
                REQUIRE( probeA.conversions() == 0 );
                REQUIRE( probeA.claimsCalls() == 0 );
                REQUIRE( probeB.claimsCalls() == 0 );
            }
        }

        WHEN( "a file whose extension both plugins list is opened" )
        {
            const auto path = converting.writeFile( "capture.clmboth", "one\n" );
            window.loadInitialFile( path, false );

            THEN( "the plugin with the smaller id converts it" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; }, 10000 ) );
                REQUIRE( probeA.conversions() == 1 );
                REQUIRE( probeB.conversions() == 0 );
            }
        }

        WHEN( "a compressed file whose own head a plugin claims is opened" )
        {
            const auto path = converting.fileDir.filePath( "archived.log.gz" );
            {
                KCompressionDevice archive( path, KCompressionDevice::GZip );
                REQUIRE( archive.open( QIODevice::WriteOnly ) );
                REQUIRE( archive.write( "one\ntwo\n" ) > 0 );
            }
            // A gzip file starts with these two bytes.
            probeA.setMagic( QByteArrayLiteral( "\x1f\x8b" ) );
            ModalAnswers modals;
            window.loadInitialFile( path, false );

            THEN( "it is converted, not decompressed" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; }, 10000 ) );
                REQUIRE( logFileTabTexts( window ).front().contains(
                    QStringLiteral( "archived.log.gz.txt" ) ) );
                REQUIRE( probeA.conversions() == 1 );
                REQUIRE( modals.unexpectedMessageBoxes() == 0 );
            }
        }

        WHEN( "a compressed file whose decompressed text a plugin claims is opened" )
        {
            const auto path = converting.fileDir.filePath( "member.log.gz" );
            {
                KCompressionDevice archive( path, KCompressionDevice::GZip );
                REQUIRE( archive.open( QIODevice::WriteOnly ) );
                REQUIRE( archive.write( ClaimMagic + "\none\n" ) > 0 );
            }
            ModalAnswers modals;
            modals.click( QMessageBox::Yes ).answerWith( []( QDialog& modal ) {
                waitUiState( [ &modal ] { return !modal.isVisible(); }, 30'000 );
            } );
            window.loadInitialFile( path, false );

            THEN( "it is decompressed, and the decompressed file is offered again and converted" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; }, 30000 ) );
                REQUIRE( allTabsTransient( window ) );
                REQUIRE( probeA.conversions() == 1 );
                // Once for the compressed file, once for what it decompressed to.
                REQUIRE( probeA.claimsCalls() == 2 );
            }
        }

        WHEN( "the check of the file holds" )
        {
            probeA.closeGate();
            // Counts how often the event loop turned meanwhile.
            int ticks = 0;
            QTimer ticker;
            QObject::connect( &ticker, &QTimer::timeout, [ &ticks ] { ++ticks; } );
            ticker.start( 1 );

            const auto path = converting.writeFile( "capture.log", ClaimMagic + "\none\n" );
            window.loadInitialFile( path, false );
            REQUIRE( waitUiState( [ & ] { return probeA.claimsRunning() == 1; }, 10000 ) );
            const auto ticksBefore = ticks;

            THEN( "the window stays in use, and no tab opens until the check returns" )
            {
                REQUIRE( waitUiState( [ & ] { return ticks > ticksBefore + 5; }, 10000 ) );
                REQUIRE( logFileTabTexts( window ).isEmpty() );

                probeA.openGate();
                REQUIRE(
                    waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; }, 10000 ) );
                REQUIRE( probeA.conversions() == 1 );
            }

            AND_WHEN( "the file is opened again meanwhile" )
            {
                window.loadInitialFile( path, false );
                probeA.openGate();

                THEN( "it is checked and converted once" )
                {
                    REQUIRE( waitUiState( [ & ] { return logFileTabTexts( window ).size() == 1; },
                                          10000 ) );
                    REQUIRE( probeA.claimsCalls() == 1 );
                    REQUIRE( probeA.conversions() == 1 );
                }
            }

            AND_WHEN( "the window goes meanwhile" )
            {
                converting.window.reset();
                probeA.openGate();

                THEN( "nothing is converted for it" )
                {
                    REQUIRE( waitUiState( [ & ] { return probeA.claimsRunning() == 0; }, 10000 ) );
                    QEventLoop loop;
                    QTimer::singleShot( 100, &loop, &QEventLoop::quit );
                    loop.exec();
                    REQUIRE( probeA.conversions() == 0 );
                }
            }
        }
    }
}
