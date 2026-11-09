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
#include "filterspanel.h"
#include "infoline.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "naminggroup.h"
#include "predefinedfilters.h"
#include "session.h"
#include "shown_widget.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"
#include "theme.h"
#include "valuenamescollection.h"
#include "valuenamespanel.h"

#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFontInfo>
#include <QFontMetrics>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QTest>
#include <QTreeWidget>
#include <QUuid>

#include <algorithm>
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
        showUntilExposed( *mainWindow );
        QTest::qWait( 50 );
        sidebar = mainWindow->findChild<QDockWidget*>( "sidebarDock" );
        REQUIRE( sidebar != nullptr );
    }

    void showSidebar() const
    {
        showUntilExposed( *sidebar );
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
    QTest::keyClick( line, Qt::Key_Return );
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

namespace {

using logsquirl::valuenames::NamingGroup;
using logsquirl::valuenames::NamingRule;

// The Predefined Filters and the Naming Groups, replaced for as long as this
// lives and restored, with what is stored of them, when it goes.
class StoredGroupsGuard {
public:
    StoredGroupsGuard()
        : filterSets_( PredefinedFiltersCollection::getSynced().filterSets() )
        , namingGroups_( ValueNamesCollection::get().ownGroups() )
        , teamNamingGroups_( ValueNamesCollection::get().groups().mid(
              ValueNamesCollection::get().ownGroups().size() ) )
    {
    }

    ~StoredGroupsGuard()
    {
        auto& filters = PredefinedFiltersCollection::getSynced();
        filters.setFilterSets( filterSets_ );
        filters.save();
        auto& valueNames = ValueNamesCollection::get();
        valueNames.setTeamGroups( teamNamingGroups_ );
        valueNames.setGroups( namingGroups_ );
        valueNames.save();
    }

    StoredGroupsGuard( const StoredGroupsGuard& ) = delete;
    StoredGroupsGuard& operator=( const StoredGroupsGuard& ) = delete;

private:
    QList<PredefinedFilterSet> filterSets_;
    QList<NamingGroup> namingGroups_;
    QList<NamingGroup> teamNamingGroups_;
};

// What the user sees of a tab of the sidebar: where its search field, rows
// and buttons are and how they look.
struct TabLook {
    QMargins margins;
    int spacing = 0;
    QRect searchField;
    QString treeFont;
    int indentation = 0;
    // Per row, in tree order: its left edge and height.
    QList<int> rowLefts;
    QList<int> rowHeights;
    // The buttons, left to right: their texts and places.
    QStringList buttonTexts;
    QList<QRect> buttons;

    bool operator==( const TabLook& ) const = default;
};

TabLook lookOf( QWidget& tab )
{
    TabLook look;
    REQUIRE( tab.layout() != nullptr );
    look.margins = tab.layout()->contentsMargins();
    look.spacing = tab.layout()->spacing();

    auto* searchField = tab.findChild<QLineEdit*>();
    REQUIRE( searchField != nullptr );
    look.searchField = searchField->geometry();

    auto* tree = tab.findChild<QTreeWidget*>();
    REQUIRE( tree != nullptr );
    look.treeFont = QFontInfo( tree->font() ).family() + QStringLiteral( " " )
                    + QString::number( QFontInfo( tree->font() ).pixelSize() );
    look.indentation = tree->indentation();
    for ( int g = 0; g < tree->topLevelItemCount(); ++g ) {
        const auto* group = tree->topLevelItem( g );
        look.rowLefts.append( tree->visualItemRect( group ).left() );
        look.rowHeights.append( tree->visualItemRect( group ).height() );
        for ( int e = 0; e < group->childCount(); ++e ) {
            look.rowLefts.append( tree->visualItemRect( group->child( e ) ).left() );
            look.rowHeights.append( tree->visualItemRect( group->child( e ) ).height() );
        }
    }

    auto buttons = tab.findChildren<QPushButton*>();
    std::sort( buttons.begin(), buttons.end(),
               []( const QPushButton* a, const QPushButton* b ) { return a->x() < b->x(); } );
    for ( const auto* button : buttons ) {
        look.buttonTexts.append( button->text() );
        look.buttons.append( button->geometry() );
    }
    return look;
}

} // namespace

SCENARIO( "The Filters tab and the Value Names tab of the sidebar look alike",
          "[ui][sidebar][look]" )
{
    // #924: the Value Names tab, filled before the window placed it in the
    // sidebar, showed its rows without the Theme's padding.
    Theme::apply( Theme::defaultTheme() );
    const StoredGroupsGuard guard;

    // The same group in both tabs: Default with Error — ERROR and Warn — WARN.
    // The Filters tab always has its Default Filter Group.
    auto& filters = PredefinedFiltersCollection::getSynced();
    filters.setFilterSets( {} );
    filters.setFilters( { { "Error", "ERROR", true }, { "Warn", "WARN", true } } );
    filters.save();

    auto namingGroup = NamingGroup::createNewGroup( QStringLiteral( "Default" ) );
    NamingRule error;
    error.name = QStringLiteral( "Error" );
    error.pattern = QStringLiteral( "ERROR" );
    NamingRule warn;
    warn.name = QStringLiteral( "Warn" );
    warn.pattern = QStringLiteral( "WARN" );
    namingGroup.setRules( { error, warn } );
    auto& valueNames = ValueNamesCollection::get();
    valueNames.setTeamGroups( {} );
    valueNames.setGroups( { namingGroup } );
    valueNames.save();

    const auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    Window window{ appSession, newWindowId() };
    window.showSidebar();

    auto* filtersTab = window.mainWindow->findChild<FiltersPanel*>();
    auto* valueNamesTab = window.mainWindow->findChild<ValueNamesPanel*>();
    REQUIRE( filtersTab != nullptr );
    REQUIRE( valueNamesTab != nullptr );
    auto* tabs = window.mainWindow->findChild<QTabWidget*>();
    REQUIRE( tabs != nullptr );
    REQUIRE( tabs->indexOf( filtersTab ) >= 0 );
    REQUIRE( tabs->indexOf( valueNamesTab ) >= 0 );

    tabs->setCurrentWidget( filtersTab );
    QTest::qWait( 50 );
    const auto filtersLook = lookOf( *filtersTab );
    tabs->setCurrentWidget( valueNamesTab );
    QTest::qWait( 50 );
    const auto valueNamesLook = lookOf( *valueNamesTab );

    // Three rows, padded by the Theme as the default style's rows are not.
    REQUIRE( filtersLook.rowHeights.size() == 3 );
    REQUIRE( filtersLook.rowHeights[ 1 ] > QFontMetrics( filtersTab->font() ).height() + 2 );

    CHECK( valueNamesLook.rowHeights == filtersLook.rowHeights );
    CHECK( valueNamesLook.rowLefts == filtersLook.rowLefts );
    CHECK( valueNamesLook.indentation == filtersLook.indentation );
    CHECK( valueNamesLook.margins == filtersLook.margins );
    CHECK( valueNamesLook.spacing == filtersLook.spacing );
    CHECK( valueNamesLook.searchField == filtersLook.searchField );
    CHECK( valueNamesLook.treeFont == filtersLook.treeFont );
    CHECK( valueNamesLook.buttonTexts == filtersLook.buttonTexts );
    CHECK( valueNamesLook.buttons == filtersLook.buttons );
    REQUIRE( valueNamesLook == filtersLook );

    window.mainWindow.reset();
    QTest::qWait( 50 );
}
