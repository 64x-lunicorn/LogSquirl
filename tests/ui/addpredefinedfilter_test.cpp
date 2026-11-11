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
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>

#include <functional>
#include <memory>
#include <utility>

#include "abstractlogview.h"
#include "addpredefinedfilterdialog.h"
#include "applicationplugins.h"
#include "configuration.h"
#include "context_menu_entries.h"
#include "crawler_children.h"
#include "crawlerwidget.h"
#include "filterspanel.h"
#include "groupchooser.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "modal_answers.h"
#include "open_log_file_of.h"
#include "openlogfile.h"
#include "predefinedfilters.h"
#include "predefinedfiltersdialog.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "shown_widget.h"
#include "teamfolder.h"
#include "test_policies.h"
#include "test_utils.h"

// The Team Folder helpers of the unit tests, which run it against real Git
// repositories.
#include "../unit/teamfoldertesting.h"

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

namespace {

using ViewAccess = AbstractLogView::access_by<AddPredefinedFilterTest>;

// The child widgets of a tab, driven as a user does.
struct CrawlerAccess {
    static AbstractLogView& textView( const CrawlerWidget& crawler )
    {
        return textViewOf( crawler );
    }

    // The Filtered View in front, as the State says.
    static AbstractLogView& filteredView( const CrawlerWidget& crawler )
    {
        return *frontFilteredViewOf( crawler );
    }

    static QString searchPattern( const CrawlerWidget& crawler )
    {
        return SearchLineAccess::patternEdit( searchLineOf( crawler ) )->currentText();
    }

    static bool searchStopped( const CrawlerWidget& crawler )
    {
        return SearchLineAccess::stopButton( searchLineOf( crawler ) )->isHidden();
    }

    static QToolButton* useRegexpButton( const CrawlerWidget& crawler )
    {
        return SearchLineAccess::useRegexpButton( searchLineOf( crawler ) );
    }

    static void search( CrawlerWidget& crawler, const QString& pattern )
    {
        auto& searchLine = searchLineOf( crawler );
        auto* edit = SearchLineAccess::patternEdit( searchLine );
        edit->clearEditText();
        QTest::keyClicks( edit, pattern );
        QTest::mouseClick( SearchLineAccess::searchButton( searchLine ), Qt::LeftButton );
        QTest::qWait( 100 );
        REQUIRE( waitUiState( [ & ] { return searchStopped( crawler ); } ) );
    }
};

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
const auto NewFilterGroup = QStringLiteral( "New Filter Group..." );
const auto NewTeamFilterGroup = QStringLiteral( "New Team Filter Group..." );

// The Filter Groups, stored again as they were when this goes.
class KeptFilterGroups {
public:
    KeptFilterGroups()
        : sets_( PredefinedFiltersCollection::getSynced().filterSets() )
        , lastGroupId_( Configuration::getSynced().lastFilterGroupId() )
    {
    }

    ~KeptFilterGroups()
    {
        store( sets_ );
        auto& config = Configuration::getSynced();
        config.setLastFilterGroupId( lastGroupId_ );
        config.save();
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
    QString lastGroupId_;
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
    // With a Team Folder, the Session sets it up as the policy says.
    explicit AddFilterWindow( std::shared_ptr<TeamFolder> teamFolder = {},
                              const TeamFolderPolicy& teamPolicy = {} )
    {
        REQUIRE( dir.isValid() );
        const auto path = dir.filePath( QStringLiteral( "filters.log" ) );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( ( TheLogLine + u'\n' + LookalikeLogLine + QStringLiteral( "\nother line\n" )
                      + TwoWordsLogLine + u'\n' )
                        .toUtf8() );
        file.close();

        auto policies = testSettingsPolicies();
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

    ~AddFilterWindow()
    {
        window.reset();
        QTest::qWait( 50 );
    }

    AddFilterWindow( const AddFilterWindow& ) = delete;
    AddFilterWindow& operator=( const AddFilterWindow& ) = delete;

    QTemporaryDir dir;
    std::shared_ptr<Session> session;
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

        THEN( "the Default Filter Group is chosen, and every group of the user's is offered; "
              "without a Team Folder no Team section and no New Team Filter Group..." )
        {
            auto* groups = childOf<QComboBox>( dialog, QStringLiteral( "groupChoice" ) );
            REQUIRE( dialog.chosenGroupId() == defaultFilterSetId() );
            REQUIRE( groups->findText( QStringLiteral( "Other" ) ) >= 0 );
            REQUIRE( groups->findText( NewFilterGroup ) >= 0 );
            REQUIRE( groups->findText( QStringLiteral( "Team Filter Groups" ) ) < 0 );
            REQUIRE( groups->findText( NewTeamFilterGroup ) < 0 );
            REQUIRE_FALSE( dialog.groupChooser()->hasTeamSection() );
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
            auto* groups = childOf<QComboBox>( dialog, QStringLiteral( "groupChoice" ) );
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
        auto* groups = childOf<QComboBox>( dialog, QStringLiteral( "groupChoice" ) );

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
                       && openLogFileOf( *shown.session, *shown.crawler )
                                  .filteredData()
                                  ->getNbLine()
                              == 1_lcount;
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
                       && openLogFileOf( *shown.session, *shown.crawler )
                                  .filteredData()
                                  ->getNbLine()
                              == 1_lcount;
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
                       && openLogFileOf( *shown.session, *shown.crawler )
                                  .filteredData()
                                  ->getNbLine()
                              == 1_lcount;
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
                       && openLogFileOf( *shown.session, *shown.crawler )
                                  .filteredData()
                                  ->getNbLine()
                              == 1_lcount;
            } ) );
        }
    }

    WHEN( "the selection in the Filtered View is added without apply now" )
    {
        CrawlerAccess::search( *shown.crawler, QStringLiteral( "ERROR|WARN" ) );
        REQUIRE( openLogFileOf( *shown.session, *shown.crawler ).filteredData()->getNbLine()
                 == 2_lcount );
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
            REQUIRE( openLogFileOf( *shown.session, *shown.crawler ).filteredData()->getNbLine()
                     == 2_lcount );
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
    REQUIRE( openLogFileOf( *shown.session, *shown.crawler ).filteredData()->getNbLine()
             == 1_lcount );

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
            REQUIRE( openLogFileOf( *shown.session, *shown.crawler ).filteredData()->getNbLine()
                     == 1_lcount );
        }
    }

    WHEN( "the Filters panel is refreshed" )
    {
        shown.window->findChild<FiltersPanel*>()->refreshFilters();
        QTest::qWait( 100 );

        THEN( "the Search pattern is as it was" )
        {
            REQUIRE( CrawlerAccess::searchPattern( *shown.crawler ) == QStringLiteral( "ERROR" ) );
            REQUIRE( openLogFileOf( *shown.session, *shown.crawler ).filteredData()->getNbLine()
                     == 1_lcount );
        }
    }
}

namespace {

// Chooses the entry of the dialog's Group Chooser with the text.
void chooseGroup( AddPredefinedFilterDialog& dialog, const QString& text )
{
    auto* combo = dialog.groupChooser()->comboBox();
    const auto row = combo->findText( text );
    REQUIRE( row >= 0 );
    combo->setCurrentIndex( row );
}

QStringList filterNamesOf( const PredefinedFilterSet& group )
{
    QStringList names;
    for ( const auto& filter : group.filters() ) {
        names.append( filter.name );
    }
    return names;
}

} // namespace

SCENARIO( "The Add as Predefined Filter dialog proposes a name free in the group chosen",
          "[ui][predefinedfilters][addpredefinedfilter][groupchooser]" )
{
    const KeptFilterGroups kept;
    KeptFilterGroups::store( { groupOf( QStringLiteral( "Other" ), QStringLiteral( "WARN" ) ) } );
    auto& collection = PredefinedFiltersCollection::getSynced();
    collection.addFilter( defaultFilterSetId(),
                          PredefinedFilter{ SelectedText, QStringLiteral( "x" ), false } );
    collection.save();

    AddPredefinedFilterDialog dialog( SelectedText );
    auto* name = childOf<QLineEdit>( dialog, QStringLiteral( "filterName" ) );
    REQUIRE( name->text() == SelectedText + QStringLiteral( " (2)" ) );

    WHEN( "a group without a filter of the name is chosen, the name still as proposed" )
    {
        chooseGroup( dialog, QStringLiteral( "Other" ) );

        THEN( "the name is proposed again, free in that group" )
        {
            REQUIRE( name->text() == SelectedText );
            REQUIRE( okButton( dialog )->isEnabled() );
        }
    }

    WHEN( "the group's tooltip is read" )
    {
        auto* combo = dialog.groupChooser()->comboBox();
        const auto tooltip
            = combo->itemData( combo->findText( QStringLiteral( "Other" ) ), Qt::ToolTipRole )
                  .toString();

        THEN( "it lists the filters the group holds" )
        {
            REQUIRE( tooltip == QStringLiteral( "Other" ) );
        }
    }
}

SCENARIO( "Add as Predefined Filter puts a filter into a new Filter Group of the user's own",
          "[ui][predefinedfilters][addpredefinedfilter][groupchooser]" )
{
    const KeptFilterGroups kept;
    KeptFilterGroups::store( {} );
    const auto groupsBefore = PredefinedFiltersCollection::getSynced().filterSets().size();

    AddPredefinedFilterDialog dialog( SelectedText );
    showUntilExposed( dialog );
    auto* newName = childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) );
    REQUIRE( newName->isHidden() );

    WHEN( "New Filter Group... is chosen" )
    {
        chooseGroup( dialog, NewFilterGroup );

        THEN( "the name field shows, with the first free name" )
        {
            REQUIRE( newName->isVisible() );
            REQUIRE( newName->text() == QStringLiteral( "New Filter Group" ) );
            REQUIRE( okButton( dialog )->isEnabled() );
        }

        AND_WHEN( "the group is named and OK is clicked" )
        {
            newName->setText( QStringLiteral( "Modem" ) );
            clickOk( dialog );

            THEN( "the group is made with the filter, after the user's other groups" )
            {
                const auto groups = PredefinedFiltersCollection::getSynced().filterSets();
                REQUIRE( groups.size() == groupsBefore + 1 );
                REQUIRE( groups.back().name() == QStringLiteral( "Modem" ) );
                REQUIRE( filterNamesOf( groups.back() ) == QStringList{ SelectedText } );
                REQUIRE( dialog.chosenGroupId() == groups.back().id() );
            }
        }

        AND_WHEN( "the name field is emptied" )
        {
            newName->setText( QString{} );

            THEN( "OK is disabled, and the dialog says why" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
                REQUIRE_FALSE( dialog.warning().isEmpty() );
            }
        }

        AND_WHEN( "Cancel is clicked" )
        {
            dialog.reject();

            THEN( "no group was made" )
            {
                REQUIRE( PredefinedFiltersCollection::getSynced().filterSets().size()
                         == groupsBefore );
            }
        }
    }
}

SCENARIO( "Add as Predefined Filter puts a filter into a Team group, which it publishes",
          "[ui][predefinedfilters][addpredefinedfilter][groupchooser]" )
{
    using logsquirl::teamfolder::GroupAction;
    using logsquirl::teamfolder::PublishRequest;

    const KeptFilterGroups kept;
    KeptFilterGroups::store( {} );
    const auto ownBefore = PredefinedFiltersCollection::getSynced().filterSets();

    auto crashes = PredefinedFilterSet::createNewSet( QStringLiteral( "Crashes" ) );
    crashes.addFilter(
        PredefinedFilter{ QStringLiteral( "Fatal" ), QStringLiteral( "FATAL" ), false } );
    const QHash<QString, QString> revisions{ { crashes.id(), QStringLiteral( "rev-1" ) } };

    AddPredefinedFilterDialog dialog( SelectedText );
    QSignalSpy published( &dialog, &AddPredefinedFilterDialog::publishRequested );

    GIVEN( "Team groups that can be changed" )
    {
        dialog.showTeamGroups( { crashes }, true, revisions );
        showUntilExposed( dialog );
        auto* hint = childOf<QLabel>( dialog, QStringLiteral( "teamHint" ) );

        THEN( "they are offered under a heading of their own, with New Team Filter Group..., and "
              "the Default Filter Group is still chosen" )
        {
            auto* combo = dialog.groupChooser()->comboBox();
            REQUIRE( combo->findText( QStringLiteral( "Team Filter Groups" ) ) >= 0 );
            REQUIRE( combo->findText( QStringLiteral( "Crashes" ) ) >= 0 );
            REQUIRE( combo->findText( NewTeamFilterGroup ) >= 0 );
            REQUIRE( dialog.chosenGroupId() == defaultFilterSetId() );
            REQUIRE( hint->isHidden() );
        }

        WHEN( "the Team group is chosen and OK clicked" )
        {
            chooseGroup( dialog, QStringLiteral( "Crashes" ) );
            REQUIRE( hint->isVisible() );
            clickOk( dialog );

            THEN( "the group with the filter is published, based on the revision it was given "
                  "with, and none of the user's own changed" )
            {
                REQUIRE( published.count() == 1 );
                const auto requests = published.at( 0 ).at( 0 ).value<QList<PublishRequest>>();
                REQUIRE( requests.size() == 1 );
                REQUIRE( requests[ 0 ].action == GroupAction::Change );
                REQUIRE( requests[ 0 ].id == crashes.id() );
                REQUIRE( requests[ 0 ].baseRevision == QStringLiteral( "rev-1" ) );
                const auto* group = logsquirl::teamfolder::groupOfKind<PredefinedFilterSet>(
                    requests[ 0 ].group );
                REQUIRE( group != nullptr );
                REQUIRE( filterNamesOf( *group )
                         == QStringList{ QStringLiteral( "Fatal" ), SelectedText } );
                REQUIRE( dialog.chosenGroupId() == crashes.id() );
                REQUIRE( PredefinedFiltersCollection::getSynced().filterSets().size()
                         == ownBefore.size() );
                REQUIRE( storedGroup( defaultFilterSetId() ).filters().isEmpty() );
            }
        }

        WHEN( "New Team Filter Group... is chosen, named, and OK clicked" )
        {
            chooseGroup( dialog, NewTeamFilterGroup );
            REQUIRE( hint->isVisible() );
            childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )
                ->setText( QStringLiteral( "Modem" ) );
            clickOk( dialog );

            THEN( "the new group with the filter is published as a new Team group" )
            {
                REQUIRE( published.count() == 1 );
                const auto requests = published.at( 0 ).at( 0 ).value<QList<PublishRequest>>();
                REQUIRE( requests.size() == 1 );
                REQUIRE( requests[ 0 ].action == GroupAction::Add );
                REQUIRE( requests[ 0 ].name == QStringLiteral( "Modem" ) );
                const auto* group = logsquirl::teamfolder::groupOfKind<PredefinedFilterSet>(
                    requests[ 0 ].group );
                REQUIRE( group != nullptr );
                REQUIRE( filterNamesOf( *group ) == QStringList{ SelectedText } );
                REQUIRE( dialog.chosenGroupId() == requests[ 0 ].id );
                REQUIRE( PredefinedFiltersCollection::getSynced().filterSets().size()
                         == ownBefore.size() );
            }
        }

        WHEN( "New Team Filter Group... is given a Team group's name" )
        {
            chooseGroup( dialog, NewTeamFilterGroup );
            childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )
                ->setText( QStringLiteral( "Crashes" ) );

            THEN( "OK is disabled, and the dialog says why" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
                REQUIRE( dialog.warning().contains( QStringLiteral( "Crashes" ) ) );
            }
        }

        WHEN( "a group of the user's own is chosen and OK clicked" )
        {
            clickOk( dialog );

            THEN( "nothing is published" )
            {
                REQUIRE( published.isEmpty() );
                REQUIRE( storedGroup( defaultFilterSetId() ).filters().size() == 1 );
            }
        }

        WHEN( "the Team group is proposed, as the group chosen last" )
        {
            dialog.proposeGroup( crashes.id() );

            THEN( "it is chosen" )
            {
                REQUIRE( dialog.chosenGroupId() == crashes.id() );
            }
        }
    }

    GIVEN( "Team groups that cannot be changed" )
    {
        dialog.showTeamGroups( { crashes }, false, revisions );
        dialog.proposeGroup( crashes.id() );

        THEN( "none of them can be chosen, and there is no New Team Filter Group..." )
        {
            REQUIRE( dialog.chosenGroupId() == defaultFilterSetId() );
            REQUIRE( dialog.groupChooser()->comboBox()->findText( NewTeamFilterGroup ) < 0 );
        }
    }
}

SCENARIO( "Add as Predefined Filter proposes the group chosen last, the Default Filter Group "
          "once it is gone",
          "[ui][predefinedfilters][addpredefinedfilter][groupchooser]" )
{
    const KeptFilterGroups kept;
    const auto other = groupOf( QStringLiteral( "Other" ), QStringLiteral( "WARN" ) );
    KeptFilterGroups::store( { other } );

    const AddFilterWindow shown;
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    // Opens the dialog from the context menu, and reads which group it chose.
    const auto proposedGroup = [ & ] {
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        QString proposed;
        auto* entry = contextMenuEntry( *menu, AddAsPredefinedFilter );
        QTimer::singleShot( 0, [ & ] {
            auto* dialog
                = qobject_cast<AddPredefinedFilterDialog*>( QApplication::activeModalWidget() );
            REQUIRE( dialog != nullptr );
            proposed = dialog->chosenGroupId();
            dialog->reject();
        } );
        entry->trigger();
        return proposed;
    };

    WHEN( "a filter is added to the other group" )
    {
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& dialog ) {
            chooseGroup( dialog, QStringLiteral( "Other" ) );
        } );

        THEN( "it is remembered, and proposed the next time" )
        {
            REQUIRE( Configuration::getSynced().lastFilterGroupId() == other.id() );
            REQUIRE( proposedGroup() == other.id() );
        }

        AND_WHEN( "the other group is gone" )
        {
            KeptFilterGroups::store( {} );

            THEN( "the Default Filter Group is proposed" )
            {
                REQUIRE( proposedGroup() == defaultFilterSetId() );
            }
        }
    }

    WHEN( "a filter is added to a new group" )
    {
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& dialog ) {
            chooseGroup( dialog, NewFilterGroup );
        } );

        THEN( "the new group is proposed the next time" )
        {
            const auto groups = PredefinedFiltersCollection::getSynced().filterSets();
            REQUIRE( groups.back().name() == QStringLiteral( "New Filter Group" ) );
            REQUIRE( proposedGroup() == groups.back().id() );
        }
    }
}

SCENARIO( "A filter added to a Team group reaches the team and every window; a colleague's "
          "change meanwhile is asked about",
          "[ui][predefinedfilters][addpredefinedfilter][groupchooser][teamfolder]" )
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
    const KeptFilterGroups kept;
    KeptFilterGroups::store( {} );

    // The team's server, holding a Filter Group a colleague published.
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto server = root.filePath( "server.git" );
    QProcess git;
    git.start( "git", { "init", "--quiet", "--bare", server } );
    REQUIRE( git.waitForFinished( SyncTimeoutMs ) );
    REQUIRE( git.exitCode() == 0 );
    const auto teamPolicy
        = teamfolder_testing::policyFor( QUrl::fromLocalFile( server ).toString() );

    auto crashes = PredefinedFilterSet::createNewSet( QStringLiteral( "Crashes" ) );
    crashes.addFilter( { QStringLiteral( "Fatal" ), QStringLiteral( "FATAL" ), false } );
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
    publish( PublishRequest::forGroup( crashes, GroupAction::Add ) );

    // Two windows of a Session whose Team Folder has synced.
    const auto member = std::make_shared<TeamFolder>( root.filePath( "member" ) );
    const AddFilterWindow shown( member, teamPolicy );
    REQUIRE( settled( *member ) );
    REQUIRE( member->filterGroups().size() == 1 );
    const auto second = std::make_unique<MainWindow>(
        WindowSession{ shown.session, QStringLiteral( "Second" ), 1 },
        std::make_shared<logsquirl::plugins::ApplicationPlugins>() );
    showUntilExposed( *second );
    shown.window->activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( shown.window.get(), 5000 ) );
    const auto listedEverywhere = [ & ]( const QString& name ) {
        for ( const auto* window : { shown.window.get(), second.get() } ) {
            auto* tree = window->findChild<FiltersPanel*>()->findChild<QTreeWidget*>();
            if ( tree == nullptr || !listsFilter( *tree, name ) ) {
                return false;
            }
        }
        return true;
    };
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    WHEN( "the selection is added to the Team group" )
    {
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& dialog ) {
            chooseGroup( dialog, QStringLiteral( "Crashes" ) );
        } );

        THEN( "the colleague gets it, and the Filters panel of both windows lists it" )
        {
            REQUIRE(
                waitUiState( [ & ] { return listedEverywhere( SelectedText ); }, SyncTimeoutMs ) );
            REQUIRE( settled( *member ) );
            colleague.sync();
            REQUIRE( settled( colleague ) );
            REQUIRE( colleague.filterGroups().size() == 1 );
            REQUIRE( filterNamesOf( colleague.filterGroups().front() )
                     == QStringList{ QStringLiteral( "Fatal" ), SelectedText } );
            REQUIRE( PredefinedFiltersCollection::getSynced().filterSets().size() == 1 );
        }
    }

    WHEN( "the selection is added to a new Team group" )
    {
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& dialog ) {
            chooseGroup( dialog, NewTeamFilterGroup );
            childOf<QLineEdit>( dialog, QStringLiteral( "newGroupName" ) )
                ->setText( QStringLiteral( "Modem" ) );
        } );

        THEN( "the colleague gets the group, and both windows list the filter" )
        {
            REQUIRE(
                waitUiState( [ & ] { return listedEverywhere( SelectedText ); }, SyncTimeoutMs ) );
            REQUIRE( settled( *member ) );
            colleague.sync();
            REQUIRE( settled( colleague ) );
            REQUIRE( colleague.filterGroups().size() == 2 );
            REQUIRE( colleague.filterGroups()[ 1 ].name() == QStringLiteral( "Modem" ) );
        }
    }

    WHEN( "the colleague changed the Team group since the last sync, and the selection is "
          "added to it" )
    {
        auto theirs = crashes;
        theirs.addFilter( { QStringLiteral( "Theirs" ), QStringLiteral( "THEIRS" ), false } );
        auto change = PublishRequest::forGroup( theirs, GroupAction::Change );
        change.baseRevision = colleague.filterGroupRevision( crashes.id() );
        publish( change );

        QSignalSpy finished( member.get(), &TeamFolder::publishFinished );
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        addInDialog( *menu, []( AddPredefinedFilterDialog& dialog ) {
            chooseGroup( dialog, QStringLiteral( "Crashes" ) );
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

        THEN( "the window asks as the Predefined Filters dialog's publish does, and Keep mine "
              "publishes the filter over their change" )
        {
            REQUIRE( waitUiState( [ & ] { return modals.unanswered() == 0; }, SyncTimeoutMs ) );
            REQUIRE( modals.messages().size() == 1 );
            REQUIRE( modals.messages()[ 0 ].contains(
                QStringLiteral( "Somebody else changed the Team group \"Crashes\"" ) ) );
            REQUIRE( modals.unexpectedMessageBoxes() == 0 );
            REQUIRE(
                waitUiState( [ & ] { return listedEverywhere( SelectedText ); }, SyncTimeoutMs ) );
            REQUIRE( settled( *member ) );
            colleague.sync();
            REQUIRE( settled( colleague ) );
            REQUIRE( filterNamesOf( colleague.filterGroups().front() )
                     == QStringList{ QStringLiteral( "Fatal" ), SelectedText } );
        }
    }
}
