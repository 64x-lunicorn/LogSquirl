/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2021 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "log.h"
#include <QtGlobal>
#include <qapplication.h>
#include <qthreadpool.h>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // _WIN32

#include <mimalloc.h>
#include <roaring.hh>

#ifdef LOGSQUIRL_HAS_HS
#include <hs.h>
#endif

#include "tbb/global_control.h"

#include "configuration.h"
#include "logger.h"
#include "mainwindow.h"
#include "styles.h"

#include "cli.h"
#include "logsquirlapp.h"
#include "logsquirl_version.h"

#include <QPainter>
#include <QPixmap>
#include <QSplashScreen>

#ifdef LOGSQUIRL_PORTABLE
const bool PersistentInfo::ForcePortable = true;
#else
const bool PersistentInfo::ForcePortable = false;
#endif

void setApplicationAttributes( bool enableQtHdpi, int scaleFactorRounding )
{
    // When QNetworkAccessManager is instantiated it regularly starts polling
    // all network interfaces to see if anything changes and if so, what. This
    // creates a latency spike every 10 seconds on Mac OS 10.12+ and Windows 7 >=
    // when on a wifi connection.
    // So here we disable it for lack of better measure.
    // This will also cause this message: QObject::startTimer: Timers cannot
    // have negative intervals
    // For more info see:
    // - https://bugreports.qt.io/browse/QTBUG-40332
    // - https://bugreports.qt.io/browse/QTBUG-46015
    qputenv( "QT_BEARER_POLL_TIMEOUT", QByteArray::number( std::numeric_limits<int>::max() ) );

    Q_UNUSED( enableQtHdpi );
    Q_UNUSED( scaleFactorRounding );

    QCoreApplication::setAttribute( Qt::AA_DontShowIconsInMenus );
}

int main( int argc, char* argv[] )
{
#ifdef LOGSQUIRL_USE_MIMALLOC
    mi_process_init();
#endif

    const auto& config = Configuration::getSynced();
    setApplicationAttributes( config.enableQtHighDpi(), config.scaleFactorRounding() );

    LogSquirlApp app( argc, argv );


    MainWindow::installLanguage( config.language() );
    CliParameters parameters( app );

    const auto logLevel
        = static_cast<logging::LogLevel>( std::max( parameters.log_level, config.loggingLevel() ) );
    logging::enableLogging( parameters.enable_logging || config.enableLogging(), logLevel );
    logging::enableFileLogging( parameters.log_to_file || config.enableLogging(), logLevel );

    app.initCrashHandler();

    auto maxConcurrency
        = tbb::global_control::active_value( tbb::global_control::max_allowed_parallelism );

    LOG_INFO << "LogSquirl instance"
             << ", mimalloc v" << mi_version()
             << ", default concurrency " << maxConcurrency;


    roaring_memory_t roaring_memory_allocators;
    roaring_memory_allocators.malloc = mi_malloc;
    roaring_memory_allocators.realloc = mi_realloc;
    roaring_memory_allocators.calloc = mi_calloc;
    roaring_memory_allocators.free = mi_free;
    roaring_memory_allocators.aligned_malloc = mi_aligned_alloc;
    roaring_memory_allocators.aligned_free = mi_free;
    roaring_init_memory_hook(roaring_memory_allocators);

#ifdef LOGSQUIRL_HAS_HS
    hs_set_allocator(mi_malloc, mi_free);
#endif

    if ( maxConcurrency < 2 ) {
        maxConcurrency = 2;
        LOG_INFO << "Overriding default concurrency to " << maxConcurrency;
        tbb::global_control concurrencyControl( tbb::global_control::max_allowed_parallelism,
                                                maxConcurrency );
        QThreadPool::globalInstance()->setMaxThreadCount( static_cast<int>( maxConcurrency ) );
    }

    if ( !parameters.multi_instance && app.isSecondary() ) {
        LOG_INFO << "Found another logsquirl, pid " << app.primaryPid();
        app.sendFilesToPrimaryInstance( parameters.filenames );
    }
    else {
        StyleManager::applyStyle( config.style() );

        // Show a splash screen while the application is initialising.
        QSplashScreen* splash = nullptr;
        if ( config.showSplashScreen() ) {
            constexpr int kSplashWidth = 420;
            constexpr int kSplashHeight = 260;
            constexpr int kIconSize = 96;

            QPixmap splashPixmap( kSplashWidth, kSplashHeight );
            splashPixmap.fill( QColor( "#282828" ) );

            QPainter painter( &splashPixmap );
            painter.setRenderHint( QPainter::Antialiasing );
            painter.setRenderHint( QPainter::SmoothPixmapTransform );

            // Draw the app icon centred near the top
            const QPixmap icon( ":/images/logsquirl-logo.png" );
            const auto scaled = icon.scaled( kIconSize, kIconSize,
                                             Qt::KeepAspectRatio, Qt::SmoothTransformation );
            painter.drawPixmap( ( kSplashWidth - kIconSize ) / 2, 36, scaled );

            // App name
            QFont nameFont( "Segoe UI", 24, QFont::DemiBold );
            painter.setFont( nameFont );
            painter.setPen( Qt::white );
            painter.drawText( QRect( 0, 140, kSplashWidth, 36 ),
                              Qt::AlignHCenter, QStringLiteral( "LogSquirl" ) );

            // Version
            QFont versionFont( "Segoe UI", 11 );
            painter.setFont( versionFont );
            painter.setPen( QColor( "#808080" ) );
            painter.drawText( QRect( 0, 174, kSplashWidth, 20 ),
                              Qt::AlignHCenter,
                              QStringLiteral( "v%1" ).arg( logsquirlVersion() ) );

            // Accent bar at the bottom
            painter.fillRect( 0, kSplashHeight - 4, kSplashWidth, 4, QColor( "#2a82da" ) );

            painter.end();

            splash = new QSplashScreen( splashPixmap );
            splash->show();
            app.processEvents();
        }

        auto updateSplash = [ splash ]( const QString& message ) {
            if ( splash ) {
                splash->showMessage( message, Qt::AlignHCenter | Qt::AlignBottom,
                                     QColor( "#808080" ) );
                QCoreApplication::processEvents();
            }
        };

        updateSplash( QObject::tr( "Loading settings..." ) );

        auto startNewSession = true;
        MainWindow* mw = nullptr;

        updateSplash( QObject::tr( "Restoring session..." ) );

        if ( parameters.load_session
             || ( parameters.filenames.empty() && !parameters.new_session
                  && config.loadLastSession() ) ) {
            mw = app.reloadSession();
            startNewSession = false;
        }
        else {
            mw = app.newWindow();
            mw->reloadGeometry();
            mw->show();
        }

        if ( splash ) {
            splash->finish( mw );
            delete splash;
        }

        if ( parameters.window_width > 0 && parameters.window_height > 0 ) {
            mw->resize( parameters.window_width, parameters.window_height );
        }

        for ( const auto& filename : parameters.filenames ) {
            mw->loadInitialFile( filename, parameters.follow_file );
        }

        if ( startNewSession ) {
            app.clearInactiveSessions();
        }

        app.startBackgroundTasks();
    }

    return app.exec();
}
