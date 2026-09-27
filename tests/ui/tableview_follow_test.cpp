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

#include "applicationplugins.h"
#include "crawlerwidget.h"
#include "fake_file_watch.h"
#include "logformatcatalog.h"
#include "logformatdefinition.h"
#include "logtableview.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "session.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QWheelEvent>

#include <memory>

#include <catch2/catch_test_macros.hpp>

// Follow is the window's action. The Table View leaves it when the user
// scrolls away from the bottom, as the Text View does, so that the next growth
// of the Log File does not snap it back; it never engages follow itself
// (#543). MainWindow builds offscreen on every platform, so this runs on
// macOS too.

struct TableViewFollowTest {};

template <>
struct CrawlerWidget::access_by<TableViewFollowTest> {
    // Recognizes the Log File with a Log Format of its own, and shows the
    // Table View.
    static LogTableView* showTableView( CrawlerWidget& crawler )
    {
        LogFormatDefinition format;
        format.setName( "tableview_follow_test" );
        format.setTitle( "Follow test" );
        QHash<QString, QString> regex;
        regex[ "basic" ] = R"(^(?<source>\w+)\s+(?<body>.*)$)";
        format.setRegexPatterns( regex );
        format.setBodyField( "body" );

        crawler.recognizedFormat_ = std::make_shared<const LogFormatDefinition>( format );
        crawler.logTableView_->setLogFormat( crawler.recognizedFormat_.get(),
                                             crawler.openLogFile_->logData().get() );
        crawler.tableViewToggle_->setVisible( true );
        crawler.tableViewToggle_->setChecked( true );
        return crawler.logTableView_;
    }
};

namespace {

QByteArray logLines( int first, int count )
{
    QByteArray lines;
    for ( int line = first; line < first + count; ++line ) {
        lines += QString( "LOGDATA follow test, line %1\n" )
                     .arg( line, 6, 10, QChar( '0' ) )
                     .toUtf8();
    }
    return lines;
}

bool isAtBottom( const LogTableView& view )
{
    const auto* scrollBar = view.verticalScrollBar();
    return scrollBar->maximum() > 0 && scrollBar->value() == scrollBar->maximum();
}

QAction* followActionOf( const MainWindow& window )
{
    for ( auto* action : window.findChildren<QAction*>() ) {
        if ( action->text()
             == QApplication::translate( "logsquirl::mainwindow::action",
                                         logsquirl::mainwindow::action::followText ) ) {
            return action;
        }
    }
    return nullptr;
}

} // namespace

SCENARIO( "The Table View leaves follow when the user scrolls away from the bottom",
          "[ui][follow][presentation]" )
{
    const QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "followed.log" );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( logLines( 0, 200 ) ) > 0 );
    }

    const auto fileWatch = std::make_shared<FakeFileWatch>();
    const auto appSession = std::make_shared<Session>(
        testSettingsPolicies(), std::make_shared<LogFormatCatalog>(), fileWatch );
    auto mainWindow = std::make_unique<MainWindow>(
        WindowSession{ appSession, QUuid::createUuid().toString( QUuid::WithoutBraces ), 0 },
        std::make_shared<logsquirl::plugins::ApplicationPlugins>() );
    mainWindow->resize( 1000, 700 );
    mainWindow->show();
    mainWindow->loadFileNonInteractive( path );

    CrawlerWidget* crawler = nullptr;
    REQUIRE( waitUiState( [ & ] {
        crawler = mainWindow->findChild<CrawlerWidget*>();
        return crawler != nullptr && fileWatch->isWatched( path );
    } ) );

    auto* followAction = followActionOf( *mainWindow );
    REQUIRE( followAction != nullptr );
    REQUIRE( followAction->isEnabled() );

    GIVEN( "the Log File shown in the Table View, followed, at its bottom" )
    {
        auto* table = CrawlerWidget::access_by<TableViewFollowTest>::showTableView( *crawler );
        REQUIRE( waitUiState(
            [ & ] { return table->model() != nullptr && table->model()->rowCount() == 200; } ) );
        followAction->setChecked( true );
        REQUIRE( crawler->isFollowEnabled() );
        REQUIRE( isAtBottom( *table ) );
        REQUIRE( fileWatch->grow( path, logLines( 200, 2 ) ) );
        REQUIRE( waitUiState(
            [ & ] { return table->model()->rowCount() == 202 && isAtBottom( *table ); } ) );
        REQUIRE( followAction->isChecked() );

        WHEN( "its scrollbar is dragged to the top, and Log Lines are appended" )
        {
            table->verticalScrollBar()->setSliderPosition( 0 );
            REQUIRE( table->rowAt( 0 ) == 0 );
            REQUIRE( fileWatch->grow( path, logLines( 202, 2 ) ) );
            REQUIRE( waitUiState( [ & ] { return table->model()->rowCount() == 204; } ) );
            QTest::qWait( 50 );

            THEN( "the first visible Row stays, and follow is off in the window and in the Log "
                  "File's views" )
            {
                REQUIRE( table->rowAt( 0 ) == 0 );
                REQUIRE_FALSE( followAction->isChecked() );
                REQUIRE_FALSE( crawler->isFollowEnabled() );
            }
        }

        WHEN( "it is scrolled up with the wheel" )
        {
            const QPoint center = table->viewport()->rect().center();
            QWheelEvent wheel( center, table->viewport()->mapToGlobal( center ), QPoint{},
                               QPoint{ 0, 120 }, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                               false );
            QCoreApplication::sendEvent( table->viewport(), &wheel );

            THEN( "follow is off" )
            {
                REQUIRE_FALSE( isAtBottom( *table ) );
                REQUIRE_FALSE( followAction->isChecked() );
                REQUIRE_FALSE( crawler->isFollowEnabled() );
            }
        }

        WHEN( "Page Up is pressed" )
        {
            table->setFocus();
            QTest::keyClick( table, Qt::Key_PageUp );

            THEN( "follow is off" )
            {
                REQUIRE_FALSE( isAtBottom( *table ) );
                REQUIRE_FALSE( followAction->isChecked() );
                REQUIRE_FALSE( crawler->isFollowEnabled() );
            }
        }

        WHEN( "its scrollbar is dragged to the bottom it is at" )
        {
            table->verticalScrollBar()->setSliderPosition( table->verticalScrollBar()->maximum() );

            THEN( "follow stays on" )
            {
                REQUIRE( followAction->isChecked() );
                REQUIRE( crawler->isFollowEnabled() );
            }
        }

        WHEN( "a Row is clicked" )
        {
            const auto row = table->rowAt( table->viewport()->height() / 2 );
            QTest::mouseClick( table->viewport(), Qt::LeftButton, Qt::NoModifier,
                               QPoint( 10, table->rowViewportPosition( row ) + 2 ) );

            THEN( "its Row is selected, and follow is off" )
            {
                REQUIRE( table->selectionModel()->isRowSelected( row ) );
                REQUIRE( waitUiState( [ & ] { return !followAction->isChecked(); } ) );
                REQUIRE_FALSE( crawler->isFollowEnabled() );
            }
        }
    }

    mainWindow.reset();
}
