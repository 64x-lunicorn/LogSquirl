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

// Add as Predefined Filter's Also highlight it (#986): checked, OK adds a
// Highlighter with the filter's pattern too -- escaped when the filter reads
// plain text, so that it colors what the filter finds -- to the Highlighter
// Set chosen, which it activates; the preview colors its Log Line as the
// Highlighter will. The dialog opens with the check as it was last, and
// proposes the set chosen last.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <memory>

#include "abstractlogview.h"
#include "addpredefinedfilterdialog.h"
#include "addtogrouplayout.h"
#include "applicationplugins.h"
#include "configuration.h"
#include "context_menu_entries.h"
#include "crawler_children.h"
#include "crawlerwidget.h"
#include "filterpreviewsearch.h"
#include "groupchooser.h"
#include "highlighterpresets.h"
#include "highlighterset.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "openlogfile.h"
#include "predefinedfilters.h"
#include "session.h"
#include "shown_widget.h"
#include "teamfolder.h"
#include "test_policies.h"
#include "test_utils.h"

struct AddFilterHighlightTest {};

template <>
struct AbstractLogView::access_by<AddFilterHighlightTest> {
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

using ViewAccess = AbstractLogView::access_by<AddFilterHighlightTest>;

const auto AddAsPredefinedFilter = QStringLiteral( "Add as Predefined Filter..." );
// Plain text with regex characters: read as a regex it would find the
// lookalike too.
const auto SelectedText = QStringLiteral( "a.b (x)" );
const auto TheLogLine = QStringLiteral( "modem a.b (x) ERROR done" );
const auto LookalikeLogLine = QStringLiteral( "modem axb x WARN done" );
const auto OtherLogLine = QStringLiteral( "other line" );
const auto NewHighlighterSet = QStringLiteral( "New Highlighter Set..." );
const auto NewTeamHighlighterSet = QStringLiteral( "New Team Highlighter Set..." );
// The strong preset whose background nothing else in the views is painted
// with.
constexpr int StrongVioletPreset = 17;

// The Filter Groups, the Highlighter Sets and which of them are active, and
// what the dialog remembers, stored again as they were when this goes.
class KeptGroupsAndSets {
public:
    KeptGroupsAndSets()
        : filterSets_( PredefinedFiltersCollection::getSynced().filterSets() )
        , highlighterSets_( HighlighterSetCollection::getSynced().highlighterSets() )
        , activeSetIds_( HighlighterSetCollection::get().activeSetIds() )
        , lastFilterGroupId_( Configuration::getSynced().lastFilterGroupId() )
        , lastHighlighterSetId_( Configuration::get().lastHighlighterSetId() )
        , alsoHighlights_( Configuration::get().addFilterAlsoHighlights() )
    {
    }

    ~KeptGroupsAndSets()
    {
        auto& filters = PredefinedFiltersCollection::getSynced();
        filters.setFilterSets( filterSets_ );
        filters.save();
        storeSets( highlighterSets_, activeSetIds_ );
        auto& config = Configuration::getSynced();
        config.setLastFilterGroupId( lastFilterGroupId_ );
        config.setLastHighlighterSetId( lastHighlighterSetId_ );
        config.setAddFilterAlsoHighlights( alsoHighlights_ );
        config.save();
    }

    KeptGroupsAndSets( const KeptGroupsAndSets& ) = delete;
    KeptGroupsAndSets& operator=( const KeptGroupsAndSets& ) = delete;

    static void storeSets( const QList<HighlighterSet>& sets, const QStringList& activeSetIds )
    {
        auto& collection = HighlighterSetCollection::getSynced();
        collection.setHighlighterSets( sets );
        collection.setTeamHighlighterSets( {} );
        collection.deactivateAll();
        for ( const auto& setId : activeSetIds ) {
            collection.activateSet( setId );
        }
        collection.save();
    }

    // No Filter Group but the Default one, empty; the sets given, none
    // active; nothing remembered.
    static void startWith( const QList<HighlighterSet>& sets )
    {
        auto& filters = PredefinedFiltersCollection::getSynced();
        filters.setFilterSets( {} );
        filters.save();
        storeSets( sets, {} );
        auto& config = Configuration::getSynced();
        config.setLastFilterGroupId( {} );
        config.setLastHighlighterSetId( {} );
        config.setAddFilterAlsoHighlights( false );
        config.save();
    }

private:
    QList<PredefinedFilterSet> filterSets_;
    QList<HighlighterSet> highlighterSets_;
    QStringList activeSetIds_;
    QString lastFilterGroupId_;
    QString lastHighlighterSetId_;
    bool alsoHighlights_;
};

HighlighterSet setOf( const QString& name, const QString& pattern )
{
    auto set = HighlighterSet::createNewSet( name );
    set.addHighlighter( Highlighter( pattern, false, true, Qt::black, Qt::yellow ) );
    return set;
}

// The set of the id as stored now.
HighlighterSet storedSet( const QString& setId )
{
    for ( const auto& set : HighlighterSetCollection::getSynced().highlighterSets() ) {
        if ( set.id() == setId ) {
            return set;
        }
    }
    FAIL( "no set " << setId.toStdString() );
    return {};
}

QList<PredefinedFilter> storedFilters()
{
    for ( const auto& set : PredefinedFiltersCollection::getSynced().filterSets() ) {
        if ( set.id() == defaultFilterSetId() ) {
            return set.filters();
        }
    }
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

void chooseSet( AddPredefinedFilterDialog& dialog, const QString& text )
{
    auto* combo = dialog.highlighterSetChooser()->comboBox();
    const auto row = combo->findText( text );
    REQUIRE( row >= 0 );
    combo->setCurrentIndex( row );
}

// Whether the form row of the field shows.
bool rowShows( AddPredefinedFilterDialog& dialog, QWidget* field )
{
    return dialog.addToGroupLayout().form()->isRowVisible( field );
}

bool showsColor( QWidget& view, const QColor& color )
{
    const auto image = view.grab().toImage();
    for ( auto y = 0; y < image.height(); ++y ) {
        for ( auto x = 0; x < image.width(); ++x ) {
            if ( image.pixelColor( x, y ).rgb() == color.rgb() ) {
                return true;
            }
        }
    }
    return false;
}

// A window showing a Log File of the Log Line, its lookalike and another.
struct HighlightWindow {
    HighlightWindow()
    {
        REQUIRE( dir.isValid() );
        const auto path = dir.filePath( QStringLiteral( "highlight.log" ) );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write(
            ( TheLogLine + u'\n' + LookalikeLogLine + u'\n' + OtherLogLine + u'\n' ).toUtf8() );
        file.close();

        session = std::make_shared<Session>( testSettingsPolicies(),
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

    ~HighlightWindow()
    {
        window.reset();
        QTest::qWait( 50 );
    }

    HighlightWindow( const HighlightWindow& ) = delete;
    HighlightWindow& operator=( const HighlightWindow& ) = delete;

    AbstractLogView& textView() const
    {
        return textViewOf( *crawler );
    }

    QTemporaryDir dir;
    std::shared_ptr<Session> session;
    std::unique_ptr<MainWindow> window;
    CrawlerWidget* crawler = nullptr;
};

// Selects the text within the first Log Line of the Text View, chooses Add
// as Predefined Filter... and in the dialog it opens does what fill does,
// then clicks OK.
void addInDialog( const HighlightWindow& shown,
                  const std::function<void( AddPredefinedFilterDialog& )>& fill )
{
    auto& view = shown.textView();
    const auto start = TheLogLine.indexOf( SelectedText );
    ViewAccess::selectPortion( view, 0_lnum, LineColumn{ start },
                               LineColumn{ start + SelectedText.size() - 1 } );
    const auto menu = ViewAccess::contextMenu( view );
    auto* entry = contextMenuEntry( *menu, AddAsPredefinedFilter );
    REQUIRE( entry != nullptr );
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

SCENARIO( "The Highlighter of a Predefined Filter reads its pattern as the filter does",
          "[ui][predefinedfilters][addpredefinedfilter][highlighters]" )
{
    GIVEN( "a filter of plain text with regex characters" )
    {
        const PredefinedFilter filter{ SelectedText, SelectedText, false };
        const auto pattern = AddPredefinedFilterDialog::highlighterPattern( filter );

        THEN( "the Highlighter's pattern is it escaped, coloring exactly what the filter finds" )
        {
            REQUIRE( pattern == QStringLiteral( "a\\.b\\ \\(x\\)" ) );
            const Highlighter highlighter( pattern, true, true, Qt::black, Qt::yellow );
            logsquirl::vector<HighlightedMatch> matches;
            REQUIRE( highlighter.colorLine( TheLogLine, matches )
                     == HighlighterMatchType::WordMatch );
            REQUIRE( matches.size() == 1 );
            REQUIRE( matches.front().startColumn().get() == TheLogLine.indexOf( SelectedText ) );
            REQUIRE( matches.front().size().get() == SelectedText.size() );
            matches.clear();
            REQUIRE( highlighter.colorLine( LookalikeLogLine, matches )
                     == HighlighterMatchType::NoMatch );
        }
    }

    GIVEN( "a filter of a regex" )
    {
        const PredefinedFilter filter{ QStringLiteral( "Modem" ), QStringLiteral( "a.b \\(x\\)" ),
                                       true };

        THEN( "the Highlighter's pattern is the regex as it is" )
        {
            REQUIRE( AddPredefinedFilterDialog::highlighterPattern( filter ) == filter.pattern );
        }
    }
}

SCENARIO( "Also highlight it shows the Highlighter's rows only while it is checked",
          "[ui][predefinedfilters][addpredefinedfilter][highlighters]" )
{
    const KeptGroupsAndSets kept;
    const auto network = setOf( QStringLiteral( "Network" ), QStringLiteral( "WARN" ) );
    KeptGroupsAndSets::startWith( { network } );

    AddPredefinedFilterDialog dialog( SelectedText );
    showUntilExposed( dialog );
    auto* check = childOf<QCheckBox>( dialog, QStringLiteral( "alsoHighlight" ) );
    auto* setChooser = dialog.highlighterSetChooser();

    THEN( "it is unchecked, the only line it adds, between the group and the preview" )
    {
        REQUIRE_FALSE( check->isChecked() );
        REQUIRE_FALSE( dialog.alsoHighlights() );
        REQUIRE( check->isVisible() );
        REQUIRE_FALSE( rowShows( dialog, setChooser ) );
        REQUIRE_FALSE(
            childOf<QPushButton>( dialog, QStringLiteral( "foreColorButton" ) )->isVisible() );
        REQUIRE_FALSE(
            childOf<QRadioButton>( dialog, QStringLiteral( "colorWholeLine" ) )->isVisible() );
        const auto sections = dialog.addToGroupLayout().rowSections();
        REQUIRE( sections.indexOf( AddToGroupLayout::Section::Also )
                 > sections.lastIndexOf( AddToGroupLayout::Section::Where ) );
        REQUIRE( sections.lastIndexOf( AddToGroupLayout::Section::Also )
                 < sections.indexOf( AddToGroupLayout::Section::Preview ) );
    }

    WHEN( "it is checked" )
    {
        check->setChecked( true );

        THEN( "the colors, what is colored and the Highlighter Set show, the user's first own "
              "set proposed" )
        {
            REQUIRE( rowShows( dialog, setChooser ) );
            REQUIRE(
                childOf<QPushButton>( dialog, QStringLiteral( "foreColorButton" ) )->isVisible() );
            REQUIRE(
                childOf<QPushButton>( dialog, QStringLiteral( "backColorButton" ) )->isVisible() );
            REQUIRE(
                childOf<QRadioButton>( dialog, QStringLiteral( "colorWholeLine" ) )->isChecked() );
            REQUIRE(
                childOf<QRadioButton>( dialog, QStringLiteral( "colorOnlyMatch" ) )->isVisible() );
            REQUIRE( dialog.chosenHighlighterSetId() == network.id() );
            REQUIRE( setChooser->comboBox()->findText( NewHighlighterSet ) >= 0 );
        }

        THEN( "the colors proposed are those a new Highlighter of the set takes" )
        {
            const auto& preset = highlighterColorPresets()[ 1 ];
            REQUIRE( dialog.highlighter().foreColor() == preset.foreColor );
            REQUIRE( dialog.highlighter().backColor() == preset.backColor );
        }

        AND_WHEN( "New Highlighter Set... is chosen and its name emptied" )
        {
            chooseSet( dialog, NewHighlighterSet );
            dialog.highlighterSetChooser()->nameEdit()->setText( QString{} );

            THEN( "OK is disabled, and the dialog says why" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
                REQUIRE( dialog.warning().contains( QStringLiteral( "Highlighter Set" ) ) );
            }

            AND_WHEN( "the check is off again" )
            {
                check->setChecked( false );

                THEN( "OK is enabled: only the filter is added" )
                {
                    REQUIRE( okButton( dialog )->isEnabled() );
                    REQUIRE( dialog.warning().isEmpty() );
                }
            }
        }

        AND_WHEN( "the new set is named as one that exists" )
        {
            chooseSet( dialog, NewHighlighterSet );
            dialog.highlighterSetChooser()->nameEdit()->setText( QStringLiteral( "Network" ) );

            THEN( "OK is disabled, and the dialog says why" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
                REQUIRE( dialog.warning().contains( QStringLiteral( "Network" ) ) );
            }
        }

        AND_WHEN( "the pattern has Field Terms" )
        {
            childOf<QLineEdit>( dialog, QStringLiteral( "filterPattern" ) )
                ->setText( QStringLiteral( "level:\"ERROR\"" ) );

            THEN( "OK is disabled while the check is on: a Highlighter knows no fields" )
            {
                REQUIRE_FALSE( okButton( dialog )->isEnabled() );
                REQUIRE( dialog.warning().contains( QStringLiteral( "Field Terms" ) ) );
                check->setChecked( false );
                REQUIRE( okButton( dialog )->isEnabled() );
            }
        }
    }
}

SCENARIO( "With Also highlight it, the preview colors its Log Line as the Highlighter will",
          "[ui][predefinedfilters][addpredefinedfilter][highlighters]" )
{
    const KeptGroupsAndSets kept;
    KeptGroupsAndSets::startWith(
        { setOf( QStringLiteral( "Network" ), QStringLiteral( "WARN" ) ) } );
    const HighlightWindow shown;

    addInDialog( shown, [ & ]( AddPredefinedFilterDialog& dialog ) {
        auto* preview = dialog.addToGroupLayout().preview();
        REQUIRE( waitUiState( [ & ] {
            return dialog.previewResult().state == FilterPreviewSearch::Result::State::Done;
        } ) );
        // Unchecked: marked as a Match, in no Highlighter's colors.
        REQUIRE( dialog.previewResult().matchCount == 1_lcount );
        REQUIRE( preview->markedTexts() == QStringList{ SelectedText } );
        REQUIRE_FALSE( preview->coloring().has_value() );

        auto* check = childOf<QCheckBox>( dialog, QStringLiteral( "alsoHighlight" ) );
        check->setChecked( true );
        childOf<QToolButton>( dialog,
                              QStringLiteral( "colorPresetButton%1" ).arg( StrongVioletPreset ) )
            ->click();
        const auto& violet = highlighterColorPresets()[ StrongVioletPreset ];
        REQUIRE( preview->coloring().has_value() );
        REQUIRE( preview->coloring()->backColor == violet.backColor );
        REQUIRE( preview->coloring()->wholeLine );
        REQUIRE( preview->text().contains( violet.backColor.name() ) );
        // Still one preview, still what the filter finds.
        REQUIRE( dialog.previewResult().matchCount == 1_lcount );

        childOf<QRadioButton>( dialog, QStringLiteral( "colorOnlyMatch" ) )->setChecked( true );
        REQUIRE_FALSE( preview->coloring()->wholeLine );
        REQUIRE( preview->markedTexts() == QStringList{ SelectedText } );

        check->setChecked( false );
        REQUIRE_FALSE( preview->coloring().has_value() );
        REQUIRE_FALSE( preview->text().contains( violet.backColor.name() ) );
        // Only the filter, then.
    } );
}

SCENARIO( "OK of Add as Predefined Filter adds a Highlighter too while Also highlight it is "
          "checked",
          "[ui][predefinedfilters][addpredefinedfilter][highlighters]" )
{
    const KeptGroupsAndSets kept;
    const auto network = setOf( QStringLiteral( "Network" ), QStringLiteral( "WARN" ) );
    KeptGroupsAndSets::startWith( { network } );
    const HighlightWindow shown;
    const auto& violet = highlighterColorPresets()[ StrongVioletPreset ];

    WHEN( "the check is off" )
    {
        addInDialog( shown, []( AddPredefinedFilterDialog& ) {} );

        THEN( "only the Predefined Filter is added" )
        {
            REQUIRE( storedFilters().size() == 1 );
            REQUIRE( storedSet( network.id() ).highlighters().size() == 1 );
            REQUIRE( HighlighterSetCollection::getSynced().highlighterSets().size() == 1 );
            REQUIRE( HighlighterSetCollection::get().activeSetIds().isEmpty() );
        }
    }

    WHEN( "the check is on, with a preset, only the matched text colored, the set proposed" )
    {
        addInDialog( shown, [ & ]( AddPredefinedFilterDialog& dialog ) {
            childOf<QCheckBox>( dialog, QStringLiteral( "alsoHighlight" ) )->setChecked( true );
            childOf<QToolButton>(
                dialog, QStringLiteral( "colorPresetButton%1" ).arg( StrongVioletPreset ) )
                ->click();
            childOf<QRadioButton>( dialog, QStringLiteral( "colorOnlyMatch" ) )->setChecked( true );
        } );

        THEN( "the filter is added, and the Highlighter at the end of the set, a copy of the "
              "pattern escaped, in the colors chosen" )
        {
            const auto filters = storedFilters();
            REQUIRE( filters.size() == 1 );
            REQUIRE( filters.front().pattern == SelectedText );
            REQUIRE_FALSE( filters.front().useRegex );

            const auto highlighters = storedSet( network.id() ).highlighters();
            REQUIRE( highlighters.size() == 2 );
            const auto& added = highlighters.back();
            REQUIRE( added.pattern()
                     == AddPredefinedFilterDialog::highlighterPattern( filters.front() ) );
            REQUIRE( added.highlightOnlyMatch() );
            REQUIRE( added.foreColor() == violet.foreColor );
            REQUIRE( added.backColor() == violet.backColor );
        }

        THEN( "the set is active and the Log File is colored at once" )
        {
            REQUIRE( HighlighterSetCollection::get().activeSetIds().contains( network.id() ) );
            // The selection, the matched text itself, is painted over it.
            shown.textView().selectAndDisplayLine( 2_lnum );
            REQUIRE( waitUiState(
                [ & ] { return showsColor( shown.textView(), violet.backColor ); }, 5000 ) );
        }

        THEN( "the check and the set are remembered" )
        {
            REQUIRE( Configuration::getSynced().addFilterAlsoHighlights() );
            REQUIRE( Configuration::get().lastHighlighterSetId() == network.id() );
        }
    }

    WHEN( "the check is on and New Highlighter Set... is chosen and named" )
    {
        addInDialog( shown, [ & ]( AddPredefinedFilterDialog& dialog ) {
            childOf<QCheckBox>( dialog, QStringLiteral( "alsoHighlight" ) )->setChecked( true );
            chooseSet( dialog, NewHighlighterSet );
            dialog.highlighterSetChooser()->nameEdit()->setText( QStringLiteral( "Modem" ) );
        } );

        THEN( "the set is made with the Highlighter, after the user's other sets, and active" )
        {
            const auto sets = HighlighterSetCollection::getSynced().highlighterSets();
            REQUIRE( sets.size() == 2 );
            REQUIRE( sets.back().name() == QStringLiteral( "Modem" ) );
            REQUIRE( sets.back().highlighters().size() == 1 );
            REQUIRE( HighlighterSetCollection::get().activeSetIds().contains( sets.back().id() ) );
            REQUIRE( Configuration::get().lastHighlighterSetId() == sets.back().id() );
        }
    }
}

SCENARIO( "Add as Predefined Filter opens Also highlight it as it was last, with the set chosen "
          "last",
          "[ui][predefinedfilters][addpredefinedfilter][highlighters]" )
{
    const KeptGroupsAndSets kept;
    const auto network = setOf( QStringLiteral( "Network" ), QStringLiteral( "WARN" ) );
    const auto modem = setOf( QStringLiteral( "Modem" ), QStringLiteral( "INFO" ) );
    KeptGroupsAndSets::startWith( { network, modem } );
    const HighlightWindow shown;

    GIVEN( "a filter added with the check on, into the second set" )
    {
        addInDialog( shown, [ & ]( AddPredefinedFilterDialog& dialog ) {
            REQUIRE_FALSE( dialog.alsoHighlights() );
            dialog.setAlsoHighlight( true );
            chooseSet( dialog, QStringLiteral( "Modem" ) );
            childOf<QLineEdit>( dialog, QStringLiteral( "filterName" ) )
                ->setText( QStringLiteral( "First" ) );
        } );

        WHEN( "the dialog is opened again" )
        {
            bool reopenedChecked = false;
            QString proposedSet;
            addInDialog( shown, [ & ]( AddPredefinedFilterDialog& dialog ) {
                reopenedChecked = dialog.alsoHighlights();
                proposedSet = dialog.chosenHighlighterSetId();
                childOf<QLineEdit>( dialog, QStringLiteral( "filterName" ) )
                    ->setText( QStringLiteral( "Second" ) );
                dialog.setAlsoHighlight( false );
            } );

            THEN( "the check is on, the set chosen last proposed" )
            {
                REQUIRE( reopenedChecked );
                REQUIRE( proposedSet == modem.id() );
            }

            AND_WHEN( "it is opened once more, after OK with the check off" )
            {
                bool checkedAgain = true;
                addInDialog( shown, [ & ]( AddPredefinedFilterDialog& dialog ) {
                    checkedAgain = dialog.alsoHighlights();
                    childOf<QLineEdit>( dialog, QStringLiteral( "filterName" ) )
                        ->setText( QStringLiteral( "Third" ) );
                } );

                THEN( "the check is off" )
                {
                    REQUIRE_FALSE( checkedAgain );
                }
            }
        }
    }
}

SCENARIO( "Also highlight it puts the Highlighter into a Team Highlighter Set, which it publishes",
          "[ui][predefinedfilters][addpredefinedfilter][highlighters][groupchooser]" )
{
    using logsquirl::groupexchange::GroupKind;
    using logsquirl::teamfolder::GroupAction;
    using logsquirl::teamfolder::PublishRequest;

    const KeptGroupsAndSets kept;
    KeptGroupsAndSets::startWith( {} );
    const auto ownBefore = HighlighterSetCollection::getSynced().highlighterSets();
    const auto crashes = setOf( QStringLiteral( "Crashes" ), QStringLiteral( "FATAL" ) );
    const QHash<QString, QString> revisions{ { crashes.id(), QStringLiteral( "rev-1" ) } };
    auto& collection = HighlighterSetCollection::get();
    collection.setTeamHighlighterSets( { crashes } );

    AddPredefinedFilterDialog dialog( SelectedText );
    QSignalSpy published( &dialog, &AddPredefinedFilterDialog::publishRequested );
    dialog.showTeamHighlighterSets( { crashes }, true, revisions );
    dialog.setAlsoHighlight( true );
    showUntilExposed( dialog );

    THEN( "the Team sets are offered with New Team Highlighter Set..." )
    {
        auto* combo = dialog.highlighterSetChooser()->comboBox();
        REQUIRE( combo->findText( QStringLiteral( "Team Highlighter Sets" ) ) >= 0 );
        REQUIRE( combo->findText( QStringLiteral( "Crashes" ) ) >= 0 );
        REQUIRE( combo->findText( NewTeamHighlighterSet ) >= 0 );
    }

    WHEN( "the Team set is chosen and OK clicked" )
    {
        chooseSet( dialog, QStringLiteral( "Crashes" ) );
        REQUIRE( dialog.highlighterSetChooser()->teamHint()->isVisible() );
        clickOk( dialog );

        THEN( "the set with the Highlighter is published, based on its revision, and active "
              "at once; the user's own sets are as they were" )
        {
            REQUIRE( published.count() == 1 );
            const auto requests = published.at( 0 ).at( 0 ).value<QList<PublishRequest>>();
            REQUIRE( requests.size() == 1 );
            REQUIRE( requests[ 0 ].kind == GroupKind::Highlighter );
            REQUIRE( requests[ 0 ].action == GroupAction::Change );
            REQUIRE( requests[ 0 ].baseRevision == QStringLiteral( "rev-1" ) );
            const auto* set
                = logsquirl::teamfolder::groupOfKind<HighlighterSet>( requests[ 0 ].group );
            REQUIRE( set != nullptr );
            REQUIRE( set->highlighters().size() == 2 );
            REQUIRE( set->highlighters().back().pattern()
                     == AddPredefinedFilterDialog::highlighterPattern( dialog.filter() ) );
            REQUIRE( collection.activeSetIds().contains( crashes.id() ) );
            REQUIRE( collection.teamHighlighterSets().front().highlighters().size() == 2 );
            REQUIRE( HighlighterSetCollection::getSynced().highlighterSets().size()
                     == ownBefore.size() );
        }
    }

    WHEN( "New Team Highlighter Set... is chosen, named, and OK clicked" )
    {
        chooseSet( dialog, NewTeamHighlighterSet );
        dialog.highlighterSetChooser()->nameEdit()->setText( QStringLiteral( "Modem" ) );
        clickOk( dialog );

        THEN( "the new set with the Highlighter is published as a new Team set" )
        {
            REQUIRE( published.count() == 1 );
            const auto requests = published.at( 0 ).at( 0 ).value<QList<PublishRequest>>();
            REQUIRE( requests.size() == 1 );
            REQUIRE( requests[ 0 ].kind == GroupKind::Highlighter );
            REQUIRE( requests[ 0 ].action == GroupAction::Add );
            REQUIRE( requests[ 0 ].name == QStringLiteral( "Modem" ) );
            REQUIRE( dialog.chosenHighlighterSetId() == requests[ 0 ].id );
            REQUIRE( collection.activeSetIds().contains( requests[ 0 ].id ) );
        }
    }
}
