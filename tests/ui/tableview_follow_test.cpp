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
#include "filteredview.h"
#include "logformatcatalog.h"
#include "logformatdefinition.h"
#include "logmainview.h"
#include "logtableview.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
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
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Follow is owned by the Log File's View Set, and the window's action mirrors
// it (#558). The Table View leaves it when the user scrolls away from the
// bottom, as the Text View does, so that the next growth of the Log File does
// not snap it back; it never engages follow itself (#543). MainWindow builds
// offscreen on every platform, so this runs on macOS too.

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

    static LogMainView* textView( CrawlerWidget& crawler )
    {
        return crawler.logMainView_;
    }

    // Searches for pattern, the current Search's results kept in their tab
    // first when keep says so.
    static void search( CrawlerWidget& crawler, const QString& pattern, bool keep )
    {
        crawler.keepSearchResultsButton_->setChecked( keep );
        crawler.searchLineEdit_->setEditText( pattern );
        crawler.startNewSearch();
    }

    static std::vector<FilteredView*> filteredViews( CrawlerWidget& crawler )
    {
        std::vector<FilteredView*> views;
        for ( int tab = 0; tab < crawler.tabbedFilteredView_->count(); ++tab ) {
            views.push_back(
                qobject_cast<FilteredView*>( crawler.tabbedFilteredView_->widget( tab ) ) );
        }
        return views;
    }
};

template <>
struct LogTableView::access_by<TableViewFollowTest> {
    static bool follows( const LogTableView& view )
    {
        return view.follow_;
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

// Every view of the Log File the user can scroll.
struct LogFileViews {
    LogMainView* text = nullptr;
    LogTableView* table = nullptr;
    std::vector<FilteredView*> filtered;

    // Whether every view follows as expected, and the Crawler Widget says so.
    bool allFollow( const CrawlerWidget& crawler, bool expected ) const
    {
        bool agree = crawler.isFollowEnabled() == expected && text->isFollowEnabled() == expected
                     && LogTableView::access_by<TableViewFollowTest>::follows( *table ) == expected;
        for ( const auto* view : filtered ) {
            agree = agree && view->isFollowEnabled() == expected;
        }
        return agree;
    }
};

// A page up by the view's scrollbar: a move away from the bottom.
void pageUp( QAbstractScrollArea* view )
{
    view->verticalScrollBar()->triggerAction( QAbstractSlider::SliderPageStepSub );
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

SCENARIO( "Follow has one owner per Log File", "[ui][follow][viewset]" )
{
    const QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "followed.log" );
    const auto otherPath = directory.filePath( "other.log" );
    for ( const auto& file : { path, otherPath } ) {
        QFile log( file );
        REQUIRE( log.open( QIODevice::WriteOnly ) );
        REQUIRE( log.write( logLines( 0, 200 ) ) > 0 );
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

    using Access = CrawlerWidget::access_by<TableViewFollowTest>;
    LogFileViews views;
    views.text = Access::textView( *crawler );
    views.table = Access::showTableView( *crawler );
    REQUIRE( waitUiState( [ & ] {
        return views.table->model() != nullptr && views.table->model()->rowCount() == 200;
    } ) );
    // Two Searches, the first kept in its tab: two Filtered Views.
    Access::search( *crawler, "line 0000", false );
    Access::search( *crawler, "line 0001", true );
    REQUIRE( waitUiState( [ & ] { return Access::filteredViews( *crawler ).size() == 2; } ) );
    views.filtered = Access::filteredViews( *crawler );

    GIVEN( "the Log File followed from the window's action" )
    {
        followAction->setChecked( true );
        REQUIRE( views.allFollow( *crawler, true ) );

        WHEN( "the user leaves follow from the Text View" )
        {
            pageUp( views.text );

            THEN( "no view of the Log File follows, and the action says so" )
            {
                REQUIRE( views.allFollow( *crawler, false ) );
                REQUIRE_FALSE( followAction->isChecked() );
            }
        }

        WHEN( "the user leaves follow from the Table View" )
        {
            pageUp( views.table );

            THEN( "no view of the Log File follows, and the action says so" )
            {
                REQUIRE( views.allFollow( *crawler, false ) );
                REQUIRE_FALSE( followAction->isChecked() );
            }
        }

        WHEN( "the user leaves follow from the kept Search's Filtered View" )
        {
            pageUp( views.filtered.front() );

            THEN( "no view of the Log File follows, and the action says so" )
            {
                REQUIRE( views.allFollow( *crawler, false ) );
                REQUIRE_FALSE( followAction->isChecked() );
            }
        }

        WHEN( "the user leaves follow from the current Search's Filtered View" )
        {
            pageUp( views.filtered.back() );

            THEN( "no view of the Log File follows, and the action says so" )
            {
                REQUIRE( views.allFollow( *crawler, false ) );
                REQUIRE_FALSE( followAction->isChecked() );
            }
        }

        WHEN( "another Log File is opened in a tab of its own" )
        {
            mainWindow->loadFileNonInteractive( otherPath );
            CrawlerWidget* other = nullptr;
            REQUIRE( waitUiState( [ & ] {
                for ( auto* candidate : mainWindow->findChildren<CrawlerWidget*>() ) {
                    if ( candidate != crawler ) {
                        other = candidate;
                    }
                }
                return other != nullptr && fileWatch->isWatched( otherPath );
            } ) );
            auto* tabs = mainWindow->findChild<TabbedCrawlerWidget*>();
            REQUIRE( tabs->currentWidget() == other );

            THEN( "the action says the other Log File is not followed, and every view of the "
                  "first still follows" )
            {
                REQUIRE_FALSE( followAction->isChecked() );
                REQUIRE_FALSE( other->isFollowEnabled() );
                REQUIRE( views.allFollow( *crawler, true ) );
            }

            AND_WHEN( "follow is left from a view of the first Log File, its tab not current" )
            {
                pageUp( views.filtered.front() );

                THEN( "no view of it follows, and its tab made current, the action says so" )
                {
                    REQUIRE( views.allFollow( *crawler, false ) );
                    tabs->setCurrentWidget( crawler );
                    REQUIRE_FALSE( followAction->isChecked() );
                    REQUIRE( views.allFollow( *crawler, false ) );
                }
            }

            AND_WHEN( "the first Log File's tab is made current again" )
            {
                tabs->setCurrentWidget( crawler );

                THEN( "the action says it is followed, as every view of it does" )
                {
                    REQUIRE( followAction->isChecked() );
                    REQUIRE( views.allFollow( *crawler, true ) );
                    REQUIRE_FALSE( other->isFollowEnabled() );
                }

                THEN( "leaving follow from any of its views leaves it in every one, and the "
                      "action says so" )
                {
                    for ( auto* view :
                          { static_cast<QAbstractScrollArea*>( views.text ),
                            static_cast<QAbstractScrollArea*>( views.table ),
                            static_cast<QAbstractScrollArea*>( views.filtered.front() ),
                            static_cast<QAbstractScrollArea*>( views.filtered.back() ) } ) {
                        followAction->setChecked( true );
                        REQUIRE( views.allFollow( *crawler, true ) );
                        pageUp( view );
                        REQUIRE( views.allFollow( *crawler, false ) );
                        REQUIRE_FALSE( followAction->isChecked() );
                    }
                }
            }
        }
    }

    mainWindow.reset();
}
