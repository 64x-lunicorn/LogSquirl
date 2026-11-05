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

// Highlight this... (#896): the context menu of the Text View and the
// Filtered View offers it for text selected within a Log Line, the dialog it
// opens proposes a Highlighter matching the selection, and OK adds it to a
// Highlighter Set and recolors the views at once.

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <memory>

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "context_menu_entries.h"
#include "crawlerwidget.h"
#include "highlighterpresets.h"
#include "highlighterset.h"
#include "highlightthisdialog.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

struct HighlightThisTest {};

template <>
struct AbstractLogView::access_by<HighlightThisTest> {
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
struct CrawlerWidget::access_by<HighlightThisTest> {
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

using ViewAccess = AbstractLogView::access_by<HighlightThisTest>;
using CrawlerAccess = CrawlerWidget::access_by<HighlightThisTest>;

const auto HighlightThis = QStringLiteral( "Highlight this..." );
const auto SelectedText = QStringLiteral( "a.b[1]" );
const auto TheLogLine = QStringLiteral( "modem a.b[1] ERROR done" );
// The strong preset whose background nothing else in the views is painted
// with.
constexpr int StrongVioletPreset = 17;

// The Highlighter Sets and which of them are active, stored again as they
// were when this goes.
class KeptHighlighterSets {
public:
    KeptHighlighterSets()
        : sets_( HighlighterSetCollection::get().highlighterSets() )
        , activeSetIds_( HighlighterSetCollection::get().activeSetIds() )
    {
    }

    ~KeptHighlighterSets()
    {
        store( sets_, activeSetIds_ );
    }

    KeptHighlighterSets( const KeptHighlighterSets& ) = delete;
    KeptHighlighterSets& operator=( const KeptHighlighterSets& ) = delete;

    static void store( const QList<HighlighterSet>& sets, const QStringList& activeSetIds )
    {
        auto& collection = HighlighterSetCollection::get();
        collection.setHighlighterSets( sets );
        collection.deactivateAll();
        for ( const auto& setId : activeSetIds ) {
            collection.activateSet( setId );
        }
        collection.save();
    }

private:
    QList<HighlighterSet> sets_;
    QStringList activeSetIds_;
};

HighlighterSet setOf( const QString& name, const QString& pattern )
{
    auto set = HighlighterSet::createNewSet( name );
    set.addHighlighter( Highlighter( pattern, false, true, Qt::black, Qt::yellow ) );
    return set;
}

// The set of the id as the collection holds it now.
HighlighterSet storedSet( const QString& setId )
{
    for ( const auto& set : HighlighterSetCollection::get().highlighterSets() ) {
        if ( set.id() == setId ) {
            return set;
        }
    }
    FAIL( "no set " << setId.toStdString() );
    return {};
}

template <class Widget>
Widget* childOf( QWidget& parent, const QString& name )
{
    auto* child = parent.findChild<Widget*>( name );
    REQUIRE( child != nullptr );
    return child;
}

void clickOk( QDialog& dialog )
{
    auto* ok = dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok );
    REQUIRE( ok->isEnabled() );
    ok->click();
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

// A window showing the Log File.
struct HighlightThisWindow {
    HighlightThisWindow()
    {
        REQUIRE( dir.isValid() );
        const auto path = dir.filePath( QStringLiteral( "highlight.log" ) );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( ( TheLogLine + QStringLiteral( "\nother line\n" ) ).toUtf8() );
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

    ~HighlightThisWindow()
    {
        window.reset();
        QTest::qWait( 50 );
    }

    HighlightThisWindow( const HighlightThisWindow& ) = delete;
    HighlightThisWindow& operator=( const HighlightThisWindow& ) = delete;

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

// Chooses Highlight this... of the menu, and in the dialog it opens does what
// fill does, then clicks OK.
void highlightInDialog( QMenu& menu, const std::function<void( HighlightThisDialog& )>& fill )
{
    auto* entry = contextMenuEntry( menu, HighlightThis );
    REQUIRE( entry != nullptr );
    REQUIRE( entry->isEnabled() );
    bool opened = false;
    QTimer::singleShot( 0, [ & ] {
        auto* dialog = qobject_cast<HighlightThisDialog*>( QApplication::activeModalWidget() );
        REQUIRE( dialog != nullptr );
        opened = true;
        fill( *dialog );
        clickOk( *dialog );
    } );
    entry->trigger();
    REQUIRE( opened );
}

} // namespace

SCENARIO( "The context menu of the Text View and the Filtered View offers Highlight this...",
          "[ui][highlighters][highlightthis]" )
{
    const HighlightThisWindow shown;
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    GIVEN( "the Text View" )
    {
        WHEN( "text is selected within a Log Line" )
        {
            selectText( textView );
            const auto menu = ViewAccess::contextMenu( textView );

            THEN( "the entry is offered at the top level, enabled, the top level kept short" )
            {
                REQUIRE( entriesOf( *menu ).contains( HighlightThis ) );
                REQUIRE( contextMenuEntry( *menu, HighlightThis )->isEnabled() );
                REQUIRE( entriesOf( *menu ).size() <= MaxTopLevelEntries );
            }
        }

        WHEN( "a whole Log Line is selected" )
        {
            textView.selectAndDisplayLine( 0_lnum );
            const auto menu = ViewAccess::contextMenu( textView );

            THEN( "the entry is disabled" )
            {
                REQUIRE_FALSE( contextMenuEntry( *menu, HighlightThis )->isEnabled() );
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
                REQUIRE( contextMenuEntry( *menu, HighlightThis ) != nullptr );
                REQUIRE( contextMenuEntry( *menu, HighlightThis )->isEnabled() );
            }
        }
    }
}

SCENARIO( "The Highlight this dialog proposes a Highlighter of the selection",
          "[ui][highlighters][highlightthis]" )
{
    const KeptHighlighterSets kept;

    GIVEN( "two sets of the user's, the second one active" )
    {
        const auto first = setOf( QStringLiteral( "First" ), QStringLiteral( "WARN" ) );
        const auto second = setOf( QStringLiteral( "Second" ), QStringLiteral( "INFO" ) );
        KeptHighlighterSets::store( { first, second }, { second.id() } );

        HighlightThisDialog dialog( SelectedText, TheLogLine );
        showUntilExposed( dialog );

        THEN( "it proposes the selection, escaped, as the pattern" )
        {
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "highlighterPattern" ) )->text()
                     == QStringLiteral( "a\\.b\\[1\\]" ) );
            REQUIRE( dialog.highlighter().pattern() == QStringLiteral( "a\\.b\\[1\\]" ) );
            REQUIRE( dialog.previewWarning().isEmpty() );
        }

        THEN( "the active set is the one proposed, and every set of the user's is offered" )
        {
            auto* sets = childOf<QComboBox>( dialog, QStringLiteral( "highlighterSet" ) );
            REQUIRE( sets->currentText() == QStringLiteral( "Second" ) );
            REQUIRE( dialog.chosenSetId() == second.id() );
            REQUIRE( sets->findText( QStringLiteral( "First" ) ) >= 0 );
            REQUIRE( sets->findText( QStringLiteral( "New Highlighter Set" ) ) >= 0 );
            REQUIRE_FALSE(
                childOf<QLineEdit>( dialog, QStringLiteral( "newSetName" ) )->isEnabled() );
        }

        THEN( "the Highlighter colors the whole Log Line, and the preview shows it so" )
        {
            REQUIRE_FALSE( dialog.highlighter().highlightOnlyMatch() );
            const auto back = dialog.highlighter().backColor().name();
            REQUIRE(
                dialog.previewHtml().contains( QStringLiteral( "background-color:%1;\">%2</span>" )
                                                   .arg( back, TheLogLine.toHtmlEscaped() ) ) );
        }

        WHEN( "a color preset is picked and only the matched text is to be colored" )
        {
            childOf<QToolButton>(
                dialog, QStringLiteral( "colorPresetButton%1" ).arg( StrongVioletPreset ) )
                ->click();
            childOf<QRadioButton>( dialog, QStringLiteral( "colorOnlyMatch" ) )->click();

            THEN( "the Highlighter has them, and the preview colors only the selection" )
            {
                const auto& preset
                    = highlighterColorPresets()[ static_cast<size_t>( StrongVioletPreset ) ];
                const auto highlighter = dialog.highlighter();
                REQUIRE( highlighter.highlightOnlyMatch() );
                REQUIRE( highlighter.foreColor() == preset.foreColor );
                REQUIRE( highlighter.backColor() == preset.backColor );
                REQUIRE( dialog.previewHtml().contains(
                    QStringLiteral( "modem <span style=\"color:%1;background-color:%2;\">%3</span> "
                                    "ERROR done" )
                        .arg( preset.foreColor.name(), preset.backColor.name(), SelectedText ) ) );
            }

            AND_WHEN( "another set is chosen" )
            {
                auto* sets = childOf<QComboBox>( dialog, QStringLiteral( "highlighterSet" ) );
                sets->setCurrentIndex( sets->findText( QStringLiteral( "First" ) ) );

                THEN( "the colors picked stay" )
                {
                    REQUIRE(
                        dialog.highlighter().backColor()
                        == highlighterColorPresets()[ static_cast<size_t>( StrongVioletPreset ) ]
                               .backColor );
                }
            }
        }

        WHEN( "the pattern is edited into an invalid regex" )
        {
            childOf<QLineEdit>( dialog, QStringLiteral( "highlighterPattern" ) )
                ->setText( QStringLiteral( "a(" ) );

            THEN( "it warns, and OK is disabled" )
            {
                REQUIRE_FALSE( dialog.previewWarning().isEmpty() );
                REQUIRE_FALSE( dialog.findChild<QDialogButtonBox*>()
                                   ->button( QDialogButtonBox::Ok )
                                   ->isEnabled() );
            }
        }

        WHEN( "OK is clicked" )
        {
            clickOk( dialog );

            THEN( "the Highlighter is added at the end of the active set" )
            {
                const auto set = storedSet( second.id() );
                REQUIRE( set.highlighters().size() == 2 );
                REQUIRE( set.highlighters().back().pattern() == QStringLiteral( "a\\.b\\[1\\]" ) );
                REQUIRE( storedSet( first.id() ).highlighters().size() == 1 );
            }
        }

        WHEN( "the first set is chosen, which is not active, and OK is clicked" )
        {
            auto* sets = childOf<QComboBox>( dialog, QStringLiteral( "highlighterSet" ) );
            sets->setCurrentIndex( sets->findText( QStringLiteral( "First" ) ) );
            clickOk( dialog );

            THEN( "the Highlighter is in it, and it is active too, so that it shows" )
            {
                REQUIRE( storedSet( first.id() ).highlighters().size() == 2 );
                REQUIRE( HighlighterSetCollection::get().activeSetIds().contains( first.id() ) );
                REQUIRE( HighlighterSetCollection::get().activeSetIds().contains( second.id() ) );
            }
        }
    }

    GIVEN( "no set of the user's" )
    {
        KeptHighlighterSets::store( {}, {} );
        HighlightThisDialog dialog( SelectedText, TheLogLine );
        showUntilExposed( dialog );

        THEN( "it proposes a new set, named in the dialog" )
        {
            REQUIRE( childOf<QComboBox>( dialog, QStringLiteral( "highlighterSet" ) )->currentText()
                     == QStringLiteral( "New Highlighter Set" ) );
            REQUIRE( dialog.chosenSetId().isEmpty() );
            REQUIRE( childOf<QLineEdit>( dialog, QStringLiteral( "newSetName" ) )->isEnabled() );
        }

        WHEN( "the new set is named and OK is clicked" )
        {
            childOf<QLineEdit>( dialog, QStringLiteral( "newSetName" ) )
                ->setText( QStringLiteral( "Mine" ) );
            clickOk( dialog );

            THEN( "the set holds the Highlighter, and it is active" )
            {
                const auto sets = HighlighterSetCollection::get().highlighterSets();
                REQUIRE( sets.size() == 1 );
                REQUIRE( sets.front().name() == QStringLiteral( "Mine" ) );
                REQUIRE( sets.front().highlighters().size() == 1 );
                REQUIRE( HighlighterSetCollection::get().activeSetIds()
                         == QStringList{ sets.front().id() } );
            }
        }
    }
}

SCENARIO( "OK in the Highlight this dialog recolors the views at once",
          "[ui][highlighters][highlightthis]" )
{
    const KeptHighlighterSets kept;
    const auto mine = setOf( QStringLiteral( "Mine" ), QStringLiteral( "WARN" ) );
    KeptHighlighterSets::store( { mine }, { mine.id() } );

    const HighlightThisWindow shown;
    CrawlerAccess::search( *shown.crawler, QStringLiteral( "ERROR" ) );
    auto& textView = CrawlerAccess::textView( *shown.crawler );
    auto& filteredView = CrawlerAccess::filteredView( *shown.crawler );
    const auto color
        = highlighterColorPresets()[ static_cast<size_t>( StrongVioletPreset ) ].backColor;
    REQUIRE_FALSE( showsColor( textView, color ) );
    REQUIRE_FALSE( showsColor( filteredView, color ) );

    WHEN( "a Highlighter of the selection in the Text View is made in the dialog" )
    {
        selectText( textView );
        const auto menu = ViewAccess::contextMenu( textView );
        highlightInDialog( *menu, []( HighlightThisDialog& dialog ) {
            childOf<QToolButton>(
                dialog, QStringLiteral( "colorPresetButton%1" ).arg( StrongVioletPreset ) )
                ->click();
        } );
        QTest::qWait( 50 );

        THEN( "the active set holds it" )
        {
            REQUIRE( storedSet( mine.id() ).highlighters().size() == 2 );
        }

        THEN( "the Text View and the Filtered View show it" )
        {
            REQUIRE( waitUiState( [ & ] { return showsColor( textView, color ); }, 5000 ) );
            REQUIRE( waitUiState( [ & ] { return showsColor( filteredView, color ); }, 5000 ) );
        }
    }

    WHEN( "the dialog opened from the Filtered View is cancelled" )
    {
        selectText( filteredView );
        const auto menu = ViewAccess::contextMenu( filteredView );
        QTimer::singleShot( 0, [] {
            auto* dialog = qobject_cast<HighlightThisDialog*>( QApplication::activeModalWidget() );
            REQUIRE( dialog != nullptr );
            dialog->reject();
        } );
        contextMenuEntry( *menu, HighlightThis )->trigger();

        THEN( "the set is as it was" )
        {
            REQUIRE( storedSet( mine.id() ).highlighters().size() == 1 );
        }
    }
}
