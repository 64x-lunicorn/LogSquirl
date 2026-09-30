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
// Files stand (#559), a Log File restored from its archive (#596), the
// loading progress of the tab in front (#541), its follow and the actions
// reaching it alone (#635), and its selected Log Line (#692).
// Unlike mainwindow_test.cpp these run on every platform.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
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
#include "configuration.h"
#include "crawlerwidget.h"
#include "filterspanel.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "openlogfile.h"
#include "pathline.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "sessionfile.h"
#include "shortcuts.h"
#include "stored_session.h"
#include "tabbedcrawlerwidget.h"
#include "tabgroupinfo.h"
#include "tabnamemapping.h"
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
        return *SearchLineAccess::patternEdit( *crawler.searchLine_ );
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

namespace {

// Waits until the archives of the window's restored Session are decompressed,
// and their tabs added or left out (#610).
bool waitForArchiveRestores( const MainWindow& window )
{
    const auto* restores = window.findChild<ArchiveMemberDecompression*>();
    return restores != nullptr && waitUiState( [ restores ] { return restores->isIdle(); }, 10000 );
}

} // namespace

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
        // The archives decompress in the background (#610).
        REQUIRE( waitForArchiveRestores( *mainWindow ) );
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

// A restored window does not wait for the archives of its decompressed Log
// Files: the other tabs are there, and in use, at once, and the tab of each
// archive comes once it is decompressed, where it stood and with its view
// state. It takes the front only while the window shows the tab the restore
// put there. A broken archive's tab never comes, and nothing is said about
// it; a window closed meanwhile saves the Log File with its archive (#610).
SCENARIO( "A restored window is in use while the archive of a Log File decompresses",
          "[ui][session][archive]" )
{
    const auto windowId = QStringLiteral( "mainwindow_load_test_window_610" );

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
    // Named as an archive, and none.
    const auto brokenPath = archives.filePath( "broken.zip" );
    {
        QFile broken( brokenPath );
        REQUIRE( broken.open( QIODevice::WriteOnly ) );
        broken.write( "not an archive at all\n" );
    }
    const ArchiveMember member{ archivePath, { QString{} } };
    const ArchiveMember brokenMember{ brokenPath, { "app.log" } };

    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_archive_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_archive_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }
    const auto firstPath = QFileInfo( firstFile ).absoluteFilePath();
    const auto secondPath = QFileInfo( secondFile ).absoluteFilePath();

    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    // The Log File from the archive was in front. The temporary files the two
    // decompressed ones were read from are gone.
    const StoredSessionWindow stored{
        windowId,
        { { firstPath, QString{} },
          { archives.filePath( "restored.log.gz.AbCdEf" ), R"({"S":[400,100],"SP":1200})", member },
          { archives.filePath( "app.log.GhIjKl" ), QString{}, brokenMember },
          { secondPath, QString{} } },
        1
    };
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

    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );
    const auto crawlerAt = [ tabArea ]( qsizetype logFileTab ) {
        return qobject_cast<CrawlerWidget*>(
            tabArea->widget( tabArea->logFileTabs().at( logFileTab ) ) );
    };
    const auto pathOf = [ tabArea ]( qsizetype logFileTab ) {
        return QDir::fromNativeSeparators(
            tabArea->tabToolTip( tabArea->logFileTabs().at( logFileTab ) ) );
    };

    WHEN( "the window's Session is restored" )
    {
        mainWindow->reloadSession();

        THEN( "the other tabs are there at once, the one before it in front" )
        {
            REQUIRE( tabArea->logFileTabs().size() == 2 );
            REQUIRE( pathOf( 0 ) == firstPath );
            REQUIRE( pathOf( 1 ) == secondPath );
            REQUIRE( tabArea->currentWidget() == crawlerAt( 0 ) );
        }

        AND_THEN( "its tab comes once decompressed, where it stood, in front and with its view "
                  "state, and the broken archive's never does" )
        {
            REQUIRE( waitForArchiveRestores( *mainWindow ) );
            REQUIRE( tabArea->logFileTabs().size() == 3 );
            REQUIRE( pathOf( 0 ) == firstPath );
            REQUIRE( QFileInfo( pathOf( 1 ) ).fileName().startsWith( "restored.log.gz" ) );
            REQUIRE( pathOf( 2 ) == secondPath );

            auto* restored = crawlerAt( 1 );
            REQUIRE( tabArea->currentWidget() == restored );
            const auto& textView = LoadAccess::textView( *restored );
            REQUIRE( waitUiState( [ & ] { return textView.getTopLine() == 1200_lnum; }, 10000 ) );
            REQUIRE( LoadAccess::openLogFile( *restored ).logData()->getNbLine().get() == 3000 );
            QTest::qWait( 50 );
            REQUIRE( messagesShown == 0 );
        }
    }

    WHEN( "the user brings another tab to the front before the archive is decompressed" )
    {
        mainWindow->reloadSession();
        auto* chosen = crawlerAt( 1 );
        tabArea->setCurrentWidget( chosen );
        REQUIRE( waitForArchiveRestores( *mainWindow ) );

        THEN( "its tab comes where it stood, behind the one the user chose" )
        {
            REQUIRE( tabArea->logFileTabs().size() == 3 );
            REQUIRE( QFileInfo( pathOf( 1 ) ).fileName().startsWith( "restored.log.gz" ) );
            REQUIRE( tabArea->currentWidget() == chosen );
        }
    }

    WHEN( "the window closes, which saves the Session, before the archive is decompressed" )
    {
        mainWindow->reloadSession();
        appSession->setExitRequested( true );
        mainWindow->close();
        appSession->setExitRequested( false );

        THEN( "the Log Files are saved with their archives, where they stood, the one in front "
              "still in front" )
        {
            const auto saved = SessionInfo::get().openFiles( windowId );
            REQUIRE( saved.size() == 4 );
            REQUIRE( saved[ 1 ].archiveMember == member );
            REQUIRE( saved[ 2 ].archiveMember == brokenMember );
            REQUIRE( SessionInfo::get().currentFile( windowId ) == 1 );
        }

        AND_THEN( "no tab comes to the closed window, and it goes without waiting for one" )
        {
            QTest::qWait( 200 );
            REQUIRE( tabArea->logFileTabs().isEmpty() );
            mainWindow.reset();
        }
    }

    WHEN( "the window goes while the archive decompresses" )
    {
        mainWindow->reloadSession();
        mainWindow.reset();

        THEN( "nothing arrives after it" )
        {
            QTest::qWait( 200 );
            REQUIRE( messagesShown == 0 );
        }
    }

    mainWindow.reset();
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

namespace {

// Leaves the stored tab names and groups as they were once it goes.
class KeptTabLabels {
public:
    KeptTabLabels()
        : names_( TabNameMapping::getSynced() )
        , groups_( TabGroupInfo::getSynced() )
    {
    }

    ~KeptTabLabels()
    {
        names_.save();
        TabNameMapping::getSynced();
        groups_.save();
        TabGroupInfo::getSynced();
    }

    KeptTabLabels( const KeptTabLabels& ) = delete;
    KeptTabLabels& operator=( const KeptTabLabels& ) = delete;
    KeptTabLabels( KeptTabLabels&& ) = delete;
    KeptTabLabels& operator=( KeptTabLabels&& ) = delete;

private:
    const TabNameMapping names_;
    const TabGroupInfo groups_;
};

} // namespace

// File > Save Session As... writes the window's Session to a Session File, and
// File > Open Session... opens it in a new window: the Log Files in their
// order, the tab in front, each where it stood, with its tab name and group,
// and one from an archive decompressed again. A Log File open already stays
// where it is; when none can be opened, the notice alone shows (#576).
SCENARIO( "A window's Session saves to a Session File and opens from it in a new window",
          "[ui][session][file]" )
{
    const auto windowId = QStringLiteral( "mainwindow_load_test_window_576" );
    const auto openerId = QStringLiteral( "mainwindow_load_test_window_576_opener" );
    const auto openedId = QStringLiteral( "mainwindow_load_test_window_576_opened" );
    const KeptTabLabels keptLabels;

    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto logPath = [ &folder ]( const QString& name ) {
        const auto path
            = QDir::cleanPath( QFileInfo( folder.filePath( name ) ).absoluteFilePath() );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        for ( auto line = 0; line < 3000; ++line ) {
            file.write( QByteArray( "Log Line " ) + QByteArray::number( line ) + '\n' );
        }
        return path;
    };
    const auto firstPath = logPath( "first.log" );
    const auto secondPath = logPath( "second.log" );
    const auto archivePath = folder.filePath( "archived.log.gz" );
    {
        KCompressionDevice archive( archivePath, KCompressionDevice::GZip );
        REQUIRE( archive.open( QIODevice::WriteOnly ) );
        archive.write( "archived Log Line\n" );
    }
    const ArchiveMember member{ archivePath, { QString{} } };
    const auto sessionPath = folder.filePath( "incident.logsquirl-session" );

    TabNameMapping::getSynced().setTabName( firstPath, "Alpha" ).save();
    auto& groups = TabGroupInfo::getSynced();
    groups.addTabToGroup( groups.addGroup( "Session576", QColor( "#3a7bd5" ) ), secondPath );
    groups.save();

    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    const StoredSessionWindow stored{ windowId,
                                      { { firstPath, QString{} },
                                        { folder.filePath( "archived.log.AbCdEf" ), QString{},
                                          member },
                                        { secondPath, R"({"S":[400,100],"SP":1200})" } },
                                      2 };
    WindowSession windowSession{ appSession, windowId, 0 };
    WindowSession openerSession{ appSession, openerId, 1 };
    WindowSession openedSession{ appSession, openedId, 2 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<MainWindow> opener;
    std::unique_ptr<MainWindow> opened;
    QTimer::singleShot( 0, [ & ] {
        mainWindow.reset( new MainWindow( windowSession, plugins ) );
        opener.reset( new MainWindow( openerSession, plugins ) );
    } );
    QTest::qWait( 100 );
    REQUIRE( mainWindow != nullptr );
    REQUIRE( opener != nullptr );
    mainWindow->show();

    // The new window the application would open for a Session File.
    int windowsAskedFor = 0;
    QObject::connect( opener.get(), &MainWindow::sessionFileOpened,
                      [ & ]( const SessionFileRead& read ) {
                          ++windowsAskedFor;
                          opened.reset( new MainWindow( openedSession, plugins ) );
                          opened->restoreSessionFile( read );
                          opened->show();
                      } );

    // Whatever a window says is kept and closed.
    QStringList messages;
    QTimer messageDriver;
    QObject::connect( &messageDriver, &QTimer::timeout, [ &messages ] {
        if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
            messages.append( box->text() );
            box->reject();
        }
    } );
    messageDriver.start( 10 );

    mainWindow->reloadSession();
    REQUIRE( waitForArchiveRestores( *mainWindow ) );
    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );
    REQUIRE( tabArea->logFileTabs().size() == 3 );

    THEN( "the File menu offers both, and each can be given a shortcut" )
    {
        REQUIRE( windowAction( *mainWindow, "Open Session..." ) != nullptr );
        REQUIRE( windowAction( *mainWindow, "Save Session As..." ) != nullptr );
        const auto& shortcuts = ShortcutAction::defaultShortcutList();
        REQUIRE( shortcuts.contains( ShortcutAction::MainWindowOpenSession ) );
        REQUIRE( shortcuts.contains( ShortcutAction::MainWindowSaveSessionAs ) );
    }

    WHEN( "the window's Session is saved to a Session File" )
    {
        REQUIRE( mainWindow->saveSessionFile( sessionPath ) );
        REQUIRE( QFileInfo::exists( sessionPath ) );

        AND_WHEN( "it is opened while its Log Files are open in the window" )
        {
            opener->openSessionFile( sessionPath );

            THEN( "no window opens, and the notice names the Log Files" )
            {
                REQUIRE( waitUiState( [ & ] { return !messages.isEmpty(); }, 5000 ) );
                REQUIRE( windowsAskedFor == 0 );
                REQUIRE( messages.front().contains( QDir::toNativeSeparators( firstPath ) ) );
            }
        }

        AND_WHEN( "the window is closed and the Session File opened" )
        {
            appSession->setExitRequested( true );
            mainWindow->close();
            appSession->setExitRequested( false );
            mainWindow.reset();
            // What the tabs are named and grouped in comes from the file.
            TabNameMapping::getSynced().setTabName( firstPath, QString{} ).save();
            TabGroupInfo::getSynced().removeTabFromGroup( secondPath ).save();

            opener->openSessionFile( sessionPath );
            REQUIRE( windowsAskedFor == 1 );
            REQUIRE( opened != nullptr );
            REQUIRE( waitForArchiveRestores( *opened ) );
            auto* openedTabs = opened->findChild<TabbedCrawlerWidget*>();
            REQUIRE( openedTabs != nullptr );
            const auto tabs = openedTabs->logFileTabs();

            THEN( "a new window shows its Log Files in order, the saved tab in front where it "
                  "stood, with their names and groups, and nothing is said" )
            {
                REQUIRE( tabs.size() == 3 );
                const auto pathOf = [ openedTabs ]( int index ) {
                    return QDir::fromNativeSeparators( openedTabs->tabToolTip( index ) );
                };
                REQUIRE( pathOf( tabs[ 0 ] ) == firstPath );
                REQUIRE( QFileInfo( pathOf( tabs[ 1 ] ) ).fileName().startsWith( "archived.log" ) );
                REQUIRE( pathOf( tabs[ 2 ] ) == secondPath );
                REQUIRE( openedTabs->currentIndex() == tabs[ 2 ] );
                REQUIRE( openedTabs->tabText( tabs[ 0 ] ) == "Alpha" );
                const auto group = openedTabs->groupOfTab( tabs[ 2 ] );
                REQUIRE( group.has_value() );
                REQUIRE( group->name == "Session576" );
                REQUIRE( group->color == QColor( "#3a7bd5" ) );

                auto* front = qobject_cast<CrawlerWidget*>( openedTabs->widget( tabs[ 2 ] ) );
                REQUIRE( front != nullptr );
                const auto& textView = LoadAccess::textView( *front );
                REQUIRE(
                    waitUiState( [ & ] { return textView.getTopLine() == 1200_lnum; }, 10000 ) );
                QTest::qWait( 50 );
                REQUIRE( messages.isEmpty() );
            }
        }
    }

    WHEN( "a file that is not a Session File is opened" )
    {
        const auto notSession = folder.filePath( "not.logsquirl-session" );
        {
            QFile file( notSession );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
            file.write( R"({"format":"something-else","version":1})" );
        }
        opener->openSessionFile( notSession );

        THEN( "it says so, and no window opens" )
        {
            REQUIRE( waitUiState( [ & ] { return !messages.isEmpty(); }, 5000 ) );
            REQUIRE( messages.front()
                     == sessionFileErrorText( SessionFileError::NotASessionFile ) );
            REQUIRE( windowsAskedFor == 0 );
        }
    }

    messageDriver.stop();
    opened.reset();
    opener.reset();
    mainWindow.reset();
    auto& left = SessionInfo::getSynced();
    for ( const auto& id : { windowId, openerId, openedId } ) {
        left.remove( id );
    }
    left.save();
}

// A Session File names and groups the tabs of the Log Files that open from
// it; one whose archive cannot be decompressed again opens no tab, and its
// name and group are not stored (#576).
SCENARIO( "A Session File names and groups only the Log Files that open", "[ui][session][file]" )
{
    const auto openedId = QStringLiteral( "mainwindow_load_test_window_576_labels" );
    const KeptTabLabels keptLabels;

    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto logPath
        = QDir::cleanPath( QFileInfo( folder.filePath( "kept.log" ) ).absoluteFilePath() );
    {
        QFile file( logPath );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( "a Log Line\n" );
    }
    const ArchiveMember goneMember{ folder.filePath( "gone.log.gz" ), { QString{} } };

    SessionFileRead read;
    read.window.files = { { logPath, QString{} },
                          { folder.filePath( "gone.log.AbCdEf" ), QString{}, goneMember } };
    read.window.currentFile = 0;
    read.window.tabs = { { "Kept", "Opened576" }, { "Gone", "Gone576" } };
    read.window.groups
        = { { "Opened576", QColor( "#3a7bd5" ) }, { "Gone576", QColor( "#d53a3a" ) } };

    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    WindowSession openedSession{ appSession, openedId, 0 };
    const auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();

    std::unique_ptr<MainWindow> opened;
    QTimer::singleShot( 0, [ & ] { opened.reset( new MainWindow( openedSession, plugins ) ); } );
    QTest::qWait( 100 );
    REQUIRE( opened != nullptr );
    opened->restoreSessionFile( read );
    opened->show();
    REQUIRE( waitForArchiveRestores( *opened ) );

    auto* tabs = opened->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabs != nullptr );
    const auto logFileTabs = tabs->logFileTabs();
    REQUIRE( logFileTabs.size() == 1 );
    const auto group = tabs->groupOfTab( logFileTabs.front() );
    REQUIRE( group.has_value() );
    REQUIRE( group->name == "Opened576" );
    REQUIRE( tabs->tabText( logFileTabs.front() ).endsWith( "Kept" ) );

    REQUIRE( TabNameMapping::get().tabName( goneMember.key() ).isEmpty() );
    REQUIRE_FALSE( TabGroupInfo::get().groupForTab( goneMember.key() ).has_value() );
    REQUIRE( std::ranges::none_of( TabGroupInfo::get().groups(), []( const auto& stored ) {
        return stored.name == "Gone576";
    } ) );

    opened.reset();
    auto& left = SessionInfo::getSynced();
    left.remove( openedId );
    left.save();
}

namespace {

// Opens the Log Files to be followed as the settings ask, for as long as it
// lives.
class FollowedOnLoad {
public:
    FollowedOnLoad()
        : before_( Configuration::get().followFileOnLoad() )
    {
        Configuration::get().setFollowFileOnLoad( true );
    }

    ~FollowedOnLoad()
    {
        Configuration::get().setFollowFileOnLoad( before_ );
    }

    FollowedOnLoad( const FollowedOnLoad& ) = delete;
    FollowedOnLoad& operator=( const FollowedOnLoad& ) = delete;
    FollowedOnLoad( FollowedOnLoad&& ) = delete;
    FollowedOnLoad& operator=( FollowedOnLoad&& ) = delete;

private:
    const bool before_;
};

} // namespace

// The window's follow action mirrors the View Set of the Log File in front:
// a Log File opened to be followed asks its View Set, and the action follows
// from its answer, not from the window setting it beside (#635).
SCENARIO( "A Log File opened to be followed shows followed in the window", "[ui][loading]" )
{
    const auto windowId = QStringLiteral( "mainwindow_load_test_window_635" );

    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_follow_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_follow_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }

    const FollowedOnLoad followedOnLoad;
    auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    const StoredSessionWindow stored{ windowId,
                                      { { QFileInfo( firstFile ).absoluteFilePath(), QString{} },
                                        { QFileInfo( secondFile ).absoluteFilePath(), QString{} } },
                                      1 };
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
    auto* followAction = windowAction( *mainWindow, logsquirl::mainwindow::action::followText );
    REQUIRE( followAction != nullptr );
    const auto frontFollowed = [ & ] {
        auto* front = qobject_cast<CrawlerWidget*>( tabArea->currentWidget() );
        return front != nullptr && front->isFollowEnabled() && followAction->isChecked();
    };

    GIVEN( "a Log File opened" )
    {
        mainWindow->loadFileNonInteractive( firstFile.fileName() );

        THEN( "its tab is followed and the follow action checked" )
        {
            REQUIRE( waitUiState( frontFollowed, 10000 ) );
        }
    }

    GIVEN( "a restored Session" )
    {
        mainWindow->reloadSession();
        const auto logFileTabs = tabArea->logFileTabs();
        REQUIRE( logFileTabs.size() == 2 );
        REQUIRE( tabArea->currentIndex() == logFileTabs.back() );

        THEN( "the tab in front is followed and the follow action checked" )
        {
            REQUIRE( waitUiState( frontFollowed, 10000 ) );
        }
    }

    mainWindow.reset();
}

// The window's actions reach the tab in front, and it alone, once, however
// often the tabs were switched (#635).
SCENARIO( "The window's actions reach only the tab in front and only once", "[ui][loading]" )
{
    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_actions_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_actions_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        file->write( "first Log Line\nsecond Log Line\n" );
        file->flush();
    }

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
    auto* reloadAction = windowAction( *mainWindow, logsquirl::mainwindow::action::reloadText );
    REQUIRE( reloadAction != nullptr );

    mainWindow->loadFileNonInteractive( firstFile.fileName() );
    mainWindow->loadFileNonInteractive( secondFile.fileName() );
    REQUIRE( waitUiState(
        [ & ] { return tabArea->logFileTabs().size() == 2 && reloadAction->isEnabled(); },
        10000 ) );
    auto* first = qobject_cast<CrawlerWidget*>( tabArea->widget( tabArea->logFileTabs().front() ) );
    auto* second = qobject_cast<CrawlerWidget*>( tabArea->widget( tabArea->logFileTabs().back() ) );
    REQUIRE( first != nullptr );
    REQUIRE( second != nullptr );
    REQUIRE( waitUiState( [ & ] { return first->hasLoaded() && second->hasLoaded(); }, 10000 ) );
    QTest::qWait( 100 );

    GIVEN( "the tabs switched back and forth, the second in front" )
    {
        tabArea->setCurrentWidget( second );
        tabArea->setCurrentWidget( first );
        tabArea->setCurrentWidget( second );
        tabArea->setCurrentWidget( first );
        tabArea->setCurrentWidget( second );
        REQUIRE( reloadAction->isEnabled() );

        QSignalSpy firstFinished( first, &CrawlerWidget::loadingFinished );
        QSignalSpy secondFinished( second, &CrawlerWidget::loadingFinished );

        WHEN( "the reload action is triggered" )
        {
            reloadAction->trigger();

            THEN( "the Log File in front is reloaded, once, and the other not" )
            {
                REQUIRE( waitUiState( [ & ] { return !secondFinished.isEmpty(); }, 10000 ) );
                QTest::qWait( 300 );
                REQUIRE( secondFinished.count() == 1 );
                REQUIRE( secondFinished.first().at( 0 ).value<LoadingStatus>()
                         == LoadingStatus::Successful );
                REQUIRE( firstFinished.isEmpty() );
            }
        }
    }

    mainWindow.reset();
}

// A loaded Log File brought to the front shows beside the info line the Log
// Line selected in it, not the first (#692).
SCENARIO( "The window shows the selected Log Line of the tab brought to the front",
          "[ui][loading]" )
{
    QTemporaryFile firstFile{ QDir::temp().filePath( "mainwindow_selected_first_XXXXXX" ) };
    QTemporaryFile secondFile{ QDir::temp().filePath( "mainwindow_selected_second_XXXXXX" ) };
    for ( auto* file : { &firstFile, &secondFile } ) {
        REQUIRE( file->open() );
        for ( int line = 0; line < 10; ++line ) {
            file->write( QByteArray( "Log Line " ) + QByteArray::number( line ) + '\n' );
        }
        file->flush();
    }

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
    // The field beside the info line that says the selected Log Line.
    const auto lineField = [ & ]() -> QString {
        for ( const auto* label : mainWindow->findChildren<QLabel*>() ) {
            if ( label->text().startsWith( "Ln:" ) ) {
                return label->text();
            }
        }
        return {};
    };

    mainWindow->loadFileNonInteractive( firstFile.fileName() );
    mainWindow->loadFileNonInteractive( secondFile.fileName() );
    REQUIRE( waitUiState( [ & ] { return tabArea->logFileTabs().size() == 2; }, 10000 ) );
    auto* first = qobject_cast<CrawlerWidget*>( tabArea->widget( tabArea->logFileTabs().front() ) );
    auto* second = qobject_cast<CrawlerWidget*>( tabArea->widget( tabArea->logFileTabs().back() ) );
    REQUIRE( first != nullptr );
    REQUIRE( second != nullptr );
    REQUIRE( waitUiState( [ & ] { return first->hasLoaded() && second->hasLoaded(); }, 10000 ) );
    QTest::qWait( 100 );

    GIVEN( "a Log Line selected in the first Log File" )
    {
        tabArea->setCurrentWidget( first );
        auto* textView = first->findChild<LogMainView*>();
        REQUIRE( textView != nullptr );
        textView->selectAndDisplayLine( 5_lnum );
        REQUIRE( waitUiState( [ & ] { return lineField().startsWith( "Ln:6/10" ); }, 10000 ) );

        WHEN( "the other tab is shown, and the first again" )
        {
            tabArea->setCurrentWidget( second );
            tabArea->setCurrentWidget( first );

            THEN( "the window shows the Log Line selected in it" )
            {
                REQUIRE( lineField().startsWith( "Ln:6/10" ) );
            }
        }
    }

    mainWindow.reset();
}
