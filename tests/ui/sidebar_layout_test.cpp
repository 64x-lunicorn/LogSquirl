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

#include "crawlerwidget.h"
#include "infoline.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFontMetrics>
#include <QLineEdit>
#include <QTemporaryFile>
#include <QTest>
#include <QUuid>

#include <memory>

#include <catch2/catch_test_macros.hpp>

// The Search line keeps a usable width while the sidebar is open, and the
// sidebar opens at a moderate width it remembers (#261). MainWindow builds
// offscreen on every platform, so these scenarios run on macOS too.

namespace {

constexpr int WindowWidth = 1200;
constexpr int WindowHeight = 800;

bool writeLogFile( QTemporaryFile& file )
{
    if ( !file.open() ) {
        return false;
    }
    for ( int i = 0; i < 50; ++i ) {
        file.write( QByteArray( "2026-09-17 12:00:00 INFO a line of the Log File number " )
                    + QByteArray::number( i ) + '\n' );
    }
    file.flush();
    return true;
}

// A window of its own in the stored Session, so a sidebar width saved by
// another test does not leak in.
struct Window {
    explicit Window( std::shared_ptr<Session> appSession, const QString& windowId )
        : mainWindow( std::make_unique<MainWindow>(
              WindowSession{ appSession, windowId, 0 },
              std::make_shared<logsquirl::plugins::ApplicationPlugins>() ) )
    {
        mainWindow->resize( WindowWidth, WindowHeight );
        mainWindow->show();
        QTest::qWait( 50 );
        sidebar = mainWindow->findChild<QDockWidget*>( "sidebarDock" );
        REQUIRE( sidebar != nullptr );
    }

    void showSidebar() const
    {
        sidebar->show();
        QTest::qWait( 100 );
    }

    std::unique_ptr<MainWindow> mainWindow;
    QDockWidget* sidebar = nullptr;
};

CrawlerWidget* currentCrawler( TabbedCrawlerWidget* tabArea )
{
    auto* crawler = qobject_cast<CrawlerWidget*>( tabArea->currentWidget() );
    REQUIRE( crawler != nullptr );
    return crawler;
}

QComboBox* searchLine( TabbedCrawlerWidget* tabArea )
{
    auto* crawler = currentCrawler( tabArea );
    for ( auto* comboBox : crawler->findChildren<QComboBox*>() ) {
        if ( comboBox->accessibleName() == "Search pattern" ) {
            return comboBox;
        }
    }
    FAIL( "no Search line" );
    return nullptr;
}

bool searchLineKeepsTwentyCharacters( const QComboBox* searchLine )
{
    return searchLine->isVisible()
           && searchLine->lineEdit()->width() >= 20 * searchLine->fontMetrics().averageCharWidth();
}

QString newWindowId()
{
    return QUuid::createUuid().toString( QUuid::WithoutBraces );
}

} // namespace

SCENARIO( "The Search line keeps a usable width while the sidebar is open",
          "[ui][sidebar][searchline]" )
{
    QTemporaryFile file{ QDir::tempPath() + "/sidebar_layout_test_XXXXXX" };
    REQUIRE( writeLogFile( file ) );

    const auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    Window window{ appSession, newWindowId() };

    auto* tabArea = window.mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );
    const auto baseTabCount = tabArea->count();
    window.mainWindow->loadInitialFile( file.fileName(), false );
    REQUIRE( waitUiState( [ & ] { return tabArea->count() == baseTabCount + 1; } ) );

    // A Search, so the row shows its match count.
    auto* line = searchLine( tabArea );
    line->setEditText( "line" );
    QTest::keyClick( line->lineEdit(), Qt::Key_Return );
    REQUIRE( waitUiState( [ & ] {
        const auto* matchCount = currentCrawler( tabArea )->findChild<InfoLine*>();
        return matchCount != nullptr && matchCount->text().contains( "matches found" );
    } ) );

    GIVEN( "a window 1200 px wide with the sidebar opened" )
    {
        window.showSidebar();
        REQUIRE( window.mainWindow->width() == WindowWidth );

        THEN( "the sidebar opens at a moderate share of the window" )
        {
            REQUIRE( window.sidebar->width() >= WindowWidth * 20 / 100 );
            REQUIRE( window.sidebar->width() <= WindowWidth * 32 / 100 );
        }

        THEN( "the Search line is wide enough for about 20 characters" )
        {
            REQUIRE( searchLineKeepsTwentyCharacters( searchLine( tabArea ) ) );
        }

        WHEN( "the user drags the sidebar over most of the window" )
        {
            window.mainWindow->resizeDocks( { window.sidebar }, { WindowWidth * 80 / 100 },
                                            Qt::Horizontal );
            QTest::qWait( 100 );

            THEN( "the sidebar gives way before the Search line does" )
            {
                REQUIRE( window.sidebar->width() < WindowWidth * 80 / 100 );
                REQUIRE( searchLineKeepsTwentyCharacters( searchLine( tabArea ) ) );
            }

            THEN( "the visibility combo box and the match count gave way first" )
            {
                auto* crawler = currentCrawler( tabArea );
                QComboBox* visibility = nullptr;
                for ( auto* comboBox : crawler->findChildren<QComboBox*>() ) {
                    if ( comboBox->count() > 0 && comboBox->itemText( 0 ) == "Marks and matches" ) {
                        visibility = comboBox;
                    }
                }
                REQUIRE( visibility != nullptr );
                REQUIRE( visibility->width() < visibility->sizeHint().width() );

                auto* matchCount = crawler->findChild<InfoLine*>();
                REQUIRE( matchCount != nullptr );
                REQUIRE( matchCount->text().contains( "matches found" ) );
                REQUIRE( matchCount->width() < matchCount->sizeHint().width() );
            }
        }
    }

    window.mainWindow.reset();
    QTest::qWait( 50 );
}

SCENARIO( "The sidebar opens at the width the user dragged it to", "[ui][sidebar]" )
{
    const auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    const auto windowId = newWindowId();
    constexpr int DraggedWidth = 420;

    {
        Window first{ appSession, windowId };
        first.showSidebar();
        first.mainWindow->resizeDocks( { first.sidebar }, { DraggedWidth }, Qt::Horizontal );
        QTest::qWait( 100 );
        REQUIRE( std::abs( first.sidebar->width() - DraggedWidth ) <= 2 );

        // Quitting saves the window's session.
        appSession->setExitRequested( true );
        first.mainWindow->close();
        QTest::qWait( 50 );
        appSession->setExitRequested( false );
    }

    Window second{ appSession, windowId };
    second.showSidebar();
    REQUIRE( std::abs( second.sidebar->width() - DraggedWidth ) <= 2 );

    second.mainWindow->close();
    QTest::qWait( 50 );
}
