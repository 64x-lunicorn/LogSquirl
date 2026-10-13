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
// sees the Conversion under way.

#include "applicationplugins.h"
#include "convert_probe.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "session.h"
#include "shown_widget.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
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
    {
        REQUIRE( pluginRoot.isValid() );
        REQUIRE( fileDir.isValid() );
        installConvertProbe( pluginRoot.path() );
        installPlainConvertProbe( pluginRoot.path() );

        plugins = std::make_shared<ApplicationPlugins>(
            [ this ]( PluginCatalog& catalog, PluginHost& host ) {
                catalog.discoverPlugins( { pluginRoot.path() } );
                loadErrors
                    = host.autoLoadPlugins( { .autoLoad = true,
                                              .enabled = { ConvertProbeId, PlainConvertProbeId } } )
                          .errors;
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
