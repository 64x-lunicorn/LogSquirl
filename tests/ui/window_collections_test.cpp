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

// The Highlighter Sets and the Predefined Filters are global, as the Value
// Names are: what one window of the Session changes in them shows in every
// other window, in its Highlighters menu and its Filters panel, without a
// restart (#942).

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <memory>

#include "abstractlogview.h"
#include "addpredefinedfilterdialog.h"
#include "applicationplugins.h"
#include "context_menu_entries.h"
#include "crawlerwidget.h"
#include "filterspanel.h"
#include "grouplist.h"
#include "highlightersdialog.h"
#include "highlighterset.h"
#include "highlightersmenu.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "predefinedfilters.h"
#include "session.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

struct WindowCollectionsTest {};

template <>
struct AbstractLogView::access_by<WindowCollectionsTest> {
    static std::unique_ptr<QMenu> contextMenu( AbstractLogView& view )
    {
        return view.createContextMenu( QPoint( view.viewport()->width() / 2, 1 ) );
    }

    static void selectPortion( AbstractLogView& view, LineNumber line, LineColumn startColumn,
                               LineColumn endColumn )
    {
        view.selection_.selectPortion( line, startColumn, endColumn );
        view.update();
    }
};

template <>
struct CrawlerWidget::access_by<WindowCollectionsTest> {
    static AbstractLogView& textView( const CrawlerWidget& crawler )
    {
        return *crawler.logMainView_;
    }
};

namespace {

using ViewAccess = AbstractLogView::access_by<WindowCollectionsTest>;
using CrawlerAccess = CrawlerWidget::access_by<WindowCollectionsTest>;

const auto TheLogLine = QStringLiteral( "modem link ERROR done" );
const auto SelectedText = QStringLiteral( "link" );
// What the Highlighters dialog names a set it adds.
const auto NewSetName = QStringLiteral( "New Highlighter Set" );

// The Filter Groups and the Highlighter Sets, stored again as they were when
// this goes.
class KeptCollections {
public:
    KeptCollections()
        : filterSets_( PredefinedFiltersCollection::getSynced().filterSets() )
        , highlighterSets_( HighlighterSetCollection::get().highlighterSets() )
        , activeSetIds_( HighlighterSetCollection::get().activeSetIds() )
    {
        storeFilterSets( {} );
        storeHighlighterSets( {}, {} );
    }

    ~KeptCollections()
    {
        storeFilterSets( filterSets_ );
        storeHighlighterSets( highlighterSets_, activeSetIds_ );
    }

    KeptCollections( const KeptCollections& ) = delete;
    KeptCollections& operator=( const KeptCollections& ) = delete;

private:
    static void storeFilterSets( const QList<PredefinedFilterSet>& sets )
    {
        auto& collection = PredefinedFiltersCollection::getSynced();
        collection.setFilterSets( sets );
        collection.save();
    }

    static void storeHighlighterSets( const QList<HighlighterSet>& sets,
                                      const QStringList& activeSetIds )
    {
        auto& collection = HighlighterSetCollection::get();
        collection.setHighlighterSets( sets );
        collection.deactivateAll();
        for ( const auto& setId : activeSetIds ) {
            collection.activateSet( setId );
        }
        collection.save();
    }

    QList<PredefinedFilterSet> filterSets_;
    QList<HighlighterSet> highlighterSets_;
    QStringList activeSetIds_;
};

// A window over the Session, showing the Log File at path when there is one.
struct SessionMainWindow {
    SessionMainWindow( const std::shared_ptr<Session>& session, const QString& name,
                       const QString& path = {} )
        : window( std::make_unique<MainWindow>(
              WindowSession{ session, name, 0 },
              std::make_shared<logsquirl::plugins::ApplicationPlugins>() ) )
    {
        showUntilExposed( *window );
        if ( !path.isEmpty() ) {
            window->loadFileNonInteractive( path );
            REQUIRE( waitUiState( [ this ] {
                crawler = window->findChild<CrawlerWidget*>();
                return crawler != nullptr
                       && crawler->state().loadStatus == LoadingStatus::Successful;
            } ) );
            QTest::qWait( 50 );
        }
    }

    ~SessionMainWindow()
    {
        window.reset();
        QTest::qWait( 50 );
    }

    SessionMainWindow( const SessionMainWindow& ) = delete;
    SessionMainWindow& operator=( const SessionMainWindow& ) = delete;

    // Whether the Filters panel lists a filter of the name.
    bool listsFilter( const QString& name ) const
    {
        auto* tree = window->findChild<FiltersPanel*>()->findChild<QTreeWidget*>();
        REQUIRE( tree != nullptr );
        for ( int top = 0; top < tree->topLevelItemCount(); ++top ) {
            const auto* group = tree->topLevelItem( top );
            for ( int child = 0; child < group->childCount(); ++child ) {
                if ( group->child( child )->data( 0, Qt::UserRole ).toString() == name ) {
                    return true;
                }
            }
        }
        return false;
    }

    // The action of the Highlighters menu with the text, or nullptr.
    QAction* highlightersAction( const QString& text ) const
    {
        auto* menu = window->findChild<HighlightersMenu*>();
        REQUIRE( menu != nullptr );
        for ( auto* action : menu->actions() ) {
            if ( action->text() == text ) {
                return action;
            }
        }
        return nullptr;
    }

    std::unique_ptr<MainWindow> window;
    CrawlerWidget* crawler = nullptr;
};

QString writeLogFile( const QTemporaryDir& dir )
{
    const auto path = dir.filePath( QStringLiteral( "first.log" ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( ( TheLogLine + QStringLiteral( "\nother line\n" ) ).toUtf8() );
    return path;
}

} // namespace

SCENARIO( "A Predefined Filter or Highlighter Set changed in one window shows in the other",
          "[ui][session][predefinedfilters][highlighters]" )
{
    const KeptCollections kept;

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto session
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    SessionMainWindow first( session, QStringLiteral( "First" ), writeLogFile( dir ) );
    const SessionMainWindow second( session, QStringLiteral( "Second" ) );

    WHEN( "a selection is added as a Predefined Filter in the first window" )
    {
        REQUIRE_FALSE( second.listsFilter( SelectedText ) );

        auto& textView = CrawlerAccess::textView( *first.crawler );
        const auto start = TheLogLine.indexOf( SelectedText );
        ViewAccess::selectPortion( textView, 0_lnum, LineColumn{ start },
                                   LineColumn{ start + SelectedText.size() - 1 } );
        const auto menu = ViewAccess::contextMenu( textView );
        auto* entry = contextMenuEntry( *menu, QStringLiteral( "Add as Predefined Filter..." ) );
        REQUIRE( entry != nullptr );
        bool opened = false;
        QTimer::singleShot( 0, [ & ] {
            auto* dialog
                = qobject_cast<AddPredefinedFilterDialog*>( QApplication::activeModalWidget() );
            REQUIRE( dialog != nullptr );
            opened = true;
            dialog->findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok )->click();
        } );
        entry->trigger();
        REQUIRE( opened );

        THEN( "the Filters panel of both windows lists it" )
        {
            REQUIRE( first.listsFilter( SelectedText ) );
            REQUIRE( second.listsFilter( SelectedText ) );
        }
    }

    WHEN( "a Highlighter Set is added in the first window's Highlighters dialog" )
    {
        REQUIRE( second.highlightersAction( NewSetName ) == nullptr );

        auto* configure = first.highlightersAction(
            QString::fromLatin1( logsquirl::mainwindow::action::editHighlightersText ) );
        REQUIRE( configure != nullptr );
        bool opened = false;
        QTimer::singleShot( 0, [ & ] {
            auto* dialog = qobject_cast<HighlightersDialog*>( QApplication::activeModalWidget() );
            REQUIRE( dialog != nullptr );
            opened = true;
            dialog->groupList()->newButton()->click();
            QCoreApplication::processEvents();
            dialog->buttonBox->button( QDialogButtonBox::Ok )->click();
        } );
        configure->trigger();
        REQUIRE( opened );

        THEN( "the Highlighters menu of both windows lists it" )
        {
            REQUIRE( first.highlightersAction( NewSetName ) != nullptr );
            REQUIRE( second.highlightersAction( NewSetName ) != nullptr );
        }

        AND_WHEN( "it is switched on in the second window's Highlighters menu" )
        {
            auto* action = second.highlightersAction( NewSetName );
            REQUIRE( action != nullptr );
            action->trigger();

            THEN( "the first window's Highlighters menu shows it switched on" )
            {
                auto* shown = first.highlightersAction( NewSetName );
                REQUIRE( shown != nullptr );
                REQUIRE( shown->isChecked() );
            }
        }
    }
}
