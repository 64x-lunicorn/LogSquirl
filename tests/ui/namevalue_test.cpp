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
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>

#include <functional>
#include <memory>
#include <utility>

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "configuration.h"
#include "context_menu_entries.h"
#include "crawler_children.h"
#include "crawlerwidget.h"
#include "groupchooser.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "modal_answers.h"
#include "namevaluedialog.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "shown_widget.h"
#include "teamfolder.h"
#include "test_policies.h"
#include "test_utils.h"
#include "valuenamer.h"
#include "valuenames_fixture.h"
#include "valuenamescollection.h"
#include "valuenamespanel.h"

// The Team Folder helpers of the unit tests, which run it against real Git
// repositories.
#include "../unit/teamfoldertesting.h"

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

namespace {

using ViewAccess = AbstractLogView::access_by<NameValueTest>;

// The child widgets of a tab, driven as a user does.
struct CrawlerAccess {
    static AbstractLogView& textView( const CrawlerWidget& crawler )
    {
        return textViewOf( crawler );
    }

    static AbstractLogView& filteredView( const CrawlerWidget& crawler )
    {
        return *frontFilteredViewOf( crawler );
    }

    static void search( CrawlerWidget& crawler, const QString& pattern )
    {
        auto& searchLine = searchLineOf( crawler );
        auto* edit = SearchLineAccess::patternEdit( searchLine );
        edit->clearEditText();
        QTest::keyClicks( edit, pattern );
        QTest::mouseClick( SearchLineAccess::searchButton( searchLine ), Qt::LeftButton );
        QTest::qWait( 100 );
        REQUIRE( waitUiState(
            [ & ] { return SearchLineAccess::stopButton( searchLine )->isHidden(); } ) );
    }
};

const auto NameThisValue = QStringLiteral( "Name this value..." );
const auto NewNamingGroup = QStringLiteral( "New Naming Group..." );
const auto NewTeamNamingGroup = QStringLiteral( "New Team Naming Group..." );
const auto EsimLine = QStringLiteral( "modem EsimState=9 done" );
// A raw Log Line with a tab before the key and one between the key and the
// value: drawn as "a       EsimState=      9 done".
const auto TabLine = QStringLiteral( "a\tEsimState=\t9 done" );

// Restores the collection, and what is stored of it, when it goes.
class StoredValueNamesGuard {
public:
    StoredValueNamesGuard()
        : before_( ValueNamesCollection::get().ownGroups() )
        , unchecked_( ValueNamesCollection::get().uncheckedKeys() )
        , lastGroupId_( Configuration::getSynced().lastNamingGroupId() )
    {
    }

    ~StoredValueNamesGuard()
    {
        storeGroups( before_ );
        ValueNamesCollection::get().setUncheckedKeys( unchecked_ );
        ValueNamesCollection::get().save();
        auto& config = Configuration::getSynced();
        config.setLastNamingGroupId( lastGroupId_ );
        config.save();
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
    QSet<QString> unchecked_;
    QString lastGroupId_;
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

// Chooses the entry of the dialog's Group Chooser with the text.
void chooseGroup( NameValueDialog& dialog, const QString& text )
{
    auto* combo = dialog.groupChooser()->comboBox();
    const auto row = combo->findText( text );
    REQUIRE( row >= 0 );
    combo->setCurrentIndex( row );
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
    // With a Team Folder, the Session sets it up as the policy says.
    explicit NameValueWindow( const QString& text
                              = EsimLine + QStringLiteral( "\nother line\n" ) + TabLine + u'\n',
                              std::shared_ptr<TeamFolder> teamFolder = {},
                              const TeamFolderPolicy& teamPolicy = {} )
    {
        REQUIRE( dir.isValid() );
        const auto path = dir.filePath( QStringLiteral( "esim.log" ) );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( text.toUtf8() );
        file.close();

        auto policies = testSettingsPolicies();
        policies.presentation.showValueNames = false;
        policies.teamFolder = teamPolicy;
        session = std::make_shared<Session>( policies, std::make_shared<LogFormatCatalog>(),
                                             nullptr, std::move( teamFolder ) );
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
    std::shared_ptr<Session> session;
    std::unique_ptr<MainWindow> window;
    CrawlerWidget* crawler = nullptr;
};

// Chooses Name this value..., or the entry of that text, of the menu, and in
// the dialog it opens does what fill does, then clicks OK.
void nameInDialog( QMenu& menu, const std::function<void( NameValueDialog& )>& fill,
                   const QString& entryText = NameThisValue )
{
    auto* entry = contextMenuEntry( menu, entryText );
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

        WHEN( "text that is not a number is selected" )
        {
            ViewAccess::selectPortion( textView, 0_lnum, 0_lcol, 4_lcol );
            const auto menu
                = ViewAccess::contextMenu( textView, ViewAccess::pointOn( textView, 0, 0 ) );

            THEN( "the entry is enabled, for a text value (#957)" )
            {
                REQUIRE( contextMenuEntry( *menu, NameThisValue )->isEnabled() );
            }
        }

        WHEN( "only a blank is selected" )
        {
            const auto blank = EsimLine.indexOf( u' ' );
            ViewAccess::selectPortion( textView, 0_lnum, LineColumn{ blank }, LineColumn{ blank } );
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

        WHEN( "nothing is selected within a Log Line, and the menu opens on a word that is not a "
              "number" )
        {
            textView.selectAndDisplayLine( 1_lnum );
            const auto menu
                = ViewAccess::contextMenu( textView, ViewAccess::pointOn( textView, 1, 1 ) );

            THEN( "the entry is enabled, for a text value (#957)" )
            {
                REQUIRE( contextMenuEntry( *menu, NameThisValue )->isEnabled() );
            }
        }

        WHEN( "nothing is selected within a Log Line, and the menu opens on a blank" )
        {
            textView.selectAndDisplayLine( 1_lnum );
            const auto menu = ViewAccess::contextMenu(
                textView, ViewAccess::pointOn( textView, 1,
                                               QStringLiteral( "other line" ).indexOf( u' ' ) ) );

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

SCENARIO( "Name this value... names a text value in the tab", "[ui][valuenames][namevalue]" )
{
    const StoredValueNamesGuard guard;
    StoredValueNamesGuard::storeGroups( {} );
    const auto userLine = QStringLiteral( "12:00 user=alice logged in" );
    const auto uuid = QStringLiteral( "84f1fc54-cd08-51c3-af5b-3687900e5e6a" );
    const auto requestLine = QStringLiteral( "request " ) + uuid + QStringLiteral( " done" );
    const NameValueWindow shown( userLine + u'\n' + requestLine + u'\n' );
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    // What the dialog the entry opens proposes.
    QString pattern;
    QString key;
    QString warning;
    const auto openAndName = [ & ]( QMenu& menu, const QString& name ) {
        nameInDialog( menu, [ & ]( NameValueDialog& dialog ) {
            pattern = childOf<QLineEdit>( dialog, QStringLiteral( "rulePattern" ) )->text();
            key = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) )
                      ->item( 0, 0 )
                      ->text();
            setName( dialog, 0, name );
            warning = dialog.previewWarning();
        } );
    };

    WHEN( "the menu opens on a word, nothing selected, and it is named" )
    {
        textView.selectAndDisplayLine( 0_lnum );
        const auto menu = ViewAccess::contextMenu(
            textView, ViewAccess::pointOn( textView, 0, userLine.indexOf( u'l' ) + 1 ) );
        openAndName( *menu, QStringLiteral( "Alice Admin" ) );

        THEN( "the rule captures the word, and the Log Line shows its name in the Template" )
        {
            REQUIRE( pattern == QStringLiteral( "user=([^ ]+)" ) );
            REQUIRE( key == QStringLiteral( "alice" ) );
            REQUIRE( warning.isEmpty() );
            REQUIRE( waitUiState( [ & ] { return textView.showsValueNames(); } ) );
            textView.selectAndDisplayLine( 0_lnum );
            REQUIRE( textView.getSelectedTextAsShown()
                     == QStringLiteral( "12:00 user=Alice Admin (alice) logged in" ) );
        }
    }

    WHEN( "the menu opens on a part of a UUID, nothing selected, and it is named" )
    {
        textView.selectAndDisplayLine( 1_lnum );
        const auto menu = ViewAccess::contextMenu(
            textView,
            ViewAccess::pointOn( textView, 1, requestLine.indexOf( QStringLiteral( "cd08" ) ) ) );
        openAndName( *menu, QStringLiteral( "Checkout" ) );

        THEN( "the whole UUID is the value, and the Log Line shows its name" )
        {
            REQUIRE( key == uuid );
            REQUIRE( warning.isEmpty() );
            REQUIRE( waitUiState( [ & ] { return textView.showsValueNames(); } ) );
            textView.selectAndDisplayLine( 1_lnum );
            REQUIRE( textView.getSelectedTextAsShown()
                     == QStringLiteral( "request Checkout (" ) + uuid
                            + QStringLiteral( ") done" ) );
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
            REQUIRE( dialog.groupChooser()->choice().isNew() );
            REQUIRE( dialog.groupChooser()->comboBox()->currentText() == NewNamingGroup );
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )->isVisible() );
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )->text()
                     == QStringLiteral( "Value Names" ) );
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
            REQUIRE( dialog.groupChooser()->comboBox()->currentText()
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
            chooseGroup( dialog, QStringLiteral( "Other" ) );

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

SCENARIO( "Name this value... proposes a rule that matches a clicked Log Line with tabs",
          "[ui][valuenames][namevalue]" )
{
    const StoredValueNamesGuard guard;
    StoredValueNamesGuard::storeGroups( {} );
    const NameValueWindow shown;
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    // Drawn with the tabs expanded: "a" and a tab take columns 0 to 7,
    // "EsimState=" 8 to 17, the second tab 18 to 23, and the 9 column 24.
    constexpr qsizetype KeyColumn = 8;
    constexpr qsizetype ValueColumn = 24;

    // What the dialog the entry opens proposes, and the Log Line it names.
    QString pattern;
    QString warning;
    QString key;
    const auto openAndName = [ & ]( QMenu& menu ) {
        nameInDialog( menu, [ & ]( NameValueDialog& dialog ) {
            pattern = childOf<QLineEdit>( dialog, QStringLiteral( "rulePattern" ) )->text();
            warning = dialog.previewWarning();
            key = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) )
                      ->item( 0, 0 )
                      ->text();
            setName( dialog, 0, QStringLiteral( "Ready" ) );
        } );
    };

    WHEN( "the menu opens on the value, with nothing selected within the Log Line" )
    {
        textView.selectAndDisplayLine( 2_lnum );
        const auto menu
            = ViewAccess::contextMenu( textView, ViewAccess::pointOn( textView, 2, ValueColumn ) );
        openAndName( *menu );

        THEN( "the regex is made from the raw Log Line and matches it, without a warning" )
        {
            REQUIRE( pattern == QStringLiteral( "EsimState=\\t(\\d+)" ) );
            const auto match = QRegularExpression( pattern ).match( TabLine );
            REQUIRE( match.hasMatch() );
            REQUIRE( match.captured( 1 ) == QStringLiteral( "9" ) );
            REQUIRE( warning.isEmpty() );
            REQUIRE( key == QStringLiteral( "9" ) );
        }

        THEN( "the Log Line shows the name" )
        {
            REQUIRE( waitUiState( [ & ] { return textView.showsValueNames(); } ) );
            textView.selectAndDisplayLine( 2_lnum );
            REQUIRE( textView.getSelectedTextAsShown()
                     == QStringLiteral( "a\tEsimState=\tReady (9) done" ) );
        }
    }

    WHEN( "the key, the tab and the value are selected, as drawn" )
    {
        ViewAccess::selectPortion( textView, 2_lnum, LineColumn{ KeyColumn },
                                   LineColumn{ ValueColumn } );
        const auto menu
            = ViewAccess::contextMenu( textView, ViewAccess::pointOn( textView, 2, 0 ) );
        openAndName( *menu );

        THEN( "the selection is taken back to the raw Log Line, and the regex matches it" )
        {
            REQUIRE( pattern == QStringLiteral( "EsimState=\\t(\\d+)" ) );
            REQUIRE( QRegularExpression( pattern ).match( TabLine ).hasMatch() );
            REQUIRE( warning.isEmpty() );
        }
    }
}

SCENARIO( "Name this value... in a Filtered View names the Log Line of the Match, not the one "
          "at its row in the file",
          "[ui][valuenames][namevalue]" )
{
    const StoredValueNamesGuard guard;
    StoredValueNamesGuard::storeGroups( {} );
    // The Search matches Log Line 2 alone; every other Log Line holds a value
    // too, so naming from the wrong one would propose something else.
    const NameValueWindow shown( QStringLiteral( "alpha=1 x\nbeta=2 x\n" ) + EsimLine
                                 + QStringLiteral( "\ngamma=4 x\ndelta=5 x\n" ) );
    CrawlerAccess::search( *shown.crawler, QStringLiteral( "EsimState" ) );
    auto& filteredView = CrawlerAccess::filteredView( *shown.crawler );

    // What the dialog the entry opens proposes, and its preview once named.
    QString pattern;
    QString key;
    QString preview;
    const auto openAndName = [ & ]( QMenu& menu ) {
        nameInDialog( menu, [ & ]( NameValueDialog& dialog ) {
            pattern = childOf<QLineEdit>( dialog, QStringLiteral( "rulePattern" ) )->text();
            key = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) )
                      ->item( 0, 0 )
                      ->text();
            setName( dialog, 0, QStringLiteral( "Ready" ) );
            preview = dialog.shownPreview();
        } );
    };

    WHEN( "the menu opens on the value of the Match, its first row, with nothing selected "
          "within it" )
    {
        filteredView.selectAndDisplayLine( 2_lnum );
        const auto menu = ViewAccess::contextMenu(
            filteredView, ViewAccess::pointOn( filteredView, 0, EsimLine.indexOf( u'9' ) ) );
        openAndName( *menu );

        THEN( "the dialog proposes from that Log Line" )
        {
            REQUIRE( pattern == QStringLiteral( "EsimState=(\\d+)" ) );
            REQUIRE( key == QStringLiteral( "9" ) );
            REQUIRE( preview == QStringLiteral( "modem EsimState=Ready (9) done" ) );
        }
    }

    WHEN( "the value of the Match is selected" )
    {
        const auto range = rangeOf( EsimLine, QStringLiteral( "EsimState=9" ) );
        ViewAccess::selectPortion( filteredView, 2_lnum, LineColumn{ range.start },
                                   LineColumn{ range.end() - 1 } );
        const auto menu
            = ViewAccess::contextMenu( filteredView, ViewAccess::pointOn( filteredView, 0, 0 ) );
        openAndName( *menu );

        THEN( "the dialog proposes from that Log Line" )
        {
            REQUIRE( pattern == QStringLiteral( "EsimState=(\\d+)" ) );
            REQUIRE( key == QStringLiteral( "9" ) );
            REQUIRE( preview == QStringLiteral( "modem EsimState=Ready (9) done" ) );
        }
    }
}

SCENARIO( "Name this value... names a new table Name Table N when no text comes before the value",
          "[ui][valuenames][namevalue]" )
{
    const StoredValueNamesGuard guard;
    const auto line = QStringLiteral( "9 done" );

    GIVEN( "no Naming Group yet" )
    {
        StoredValueNamesGuard::storeGroups( {} );
        NameValueDialog dialog( line, rangeOf( line, QStringLiteral( "9" ) ) );
        showUntilExposed( dialog );

        THEN( "the new table is Name Table 1, as the Value Names dialog names a new one" )
        {
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "newTableName" ) )->text()
                     == QStringLiteral( "Name Table 1" ) );
        }

        WHEN( "the value is named and OK is clicked" )
        {
            setName( dialog, 0, QStringLiteral( "Ready" ) );
            clickOk( dialog );

            THEN( "the table and the rule are Name Table 1" )
            {
                const auto& groups = ValueNamesCollection::get().ownGroups();
                REQUIRE( groups.size() == 1 );
                REQUIRE( groups.front().table( QStringLiteral( "Name Table 1" ) ) != nullptr );
                REQUIRE( groups.front().rules().front().tableFor( QStringLiteral( "1" ) )
                         == QStringLiteral( "Name Table 1" ) );
            }
        }
    }

    GIVEN( "a group that has a Name Table 1" )
    {
        auto modem = NamingGroup::createNewGroup( QStringLiteral( "Modem" ) );
        NameTable first;
        first.name = QStringLiteral( "Name Table 1" );
        modem.setTables( { first } );
        StoredValueNamesGuard::storeGroups( { modem } );
        NameValueDialog dialog( line, rangeOf( line, QStringLiteral( "9" ) ) );
        showUntilExposed( dialog );

        THEN( "the new table is Name Table 2" )
        {
            REQUIRE( dialog.groupChooser()->comboBox()->currentText()
                     == QStringLiteral( "Modem" ) );
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "newTableName" ) )->text()
                     == QStringLiteral( "Name Table 2" ) );
        }

        WHEN( "a new group is chosen" )
        {
            chooseGroup( dialog, NewNamingGroup );

            THEN( "the new table is Name Table 1 again" )
            {
                REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "newTableName" ) )->text()
                         == QStringLiteral( "Name Table 1" ) );
            }
        }
    }
}

namespace {

// The Log Lines of a Naming Rule's values: 4 is not named, 9 is Ready, and
// EsimMode no rule captures.
const auto CapturedLines = QStringLiteral( "modem EsimState=4 done\n"
                                           "modem EsimState=9 done\n"
                                           "modem EsimMode=4 done\n" );

// The group whose rule captures EsimState's values, its table naming 9.
NamingGroup esimGroup()
{
    auto modem = NamingGroup::createNewGroup( QStringLiteral( "Modem" ) );
    valuenamesfixture::NamingRule rule;
    rule.name = QStringLiteral( "EsimState" );
    rule.pattern = QStringLiteral( "EsimState=(\\d+)" );
    rule.groupTables
        = { valuenamesfixture::GroupTable{ QStringLiteral( "1" ), QStringLiteral( "EsimState" ) } };
    rule.displayTemplate = QStringLiteral( "{name} ({value})" );
    modem.setRules( { rule } );
    NameTable esim;
    esim.name = QStringLiteral( "EsimState" );
    esim.rows = { NameRow{ QStringLiteral( "9" ), QStringLiteral( "Ready" ) } };
    modem.setTables( { esim } );
    return modem;
}

// The context menu of the Text View opened on the first character of text in
// the Log Line, nothing selected within it.
std::unique_ptr<QMenu> menuOn( AbstractLogView& view, LineNumber line, const QString& text )
{
    const auto lines = CapturedLines.split( u'\n' );
    const auto column = lines[ static_cast<int>( line.get() ) ].indexOf( text );
    REQUIRE( column >= 0 );
    view.selectAndDisplayLine( line );
    return ViewAccess::contextMenu(
        view, ViewAccess::pointOn( view, static_cast<int>( line.get() ), column ) );
}

} // namespace

SCENARIO( "A further value of an existing Naming Rule is named from its Log Line",
          "[ui][valuenames][namevalue][capturedvalue]" )
{
    const StoredValueNamesGuard guard;
    const auto modem = esimGroup();
    StoredValueNamesGuard::storeGroups(
        { NamingGroup::createNewGroup( QStringLiteral( "Other" ) ), modem } );
    const NameValueWindow shown( CapturedLines );
    auto& textView = CrawlerAccess::textView( *shown.crawler );
    const auto NameFour = QStringLiteral( "Name 4 in EsimState..." );
    const auto RenameNine = QStringLiteral( "Rename EsimState 9..." );

    WHEN( "the menu opens on a value the rule captures and its table does not name" )
    {
        const auto menu = menuOn( textView, 0_lnum, QStringLiteral( "4" ) );

        THEN( "it offers to name it in that table, instead of Name this value..." )
        {
            REQUIRE( contextMenuEntry( *menu, NameFour ) != nullptr );
            REQUIRE( contextMenuEntry( *menu, NameFour )->isEnabled() );
            REQUIRE( contextMenuEntry( *menu, NameThisValue ) == nullptr );
            REQUIRE( entriesOf( *menu ).size() <= MaxTopLevelEntries );
        }

        AND_WHEN( "the value is named in the dialog the entry opens, and OK is clicked" )
        {
            bool fixed = false;
            int valueRow = -1;
            nameInDialog(
                *menu,
                [ & ]( NameValueDialog& dialog ) {
                    fixed = !dialog.groupChooser()->isEnabled()
                            && dialog.groupChooser()->comboBox()->currentText()
                                   == QStringLiteral( "Modem" )
                            && childOf<QComboBox>( dialog, QStringLiteral( "nameTable" ) )
                                       ->currentText()
                                   == QStringLiteral( "EsimState" )
                            && !childOf<QComboBox>( dialog, QStringLiteral( "nameTable" ) )
                                    ->isEnabled()
                            && childOf<QLineEdit>( dialog, QStringLiteral( "rulePattern" ) )
                                   ->isReadOnly();
                    auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
                    REQUIRE( rows->rowCount() == 2 );
                    REQUIRE( rows->item( 1, 0 )->text() == QStringLiteral( "4" ) );
                    valueRow = rows->currentRow();
                    setName( dialog, 1, QStringLiteral( "Off" ) );
                },
                NameFour );

            THEN( "the dialog showed the rule and its table, the value a new key" )
            {
                REQUIRE( fixed );
                REQUIRE( valueRow == 1 );
            }

            THEN( "one row is added to that table, and no rule or table is created" )
            {
                const auto& groups = ValueNamesCollection::get().ownGroups();
                REQUIRE( groups.size() == 2 );
                REQUIRE( groups[ 0 ].rules().isEmpty() );
                REQUIRE( groups[ 0 ].tables().isEmpty() );
                REQUIRE( groups[ 1 ].rules() == modem.rules() );
                REQUIRE( groups[ 1 ].tables().size() == 1 );
                REQUIRE( groups[ 1 ].tables().front().rows
                         == QList<NameRow>{
                             NameRow{ QStringLiteral( "9" ), QStringLiteral( "Ready" ) },
                             NameRow{ QStringLiteral( "4" ), QStringLiteral( "Off" ) } } );
            }

            THEN( "the Log Line shows the name at once" )
            {
                REQUIRE( waitUiState( [ & ] { return textView.showsValueNames(); } ) );
                textView.selectAndDisplayLine( 0_lnum );
                REQUIRE( textView.getSelectedTextAsShown()
                         == QStringLiteral( "modem EsimState=Off (4) done" ) );
            }
        }
    }

    WHEN( "the menu opens on a Named Value" )
    {
        const auto menu = menuOn( textView, 1_lnum, QStringLiteral( "9" ) );

        THEN( "it offers to rename it" )
        {
            REQUIRE( contextMenuEntry( *menu, RenameNine ) != nullptr );
            REQUIRE( contextMenuEntry( *menu, RenameNine )->isEnabled() );
            REQUIRE( contextMenuEntry( *menu, NameThisValue ) == nullptr );
        }

        AND_WHEN( "the name is changed in the dialog the entry opens, and OK is clicked" )
        {
            int rowCount = 0;
            int valueRow = -1;
            nameInDialog(
                *menu,
                [ & ]( NameValueDialog& dialog ) {
                    auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
                    rowCount = rows->rowCount();
                    valueRow = rows->currentRow();
                    setName( dialog, 0, QStringLiteral( "Go" ) );
                },
                RenameNine );

            THEN( "the dialog had that row selected, and OK changed its name" )
            {
                REQUIRE( rowCount == 1 );
                REQUIRE( valueRow == 0 );
                const auto& groups = ValueNamesCollection::get().ownGroups();
                REQUIRE( groups.size() == 2 );
                REQUIRE( groups[ 1 ].rules() == modem.rules() );
                REQUIRE(
                    groups[ 1 ].tables().front().rows
                    == QList<NameRow>{ NameRow{ QStringLiteral( "9" ), QStringLiteral( "Go" ) } } );
            }
        }
    }

    WHEN( "the row naming the value moved after the menu looked it up" )
    {
        const auto line = QStringLiteral( "modem EsimState=9 done" );
        const auto range = rangeOf( line, QStringLiteral( "9" ) );
        const auto captured = logsquirl::valuenames::capturedValueIn(
            ValueNamesCollection::get().groups(), line, range.start, range.length );
        REQUIRE( captured.has_value() );
        REQUIRE( captured->row == 0 );
        auto moved = modem;
        auto tables = moved.tables();
        tables.front().rows.prepend( NameRow{ QStringLiteral( "1" ), QStringLiteral( "Idle" ) } );
        moved.setTables( tables );
        StoredValueNamesGuard::storeGroups(
            { NamingGroup::createNewGroup( QStringLiteral( "Other" ) ), moved } );

        NameValueDialog dialog( line, *captured );

        THEN( "the dialog selects the row as the groups are now" )
        {
            auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
            REQUIRE( rows->rowCount() == 2 );
            REQUIRE( rows->currentRow() == 1 );
        }
    }

    WHEN( "the menu opens on the Named Value selected" )
    {
        const auto range = rangeOf( QStringLiteral( "modem EsimState=9 done" ),
                                    QStringLiteral( "EsimState=9" ) );
        ViewAccess::selectPortion( textView, 1_lnum, LineColumn{ range.start },
                                   LineColumn{ range.end() - 1 } );
        const auto menu
            = ViewAccess::contextMenu( textView, ViewAccess::pointOn( textView, 1, 0 ) );

        THEN( "it offers to rename it too" )
        {
            REQUIRE( contextMenuEntry( *menu, RenameNine ) != nullptr );
        }
    }

    WHEN( "the menu opens on a value no rule captures" )
    {
        const auto menu = menuOn( textView, 2_lnum, QStringLiteral( "4" ) );

        THEN( "it still offers Name this value..." )
        {
            REQUIRE( contextMenuEntry( *menu, NameThisValue ) != nullptr );
            REQUIRE( contextMenuEntry( *menu, NameThisValue )->isEnabled() );
            REQUIRE( contextMenuEntry( *menu, NameFour ) == nullptr );
        }
    }

    WHEN( "the rule is unchecked" )
    {
        auto groups = ValueNamesCollection::get().ownGroups();
        auto rules = groups[ 1 ].rules();
        rules[ 0 ].enabled = false;
        groups[ 1 ].setRules( rules );
        ValueNamesCollection::get().setGroups( groups );
        const auto menu = menuOn( textView, 0_lnum, QStringLiteral( "4" ) );

        THEN( "the value is one no rule captures" )
        {
            REQUIRE( contextMenuEntry( *menu, NameThisValue ) != nullptr );
        }
    }
}

SCENARIO( "A value a Team group's Naming Rule captures is not named from the context menu",
          "[ui][valuenames][namevalue][capturedvalue]" )
{
    const StoredValueNamesGuard guard;
    StoredValueNamesGuard::storeGroups( {} );
    const NameValueWindow shown( CapturedLines );
    ValueNamesCollection::get().setTeamGroups( { esimGroup() } );
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    WHEN( "the menu opens on a value the rule captures, or on a Named Value" )
    {
        const auto four = menuOn( textView, 0_lnum, QStringLiteral( "4" ) );
        const auto nine = menuOn( textView, 1_lnum, QStringLiteral( "9" ) );

        THEN( "the entry says the group is read-only, and is disabled" )
        {
            const auto* nameFour = contextMenuEntry(
                *four, QStringLiteral( "Name 4 in EsimState (Team group, read-only)" ) );
            REQUIRE( nameFour != nullptr );
            REQUIRE_FALSE( nameFour->isEnabled() );
            REQUIRE( contextMenuEntry( *four, NameThisValue ) == nullptr );

            const auto* renameNine = contextMenuEntry(
                *nine, QStringLiteral( "Rename EsimState 9 (Team group, read-only)" ) );
            REQUIRE( renameNine != nullptr );
            REQUIRE_FALSE( renameNine->isEnabled() );
        }
    }
}

namespace {

QPushButton* okButton( QDialog& dialog )
{
    return dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok );
}

// The Team Naming Group "Modem", its Name Table EsimState naming 1.
NamingGroup teamModem()
{
    auto modem = NamingGroup::createNewGroup( QStringLiteral( "Modem" ) );
    NameTable esim;
    esim.name = QStringLiteral( "EsimState" );
    esim.rows = { NameRow{ QStringLiteral( "1" ), QStringLiteral( "Idle" ) } };
    modem.setTables( { esim } );
    return modem;
}

} // namespace

SCENARIO( "Name This Value chooses its Naming Group with the Group Chooser",
          "[ui][valuenames][namevalue][groupchooser]" )
{
    const StoredValueNamesGuard guard;
    const auto other = NamingGroup::createNewGroup( QStringLiteral( "Other" ) );
    StoredValueNamesGuard::storeGroups( { other } );

    NameValueDialog dialog( EsimLine, rangeOf( EsimLine, QStringLiteral( "9" ) ) );
    showUntilExposed( dialog );
    auto* combo = dialog.groupChooser()->comboBox();
    auto* newName = childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) );

    THEN( "the user's own groups are offered under their heading, the first chosen, with New "
          "Naming Group...; the name field is hidden; without a Team Folder no Team section" )
    {
        REQUIRE( combo->findText( QStringLiteral( "My Naming Groups" ) ) >= 0 );
        REQUIRE( combo->currentText() == QStringLiteral( "Other" ) );
        REQUIRE( dialog.chosenGroupId() == other.id() );
        REQUIRE( combo->findText( NewNamingGroup ) >= 0 );
        REQUIRE( newName->isHidden() );
        REQUIRE_FALSE( dialog.groupChooser()->hasTeamSection() );
        REQUIRE( combo->findText( QStringLiteral( "Team Naming Groups" ) ) < 0 );
        REQUIRE( combo->findText( NewTeamNamingGroup ) < 0 );
        REQUIRE( okButton( dialog )->isEnabled() );
    }

    WHEN( "New Naming Group... is chosen" )
    {
        chooseGroup( dialog, NewNamingGroup );
        setName( dialog, 0, QStringLiteral( "Ready" ) );

        THEN( "the name field shows, with the first free name" )
        {
            REQUIRE( newName->isVisible() );
            REQUIRE( newName->text() == QStringLiteral( "Value Names" ) );
            REQUIRE( okButton( dialog )->isEnabled() );
        }

        AND_WHEN( "the group is named, which keeps the rows as edited, and OK is clicked" )
        {
            newName->setText( QStringLiteral( "Modem" ) );
            auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
            REQUIRE( rows->item( 0, 1 )->text() == QStringLiteral( "Ready" ) );
            clickOk( dialog );

            THEN( "the group is made with the rule and the table, after the user's other group" )
            {
                const auto& groups = ValueNamesCollection::get().ownGroups();
                REQUIRE( groups.size() == 2 );
                REQUIRE( groups[ 0 ].id() == other.id() );
                REQUIRE( groups[ 0 ].rules().isEmpty() );
                REQUIRE( groups[ 1 ].name() == QStringLiteral( "Modem" ) );
                REQUIRE( groups[ 1 ].rules().size() == 1 );
                REQUIRE( groups[ 1 ].table( QStringLiteral( "EsimState" ) ) != nullptr );
                REQUIRE( dialog.chosenGroupId() == groups[ 1 ].id() );
            }
        }

        AND_WHEN( "the name field is emptied" )
        {
            newName->setText( QString{} );

            THEN( "OK is disabled, and the dialog says why" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
                REQUIRE( childOf<QLabel>( dialog, QStringLiteral( "previewWarning" ) )->text()
                         == QStringLiteral( "The new Naming Group has no name." ) );
            }
        }

        AND_WHEN( "it is given the name of a group of the user's own" )
        {
            newName->setText( QStringLiteral( "Other" ) );

            THEN( "OK is disabled, and the dialog says why" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
                REQUIRE( childOf<QLabel>( dialog, QStringLiteral( "previewWarning" ) )
                             ->text()
                             .contains( QStringLiteral( "Other" ) ) );
            }
        }
    }

    WHEN( "a group that is gone is proposed, as the one chosen last" )
    {
        dialog.proposeGroup( QStringLiteral( "{gone}" ) );

        THEN( "the first of the user's own is chosen" )
        {
            REQUIRE( dialog.chosenGroupId() == other.id() );
        }
    }
}

SCENARIO( "Name This Value names a value in a Team Naming Group, which it publishes",
          "[ui][valuenames][namevalue][groupchooser]" )
{
    using logsquirl::teamfolder::GroupAction;
    using logsquirl::teamfolder::PublishRequest;

    const StoredValueNamesGuard guard;
    const auto other = NamingGroup::createNewGroup( QStringLiteral( "Other" ) );
    StoredValueNamesGuard::storeGroups( { other } );
    const auto modem = teamModem();
    const auto spare = NamingGroup::createNewGroup( QStringLiteral( "Spare" ) );
    const QHash<QString, QString> revisions{ { modem.id(), QStringLiteral( "rev-1" ) },
                                             { spare.id(), QStringLiteral( "rev-2" ) } };

    // A line no table of any group is named after: the remembered group is
    // the one chosen.
    const auto line = QStringLiteral( "modem Signal=9 done" );
    NameValueDialog dialog( line, rangeOf( line, QStringLiteral( "9" ) ) );
    QSignalSpy published( &dialog, &NameValueDialog::publishRequested );

    GIVEN( "Team Naming Groups that can be changed" )
    {
        dialog.showTeamGroups( { modem, spare }, true, revisions );
        showUntilExposed( dialog );
        auto* combo = dialog.groupChooser()->comboBox();
        auto* hint = childOf<QLabel>( dialog, QStringLiteral( "teamHint" ) );
        auto* tables = childOf<QComboBox>( dialog, QStringLiteral( "nameTable" ) );

        THEN( "they are offered under a heading of their own, with New Team Naming Group..., "
              "and the user's own group is still chosen" )
        {
            REQUIRE( combo->findText( QStringLiteral( "Team Naming Groups" ) ) >= 0 );
            REQUIRE( combo->findText( QStringLiteral( "Modem" ) ) >= 0 );
            REQUIRE( combo->findText( NewTeamNamingGroup ) >= 0 );
            REQUIRE( dialog.chosenGroupId() == other.id() );
            REQUIRE( hint->isHidden() );
            REQUIRE( tables->count() == 1 );
        }

        WHEN( "a Team group is chosen" )
        {
            chooseGroup( dialog, QStringLiteral( "Modem" ) );

            THEN( "the hint shows, and the Name Table choice offers that group's tables" )
            {
                REQUIRE( hint->isVisible() );
                REQUIRE( tables->count() == 2 );
                REQUIRE( tables->findText( QStringLiteral( "EsimState" ) ) >= 0 );
            }

            AND_WHEN( "the value is named in its table, and OK is clicked" )
            {
                tables->setCurrentIndex( tables->findText( QStringLiteral( "EsimState" ) ) );
                auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
                REQUIRE( rows->rowCount() == 2 );
                setName( dialog, 1, QStringLiteral( "Ready" ) );
                clickOk( dialog );

                THEN( "the group with the rule is published, based on the revision it was "
                      "given with, and none of the user's own changed" )
                {
                    REQUIRE( published.count() == 1 );
                    const auto requests = published.at( 0 ).at( 0 ).value<QList<PublishRequest>>();
                    REQUIRE( requests.size() == 1 );
                    REQUIRE( requests[ 0 ].action == GroupAction::Change );
                    REQUIRE( requests[ 0 ].id == modem.id() );
                    REQUIRE( requests[ 0 ].baseRevision == QStringLiteral( "rev-1" ) );
                    const auto* group
                        = logsquirl::teamfolder::groupOfKind<NamingGroup>( requests[ 0 ].group );
                    REQUIRE( group != nullptr );
                    REQUIRE( group->rules().size() == 1 );
                    REQUIRE( group->tables().size() == 1 );
                    REQUIRE( group->tables().front().rows
                             == QList<NameRow>{
                                 NameRow{ QStringLiteral( "1" ), QStringLiteral( "Idle" ) },
                                 NameRow{ QStringLiteral( "9" ), QStringLiteral( "Ready" ) } } );
                    REQUIRE( dialog.chosenGroupId() == modem.id() );
                    const auto& own = ValueNamesCollection::get().ownGroups();
                    REQUIRE( own.size() == 1 );
                    REQUIRE( own.front().rules().isEmpty() );
                }
            }
        }

        WHEN( "the Team group was unchecked, and the value is named in it" )
        {
            auto& collection = ValueNamesCollection::get();
            collection.setUncheckedKeys( { ValueNamesCollection::groupCheckKey( spare.id() ) } );
            chooseGroup( dialog, QStringLiteral( "Spare" ) );
            setName( dialog, 0, QStringLiteral( "Ready" ) );
            clickOk( dialog );

            THEN( "it is checked again, so that what is named shows" )
            {
                REQUIRE_FALSE( collection.uncheckedKeys().contains(
                    ValueNamesCollection::groupCheckKey( spare.id() ) ) );
            }
        }

        WHEN( "New Team Naming Group... is chosen, named, and OK clicked" )
        {
            chooseGroup( dialog, NewTeamNamingGroup );
            REQUIRE( hint->isVisible() );
            childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )
                ->setText( QStringLiteral( "Signals" ) );
            setName( dialog, 0, QStringLiteral( "Strong" ) );
            clickOk( dialog );

            THEN( "the new group with the rule and the table is published as a new Team group" )
            {
                REQUIRE( published.count() == 1 );
                const auto requests = published.at( 0 ).at( 0 ).value<QList<PublishRequest>>();
                REQUIRE( requests.size() == 1 );
                REQUIRE( requests[ 0 ].action == GroupAction::Add );
                REQUIRE( requests[ 0 ].name == QStringLiteral( "Signals" ) );
                const auto* group
                    = logsquirl::teamfolder::groupOfKind<NamingGroup>( requests[ 0 ].group );
                REQUIRE( group != nullptr );
                REQUIRE( group->rules().size() == 1 );
                REQUIRE( group->table( QStringLiteral( "Signal" ) ) != nullptr );
                REQUIRE( dialog.chosenGroupId() == requests[ 0 ].id );
                REQUIRE( ValueNamesCollection::get().ownGroups().size() == 1 );
            }
        }

        WHEN( "New Team Naming Group... is given a Team group's name" )
        {
            chooseGroup( dialog, NewTeamNamingGroup );
            childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )
                ->setText( QStringLiteral( "Modem" ) );

            THEN( "OK is disabled" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
            }
        }

        WHEN( "the user's own group is kept and OK clicked" )
        {
            setName( dialog, 0, QStringLiteral( "Ready" ) );
            clickOk( dialog );

            THEN( "nothing is published" )
            {
                REQUIRE( published.isEmpty() );
                REQUIRE( ValueNamesCollection::get().ownGroups().front().rules().size() == 1 );
            }
        }

        WHEN( "the Team group is proposed, as the group chosen last" )
        {
            dialog.proposeGroup( modem.id() );

            THEN( "it is chosen" )
            {
                REQUIRE( dialog.chosenGroupId() == modem.id() );
            }
        }
    }

    GIVEN( "Team Naming Groups that cannot be changed" )
    {
        dialog.showTeamGroups( { modem }, false, revisions );
        dialog.proposeGroup( modem.id() );

        THEN( "none of them can be chosen, and there is no New Team Naming Group..." )
        {
            REQUIRE( dialog.chosenGroupId() == other.id() );
            REQUIRE( dialog.groupChooser()->comboBox()->findText( NewTeamNamingGroup ) < 0 );
        }
    }
}

SCENARIO( "Name This Value chooses the Team group that has a Name Table of the name proposed",
          "[ui][valuenames][namevalue][groupchooser]" )
{
    const StoredValueNamesGuard guard;
    const auto other = NamingGroup::createNewGroup( QStringLiteral( "Other" ) );
    StoredValueNamesGuard::storeGroups( { other } );
    const auto modem = teamModem();

    NameValueDialog dialog( EsimLine, rangeOf( EsimLine, QStringLiteral( "9" ) ) );
    dialog.showTeamGroups( { modem }, true, {} );
    dialog.proposeGroup( other.id() );

    THEN( "that group and its table are chosen, whatever was chosen last" )
    {
        REQUIRE( dialog.chosenGroupId() == modem.id() );
        REQUIRE( childOf<QComboBox>( dialog, QStringLiteral( "nameTable" ) )->currentText()
                 == QStringLiteral( "EsimState" ) );
    }
}

SCENARIO( "Name This Value proposes the Naming Group chosen last, the first of the user's own "
          "once it is gone",
          "[ui][valuenames][namevalue][groupchooser]" )
{
    const StoredValueNamesGuard guard;
    const auto first = NamingGroup::createNewGroup( QStringLiteral( "First" ) );
    const auto other = NamingGroup::createNewGroup( QStringLiteral( "Other" ) );
    StoredValueNamesGuard::storeGroups( { first, other } );
    const NameValueWindow shown( CapturedLines );
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    // Opens the dialog on a value no rule captures, and reads which group it
    // chose.
    const auto proposedGroup = [ & ] {
        const auto menu = menuOn( textView, 0_lnum, QStringLiteral( "4" ) );
        QString proposed;
        QTimer::singleShot( 0, [ & ] {
            auto* dialog = qobject_cast<NameValueDialog*>( QApplication::activeModalWidget() );
            REQUIRE( dialog != nullptr );
            proposed = dialog->chosenGroupId();
            dialog->reject();
        } );
        contextMenuEntry( *menu, NameThisValue )->trigger();
        return proposed;
    };
    REQUIRE( proposedGroup() == first.id() );

    WHEN( "a value is named in the other group" )
    {
        const auto menu = menuOn( textView, 2_lnum, QStringLiteral( "4" ) );
        nameInDialog( *menu, []( NameValueDialog& dialog ) {
            chooseGroup( dialog, QStringLiteral( "Other" ) );
            setName( dialog, 0, QStringLiteral( "Four" ) );
        } );

        THEN( "it is remembered, and proposed the next time" )
        {
            REQUIRE( Configuration::getSynced().lastNamingGroupId() == other.id() );
            REQUIRE( proposedGroup() == other.id() );
        }

        AND_WHEN( "the other group is gone" )
        {
            StoredValueNamesGuard::storeGroups( { first } );

            THEN( "the first of the user's own is proposed" )
            {
                REQUIRE( proposedGroup() == first.id() );
            }
        }
    }

    WHEN( "a value is named in a new group" )
    {
        const auto menu = menuOn( textView, 2_lnum, QStringLiteral( "4" ) );
        nameInDialog( *menu, []( NameValueDialog& dialog ) {
            chooseGroup( dialog, NewNamingGroup );
            setName( dialog, 0, QStringLiteral( "Four" ) );
        } );

        THEN( "the new group is proposed the next time" )
        {
            const auto& groups = ValueNamesCollection::get().ownGroups();
            REQUIRE( groups.size() == 3 );
            REQUIRE( groups.back().name() == QStringLiteral( "Value Names" ) );
            REQUIRE( proposedGroup() == groups.back().id() );
        }
    }
}

SCENARIO( "Rename and Name ... in ... keep the group and the Name Table of the rule, whatever "
          "was chosen last",
          "[ui][valuenames][namevalue][capturedvalue][groupchooser]" )
{
    const StoredValueNamesGuard guard;
    const auto other = NamingGroup::createNewGroup( QStringLiteral( "Other" ) );
    const auto modem = esimGroup();
    StoredValueNamesGuard::storeGroups( { other, modem } );
    {
        auto& config = Configuration::getSynced();
        config.setLastNamingGroupId( other.id() );
        config.save();
    }
    const NameValueWindow shown( CapturedLines );
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    // Opens the dialog from the entry of the menu, notes what it shows, and
    // names the value.
    QString groupShown;
    QString tableShown;
    bool chooserFixed = false;
    bool nameFieldHidden = false;
    const auto nameFrom = [ & ]( QMenu& menu, const QString& entry ) {
        nameInDialog(
            menu,
            [ & ]( NameValueDialog& dialog ) {
                groupShown = dialog.groupChooser()->comboBox()->currentText();
                tableShown
                    = childOf<QComboBox>( dialog, QStringLiteral( "nameTable" ) )->currentText();
                chooserFixed = !dialog.groupChooser()->isEnabled();
                nameFieldHidden
                    = childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )->isHidden();
                auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
                setName( dialog, rows->currentRow(), QStringLiteral( "Changed" ) );
            },
            entry );
    };
    const auto keptGroupAndTable = [ & ] {
        REQUIRE( groupShown == QStringLiteral( "Modem" ) );
        REQUIRE( tableShown == QStringLiteral( "EsimState" ) );
        REQUIRE( chooserFixed );
        REQUIRE( nameFieldHidden );
        REQUIRE( Configuration::getSynced().lastNamingGroupId() == other.id() );
        const auto& groups = ValueNamesCollection::get().ownGroups();
        REQUIRE( groups.size() == 2 );
        REQUIRE( groups[ 0 ].tables().isEmpty() );
        REQUIRE( groups[ 1 ].rules() == modem.rules() );
    };

    WHEN( "a further value is named from Name ... in ..." )
    {
        const auto menu = menuOn( textView, 0_lnum, QStringLiteral( "4" ) );
        nameFrom( *menu, QStringLiteral( "Name 4 in EsimState..." ) );

        THEN( "the dialog showed the rule's group and table, fixed, and the group chosen last is "
              "still the one remembered" )
        {
            keptGroupAndTable();
        }
    }

    WHEN( "a Named Value is renamed" )
    {
        const auto menu = menuOn( textView, 1_lnum, QStringLiteral( "9" ) );
        nameFrom( *menu, QStringLiteral( "Rename EsimState 9..." ) );

        THEN( "the dialog showed the rule's group and table, fixed, and the group chosen last is "
              "still the one remembered" )
        {
            keptGroupAndTable();
        }
    }
}

namespace {

// Whether the Value Names tab of the window lists a rule of the name in the
// Team group of the name.
bool listsRule( MainWindow& window, const QString& teamGroup, const QString& rule )
{
    const auto group = QStringLiteral( "%1 (Team)" ).arg( teamGroup );
    auto* panel = window.findChild<ValueNamesPanel*>();
    auto* tree = panel != nullptr ? panel->findChild<QTreeWidget*>() : nullptr;
    if ( tree == nullptr ) {
        return false;
    }
    for ( int row = 0; row < tree->topLevelItemCount(); ++row ) {
        const auto* item = tree->topLevelItem( row );
        if ( item->text( 0 ) != group ) {
            continue;
        }
        for ( int child = 0; child < item->childCount(); ++child ) {
            if ( item->child( child )->text( 0 ).startsWith( rule ) ) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

SCENARIO( "A value named in a Team Naming Group reaches the team and every window; a colleague's "
          "change meanwhile is asked about",
          "[ui][valuenames][namevalue][groupchooser][teamfolder]" )
{
    using logsquirl::teamfolder::GroupAction;
    using logsquirl::teamfolder::PublishOutcome;
    using logsquirl::teamfolder::PublishRequest;
    using logsquirl::teamfolder::PublishStatus;
    using teamfolder_testing::settled;
    using teamfolder_testing::SyncTimeoutMs;

    const teamfolder_testing::IsolatedGitEnvironment environment;
    if ( !teamfolder_testing::gitInstalled() ) {
        SKIP( "Git is not installed" );
    }
    const StoredValueNamesGuard guard;
    StoredValueNamesGuard::storeGroups( {} );

    // The team's server, holding a Naming Group a colleague published.
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto server = root.filePath( "server.git" );
    QProcess git;
    git.start( "git", { "init", "--quiet", "--bare", server } );
    REQUIRE( git.waitForFinished( SyncTimeoutMs ) );
    REQUIRE( git.exitCode() == 0 );
    const auto teamPolicy
        = teamfolder_testing::policyFor( QUrl::fromLocalFile( server ).toString() );

    const auto modem = teamModem();
    TeamFolder colleague( root.filePath( "colleague" ) );
    colleague.setUp( teamPolicy );
    REQUIRE( settled( colleague ) );
    const auto publish = [ & ]( const PublishRequest& request ) {
        QSignalSpy finished( &colleague, &TeamFolder::publishFinished );
        colleague.publish( { request } );
        REQUIRE( finished.wait( SyncTimeoutMs ) );
        const auto outcome = finished.at( 0 ).at( 0 ).value<PublishOutcome>();
        REQUIRE( outcome.results.size() == 1 );
        REQUIRE( outcome.results[ 0 ].status == PublishStatus::Published );
        REQUIRE( settled( colleague ) );
    };
    publish( PublishRequest::forGroup( modem, GroupAction::Add ) );

    // Two windows of a Session whose Team Folder has synced.
    const auto member = std::make_shared<TeamFolder>( root.filePath( "member" ) );
    const NameValueWindow shown( EsimLine + u'\n', member, teamPolicy );
    REQUIRE( settled( *member ) );
    REQUIRE( member->namingGroups().size() == 1 );
    const auto second = std::make_unique<MainWindow>(
        WindowSession{ shown.session, QStringLiteral( "Second" ), 1 },
        std::make_shared<logsquirl::plugins::ApplicationPlugins>() );
    showUntilExposed( *second );
    shown.window->activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( shown.window.get(), 5000 ) );
    const auto listedEverywhere = [ & ]( const QString& group ) {
        return listsRule( *shown.window, group, QStringLiteral( "EsimState" ) )
               && listsRule( *second, group, QStringLiteral( "EsimState" ) );
    };
    auto& textView = CrawlerAccess::textView( *shown.crawler );
    const auto openMenu = [ & ] {
        textView.selectAndDisplayLine( 0_lnum );
        return ViewAccess::contextMenu(
            textView, ViewAccess::pointOn( textView, 0, EsimLine.indexOf( u'9' ) ) );
    };
    // The rows of the colleague's copy of the Team group.
    const auto colleaguesRows = [ & ]( const QString& group ) {
        for ( const auto& teamGroup : colleague.namingGroups() ) {
            if ( teamGroup.name() == group && !teamGroup.tables().isEmpty() ) {
                return teamGroup.tables().front().rows;
            }
        }
        return QList<NameRow>{};
    };

    WHEN( "the value is named in the Team group" )
    {
        const auto menu = openMenu();
        nameInDialog( *menu, []( NameValueDialog& dialog ) {
            REQUIRE( dialog.groupChooser()->hasTeamSection() );
            REQUIRE( dialog.groupChooser()->choice().name == QStringLiteral( "Modem" ) );
            setName( dialog, 1, QStringLiteral( "Ready" ) );
        } );

        THEN( "the colleague gets it, both windows list the rule, and the Log Line shows the "
              "name" )
        {
            REQUIRE( waitUiState( [ & ] { return listedEverywhere( QStringLiteral( "Modem" ) ); },
                                  SyncTimeoutMs ) );
            REQUIRE( waitUiState( [ & ] {
                textView.selectAndDisplayLine( 0_lnum );
                return textView.showsValueNames()
                       && textView.getSelectedTextAsShown()
                              == QStringLiteral( "modem EsimState=Ready (9) done" );
            } ) );
            REQUIRE( settled( *member ) );
            colleague.sync();
            REQUIRE( settled( colleague ) );
            REQUIRE(
                colleaguesRows( QStringLiteral( "Modem" ) )
                == QList<NameRow>{ NameRow{ QStringLiteral( "1" ), QStringLiteral( "Idle" ) },
                                   NameRow{ QStringLiteral( "9" ), QStringLiteral( "Ready" ) } } );
            REQUIRE( ValueNamesCollection::get().ownGroups().isEmpty() );
            REQUIRE( Configuration::getSynced().lastNamingGroupId() == modem.id() );
        }
    }

    WHEN( "the value is named in a new Team group" )
    {
        const auto menu = openMenu();
        nameInDialog( *menu, []( NameValueDialog& dialog ) {
            chooseGroup( dialog, NewTeamNamingGroup );
            childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )
                ->setText( QStringLiteral( "Signals" ) );
            auto* tables = childOf<QComboBox>( dialog, QStringLiteral( "nameTable" ) );
            REQUIRE( tables->count() == 1 );
            setName( dialog, 0, QStringLiteral( "Ready" ) );
        } );

        THEN( "the colleague gets the group, and both windows list it" )
        {
            REQUIRE( waitUiState( [ & ] { return listedEverywhere( QStringLiteral( "Signals" ) ); },
                                  SyncTimeoutMs ) );
            REQUIRE( settled( *member ) );
            colleague.sync();
            REQUIRE( settled( colleague ) );
            REQUIRE( colleague.namingGroups().size() == 2 );
            REQUIRE(
                colleaguesRows( QStringLiteral( "Signals" ) )
                == QList<NameRow>{ NameRow{ QStringLiteral( "9" ), QStringLiteral( "Ready" ) } } );
        }
    }

    WHEN( "the colleague changed the Team group since the last sync, and the value is named in "
          "it" )
    {
        auto theirs = modem;
        auto tables = theirs.tables();
        tables.front().rows.append( NameRow{ QStringLiteral( "2" ), QStringLiteral( "Theirs" ) } );
        theirs.setTables( tables );
        auto change = PublishRequest::forGroup( theirs, GroupAction::Change );
        change.baseRevision = colleague.namingGroupRevision( modem.id() );
        publish( change );

        QSignalSpy finished( member.get(), &TeamFolder::publishFinished );
        const auto menu = openMenu();
        nameInDialog( *menu, []( NameValueDialog& dialog ) {
            setName( dialog, 1, QStringLiteral( "Ready" ) );
        } );
        // The publish meets their change. The window system gives the window
        // the focus back once the dialog is gone (the offscreen platform does
        // not): then it asks.
        REQUIRE( finished.wait( SyncTimeoutMs ) );
        const auto outcome = finished.at( 0 ).at( 0 ).value<PublishOutcome>();
        REQUIRE( outcome.results.size() == 1 );
        REQUIRE( outcome.results[ 0 ].status == PublishStatus::Conflict );
        shown.window->activateWindow();
        ModalAnswers modals;
        modals.clickButton( QStringLiteral( "Keep mine" ) );

        THEN( "the window asks as the Value Names dialog's publish does, and Keep mine publishes "
              "the name over their change" )
        {
            REQUIRE( waitUiState( [ & ] { return modals.unanswered() == 0; }, SyncTimeoutMs ) );
            REQUIRE( modals.messages().size() == 1 );
            REQUIRE( modals.messages()[ 0 ].contains(
                QStringLiteral( "Somebody else changed the Team group \"Modem\"" ) ) );
            REQUIRE( modals.unexpectedMessageBoxes() == 0 );
            REQUIRE( waitUiState( [ & ] { return listedEverywhere( QStringLiteral( "Modem" ) ); },
                                  SyncTimeoutMs ) );
            REQUIRE( settled( *member ) );
            colleague.sync();
            REQUIRE( settled( colleague ) );
            REQUIRE(
                colleaguesRows( QStringLiteral( "Modem" ) )
                == QList<NameRow>{ NameRow{ QStringLiteral( "1" ), QStringLiteral( "Idle" ) },
                                   NameRow{ QStringLiteral( "9" ), QStringLiteral( "Ready" ) } } );
        }
    }
}
