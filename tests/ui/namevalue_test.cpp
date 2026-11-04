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

// Name this value... (#883): the context menu of the Text View and the
// Filtered View offers it for a value, the dialog it opens proposes a rule, a
// table and a template from the Log Line, and OK names the value in the tab.

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "context_menu_entries.h"
#include "crawlerwidget.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "namevaluedialog.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"
#include "valuenames_fixture.h"
#include "valuenamescollection.h"

using logsquirl::valuenames::LineRange;
using valuenamesfixture::NameRow;
using valuenamesfixture::NameTable;
using valuenamesfixture::NamingGroup;

struct NameValueTest {};

template <>
struct AbstractLogView::access_by<NameValueTest> {
    static std::unique_ptr<QMenu> contextMenu( AbstractLogView& view, const QPoint& pos )
    {
        return view.createContextMenu( pos );
    }

    static void selectPortion( AbstractLogView& view, LineNumber line, LineColumn startColumn,
                               LineColumn endColumn )
    {
        view.selection_.selectPortion( line, startColumn, endColumn );
        view.update();
    }

    // A point, in viewport coordinates, on the column of the Log Line shown at
    // row, counted from the top of the view.
    static QPoint pointOn( const AbstractLogView& view, int row, qsizetype column )
    {
        const auto y = row * view.charHeight_ + view.charHeight_ / 2;
        for ( int x = 0; x < view.viewport()->width(); ++x ) {
            const auto position = view.logLineFilePosAt( QPoint( x, y ) );
            if ( position.column().get() == column ) {
                return { x + static_cast<int>( view.charWidth_ / 2 ), y };
            }
        }
        FAIL( "no point on column " << column );
        return {};
    }
};

template <>
struct CrawlerWidget::access_by<NameValueTest> {
    static AbstractLogView& textView( const CrawlerWidget& crawler )
    {
        return *crawler.logMainView_;
    }

    static AbstractLogView& filteredView( const CrawlerWidget& crawler )
    {
        return *crawler.currentFilteredView();
    }

    static void search( CrawlerWidget& crawler, const QString& pattern )
    {
        auto* edit = SearchLineAccess::patternEdit( *crawler.searchLine_ );
        edit->clearEditText();
        QTest::keyClicks( edit, pattern );
        QTest::mouseClick( SearchLineAccess::searchButton( *crawler.searchLine_ ), Qt::LeftButton );
        QTest::qWait( 100 );
        REQUIRE( waitUiState(
            [ & ] { return SearchLineAccess::stopButton( *crawler.searchLine_ )->isHidden(); } ) );
    }
};

namespace {

using ViewAccess = AbstractLogView::access_by<NameValueTest>;
using CrawlerAccess = CrawlerWidget::access_by<NameValueTest>;

const auto NameThisValue = QStringLiteral( "Name this value..." );
const auto EsimLine = QStringLiteral( "modem EsimState=9 done" );

// Restores the collection, and what is stored of it, when it goes.
class StoredValueNamesGuard {
public:
    StoredValueNamesGuard()
        : before_( ValueNamesCollection::get().ownGroups() )
    {
    }

    ~StoredValueNamesGuard()
    {
        storeGroups( before_ );
    }

    StoredValueNamesGuard( const StoredValueNamesGuard& ) = delete;
    StoredValueNamesGuard& operator=( const StoredValueNamesGuard& ) = delete;

    static void storeGroups( const QList<NamingGroup>& groups )
    {
        auto& collection = ValueNamesCollection::get();
        collection.setTeamGroups( {} );
        collection.setGroups( groups );
        collection.save();
    }

private:
    QList<NamingGroup> before_;
};

LineRange rangeOf( const QString& line, const QString& text )
{
    const auto start = line.indexOf( text );
    REQUIRE( start >= 0 );
    return LineRange{ start, text.size() };
}

template <class Widget>
Widget* childOf( QWidget& parent, const QString& name )
{
    auto* child = parent.findChild<Widget*>( name );
    REQUIRE( child != nullptr );
    return child;
}

void setName( NameValueDialog& dialog, int row, const QString& name )
{
    auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
    REQUIRE( rows->item( row, 1 ) != nullptr );
    rows->item( row, 1 )->setText( name );
}

void clickOk( QDialog& dialog )
{
    auto* ok = dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok );
    REQUIRE( ok->isEnabled() );
    ok->click();
}

// A window showing the Log File, Value Names off in its tab.
struct NameValueWindow {
    NameValueWindow()
    {
        REQUIRE( dir.isValid() );
        const auto path = dir.filePath( QStringLiteral( "esim.log" ) );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( ( EsimLine + QStringLiteral( "\nother line\n" ) ).toUtf8() );
        file.close();

        auto policies = testSettingsPolicies();
        policies.presentation.showValueNames = false;
        const auto session
            = std::make_shared<Session>( policies, std::make_shared<LogFormatCatalog>() );
        window = std::make_unique<MainWindow>(
            WindowSession{ session, QStringLiteral( "Main" ), 0 },
            std::make_shared<logsquirl::plugins::ApplicationPlugins>() );
        showUntilExposed( *window );
        window->loadFileNonInteractive( path );
        REQUIRE( waitUiState( [ this ] {
            crawler = window->findChild<CrawlerWidget*>();
            return crawler != nullptr && crawler->state().loadStatus == LoadingStatus::Successful;
        } ) );
        QTest::qWait( 50 );
    }

    ~NameValueWindow()
    {
        window.reset();
        QTest::qWait( 50 );
    }

    NameValueWindow( const NameValueWindow& ) = delete;
    NameValueWindow& operator=( const NameValueWindow& ) = delete;

    QAction* action( const QString& text ) const
    {
        for ( auto* candidate : window->findChildren<QAction*>() ) {
            if ( candidate->text() == text ) {
                return candidate;
            }
        }
        FAIL( "no action " << text.toStdString() );
        return nullptr;
    }

    QTemporaryDir dir;
    std::unique_ptr<MainWindow> window;
    CrawlerWidget* crawler = nullptr;
};

// Chooses Name this value... of the menu, and in the dialog it opens does
// what fill does, then clicks OK.
void nameInDialog( QMenu& menu, const std::function<void( NameValueDialog& )>& fill )
{
    auto* entry = contextMenuEntry( menu, NameThisValue );
    REQUIRE( entry != nullptr );
    REQUIRE( entry->isEnabled() );
    bool opened = false;
    QTimer::singleShot( 0, [ & ] {
        auto* dialog = qobject_cast<NameValueDialog*>( QApplication::activeModalWidget() );
        REQUIRE( dialog != nullptr );
        opened = true;
        fill( *dialog );
        clickOk( *dialog );
    } );
    entry->trigger();
    REQUIRE( opened );
}

} // namespace

SCENARIO( "The context menu of the Text View and the Filtered View offers Name this value...",
          "[ui][valuenames][namevalue]" )
{
    const StoredValueNamesGuard guard;
    StoredValueNamesGuard::storeGroups( {} );
    const NameValueWindow shown;
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    GIVEN( "the Text View" )
    {
        WHEN( "text holding a value is selected" )
        {
            const auto range = rangeOf( EsimLine, QStringLiteral( "EsimState=9" ) );
            ViewAccess::selectPortion( textView, 0_lnum, LineColumn{ range.start },
                                       LineColumn{ range.end() - 1 } );
            const auto menu
                = ViewAccess::contextMenu( textView, ViewAccess::pointOn( textView, 0, 0 ) );

            THEN( "the entry is offered at the top level, enabled" )
            {
                REQUIRE( entriesOf( *menu ).contains( NameThisValue ) );
                REQUIRE( contextMenuEntry( *menu, NameThisValue )->isEnabled() );
                REQUIRE( entriesOf( *menu ).size() <= MaxTopLevelEntries );
            }
        }

        WHEN( "text without a value is selected" )
        {
            ViewAccess::selectPortion( textView, 0_lnum, 0_lcol, 4_lcol );
            const auto menu
                = ViewAccess::contextMenu( textView, ViewAccess::pointOn( textView, 0, 0 ) );

            THEN( "the entry is disabled" )
            {
                REQUIRE_FALSE( contextMenuEntry( *menu, NameThisValue )->isEnabled() );
            }
        }

        WHEN( "nothing is selected within a Log Line, and the menu opens on a value" )
        {
            textView.selectAndDisplayLine( 0_lnum );
            const auto menu = ViewAccess::contextMenu(
                textView, ViewAccess::pointOn( textView, 0, EsimLine.indexOf( u'9' ) ) );

            THEN( "the entry names the word under the cursor" )
            {
                REQUIRE( contextMenuEntry( *menu, NameThisValue )->isEnabled() );
            }
        }

        WHEN( "nothing is selected within a Log Line, and the menu opens on a word without a "
              "value" )
        {
            textView.selectAndDisplayLine( 1_lnum );
            const auto menu
                = ViewAccess::contextMenu( textView, ViewAccess::pointOn( textView, 1, 1 ) );

            THEN( "the entry is disabled" )
            {
                REQUIRE_FALSE( contextMenuEntry( *menu, NameThisValue )->isEnabled() );
            }
        }
    }

    GIVEN( "the Filtered View of a Search for the Log Line" )
    {
        CrawlerAccess::search( *shown.crawler, QStringLiteral( "EsimState" ) );
        auto& filteredView = CrawlerAccess::filteredView( *shown.crawler );
        filteredView.selectAndDisplayLine( 0_lnum );

        WHEN( "its menu opens on the value" )
        {
            const auto menu = ViewAccess::contextMenu(
                filteredView, ViewAccess::pointOn( filteredView, 0, EsimLine.indexOf( u'9' ) ) );

            THEN( "it offers the entry, enabled" )
            {
                REQUIRE( contextMenuEntry( *menu, NameThisValue ) != nullptr );
                REQUIRE( contextMenuEntry( *menu, NameThisValue )->isEnabled() );
            }
        }
    }
}

SCENARIO( "Name this value... names the value in the tab", "[ui][valuenames][namevalue]" )
{
    const StoredValueNamesGuard guard;
    StoredValueNamesGuard::storeGroups( {} );
    const NameValueWindow shown;
    auto& textView = CrawlerAccess::textView( *shown.crawler );
    REQUIRE_FALSE( textView.isValueNamesShownSet() );

    WHEN( "the value is named in the dialog the entry opens, and OK is clicked" )
    {
        textView.selectAndDisplayLine( 0_lnum );
        const auto menu = ViewAccess::contextMenu(
            textView, ViewAccess::pointOn( textView, 0, EsimLine.indexOf( u'9' ) ) );
        nameInDialog( *menu, []( NameValueDialog& dialog ) {
            setName( dialog, 0, QStringLiteral( "Ready" ) );
        } );

        THEN( "the rule and the table are in a new group, checked" )
        {
            const auto& groups = ValueNamesCollection::get().ownGroups();
            REQUIRE( groups.size() == 1 );
            REQUIRE( groups.front().isEnabled() );
            REQUIRE( groups.front().rules().size() == 1 );
            REQUIRE( groups.front().rules().front().pattern
                     == QStringLiteral( "EsimState=(\\d+)" ) );
            REQUIRE( groups.front().rules().front().tableFor( QStringLiteral( "1" ) )
                     == QStringLiteral( "EsimState" ) );
            const auto* table = groups.front().table( QStringLiteral( "EsimState" ) );
            REQUIRE( table != nullptr );
            REQUIRE(
                table->rows
                == QList<NameRow>{ NameRow{ QStringLiteral( "9" ), QStringLiteral( "Ready" ) } } );
        }

        THEN( "Show Value Names is on in the tab, and the Log Line shows the name at once" )
        {
            REQUIRE( shown.action( QStringLiteral( "Show &Value Names" ) )->isChecked() );
            REQUIRE( waitUiState( [ & ] { return textView.showsValueNames(); } ) );
            textView.selectAndDisplayLine( 0_lnum );
            REQUIRE( textView.getSelectedTextAsShown()
                     == QStringLiteral( "modem EsimState=Ready (9) done" ) );
        }
    }

    WHEN( "the dialog is cancelled" )
    {
        textView.selectAndDisplayLine( 0_lnum );
        const auto menu = ViewAccess::contextMenu(
            textView, ViewAccess::pointOn( textView, 0, EsimLine.indexOf( u'9' ) ) );
        QTimer::singleShot( 0, [] {
            auto* dialog = qobject_cast<NameValueDialog*>( QApplication::activeModalWidget() );
            REQUIRE( dialog != nullptr );
            dialog->reject();
        } );
        contextMenuEntry( *menu, NameThisValue )->trigger();

        THEN( "nothing is named, and Value Names stay off" )
        {
            REQUIRE( ValueNamesCollection::get().ownGroups().isEmpty() );
            REQUIRE_FALSE( shown.action( QStringLiteral( "Show &Value Names" ) )->isChecked() );
        }
    }
}

SCENARIO( "The Name this value dialog proposes everything from the Log Line, all editable",
          "[ui][valuenames][namevalue]" )
{
    const StoredValueNamesGuard guard;

    GIVEN( "no Naming Group yet, and the selection EsimState=9" )
    {
        StoredValueNamesGuard::storeGroups( {} );
        NameValueDialog dialog( EsimLine, rangeOf( EsimLine, QStringLiteral( "EsimState=9" ) ) );
        showUntilExposed( dialog );

        THEN( "it proposes the regex, a new table named after the text before the value, the "
              "template and a new group" )
        {
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "rulePattern" ) )->text()
                     == QStringLiteral( "EsimState=(\\d+)" ) );
            REQUIRE( childOf<QComboBox>( dialog, QStringLiteral( "nameTable" ) )->currentIndex()
                     == 0 );
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "newTableName" ) )->text()
                     == QStringLiteral( "EsimState" ) );
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "ruleTemplate" ) )->text()
                     == QStringLiteral( "{name} ({value})" ) );
            REQUIRE( childOf<QComboBox>( dialog, QStringLiteral( "namingGroup" ) )->currentText()
                     == QStringLiteral( "New Naming Group" ) );
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )->isEnabled() );
        }

        THEN( "the table's rows hold the value as a key waiting for its name" )
        {
            auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
            REQUIRE( rows->rowCount() == 1 );
            REQUIRE( rows->item( 0, 0 )->text() == QStringLiteral( "9" ) );
            REQUIRE( rows->item( 0, 1 )->text().isEmpty() );
            REQUIRE( rows->currentRow() == 0 );
            REQUIRE( rows->currentColumn() == 1 );
        }

        WHEN( "the value is given a name" )
        {
            setName( dialog, 0, QStringLiteral( "Ready" ) );

            THEN( "the preview shows the Log Line with it, without a warning" )
            {
                REQUIRE( dialog.shownPreview()
                         == QStringLiteral( "modem EsimState=Ready (9) done" ) );
                REQUIRE( dialog.previewWarning().isEmpty() );
            }

            AND_WHEN( "the template is edited" )
            {
                childOf<QLineEdit>( dialog, QStringLiteral( "ruleTemplate" ) )
                    ->setText( QStringLiteral( "<{name}>" ) );

                THEN( "the preview follows" )
                {
                    REQUIRE( dialog.shownPreview()
                             == QStringLiteral( "modem EsimState=<Ready> done" ) );
                }
            }

            AND_WHEN( "a row is added and filled in" )
            {
                QTest::mouseClick( childOf<QPushButton>( dialog, QStringLiteral( "addRow" ) ),
                                   Qt::LeftButton );
                auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
                REQUIRE( rows->rowCount() == 2 );
                rows->item( 1, 0 )->setText( QStringLiteral( "3" ) );
                rows->item( 1, 1 )->setText( QStringLiteral( "Off" ) );

                THEN( "the table OK creates has both rows" )
                {
                    REQUIRE( dialog.ruleWithTable().table.rows.size() == 2 );
                }
            }

            AND_WHEN( "the regex is changed to one that does not match the Log Line, and OK is "
                      "clicked" )
            {
                childOf<QLineEdit>( dialog, QStringLiteral( "rulePattern" ) )
                    ->setText( QStringLiteral( "EsimMode=(\\d+)" ) );
                REQUIRE_FALSE( dialog.previewWarning().isEmpty() );
                REQUIRE( dialog.shownPreview() == EsimLine );
                clickOk( dialog );

                THEN( "the rule is created all the same, in a new group" )
                {
                    const auto& groups = ValueNamesCollection::get().ownGroups();
                    REQUIRE( groups.size() == 1 );
                    REQUIRE( groups.front().name() == QStringLiteral( "Value Names" ) );
                    REQUIRE( groups.front().rules().front().pattern
                             == QStringLiteral( "EsimMode=(\\d+)" ) );
                }
            }
        }

        WHEN( "the regex is not valid" )
        {
            childOf<QLineEdit>( dialog, QStringLiteral( "rulePattern" ) )
                ->setText( QStringLiteral( "EsimState=(\\d+" ) );

            THEN( "the preview warns of it" )
            {
                REQUIRE( dialog.previewWarning().contains( QStringLiteral( "not valid" ) ) );
            }
        }
    }

    GIVEN( "a group with a Name Table of the name proposed" )
    {
        auto other = NamingGroup::createNewGroup( QStringLiteral( "Other" ) );
        auto modem = NamingGroup::createNewGroup( QStringLiteral( "Modem" ) );
        NameTable esim;
        esim.name = QStringLiteral( "EsimState" );
        esim.rows = { NameRow{ QStringLiteral( "1" ), QStringLiteral( "Idle" ) } };
        modem.setTables( { esim } );
        StoredValueNamesGuard::storeGroups( { other, modem } );

        NameValueDialog dialog( EsimLine, rangeOf( EsimLine, QStringLiteral( "9" ) ) );
        showUntilExposed( dialog );

        THEN( "that group and its table are chosen, its rows shown with the value after them" )
        {
            REQUIRE( childOf<QComboBox>( dialog, QStringLiteral( "namingGroup" ) )->currentText()
                     == QStringLiteral( "Modem" ) );
            REQUIRE( childOf<QComboBox>( dialog, QStringLiteral( "nameTable" ) )->currentText()
                     == QStringLiteral( "EsimState" ) );
            auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
            REQUIRE( rows->rowCount() == 2 );
            REQUIRE( rows->item( 0, 0 )->text() == QStringLiteral( "1" ) );
            REQUIRE( rows->item( 1, 0 )->text() == QStringLiteral( "9" ) );
        }

        WHEN( "the value is named and OK is clicked" )
        {
            setName( dialog, 1, QStringLiteral( "Ready" ) );
            clickOk( dialog );

            THEN( "the rule is added to that group, using its table, which has the new row" )
            {
                const auto& groups = ValueNamesCollection::get().ownGroups();
                REQUIRE( groups.size() == 2 );
                const auto& changed = groups[ 1 ];
                REQUIRE( changed.tables().size() == 1 );
                REQUIRE( changed.tables().front().rows.size() == 2 );
                REQUIRE( changed.rules().size() == 1 );
                REQUIRE( changed.rules().front().tableFor( QStringLiteral( "1" ) )
                         == QStringLiteral( "EsimState" ) );
                REQUIRE( groups[ 0 ].rules().isEmpty() );
            }
        }

        WHEN( "another group is chosen" )
        {
            childOf<QComboBox>( dialog, QStringLiteral( "namingGroup" ) )->setCurrentIndex( 0 );

            THEN( "a new table is proposed, as that group has none" )
            {
                REQUIRE( childOf<QComboBox>( dialog, QStringLiteral( "nameTable" ) )->currentIndex()
                         == 0 );
                REQUIRE(
                    childOf<QLineEdit>( dialog, QStringLiteral( "newTableName" ) )->isEnabled() );
                REQUIRE( childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) )->rowCount()
                         == 1 );
            }
        }
    }
}
