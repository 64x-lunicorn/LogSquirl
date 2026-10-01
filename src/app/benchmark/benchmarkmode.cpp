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

#include "benchmarkmode.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <utility>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QSettings>
#include <QSize>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

#include "cli.h"
#include "datalocation.h"
#include "logsquirl_version.h"
#include "logsquirlapp.h"
#include "persistentinfo.h"
#include "scenariorun.h"

namespace logsquirl::benchmark {

namespace {

constexpr const char BenchmarkOption[] = "--benchmark";

// The window a scenario opens, unless --window-width and --window-height say
// otherwise: one size for every run, so runs paint the same number of Log
// Lines.
constexpr QSize DefaultWindowSize{ 1280, 800 };

// What a Benchmark Run starts with instead of the user's settings: the
// defaults, except for what would make it wait on something it does not
// measure, or measure something else.
void writeSettings( const QString& settingsPath )
{
    QSettings settings{ settingsPath, QSettings::IniFormat };
    // A "new version" message box would block the event loop.
    settings.setValue( "versionchecker.enabled", false );
    settings.setValue( "session.loadLast", false );
    settings.setValue( "session.confirmTabClose", false );
    settings.setValue( "view.showSplashScreen", false );
    // The shipped plugins are not loaded: a run measures LogSquirl.
    settings.setValue( "plugins.autoLoad", false );
    // Every run indexes its Log File; an Index Cache would also live in the
    // user profile.
    settings.setValue( "perf.useIndexCache", false );
    settings.sync();
}

void printScenarios( std::FILE* stream )
{
    std::fprintf( stream, "The benchmark scenarios are:\n" );
    for ( const auto* entry : ScenarioRegistry::instance().entries() ) {
        std::fprintf( stream, "  %-20s %s\n", qPrintable( entry->name ),
                      qPrintable( entry->description ) );
    }
}

} // namespace

// The run a scenario is handed: the report, the Log Files and options, and the
// application to open windows in. It writes the report and ends the event
// loop when the scenario is done or the time is up.
class BenchmarkRun : public ScenarioRun {
public:
    BenchmarkRun( const CliParameters& parameters, const ScenarioEntry& scenario,
                  ProcessClock clock, QString dataDirectory, QMap<QString, QString> options )
        : report_( scenario.name, clock )
        , logFiles_( parameters.filenames )
        , options_( std::move( options ) )
        , output_( parameters.benchmark_output )
        , dataDirectory_( std::move( dataDirectory ) )
        , timeoutS_( parameters.benchmark_timeout_s )
        , scenario_( scenario.create() )
    {
        if ( parameters.window_width > 0 && parameters.window_height > 0 ) {
            windowSize_ = QSize{ parameters.window_width, parameters.window_height };
        }
    }

    void start( LogSquirlApp& app )
    {
        app_ = &app;

        report_.setApplication( QJsonObject{
            { "version", QString( logsquirlVersion() ) },
            { "commit", QString( logsquirlCommit() ) },
        } );
        report_.setPlatform( QJsonObject{
            { "os", QSysInfo::prettyProductName() },
            { "kernel", QSysInfo::kernelVersion() },
            { "cpu_architecture", QSysInfo::currentCpuArchitecture() },
            { "qt_version", QString::fromLatin1( qVersion() ) },
            { "qpa_platform", QGuiApplication::platformName() },
        } );
        for ( auto option = options_.cbegin(); option != options_.cend(); ++option ) {
            report_.setOption( option.key(), option.value() );
        }
        for ( const auto& logFile : logFiles_ ) {
            report_.addLogFile( logFile, QFileInfo( logFile ).size() );
        }

        if ( timeoutS_ > 0 ) {
            QTimer::singleShot( std::chrono::seconds{ timeoutS_ }, &context_, [ this ] {
                fail(
                    QStringLiteral( "the scenario did not finish within %1 s" ).arg( timeoutS_ ) );
            } );
        }

        // From the event loop, as a user's first events are.
        QTimer::singleShot( 0, &context_, [ this ] {
            report_.markScenarioStart();
            scenario_->start( *this );
        } );
    }

    // Before the application reads a setting: the scenario writes those it
    // measures under.
    void prepare()
    {
        scenario_->prepare( *this );
    }

    int exitCode() const
    {
        return exitCode_;
    }

    BenchmarkReport& report() override
    {
        return report_;
    }

    const std::vector<QString>& logFiles() const override
    {
        return logFiles_;
    }

    QString option( const QString& name, const QString& fallback ) const override
    {
        return options_.value( name, fallback );
    }

    QString dataDirectory() const override
    {
        return dataDirectory_;
    }

    MainWindow* newWindow() override
    {
        auto* window = app_->newWindow();
        window->resize( windowSize_ );
        window->show();
        return window;
    }

    MainWindow* restoreSession() override
    {
        return app_->reloadSession();
    }

    QObject* context() override
    {
        return &context_;
    }

    void finish() override
    {
        end();
    }

    void fail( const QString& reason ) override
    {
        if ( ended_ ) {
            return;
        }
        report_.fail( reason );
        end();
    }

private:
    void end()
    {
        if ( ended_ ) {
            return;
        }
        ended_ = true;

        const auto written = write( report_.toJson( peakResidentBytes() ) );
        exitCode_ = written && !report_.failed() ? 0 : 1;

        // Not from within whatever the scenario was called from, a paint
        // among others.
        QTimer::singleShot( 0, &context_, [ this ] {
            QApplication::closeAllWindows();
            QCoreApplication::exit( exitCode_ );
        } );
    }

    bool write( const QJsonObject& json ) const
    {
        const auto document = QJsonDocument( json ).toJson( QJsonDocument::Indented );
        if ( output_.isEmpty() ) {
            std::cout << document.toStdString() << std::flush;
            return true;
        }

        QFile file( output_ );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate )
             || file.write( document ) != document.size() ) {
            std::fprintf( stderr, "logsquirl: could not write the benchmark report to %s: %s\n",
                          qPrintable( output_ ), qPrintable( file.errorString() ) );
            return false;
        }
        return true;
    }

    LogSquirlApp* app_ = nullptr;
    QObject context_;
    BenchmarkReport report_;
    std::vector<QString> logFiles_;
    QMap<QString, QString> options_;
    QString output_;
    QString dataDirectory_;
    int timeoutS_;
    QSize windowSize_ = DefaultWindowSize;
    std::unique_ptr<Scenario> scenario_;
    bool ended_ = false;
    int exitCode_ = 1;
};

bool BenchmarkMode::requested( int argc, char* argv[] )
{
    const auto length = std::strlen( BenchmarkOption );
    for ( int index = 1; index < argc; ++index ) {
        const char* argument = argv[ index ];
        if ( std::strncmp( argument, BenchmarkOption, length ) == 0
             && ( argument[ length ] == '\0' || argument[ length ] == '=' ) ) {
            return true;
        }
    }
    return false;
}

void BenchmarkMode::prepareProcess()
{
    // A lock and local socket (a named pipe on Windows) of its own: the run
    // neither hands its Log Files to a LogSquirl the user is running nor is
    // handed theirs (logsquirlapp.h). Short: a local socket's path is at most
    // 103 characters, and it lies in the temporary directory.
    if ( !qEnvironmentVariableIsSet( "LOGSQUIRL_INSTANCE_ID" ) ) {
        qputenv( "LOGSQUIRL_INSTANCE_ID",
                 "b" + QByteArray::number( QCoreApplication::applicationPid() ) );
    }
    // Where Qt still resolves a standard location -- the Index Cache, which
    // the run does not use -- it is not the user's.
    QStandardPaths::setTestModeEnabled( true );
}

std::unique_ptr<BenchmarkMode> BenchmarkMode::prepare( const CliParameters& parameters,
                                                       Clock::time_point mainEntered )
{
    if ( !ScenarioRegistry::instance().find( parameters.benchmark_scenario ) ) {
        std::fprintf( stderr, "logsquirl: there is no benchmark scenario '%s'.\n",
                      qPrintable( parameters.benchmark_scenario ) );
        printScenarios( stderr );
        return nullptr;
    }

    for ( const auto& option : parameters.benchmark_options ) {
        if ( option.indexOf( '=' ) <= 0 ) {
            std::fprintf( stderr, "logsquirl: --benchmark-option takes name=value, not '%s'.\n",
                          qPrintable( option ) );
            return nullptr;
        }
    }

    auto mode = std::unique_ptr<BenchmarkMode>( new BenchmarkMode( parameters, mainEntered ) );
    if ( !mode->dataDirectory_->isValid() ) {
        std::fprintf( stderr, "logsquirl: no temporary directory for the benchmark run: %s\n",
                      qPrintable( mode->dataDirectory_->errorString() ) );
        return nullptr;
    }
    if ( !DataLocation::isolateCurrentIn( mode->dataDirectory_->path() ) ) {
        std::fprintf( stderr, "logsquirl: the benchmark run was isolated too late.\n" );
        return nullptr;
    }
    writeSettings( DataLocation::current().portableSettingsPath() );
    mode->run_->prepare();
    return mode;
}

BenchmarkMode::BenchmarkMode( const CliParameters& parameters, Clock::time_point mainEntered )
    : dataDirectory_( std::make_unique<QTemporaryDir>(
          QDir( QDir::tempPath() ).filePath( "logsquirl-benchmark-XXXXXX" ) ) )
{
    QMap<QString, QString> options;
    for ( const auto& option : parameters.benchmark_options ) {
        const auto separator = option.indexOf( '=' );
        options.insert( option.left( separator ), option.mid( separator + 1 ) );
    }

    run_ = std::make_unique<BenchmarkRun>(
        parameters, *ScenarioRegistry::instance().find( parameters.benchmark_scenario ),
        ProcessClock::measure( mainEntered ), dataDirectory_->path(), std::move( options ) );
}

BenchmarkMode::~BenchmarkMode() = default;

void BenchmarkMode::start( LogSquirlApp& app )
{
    run_->start( app );
}

int BenchmarkMode::end( int eventLoopExitCode )
{
    const auto exitCode = run_->exitCode() != 0 ? run_->exitCode() : eventLoopExitCode;
    run_.reset();

    // Whatever the settings still hold is written now, while the directory is
    // there, and not into a directory made again when they are destroyed.
    PersistentInfo::getSettings( app_settings{} ).sync();
    PersistentInfo::getSettings( session_settings{} ).sync();
    dataDirectory_.reset();

    return exitCode;
}

} // namespace logsquirl::benchmark
