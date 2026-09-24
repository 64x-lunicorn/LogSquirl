/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#ifndef LOGSQUIRL_LOGSQUIRLAPP_H
#define LOGSQUIRL_LOGSQUIRLAPP_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <numeric>
#include <qapplication.h>
#include <stack>

#include <QApplication>
#include <vector>

#include <QCborValue>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QUuid>

#ifdef Q_OS_MAC
#include <QFileOpenEvent>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "applicationplugins.h"
#include "configuration.h"
#include "crashhandler.h"
#include "filewatcher.h"
#include "log.h"
#include "logformatcatalog.h"
#include "logsquirl_version.h"
#include "session.h"
#include "settingspolicies.h"
#include "teamfolder.h"
#include "uuid.h"

#include <kdsingleapplication.h>

#include "mainwindow.h"
#include "messagereceiver.h"
#include "installsource.h"
#include "versionchecker.h"

class LogSquirlApp : public QApplication {

    Q_OBJECT

    // The name KDSingleApplication builds its lock file and local socket (a
    // named pipe on Windows) from. Unchanged from KDSingleApplication's own
    // default (the executable's file name) unless LOGSQUIRL_INSTANCE_ID is
    // set, in which case it is folded in.
    //
    // On macOS and Linux the lock file and socket already live under
    // QDir::tempPath(), which an isolated E2E instance redirects (#328), so
    // two instances never collide there regardless of this name. On Windows
    // a named pipe is not scoped by a directory at all (#320): without a
    // per-instance name here, an isolated test instance could hand its files
    // to, or be activated by, a real running LogSquirl on the same machine.
    // tests/e2e/isolated_instance.py sets the variable for its instances; a
    // real run never sets it, so it keeps today's fixed name.
    static QString singleApplicationName()
    {
        auto executableName = QFileInfo( QCoreApplication::applicationFilePath() ).fileName();
        const auto instanceId = qEnvironmentVariable( "LOGSQUIRL_INSTANCE_ID" );
        if ( instanceId.isEmpty() ) {
            return executableName;
        }
        return QStringLiteral( "%1-%2" ).arg( executableName, instanceId );
    }

public:
    LogSquirlApp( int& argc, char* argv[] )
        : QApplication( argc, argv )
        , singleApplication_( singleApplicationName() )
    {
        if ( singleApplication_.isPrimaryInstance() ) {
            QObject::connect( &singleApplication_, &KDSingleApplication::messageReceived,
                              &messageReceiver_, &MessageReceiver::receiveMessage,
                              Qt::QueuedConnection );

            QObject::connect( &messageReceiver_, &MessageReceiver::loadFile, this,
                              &LogSquirlApp::loadFileNonInteractive );
        }
    }

    // Everything a LogSquirl that shows main windows needs. main() calls it
    // before any event is processed, so before a Log File handed over by a
    // secondary instance can arrive. A secondary instance never calls it: it
    // only hands its Log Files over (#302).
    void prepareForMainWindows()
    {
        QFontDatabase::addApplicationFont( ":/fonts/DejaVuSansMono.ttf" );

        QNetworkProxyFactory::setUseSystemConfiguration( true );

        // The types the log data and the Open Log File signal with are
        // registered by them (#394); these are the application's own.
        qRegisterMetaType<std::vector<LineNumber>>( "std::vector<LineNumber>" );
        qRegisterMetaType<logsquirl::vector<LineNumber>>( "logsquirl::vector<LineNumber>" );
        qRegisterMetaType<Portion>( "Portion" );
        qRegisterMetaType<Selection>( "Selection" );
        qRegisterMetaType<QFNotification>( "QFNotification" );
        qRegisterMetaType<QFNotificationReachedEndOfFile>( "QFNotificationReachedEndOfFile" );
        qRegisterMetaType<QFNotificationReachedBegininningOfFile>(
            "QFNotificationReachedBegininningOfFile" );
        qRegisterMetaType<QFNotificationProgress>( "QFNotificationProgress" );
        qRegisterMetaType<QFNotificationInterrupted>( "QFNotificationInterrupted" );
        qRegisterMetaType<QuickFindMatcher>( "QuickFindMatcher" );

        // Settings are loaded by now: main() calls Configuration::getSynced()
        // first, so this snapshot of the Policies is complete. A settings
        // change is re-derived by the Session (#245).
        settingsPolicies_ = deriveSettingsPolicies( Configuration::get() );

        // The application's one Log Format Catalog, built once here, beside
        // the Policies, and handed to the Session -- and through it to every
        // window, view and options dialog. Nothing else builds one.
        logFormatCatalog_
            = std::make_shared<LogFormatCatalog>( LogFormatCatalog::defaultUserFormatsDirectory() );
        logFormatCatalog_->rebuild();

        fileWatcher_ = FileWatcher::sharedFileWatcher();

        // The one Team Folder. The Session sets it up from the Team Folder
        // Policy, which starts its first sync, and every window shows it.
        teamFolder_ = std::make_shared<TeamFolder>( TeamFolder::defaultCloneDirectory() );

        // Loaded once, after the first window shows, and shared by every
        // window (#303).
        plugins_ = std::make_shared<logsquirl::plugins::ApplicationPlugins>(
            &LogSquirlApp::loadConfiguredPlugins );

        versionChecker_ = std::make_unique<VersionChecker>();
        if ( singleApplication_.isPrimaryInstance() ) {
            connect( versionChecker_.get(), &VersionChecker::newVersionFound,
                     [ this ]( const QString& new_version, const QString& url,
                               const QStringList& changes ) {
                         newVersionNotification( new_version, url, changes );
                     } );
        }
    }

    bool isSecondary() const
    {
        return !singleApplication_.isPrimaryInstance();
    }

    qint64 primaryPid() const
    {
        return singleApplication_.primaryPid();
    }

    // Hands the Log Files given on the command line over to the primary
    // instance and returns the exit code for this secondary instance.
    int handOverToPrimaryInstance( const std::vector<QString>& filenames )
    {
        if ( !filenames.empty() ) {
            return sendFilesToPrimaryInstance( filenames ) ? EXIT_SUCCESS : EXIT_FAILURE;
        }

#ifdef Q_OS_MAC
        // Launched by the Finder or `open -n` with a Log File, macOS delivers
        // it as a QFileOpenEvent, and only once the event loop runs. event()
        // hands it over and quits; with no Log File to come, give up after a
        // while.
        constexpr auto FileOpenEventTimeout = std::chrono::milliseconds( 500 );
        QTimer::singleShot( FileOpenEventTimeout, this, &QCoreApplication::quit );
        return exec();
#else
        return EXIT_SUCCESS;
#endif
    }

    // Returns once the message is written to the primary instance's local
    // socket (named pipe on Windows) -- no fixed delay. The primary reads it
    // from its event loop whenever that gets to it, so a busy primary does
    // not lose it; only if it cannot even be connected to or written to
    // within the timeout does this fail.
    bool sendFilesToPrimaryInstance( const std::vector<QString>& filenames )
    {
#ifdef Q_OS_WIN
        // TODO: fix pid passing
        ::AllowSetForegroundWindow( static_cast<DWORD>( primaryPid() ) );
#endif

        LOG_INFO << "Handing over " << filenames.size() << " file(s) to the primary instance";

        QStringList filesToOpen;
        std::copy( filenames.cbegin(), filenames.cend(), std::back_inserter( filesToOpen ) );

        QVariantMap data;
        data.insert( "version", logsquirlVersion() );
        data.insert( "files", QVariant{ filesToOpen } );

        constexpr auto SendTimeoutMs = 5000;
        const auto cbor = QCborValue::fromVariant( data );
        const auto sent = singleApplication_.sendMessageWithTimeout( cbor.toCbor(), SendTimeoutMs );
        if ( !sent ) {
            LOG_ERROR << "Could not hand files over to the primary instance, pid " << primaryPid();
        }
        return sent;
    }

    void initCrashHandler()
    {
        crashHandler_ = std::make_unique<CrashHandler>();
    }

    MainWindow* reloadSession()
    {
        if ( !session_ ) {
            session_ = std::make_shared<Session>( settingsPolicies_, logFormatCatalog_,
                                                  fileWatcher_, teamFolder_ );
        }

        for ( auto&& windowSession : session_->windowSessions() ) {
            auto w = newWindow( std::move( windowSession ) );
            w->reloadGeometry();
            w->reloadSession();
            w->show();
        }

        if ( mainWindows_.empty() ) {
            auto w = newWindow();
            w->show();
        }

        return mainWindows_.back().second;
    }

    void clearInactiveSessions()
    {
        LOG_INFO << "Clear inactive sessions";

        auto existingSessions = session_->windowSessions();
        existingSessions.erase( std::remove_if( existingSessions.begin(), existingSessions.end(),
                                                [ this ]( const auto& session ) {
                                                    return std::any_of(
                                                        mainWindows_.begin(), mainWindows_.end(),
                                                        [ &session ]( const auto& window ) {
                                                            return window.first.windowId()
                                                                   == session.windowId();
                                                        } );
                                                } ),
                                existingSessions.end() );

        for ( auto& session : existingSessions ) {
            session.close();
        }
    }

    MainWindow* newWindow()
    {
        if ( !session_ ) {
            session_ = std::make_shared<Session>( settingsPolicies_, logFormatCatalog_,
                                                  fileWatcher_, teamFolder_ );
        }

        const auto previousSessions = session_->windowSessions();

        QByteArray geometry;
        if ( !previousSessions.empty() ) {
            previousSessions.back().restoreGeometry( &geometry );
        }

        auto window = newWindow( { session_, generateIdFromUuid(), nextWindowIndex() } );
        window->restoreGeometry( geometry );

        return window;
    }

    void loadFileNonInteractive( const QString& file )
    {
        while ( !activeWindows_.empty() && activeWindows_.top().isNull() ) {
            activeWindows_.pop();
        }

        if ( activeWindows_.empty() ) {
            newWindow();
        }

        activeWindows_.top()->loadFileNonInteractive( file );
    }

    void startBackgroundTasks()
    {
        LOG_DEBUG << "startBackgroundTasks";
        versionChecker_->startCheck();
    }

#ifdef Q_OS_MAC
    bool event( QEvent* event ) override
    {
        if ( event->type() == QEvent::FileOpen ) {
            QFileOpenEvent* openEvent = static_cast<QFileOpenEvent*>( event );
            LOG_INFO << "File open request " << openEvent->file();

            if ( !isSecondary() ) {
                loadFileNonInteractive( openEvent->file() );
            }
            else {
                sendFilesToPrimaryInstance( { openEvent->file() } );
                // Every QFileOpenEvent of one launch arrives before the event
                // loop gets to this timer.
                QTimer::singleShot( 0, this, &QCoreApplication::quit );
            }
        }

        return QApplication::event( event );
    }
#endif

private:
    MainWindow* newWindow( WindowSession&& session )
    {
        mainWindows_.emplace_back( session, new MainWindow( session, plugins_ ) );

        auto& window = mainWindows_.back().second;

        activeWindows_.push( QPointer<MainWindow>( window ) );

        LOG_INFO << "Window " << &window << " created";
        connect( window, &MainWindow::newWindow, [ =, this ]() { newWindow()->show(); } );
        connect( window, &MainWindow::windowActivated,
                 [ this, window ]() { onWindowActivated( *window ); } );
        connect( window, &MainWindow::windowClosed,
                 [ this, window ]() { onWindowClosed( *window ); } );
        connect( window, &MainWindow::exitRequested, [ this ] { exitApplication(); } );

        return window;
    }

    // Discovers the installed plugins and loads the enabled ones. The plugin
    // layer reads no settings: it is handed them, and a first run's default,
    // every discovered plugin enabled, is kept here.
    static void loadConfiguredPlugins( logsquirl::plugins::PluginCatalog& catalog,
                                       logsquirl::plugins::PluginHost& host )
    {
        catalog.discoverPlugins();

        auto& config = Configuration::get();
        const auto autoLoaded = host.autoLoadPlugins(
            { .autoLoad = config.pluginsAutoLoad(), .enabled = config.enabledPlugins() } );
        if ( autoLoaded.enabledOnFirstRun ) {
            config.setEnabledPlugins( *autoLoaded.enabledOnFirstRun );
            config.save();
        }
        for ( const auto& error : autoLoaded.errors ) {
            LOG_WARNING << "Plugin auto-load error: " << error;
        }
    }

    void onWindowActivated( MainWindow& window )
    {
        LOG_INFO << "Window " << &window << " activated";
        activeWindows_.push( QPointer<MainWindow>( &window ) );
    }

    void onWindowClosed( MainWindow& window )
    {
        LOG_INFO << "Window " << &window << " closed";
        auto w = std::find_if( mainWindows_.begin(), mainWindows_.end(),
                               [ &window ]( const auto& p ) { return p.second == &window; } );

        if ( w != mainWindows_.end() ) {
            mainWindows_.erase( w );
        }
    }

    void exitApplication()
    {
        LOG_INFO << "exit application";
        session_->setExitRequested( true );
        auto mainWindows = mainWindows_;
        mainWindows.reverse();
        for ( const auto& [ session, window ] : mainWindows ) {
            Q_UNUSED( session );
            window->close();
        }

        QTimer::singleShot( 100, this, &QCoreApplication::quit );
    }

    void newVersionNotification( const QString& new_version, const QString& url,
                                 const QStringList& changes )
    {
        LOG_DEBUG << "newVersionNotification( " << new_version << " from " << url << " )";

        // Decided now, when the notice is shown, from what is on this machine.
        const auto message = logsquirl::versioncheck::updateNoticeHtml(
            new_version, url, changes,
            logsquirl::versioncheck::detectInstallSource(
                logsquirl::versioncheck::runningInstallEnvironment() ) );

        QMessageBox msgBox;
        msgBox.setText( message );
        msgBox.exec();
    }

    size_t nextWindowIndex() const
    {
        if ( mainWindows_.empty() ) {
            return 0;
        }
        else {
            const auto windowWithMaxIndex = std::max_element(
                mainWindows_.begin(), mainWindows_.end(), []( const auto& lhs, const auto& rhs ) {
                    return lhs.first.windowIndex() < rhs.first.windowIndex();
                } );
            return windowWithMaxIndex->first.windowIndex() + 1;
        }
    }

private:
    KDSingleApplication singleApplication_;
    std::unique_ptr<CrashHandler> crashHandler_;

    MessageReceiver messageReceiver_;

    // The one file watcher, looked up in prepareForMainWindows() and nowhere
    // else: the Session hands it the Watch Policy, and to every Log File it
    // opens as their File Watch Port (#249, #245). File watching reads no
    // setting of its own (#93).
    std::shared_ptr<FileWatcher> fileWatcher_;

    // The application's one Team Folder, handed to the Session (#470).
    std::shared_ptr<TeamFolder> teamFolder_;

    std::shared_ptr<Session> session_;

    // The Policies the Session starts with and the Log Format Catalog, both
    // set up by prepareForMainWindows().
    SettingsPolicies settingsPolicies_;
    std::shared_ptr<LogFormatCatalog> logFormatCatalog_;

    // The one Plugin Catalog and Plugin Host, set up by
    // prepareForMainWindows() and handed to every window (#303).
    std::shared_ptr<logsquirl::plugins::ApplicationPlugins> plugins_;

    std::list<std::pair<WindowSession, MainWindow*>> mainWindows_;
    std::stack<QPointer<MainWindow>> activeWindows_;

    std::unique_ptr<VersionChecker> versionChecker_;
};

#endif // LOGSQUIRL_LOGSQUIRLAPP_H
