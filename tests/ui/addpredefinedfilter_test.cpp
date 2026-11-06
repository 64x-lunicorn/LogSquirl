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

// Add as Predefined Filter... (#897): the context menu of the Text View and
// the Filtered View offers it for text selected within a Log Line, the dialog
// it opens proposes a name and the selection as the pattern in the Default
// Filter Group, as plain text or, with Regular expression, escaped as a regex,
// and OK adds the Predefined Filter; with apply now the Search runs with it,
// and finds the selection with Use regex on or off.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include <functional>
#include <memory>

#include "abstractlogview.h"
#include "addpredefinedfilterdialog.h"
#include "applicationplugins.h"
#include "context_menu_entries.h"
#include "crawlerwidget.h"
#include "filterspanel.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "openlogfile.h"
#include "predefinedfilters.h"
#include "predefinedfiltersdialog.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

struct AddPredefinedFilterTest {};

template <>
struct AbstractLogView::access_by<AddPredefinedFilterTest> {
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
struct CrawlerWidget::access_by<AddPredefinedFilterTest> {
    static AbstractLogView& textView( const CrawlerWidget& crawler )
    {
        return *crawler.logMainView_;
    }

    static AbstractLogView& filteredView( const CrawlerWidget& crawler )
    {
        return *crawler.currentFilteredView();
    }

    static QString searchPattern( const CrawlerWidget& crawler )
    {
        return SearchLineAccess::patternEdit( *crawler.searchLine_ )->currentText();
    }

    static bool searchStopped( const CrawlerWidget& crawler )
    {
        return SearchLineAccess::stopButton( *crawler.searchLine_ )->isHidden();
    }

    static QToolButton* useRegexpButton( const CrawlerWidget& crawler )
    {
        return SearchLineAccess::useRegexpButton( *crawler.searchLine_ );
    }

    static LinesCount matchCount( const CrawlerWidget& crawler )
    {
        return crawler.openLogFile_->filteredData()->getNbLine();
    }

    static void search( CrawlerWidget& crawler, const QString& pattern )
    {
        auto* edit = SearchLineAccess::patternEdit( *crawler.searchLine_ );
        edit->clearEditText();
        QTest::keyClicks( edit, pattern );
        QTest::mouseClick( SearchLineAccess::searchButton( *crawler.searchLine_ ), Qt::LeftButton );
        QTest::qWait( 100 );
        REQUIRE( waitUiState( [ & ] { return searchStopped( crawler ); } ) );
    }
};

namespace {

using ViewAccess = AbstractLogView::access_by<AddPredefinedFilterTest>;
using CrawlerAccess = CrawlerWidget::access_by<AddPredefinedFilterTest>;

const auto AddAsPredefinedFilter = QStringLiteral( "Add as Predefined Filter..." );
const auto SelectedText = QStringLiteral( "a.b[1]" );
const auto EscapedText = QStringLiteral( "a\\.b\\[1\\]" );
const auto TheLogLine = QStringLiteral( "modem a.b[1] ERROR done" );
// A Log Line the pattern would match, were its dots and brackets read as a
// regex: only the escaped pattern tells the two apart.
const auto LookalikeLogLine = QStringLiteral( "modem axb1 WARN done" );
// A selection of two words, and the Log Line it is in.
const auto TwoWords = QStringLiteral( "connection refused" );
const auto TwoWordsLogLine = QStringLiteral( "peer connection refused by host" );
const auto TwoWordsLine = 3_lnum;

// The Filter Groups, stored again as they were when this goes.
class KeptFilterGroups {
public:
    KeptFilterGroups()
        : sets_( PredefinedFiltersCollection::getSynced().filterSets() )
    {
    }

    ~KeptFilterGroups()
    {
        store( sets_ );
    }

    KeptFilterGroups( const KeptFilterGroups& ) = delete;
    KeptFilterGroups& operator=( const KeptFilterGroups& ) = delete;

    static void store( const QList<PredefinedFilterSet>& sets )
    {
        auto& collection = PredefinedFiltersCollection::getSynced();
        collection.setFilterSets( sets );
        collection.save();
    }

private:
    QList<PredefinedFilterSet> sets_;
};

PredefinedFilterSet groupOf( const QString& name, const QString& pattern )
{
    auto set = PredefinedFilterSet::createNewSet( name );
    set.addFilter( PredefinedFilter{ name, pattern, true } );
    return set;
}

// The group of the id as stored now: read again from the settings store.
PredefinedFilterSet storedGroup( const QString& groupId )
{
    for ( const auto& set : PredefinedFiltersCollection::getSynced().filterSets() ) {
        if ( set.id() == groupId ) {
            return set;
        }
    }
    FAIL( "no group " << groupId.toStdString() );
    return {};
}

template <class Widget>
Widget* childOf( QWidget& parent, const QString& name )
{
    auto* child = parent.findChild<Widget*>( name );
    REQUIRE( child != nullptr );
    return child;
}

QPushButton* okButton( QDialog& dialog )
{
    return dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok );
}

void clickOk( QDialog& dialog )
{
    auto* ok = okButton( dialog );
    REQUIRE( ok->isEnabled() );
    ok->click();
}

bool listsFilter( QTreeWidget& tree, const QString& name )
{
    for ( int top = 0; top < tree.topLevelItemCount(); ++top ) {
        const auto* group = tree.topLevelItem( top );
        for ( int child = 0; child < group->childCount(); ++child ) {
            if ( group->child( child )->data( 0, Qt::UserRole ).toString() == name ) {
                return true;
            }
        }
    }
    return false;
}

// A window showing the Log File.
struct AddFilterWindow {
    AddFilterWindow()
    {
        REQUIRE( dir.isValid() );
        const auto path = dir.filePath( QStringLiteral( "filters.log" ) );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( ( TheLogLine + u'\n' + LookalikeLogLine + QStringLiteral( "\nother line\n" )
                      + TwoWordsLogLine + u'\n' )
                        .toUtf8() );
        file.close();

        const auto session = std::make_shared<Session>( testSettingsPolicies(),
                                                        std::make_shared<LogFormatCatalog>() );
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

    ~AddFilterWindow()
    {
        window.reset();
        QTest::qWait( 50 );
    }

    AddFilterWindow( const AddFilterWindow& ) = delete;
    AddFilterWindow& operator=( const AddFilterWindow& ) = delete;

    QTemporaryDir dir;
    std::unique_ptr<MainWindow> window;
    CrawlerWidget* crawler = nullptr;
};

// Selects the text within the first Log Line of the view.
void selectText( AbstractLogView& view, LineNumber line = 0_lnum )
{
    const auto start = TheLogLine.indexOf( SelectedText );
    ViewAccess::selectPortion( view, line, LineColumn{ start },
                               LineColumn{ start + SelectedText.size() - 1 } );
}

// Selects the two words within their Log Line of the Text View.
void selectTwoWords( AbstractLogView& view )
{
    const auto start = TwoWordsLogLine.indexOf( TwoWords );
    ViewAccess::selectPortion( view, TwoWordsLine, LineColumn{ start },
                               LineColumn{ start + TwoWords.size() - 1 } );
}

// Chooses Add as Predefined Filter... of the menu, and in the dialog it opens
// does what fill does, then clicks OK.
void addInDialog( QMenu& menu, const std::function<void( AddPredefinedFilterDialog& )>& fill )
{
    auto* entry = contextMenuEntry( menu, AddAsPredefinedFilter );
    REQUIRE( entry != nullptr );
    REQUIRE( entry->isEnabled() );
    bool opened = false;
    QTimer::singleShot( 0, [ & ] {
        auto* dialog
            = qobject_cast<AddPredefinedFilterDialog*>( QApplication::activeModalWidget() );
        REQUIRE( dialog != nullptr );
        opened = true;
        fill( *dialog );
        clickOk( *dialog );
    } );
    entry->trigger();
    REQUIRE( opened );
}

} // namespace

SCENARIO( "The context menu of the Text View and the Filtered View offers Add as Predefined "
          "Filter... for a selection",
          "[ui][predefinedfilters][addpredefinedfilter]" )
{
    const AddFilterWindow shown;
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    GIVEN( "the Text View" )
    {
        WHEN( "nothing is selected" )
        {
            const auto menu = ViewAccess::contextMenu( textView );

            THEN( "the entry is there, disabled" )
            {
                REQUIRE( contextMenuEntry( *menu, AddAsPredefinedFilter ) != nullptr );
                REQUIRE_FALSE( contextMenuEntry( *menu, AddAsPredefinedFilter )->isEnabled() );
            }
        }

        WHEN( "text is selected within a Log Line" )
        {
            selectText( textView );
            const auto menu = ViewAccess::contextMenu( textView );

            THEN( "the entry is offered at the top level with the selection's entries, enabled, "
                  "the top level kept short" )
            {
                const auto entries = entriesOf( *menu );
                REQUIRE( entries.contains( AddAsPredefinedFilter ) );
                REQUIRE( entries.indexOf( AddAsPredefinedFilter )
                         < entries.indexOf( QStringLiteral( "Highlighters" ) ) );
                REQUIRE( contextMenuEntry( *menu, AddAsPredefinedFilter )->isEnabled() );
                REQUIRE( entries.size() <= MaxTopLevelEntries );
            }
        }

        WHEN( "a whole Log Line is selected" )
        {
            textView.selectAndDisplayLine( 0_lnum );
            const auto menu = ViewAccess::contextMenu( textView );

            THEN( "the entry is disabled" )
            {
                REQUIRE_FALSE( contextMenuEntry( *menu, AddAsPredefinedFilter )->isEnabled() );
            }
        }
    }

    GIVEN( "the Filtered View of a Search for the Log Line" )
    {
        CrawlerAccess::search( *shown.crawler, QStringLiteral( "ERROR" ) );
        auto& filteredView = CrawlerAccess::filteredView( *shown.crawler );

        WHEN( "text is selected within its Log Line" )
        {
            selectText( filteredView );
            const auto menu = ViewAccess::contextMenu( filteredView );

            THEN( "it offers the entry, enabled" )
            {
                REQUIRE( contextMenuEntry( *menu, AddAsPredefinedFilter ) != nullptr );
                REQUIRE( contextMenuEntry( *menu, AddAsPredefinedFilter )->isEnabled() );
            }
        }
    }
}

SCENARIO( "The name proposed for a Predefined Filter is the selection, on one line and short",
          "[ui][predefinedfilters][addpredefinedfilter]" )
{
    CHECK( AddPredefinedFilterDialog::proposedName( SelectedText ) == SelectedText );
    CHECK( AddPredefinedFilterDialog::proposedName( QStringLiteral( "  connection \t lost  " ) )
           == QStringLiteral( "connection lost" ) );

    const auto longText = QString( 100, u'x' );
    const auto name = AddPredefinedFilterDialog::proposedName( longText );
    CHECK( name.size() == AddPredefinedFilterDialog::MaxProposedNameLength );
    CHECK( name.endsWith( QStringLiteral( "..." ) ) );
}

SCENARIO( "The Add as Predefined Filter dialog proposes the selection as plain text in the "
          "Default Filter Group",
          "[ui][predefinedfilters][addpredefinedfilter]" )
{
    const KeptFilterGroups kept;

    GIVEN( "a Filter Group of the user's besides the Default Filter Group" )
    {
        const auto other = groupOf( QStringLiteral( "Other" ), QStringLiteral( "WARN" ) );
        KeptFilterGroups::store( { other } );
        const auto defaultFilters = storedGroup( defaultFilterSetId() ).filters().size();

        AddPredefinedFilterDialog dialog( SelectedText );
        showUntilExposed( dialog );
        auto* pattern = childOf<QLineEdit>( dialog, QStringLiteral( "filterPattern" ) );
        auto* regex = childOf<QCheckBox>( dialog, QStringLiteral( "filterRegex" ) );

        THEN( "it proposes the selection as the pattern, not as a regex, and a name from it" )
        {
            REQUIRE( pattern->text() == SelectedText );
            REQUIRE_FALSE( regex->isChecked() );
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "filterName" ) )->text()
                     == SelectedText );
            const auto filter = dialog.filter();
            REQUIRE( filter.pattern == SelectedText );
            REQUIRE( filter.name == SelectedText );
            REQUIRE_FALSE( filter.useRegex );
            REQUIRE( okButton( dialog )->isEnabled() );
        }

        THEN( "the Default Filter Group is chosen, and every group of the user's is offered" )
        {
            auto* groups = childOf<QComboBox>( dialog, QStringLiteral( "filterGroup" ) );
            REQUIRE( dialog.chosenGroupId() == defaultFilterSetId() );
            REQUIRE( groups->findText( QStringLiteral( "Other" ) ) >= 0 );
            REQUIRE( groups->count() == 2 );
        }

        THEN( "it does not apply the filter unless asked to" )
        {
            REQUIRE_FALSE(
                childOf<QCheckBox>( dialog, QStringLiteral( "applyNow" ) )->isChecked() );
            REQUIRE_FALSE( dialog.applyNow() );
        }

        WHEN( "Regular expression is checked" )
        {
            regex->setChecked( true );

            THEN( "the pattern is the selection escaped as a regex, which the filter is" )
            {
                REQUIRE( pattern->text() == EscapedText );
                REQUIRE( dialog.filter().pattern == EscapedText );
                REQUIRE( dialog.filter().useRegex );
                REQUIRE( okButton( dialog )->isEnabled() );
            }

            AND_WHEN( "it is unchecked again" )
            {
                regex->setChecked( false );

                THEN( "the pattern is the selection again" )
                {
                    REQUIRE( pattern->text() == SelectedText );
                    REQUIRE_FALSE( dialog.filter().useRegex );
                }
            }

            AND_WHEN( "the pattern is edited into an invalid regex" )
            {
                pattern->setText( QStringLiteral( "a(" ) );

                THEN( "it warns, and OK is disabled" )
                {
                    REQUIRE_FALSE( dialog.warning().isEmpty() );
                    REQUIRE_FALSE( okButton( dialog )->isEnabled() );
                }
            }
        }

        WHEN( "the pattern is edited into what would be an invalid regex, read as plain text" )
        {
            pattern->setText( QStringLiteral( "a(" ) );

            THEN( "OK is enabled: plain text needs no valid regex" )
            {
                REQUIRE( dialog.warning().isEmpty() );
                REQUIRE( okButton( dialog )->isEnabled() );
            }
        }

        WHEN( "the name is emptied" )
        {
            childOf<QLineEdit>( dialog, QStringLiteral( "filterName" ) )->setText( QString{} );

            THEN( "OK is disabled" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
            }
        }

        WHEN( "OK is clicked" )
        {
            clickOk( dialog );

            THEN( "the Predefined Filter is stored at the end of the Default Filter Group" )
            {
                const auto filters = storedGroup( defaultFilterSetId() ).filters();
                REQUIRE( filters.size() == defaultFilters + 1 );
                REQUIRE( filters.back().name == SelectedText );
                REQUIRE( filters.back().pattern == SelectedText );
                REQUIRE_FALSE( filters.back().useRegex );
                REQUIRE( storedGroup( other.id() ).filters().size() == 1 );
            }
        }

        WHEN( "Regular expression is checked and OK is clicked" )
        {
            regex->setChecked( true );
            clickOk( dialog );

            THEN( "the Predefined Filter is stored as the escaped regex" )
            {
                const auto filters = storedGroup( defaultFilterSetId() ).filters();
                REQUIRE( filters.size() == defaultFilters + 1 );
                REQUIRE( filters.back().pattern == EscapedText );
                REQUIRE( filters.back().useRegex );
            }
        }

        WHEN( "the other group is chosen, the name changed, and OK is clicked" )
        {
            auto* groups = childOf<QComboBox>( dialog, QStringLiteral( "filterGroup" ) );
            groups->setCurrentIndex( groups->findText( QStringLiteral( "Other" ) ) );
            childOf<QLineEdit>( dialog, QStringLiteral( "filterName" ) )
                ->setText( QStringLiteral( "Modem index" ) );
            clickOk( dialog );

            THEN( "the Predefined Filter is stored in it, and the Default Filter Group is as it "
                  "was" )
            {
                const auto filters = storedGroup( other.id() ).filters();
                REQUIRE( filters.size() == 2 );
                REQUIRE( filters.back().name == QStringLiteral( "Modem index" ) );
                REQUIRE( filters.back().pattern == SelectedText );
                REQUIRE( storedGroup( defaultFilterSetId() ).filters().size() == defaultFilters );
            }
        }

        WHEN( "Cancel is clicked" )
        {
            dialog.reject();

            THEN( "no group changed" )
            {
                REQUIRE( storedGroup( defaultFilterSetId() ).filters().size() == defaultFilters );
                REQUIRE( storedGroup( other.id() ).filters().size() == 1 );
            }
        }
    }
}

SCENARIO( "The Add as Predefined Filter dialog keeps the names of a Filter Group's filters "
          "apart",
          "[ui][predefinedfilters][addpredefinedfilter]" )
{
    const KeptFilterGroups kept;

    GIVEN( "a Default Filter Group with a filter named as the selection, and another group "
           "without" )
    {
        const auto other = groupOf( QStringLiteral( "Other" ), QStringLiteral( "WARN" ) );
        KeptFilterGroups::store( { other } );
        auto& collection = PredefinedFiltersCollection::getSynced();
        collection.addFilter( defaultFilterSetId(),
                              PredefinedFilter{ SelectedText, QStringLiteral( "x" ), false } );
        collection.save();

        AddPredefinedFilterDialog dialog( SelectedText );
        showUntilExposed( dialog );
        auto* name = childOf<QLineEdit>( dialog, QStringLiteral( "filterName" ) );
        auto* groups = childOf<QComboBox>( dialog, QStringLiteral( "filterGroup" ) );

        THEN( "it proposes the name numbered, which no filter of the group has" )
        {
            REQUIRE( name->text() == SelectedText + QStringLiteral( " (2)" ) );
            REQUIRE( okButton( dialog )->isEnabled() );
            REQUIRE( dialog.warning().isEmpty() );
        }

        WHEN( "the name is edited into the name the group has" )
        {
            name->setText( SelectedText );

            THEN( "OK is disabled, and the dialog says why" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
                REQUIRE( dialog.warning().contains( SelectedText ) );
                REQUIRE( childOf<QLabel>( dialog, QStringLiteral( "warning" ) )->isVisible() );
            }

            AND_WHEN( "the other group is chosen" )
            {
                groups->setCurrentIndex( groups->findText( QStringLiteral( "Other" ) ) );

                THEN( "OK is enabled again: that group has no filter of the name" )
                {
                    REQUIRE( okButton( dialog )->isEnabled() );
                    REQUIRE( dialog.warning().isEmpty() );
                }
            }
        }

        WHEN( "the name is edited into the other group's filter's name and that group chosen" )
        {
            name->setText( QStringLiteral( "Other" ) );
            groups->setCurrentIndex( groups->findText( QStringLiteral( "Other" ) ) );

            THEN( "OK is disabled" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
            }
        }
    }
}

SCENARIO( "A filter added to a Filter Group that has gone goes into the Default Filter Group",
          "[ui][predefinedfilters][addpredefinedfilter]" )
{
    const KeptFilterGroups kept;
    KeptFilterGroups::store( {} );
    const auto defaultFilters = storedGroup( defaultFilterSetId() ).filters().size();

    auto& collection = PredefinedFiltersCollection::getSynced();
    collection.addFilter(
        QStringLiteral( "no such group" ),
        PredefinedFilter{ QStringLiteral( "kept" ), QStringLiteral( "x" ), false } );
    collection.save();

    const auto filters = storedGroup( defaultFilterSetId() ).filters();
    REQUIRE( filters.size() == defaultFilters + 1 );
    REQUIRE( filters.back().name == QStringLiteral( "kept" ) );
}

SCENARIO( "OK in the Add as Predefined Filter dialog adds the filter, and with apply now the "
          "Search runs with it",
          "[ui][predefinedfilters][addpredefinedfilter]" )
{
    const KeptFilterGroups kept;
    KeptFilterGroups::store( {} );

    const AddFilterWindow shown;
    auto& textView = CrawlerAccess::textView( *shown.crawler );
    auto* filtersTree = shown.window->findChild<FiltersPanel*>()->findChild<QTreeWidget*>();
    REQUIRE( filtersTree != nullptr );
    REQUIRE_FALSE( listsFilter( *filtersTree, SelectedText ) );

    WHEN( "the selection in the Text View is added with apply now" )
    {
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& dialog ) {
            childOf<QCheckBox>( dialog, QStringLiteral( "applyNow" ) )->setChecked( true );
        } );

        THEN( "the Default Filter Group holds it, and the Filters panel lists it" )
        {
            const auto filters = storedGroup( defaultFilterSetId() ).filters();
            REQUIRE( filters.size() == 1 );
            REQUIRE( filters.front().pattern == SelectedText );
            REQUIRE( listsFilter( *filtersTree, SelectedText ) );
        }

        THEN( "the Search, reading regexes, runs with it escaped, and finds the Log Line and not "
              "the lookalike" )
        {
            REQUIRE( CrawlerAccess::useRegexpButton( *shown.crawler )->isChecked() );
            REQUIRE( waitUiState( [ & ] {
                return CrawlerAccess::searchPattern( *shown.crawler ) == EscapedText
                       && CrawlerAccess::searchStopped( *shown.crawler )
                       && CrawlerAccess::matchCount( *shown.crawler ) == 1_lcount;
            } ) );
        }
    }

    WHEN( "the selection is added with apply now while the Search reads plain text" )
    {
        CrawlerAccess::useRegexpButton( *shown.crawler )->setChecked( false );
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& dialog ) {
            childOf<QCheckBox>( dialog, QStringLiteral( "applyNow" ) )->setChecked( true );
        } );

        THEN( "the Search runs with the selection as it is, and finds the Log Line and not the "
              "lookalike" )
        {
            REQUIRE_FALSE( CrawlerAccess::useRegexpButton( *shown.crawler )->isChecked() );
            REQUIRE( waitUiState( [ & ] {
                return CrawlerAccess::searchPattern( *shown.crawler ) == SelectedText
                       && CrawlerAccess::searchStopped( *shown.crawler )
                       && CrawlerAccess::matchCount( *shown.crawler ) == 1_lcount;
            } ) );
        }
    }

    WHEN( "two words are added with apply now, the Search reading regexes or plain text" )
    {
        const bool regexSearch = GENERATE( true, false );
        CrawlerAccess::useRegexpButton( *shown.crawler )->setChecked( regexSearch );
        selectTwoWords( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& dialog ) {
            childOf<QCheckBox>( dialog, QStringLiteral( "applyNow" ) )->setChecked( true );
        } );

        THEN( "the Search finds their Log Line" )
        {
            REQUIRE( storedGroup( defaultFilterSetId() ).filters().front().pattern == TwoWords );
            REQUIRE( waitUiState( [ & ] {
                return CrawlerAccess::searchStopped( *shown.crawler )
                       && CrawlerAccess::matchCount( *shown.crawler ) == 1_lcount;
            } ) );
        }
    }

    WHEN( "the selection is added as a regex with apply now" )
    {
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& dialog ) {
            childOf<QCheckBox>( dialog, QStringLiteral( "filterRegex" ) )->setChecked( true );
            childOf<QCheckBox>( dialog, QStringLiteral( "applyNow" ) )->setChecked( true );
        } );

        THEN( "the Search runs with the escaped regex, and finds the Log Line and not the "
              "lookalike" )
        {
            REQUIRE( storedGroup( defaultFilterSetId() ).filters().front().useRegex );
            REQUIRE( waitUiState( [ & ] {
                return CrawlerAccess::searchPattern( *shown.crawler ) == EscapedText
                       && CrawlerAccess::searchStopped( *shown.crawler )
                       && CrawlerAccess::matchCount( *shown.crawler ) == 1_lcount;
            } ) );
        }
    }

    WHEN( "the selection in the Filtered View is added without apply now" )
    {
        CrawlerAccess::search( *shown.crawler, QStringLiteral( "ERROR|WARN" ) );
        REQUIRE( CrawlerAccess::matchCount( *shown.crawler ) == 2_lcount );
        auto& filteredView = CrawlerAccess::filteredView( *shown.crawler );
        selectText( filteredView );
        const auto menu = ViewAccess::contextMenu( filteredView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& ) {} );
        QTest::qWait( 100 );

        THEN( "the filter is added, and the Search is as it was" )
        {
            REQUIRE( storedGroup( defaultFilterSetId() ).filters().size() == 1 );
            REQUIRE( CrawlerAccess::searchPattern( *shown.crawler )
                     == QStringLiteral( "ERROR|WARN" ) );
            REQUIRE( CrawlerAccess::matchCount( *shown.crawler ) == 2_lcount );
        }
    }
}

SCENARIO( "Refreshing the Filters panel with no filter checked leaves the Search pattern as it is",
          "[ui][filterspanel][predefinedfilters]" )
{
    // #914: every refresh of the panel replaced the pattern with an empty one.
    const KeptFilterGroups kept;
    KeptFilterGroups::store( { groupOf( QStringLiteral( "Other" ), QStringLiteral( "WARN" ) ) } );

    const AddFilterWindow shown;
    CrawlerAccess::search( *shown.crawler, QStringLiteral( "ERROR" ) );
    REQUIRE( CrawlerAccess::matchCount( *shown.crawler ) == 1_lcount );

    WHEN( "the Predefined Filters editor is opened and OK clicked without checking a filter" )
    {
        QAction* editorEntry = nullptr;
        for ( auto* action : shown.window->findChildren<QAction*>() ) {
            if ( action->text() == QStringLiteral( "Predefined Filters..." ) ) {
                editorEntry = action;
            }
        }
        REQUIRE( editorEntry != nullptr );
        bool opened = false;
        QTimer::singleShot( 0, [ & ] {
            auto* dialog
                = qobject_cast<PredefinedFiltersDialog*>( QApplication::activeModalWidget() );
            REQUIRE( dialog != nullptr );
            opened = true;
            dialog->findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok )->click();
        } );
        editorEntry->trigger();
        REQUIRE( opened );
        QTest::qWait( 100 );

        THEN( "the Search pattern and its Matches are as they were" )
        {
            REQUIRE( CrawlerAccess::searchPattern( *shown.crawler ) == QStringLiteral( "ERROR" ) );
            REQUIRE( CrawlerAccess::searchStopped( *shown.crawler ) );
            REQUIRE( CrawlerAccess::matchCount( *shown.crawler ) == 1_lcount );
        }
    }

    WHEN( "the Filters panel is refreshed" )
    {
        shown.window->findChild<FiltersPanel*>()->refreshFilters();
        QTest::qWait( 100 );

        THEN( "the Search pattern is as it was" )
        {
            REQUIRE( CrawlerAccess::searchPattern( *shown.crawler ) == QStringLiteral( "ERROR" ) );
            REQUIRE( CrawlerAccess::matchCount( *shown.crawler ) == 1_lcount );
        }
    }
}
