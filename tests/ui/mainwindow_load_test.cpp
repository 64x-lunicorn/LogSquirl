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

// The main window and the loads of its Log Files: what a tab brought to the
// front says about its load (#540), a Filters-panel click that runs one
// Search (#538), the tab a restored Session opens on (#542) and where its Log
// Files stand (#559), a Log File restored from its archive (#596), and the
// loading progress of the tab in front (#541).
// Unlike mainwindow_test.cpp these run on every platform.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QToolBar>

#include <kcompressiondevice.h>

#include "applicationplugins.h"
#include "archivemember.h"
#include "crawlerwidget.h"
#include "filterspanel.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "openlogfile.h"
#include "pathline.h"
#include "session.h"
#include "stored_session.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"
#include "textencoding.h"
#include "viewstatecodec.h"

struct MainWindowLoadAccess;

template <>
struct CrawlerWidget::access_by<MainWindowLoadAccess> {
    static OpenLogFile& openLogFile( CrawlerWidget& crawler )
    {
        return *crawler.openLogFile_;
    }

    // The Search line, whose drop-down list shows the Search history.
    static QComboBox& searchLine( CrawlerWidget& crawler )
    {
        return *crawler.searchLineEdit_;
    }

    // The text view of the Log File.
    static const AbstractLogView& textView( const CrawlerWidget& crawler )
    {
        return *crawler.logMainView_;
    }
};

namespace {

using LoadAccess = CrawlerWidget::access_by<MainWindowLoadAccess>;

// Reloads the Log File of crawler with an Encoding the indexing fails on --
// no converter exists for its name -- and waits until the load has finished
// Failed. encoding must outlive the Log File, which holds on to it.
bool failToLoad( CrawlerWidget& crawler, const TextEncoding& encoding )
{
    auto& openLogFile = LoadAccess::openLogFile( crawler );
    bool failed = false;
    QObject context;
    QObject::connect( &openLogFile, &OpenLogFile::loadingFinished, &context,
                      [ &failed ]( const OpenLogFile::LoadFinished& load ) {
                          failed = load.status == LoadingStatus::Failed;
                      } );
    openLogFile.logData()->reload( &encoding );
    return waitUiState( [ &failed ] { return failed; }, 10000 );
}

} // namespace

// The window hears only the tab in front. A Log File that failed to load in a
// background tab keeps that status and replays it when its tab is shown, and
// the window treats it like a failure it heard live: it offers to report it
// and closes the tab (#540).
SCENARIO( "A Log File that failed to load in a background tab says so when its tab is shown",
          "[ui][loading]" )
{
    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    WindowSession windowSession{ appSession, "Main", 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    QTemporaryFile failingFile{ QDir::temp().filePath( "mainwindow_failing_XXXXXX" ) };
    QTemporaryFile otherFile{ QDir::temp().filePath( "mainwindow_loaded_XXXXXX" ) };
    for ( auto* file : { &failingFile, &otherFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }
    const auto failingName = QFileInfo( failingFile.fileName() ).fileName();

    // Outlives the Log Files, which hold on to the Encoding they were reloaded with.
    const TextEncoding unusableEncoding( -4242, "LogSquirl-Unusable-Encoding", std::nullopt );

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    // Every question the window asks is answered No, so that no issue is
    // opened in a browser; the ones asked are counted.
    int questionsAsked = 0;
    QTimer questionDriver;
    QObject::connect( &questionDriver, &QTimer::timeout, [ &questionsAsked ] {
        if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
            ++questionsAsked;
            box->reject();
        }
    } );
    questionDriver.start( 10 );

    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );
    const int baseTabCount = tabArea->count();
    auto* toolBar = mainWindow->findChild<QToolBar*>();
    REQUIRE( toolBar != nullptr );
    auto* infoLine = toolBar->findChild<PathLine*>();
    REQUIRE( infoLine != nullptr );

    mainWindow->loadFileNonInteractive( failingFile.fileName() );
    REQUIRE( waitUiState( [ & ] { return infoLine->text().contains( failingName ); }, 10000 ) );
    auto* failing = qobject_cast<CrawlerWidget*>( tabArea->currentWidget() );
    REQUIRE( failing != nullptr );

    GIVEN( "two Log Files, the one in the background failed to load" )
    {
        mainWindow->loadFileNonInteractive( otherFile.fileName() );
        REQUIRE( waitUiState(
            [ & ] {
                return tabArea->count() == baseTabCount + 2
                       && infoLine->text().contains( QFileInfo( otherFile ).fileName() );
            },
            10000 ) );
        REQUIRE( tabArea->currentWidget() != failing );

        REQUIRE( failToLoad( *failing, unusableEncoding ) );
        QTest::qWait( 100 );
        questionsAsked = 0;

        WHEN( "its tab is shown" )
        {
            // The info line is watched from the tab switch until the tab has
            // closed, at every turn of the event loop.
            bool reportedAsLoaded = false;
            const auto watchInfoLine = [ & ] {
                reportedAsLoaded = reportedAsLoaded || infoLine->text().contains( failingName );
            };
            QTimer infoLineWatch;
            QObject::connect( &infoLineWatch, &QTimer::timeout, watchInfoLine );
            infoLineWatch.start( 0 );

            tabArea->setCurrentWidget( failing );
            watchInfoLine();

            THEN( "the failure is offered to be reported and the tab is closed" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        watchInfoLine();
                        return questionsAsked == 1 && tabArea->count() == baseTabCount + 1;
                    },
                    10000 ) );
                REQUIRE( tabArea->indexOf( failing ) < 0 );
                infoLineWatch.stop();

                AND_THEN( "the info line never reported the Log File as loaded meanwhile" )
                {
                    REQUIRE_FALSE( reportedAsLoaded );
                }
            }
        }
    }

    GIVEN( "a Log File in front that fails to load" )
    {
        REQUIRE( failToLoad( *failing, unusableEncoding ) );

        THEN( "the failure is offered to be reported and the tab is closed" )
        {
            REQUIRE( waitUiState(
                [ & ] { return questionsAsked == 1 && tabArea->count() == baseTabCount; },
                10000 ) );
            REQUIRE_FALSE( infoLine->text().contains( failingName ) );
        }
    }

    questionDriver.stop();
    mainWindow.reset();
}

// Choosing Predefined Filters in the Filters panel edits the Search line's
// pattern, and the Search line answers whether the Search runs now, as it does
// for adding a word, excluding one or replacing the pattern: the window does
// not start the Search again itself (#538).
SCENARIO( "One Filters-panel click runs one Search", "[ui][search]" )
{
    const auto autoRun = GENERATE( true, false );

    auto policies = testSettingsPolicies();
    policies.quickFind.autoRunSearchOnPatternChange = autoRun;
    auto appSession = std::make_shared<Session>( policies, std::make_shared<LogFormatCatalog>() );
    WindowSession windowSession{ appSession, "Main", 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    QTemporaryFile file{ QDir::temp().filePath( "mainwindow_filters_XXXXXX" ) };
    REQUIRE( file.open() );
    file.write( "alpha Log Line\nbeta Log Line\n" );
    file.flush();

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );
    auto* filtersPanel = mainWindow->findChild<FiltersPanel*>();
    REQUIRE( filtersPanel != nullptr );

    mainWindow->loadFileNonInteractive( file.fileName() );
    CrawlerWidget* crawler = nullptr;
    REQUIRE( waitUiState(
        [ & ] {
            crawler = qobject_cast<CrawlerWidget*>( tabArea->currentWidget() );
            return crawler != nullptr
                   && LoadAccess::openLogFile( *crawler ).logData()->getNbLine().get() == 2;
        },
        10000 ) );
    QTest::qWait( 100 );

    // Every Search started clears the one before, and shows the Search
    // history it saved in the Search line's drop-down list.
    int searchesStarted = 0;
    QObject context;
    QObject::connect( LoadAccess::openLogFile( *crawler ).filteredData().get(),
                      &LogFilteredData::searchStateChanged, &context,
                      [ &searchesStarted ]( const SearchSession::State& state ) {
                          if ( state.phase == SearchSession::Phase::Idle ) {
                              ++searchesStarted;
                          }
                      } );
    auto& searchLine = LoadAccess::searchLine( *crawler );
    QSignalSpy historyShown( searchLine.model(), &QAbstractItemModel::rowsInserted );

    GIVEN( ( autoRun ? "a Search line that runs the Search as its pattern changes"
                     : "a Search line that waits for Enter as its pattern changes" ) )
    {
        WHEN( "a Predefined Filter is chosen in the Filters panel" )
        {
            Q_EMIT filtersPanel->filtersChanged( { { "beta", "beta", false } } );
            QTest::qWait( 300 );

            THEN( "the Search line shows its pattern" )
            {
                REQUIRE( searchLine.currentText() == "beta" );
            }

            if ( autoRun ) {
                THEN( "exactly one Search is started, and the Search history saved once" )
                {
                    REQUIRE( searchesStarted == 1 );
                    REQUIRE( historyShown.count() == 1 );
                }
            }
            else {
                THEN( "no Search is started" )
                {
                    REQUIRE( searchesStarted == 0 );
                    REQUIRE( historyShown.isEmpty() );
                }
            }
        }
    }

    mainWindow.reset();
}

// A window restored from the Session shows the tab that was in front when the
// Session was saved, not the last one (#542).
SCENARIO( "A restored window shows the tab that was in front", "[ui][session]" )
{
    const auto windowId = QStringLiteral( "mainwindow_load_test_window_542" );

    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_restore_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_restore_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }

    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    const StoredSessionWindow stored{ windowId,
                                      { { QFileInfo( firstFile ).absoluteFilePath(), QString{} },
                                        { QFileInfo( secondFile ).absoluteFilePath(), QString{} } },
                                      0 };
    WindowSession windowSession{ appSession, windowId, 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    WHEN( "the window's Session is restored with its first tab saved in front" )
    {
        mainWindow->reloadSession();

        THEN( "the first tab is in front" )
        {
            auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
            REQUIRE( tabArea != nullptr );
            REQUIRE( tabArea->findChildren<CrawlerWidget*>().size() == 2 );
            // The Log Files' tabs are the last two, in the order they were
            // saved; a dashboard tab, if any, comes before them.
            REQUIRE( tabArea->currentIndex() == tabArea->count() - 2 );
        }
    }

    mainWindow.reset();
}

// A restored Log File shows the Log Line that was at the top of its Viewport
// when the Session was saved, once its first load is done; one still waiting
// for its turn keeps that Scroll Position for the next save (#559).
SCENARIO( "A restored Log File stands where it stood", "[ui][session]" )
{
    const auto windowId = QStringLiteral( "mainwindow_load_test_window_559" );

    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_position_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_position_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        for ( auto line = 0; line < 3000; ++line ) {
            file->write( QByteArray( "Log Line " ) + QByteArray::number( line ) + '\n' );
        }
        file->flush();
    }

    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    const StoredSessionWindow stored{
        windowId,
        { { QFileInfo( firstFile ).absoluteFilePath(), R"({"S":[400,100],"SP":1200})" },
          { QFileInfo( secondFile ).absoluteFilePath(), R"({"S":[400,100],"SP":900})" } },
        0
    };
    WindowSession windowSession{ appSession, windowId, 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    WHEN( "the window's Session is restored" )
    {
        mainWindow->reloadSession();
        auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabArea != nullptr );
        const auto logFileTabs = tabArea->logFileTabs();
        REQUIRE( logFileTabs.size() == 2 );
        const auto* first = qobject_cast<CrawlerWidget*>( tabArea->widget( logFileTabs.front() ) );
        const auto* second = qobject_cast<CrawlerWidget*>( tabArea->widget( logFileTabs.back() ) );
        REQUIRE( first != nullptr );
        REQUIRE( second != nullptr );

        THEN( "the text view of the tab in front stands on the saved Log Line once loaded" )
        {
            const auto& textView = LoadAccess::textView( *first );
            REQUIRE( waitUiState( [ & ] { return textView.getTopLine() == 1200_lnum; }, 10000 ) );
        }

        AND_THEN( "the tab behind it saves the Scroll Position it was restored with, loaded or "
                  "not" )
        {
            const auto saved = decodeViewState( second->context()->toString(), QuickFindPolicy{} );
            REQUIRE( saved.scrollPosition == 900 );
        }
    }

    mainWindow.reset();
}

// A Log File decompressed from an archive is read from a temporary file that
// is gone by the next start. A restored window decompresses the archive again,
// without asking, and the Log File comes back where it stood; one whose
// archive is gone does not come back, and nothing is said about it (#596).
SCENARIO( "A restored window reopens a decompressed Log File from its archive",
          "[ui][session][archive]" )
{
    const auto windowId = QStringLiteral( "mainwindow_load_test_window_596" );

    QTemporaryDir archives;
    REQUIRE( archives.isValid() );
    const auto archivePath = archives.filePath( "restored.log.gz" );
    {
        KCompressionDevice archive( archivePath, KCompressionDevice::GZip );
        REQUIRE( archive.open( QIODevice::WriteOnly ) );
        for ( auto line = 0; line < 3000; ++line ) {
            archive.write( QByteArray( "Log Line " ) + QByteArray::number( line ) + '\n' );
        }
    }
    const ArchiveMember member{ archivePath, { QString{} } };
    const ArchiveMember goneMember{ archives.filePath( "gone.zip" ), { "app.log" } };

    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    // Stored after the Session was built, which reads the settings store, and
    // written to it, so that the window reads it too. The temporary files the
    // two were read from are gone.
    auto& stored = SessionInfo::getSynced();
    stored.add( windowId );
    stored.setOpenFiles(
        windowId,
        { { archives.filePath( "restored.log.gz.AbCdEf" ), R"({"S":[400,100],"SP":1200})", member },
          { archives.filePath( "app.log.GhIjKl" ), QString{}, goneMember } },
        0 );
    stored.save();
    WindowSession windowSession{ appSession, windowId, 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    // Whatever the window asks or reports is counted and closed.
    int messagesShown = 0;
    QTimer messageDriver;
    QObject::connect( &messageDriver, &QTimer::timeout, [ &messagesShown ] {
        if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
            ++messagesShown;
            box->reject();
        }
    } );
    messageDriver.start( 10 );

    WHEN( "the window's Session is restored" )
    {
        mainWindow->reloadSession();
        auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabArea != nullptr );
        const auto logFileTabs = tabArea->logFileTabs();

        THEN( "the Log File is back from its archive, where it stood, and nothing else is" )
        {
            REQUIRE( logFileTabs.size() == 1 );
            auto* restored = qobject_cast<CrawlerWidget*>( tabArea->widget( logFileTabs.front() ) );
            REQUIRE( restored != nullptr );
            const auto& textView = LoadAccess::textView( *restored );
            REQUIRE( waitUiState( [ & ] { return textView.getTopLine() == 1200_lnum; }, 10000 ) );
            REQUIRE( LoadAccess::openLogFile( *restored ).logData()->getNbLine().get() == 3000 );
            QTest::qWait( 50 );
            REQUIRE( messagesShown == 0 );
        }

        AND_WHEN( "the application quits, which saves the Session" )
        {
            appSession->setExitRequested( true );
            mainWindow->close();
            appSession->setExitRequested( false );

            THEN( "the Log File is saved with its archive once more" )
            {
                const auto saved = SessionInfo::get().openFiles( windowId );
                REQUIRE( saved.size() == 1 );
                REQUIRE( saved.front().archiveMember == member );
            }
        }
    }

    mainWindow.reset();
    auto& left = SessionInfo::getSynced();
    left.remove( windowId );
    left.save();
}

namespace {

// The window's action with this text, as the menus show it.
QAction* windowAction( const MainWindow& window, const char* text )
{
    const auto translated = QApplication::translate( "logsquirl::mainwindow::action", text );
    for ( auto* action : window.findChildren<QAction*>() ) {
        if ( action->text() == translated ) {
            return action;
        }
    }
    return nullptr;
}

} // namespace

// The window hears only the tab in front. A tab brought to the front while
// its Log File is still loading -- a restored Log File still waiting for its
// turn, or one being reloaded -- replays that it is loading, and the window
// shows it loading at once instead of what it showed for the tab before, even
// before the load has made any progress (#540).
SCENARIO( "A Log File still loading in a background tab shows as loading when its tab is shown",
          "[ui][loading]" )
{
    const auto windowId = QStringLiteral( "mainwindow_load_test_window_540" );

    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_loading_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_loading_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }
    const auto firstName = QFileInfo( firstFile ).fileName();
    const auto secondName = QFileInfo( secondFile ).fileName();

    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    const StoredSessionWindow stored{ windowId,
                                      { { QFileInfo( firstFile ).absoluteFilePath(), QString{} },
                                        { QFileInfo( secondFile ).absoluteFilePath(), QString{} } },
                                      0 };
    WindowSession windowSession{ appSession, windowId, 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );
    auto* toolBar = mainWindow->findChild<QToolBar*>();
    REQUIRE( toolBar != nullptr );
    auto* infoLine = toolBar->findChild<PathLine*>();
    REQUIRE( infoLine != nullptr );
    auto* reloadAction = windowAction( *mainWindow, logsquirl::mainwindow::action::reloadText );
    REQUIRE( reloadAction != nullptr );
    auto* stopAction = windowAction( *mainWindow, logsquirl::mainwindow::action::stopText );
    REQUIRE( stopAction != nullptr );

    const auto showsLoading = [ & ]( const QString& name ) {
        return infoLine->text().contains( name ) && !reloadAction->isEnabled()
               && stopAction->isEnabled();
    };
    const auto showsLoaded = [ & ]( const QString& name ) {
        return infoLine->text().contains( name ) && reloadAction->isEnabled()
               && !stopAction->isEnabled();
    };

    GIVEN( "a restored Session whose second Log File waits for its turn" )
    {
        mainWindow->reloadSession();
        const auto logFileTabs = tabArea->logFileTabs();
        REQUIRE( logFileTabs.size() == 2 );
        REQUIRE( tabArea->currentIndex() == logFileTabs.front() );

        WHEN( "its tab is shown before it has loaded" )
        {
            tabArea->setCurrentIndex( logFileTabs.back() );

            THEN( "the window shows it loading at once" )
            {
                REQUIRE( showsLoading( secondName ) );

                AND_THEN( "it shows it loaded once the load has finished" )
                {
                    REQUIRE( waitUiState( [ & ] { return showsLoaded( secondName ); }, 10000 ) );
                }
            }
        }
    }

    GIVEN( "two loaded Log Files, the one in the background reloaded" )
    {
        mainWindow->loadFileNonInteractive( firstFile.fileName() );
        mainWindow->loadFileNonInteractive( secondFile.fileName() );
        REQUIRE( waitUiState(
            [ & ] { return tabArea->logFileTabs().size() == 2 && showsLoaded( secondName ); },
            10000 ) );
        auto* first
            = qobject_cast<CrawlerWidget*>( tabArea->widget( tabArea->logFileTabs().front() ) );
        REQUIRE( first != nullptr );
        REQUIRE( tabArea->currentWidget() != first );
        QTest::qWait( 100 );

        first->reload();

        WHEN( "its tab is shown before the reload has finished" )
        {
            tabArea->setCurrentWidget( first );

            THEN( "the window shows it loading, not the Log File shown before" )
            {
                REQUIRE( showsLoading( firstName ) );

                AND_THEN( "it shows it loaded once the reload has finished" )
                {
                    REQUIRE( waitUiState( [ & ] { return showsLoaded( firstName ); }, 10000 ) );
                }
            }
        }
    }

    mainWindow.reset();
}

// The window shows the loading progress of the tab in front only. A tab keeps
// the progress of its load and shows it at once when it is brought to the
// front; a tab that has loaded takes the gauge away (#541).
SCENARIO( "Loading progress follows the tab in front", "[ui][loading]" )
{
    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_progress_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_progress_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }
    const auto firstName = QFileInfo( firstFile ).fileName();
    const auto secondName = QFileInfo( secondFile ).fileName();

    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    WindowSession windowSession{ appSession, "Main", 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    std::unique_ptr<MainWindow> mainWindow;
    QTimer::singleShot( 0,
                        [ & ] { mainWindow.reset( new MainWindow( windowSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    mainWindow->show();

    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );
    auto* toolBar = mainWindow->findChild<QToolBar*>();
    REQUIRE( toolBar != nullptr );
    auto* infoLine = toolBar->findChild<PathLine*>();
    REQUIRE( infoLine != nullptr );
    auto* reloadAction = windowAction( *mainWindow, logsquirl::mainwindow::action::reloadText );
    REQUIRE( reloadAction != nullptr );

    // The gauge is the gradient behind the info line's text.
    const auto showsGauge = [ & ] {
        return infoLine->palette().brush( infoLine->backgroundRole() ).gradient() != nullptr;
    };
    const auto showsLoaded = [ & ]( const QString& name ) {
        return infoLine->text().contains( name ) && reloadAction->isEnabled() && !showsGauge();
    };

    GIVEN( "two loaded Log Files, the second in front" )
    {
        mainWindow->loadFileNonInteractive( firstFile.fileName() );
        mainWindow->loadFileNonInteractive( secondFile.fileName() );
        REQUIRE( waitUiState(
            [ & ] { return tabArea->logFileTabs().size() == 2 && showsLoaded( secondName ); },
            10000 ) );
        auto* first
            = qobject_cast<CrawlerWidget*>( tabArea->widget( tabArea->logFileTabs().front() ) );
        auto* second
            = qobject_cast<CrawlerWidget*>( tabArea->widget( tabArea->logFileTabs().back() ) );
        REQUIRE( first != nullptr );
        REQUIRE( second != nullptr );
        REQUIRE( tabArea->currentWidget() == second );
        QTest::qWait( 100 );

        WHEN( "the Log File in the background tells that its load has progressed" )
        {
            // As a load under way tells it; these Log Files are too small to
            // be caught between two ticks of a real load.
            Q_EMIT LoadAccess::openLogFile( *first ).loadingProgressed( 42 );
            QTest::qWait( 50 );

            THEN( "the window still shows the Log File in front, loaded, with no gauge" )
            {
                REQUIRE( showsLoaded( secondName ) );
                REQUIRE_FALSE( infoLine->text().contains( "42 %" ) );
            }

            AND_WHEN( "its tab is brought to the front" )
            {
                tabArea->setCurrentWidget( first );

                THEN( "the window shows its progress at once" )
                {
                    REQUIRE( infoLine->text().contains( firstName ) );
                    REQUIRE( infoLine->text().contains( "(42 %)" ) );
                    REQUIRE( showsGauge() );
                    REQUIRE_FALSE( reloadAction->isEnabled() );

                    AND_WHEN( "the loaded tab is brought back to the front" )
                    {
                        tabArea->setCurrentWidget( second );

                        THEN( "the gauge is gone" )
                        {
                            REQUIRE( showsLoaded( secondName ) );
                        }
                    }
                }
            }
        }
    }

    mainWindow.reset();
}
