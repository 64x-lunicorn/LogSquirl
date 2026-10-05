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

// A text view showing Value Names (#647): what it shows, where a point lands,
// what is selected and copied, what a Search colors, how it wraps and scrolls,
// and what it saves -- driven through the view's own events where a user
// would, with the painting test's font, so every character is 8 x 16 px and
// each point lies on a known character.

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QFile>
#include <QFontInfo>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include "abstractlogview.h"
#include "fake_log_data.h"
#include "painting_test_font.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "savelinesdialog.h"
#include "valuenames_fixture.h"
#include "vector_lines.h"
#include "viewportlayout.h"

using valuenamesfixture::ScopedValueNames;

struct ValueNamesViewTest {};

template <>
struct AbstractLogView::access_by<ValueNamesViewTest> {
    // The Decoration the Log Line at a position was painted with last.
    static logsquirl::vector<HighlightedMatch> decorationSpans( const AbstractLogView& view,
                                                                LineNumber position )
    {
        const auto* logLine = view.viewportLogLineAt( position );
        REQUIRE( logLine != nullptr );
        REQUIRE( logLine->decorated.has_value() );
        return logLine->decorated->decoration.spans();
    }

    // What a QuickFind that found the portion does.
    static void quickFindFound( AbstractLogView& view, const Portion& portion )
    {
        view.setQuickFindResult( true, portion );
    }

    // Whether any Log Line in the Viewport was named.
    static bool namesAny( const AbstractLogView& view )
    {
        const auto& logLines = view.viewportContent().logLines;
        return std::any_of( logLines.begin(), logLines.end(),
                            []( const auto& logLine ) { return logLine.shown.hasNamedValues(); } );
    }
};

namespace {

using paintingtestfont::CharHeight;
using paintingtestfont::CharWidth;
using Access = AbstractLogView::access_by<ValueNamesViewTest>;

constexpr int ViewWidth = 480;
constexpr int ViewHeight = 224;
constexpr int TextLeftPx = ViewportLayout::BulletAreaWidth + 2 * ViewportLayout::SeparatorWidth;
// How many columns of text fit.
constexpr int Columns = ( ViewWidth - TextLeftPx ) / CharWidth;

// Shown as "BAP << ECU Beispiel(0x15) Sample(0x14) sonstiges": Beispiel(0x15)
// in columns 11 to 24, Sample(0x14) in 26 to 37.
const LineNumber NamedLine{ 0 };
// Log Line 1 is shown as "a" and a tab to 8, "id=seven(7)" in 8 to 18, a
// tab to 24, "b".
// 51 columns raw, 69 shown: wider than the Viewport only with Value Names.
const LineNumber WideLine{ 2 };
// Log Line 3 holds 0x15 outside the Naming Rule's context.

QStringList valueNamesLines()
{
    return { QStringLiteral( "BAP << ECU 0x15 0x14 sonstiges" ), QStringLiteral( "a\tid=7\tb" ),
             QStringLiteral( "BAP << ECU 0x15 0x14 and thirty more characters." ),
             QStringLiteral( "regenbogen 0x15" ) };
}

class ValueNamesLogView : public AbstractLogView {
public:
    ValueNamesLogView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern,
                       bool textWrap )
        : AbstractLogView( logData, quickFindPattern, textWrap )
    {
    }

    using AbstractLogView::createContextMenu;
    using AbstractLogView::offersSaveWithValueNames;
    using AbstractLogView::saveLinesTo;
};

void showForTest( AbstractLogView& view )
{
    const auto font = paintingtestfont::requirePaintingTestFont();

    view.setFrameShape( QFrame::NoFrame );
    view.setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.resize( ViewWidth, ViewHeight );
    view.show();
    QCoreApplication::processEvents();
    view.updateFont( font );
    view.updateData();

    REQUIRE( QFontInfo( view.font() ).family() == font.family() );
}

QPointF onText( int row, int column )
{
    return QPointF{ TextLeftPx + column * CharWidth + CharWidth / 2.0,
                    row * CharHeight + CharHeight / 2.0 };
}

void sendMouse( AbstractLogView& view, QEvent::Type type, QPointF pos, Qt::MouseButton button,
                Qt::MouseButtons buttons )
{
    QMouseEvent event( type, pos, view.viewport()->mapToGlobal( pos ), button, buttons,
                       Qt::NoModifier );
    QCoreApplication::sendEvent( view.viewport(), &event );
}

void drag( AbstractLogView& view, QPointF from, QPointF to )
{
    sendMouse( view, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton );
    sendMouse( view, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton );
    sendMouse( view, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton );
}

void doubleClick( AbstractLogView& view, QPointF pos )
{
    sendMouse( view, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton );
    sendMouse( view, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton );
    sendMouse( view, QEvent::MouseButtonDblClick, pos, Qt::LeftButton, Qt::LeftButton );
    sendMouse( view, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton );
}

// Chooses the entry of the view's context menu, opened at pos.
void chooseFromMenu( ValueNamesLogView& view, QPointF pos, const QString& entry )
{
    const auto menu = view.createContextMenu( pos.toPoint() );
    for ( auto* action : menu->actions() ) {
        if ( action->text() == entry ) {
            REQUIRE( action->isEnabled() );
            action->trigger();
            return;
        }
    }
    FAIL( "no entry " << entry.toStdString() );
}

// How many Visual Lines of the Log Line the Viewport holds.
size_t visualLinesOf( const AbstractLogView& view, LineNumber line )
{
    const auto& visualLines = view.viewportLayout().visualLines();
    return static_cast<size_t>( std::count_if(
        visualLines.begin(), visualLines.end(),
        [ line ]( const VisualLine& visualLine ) { return visualLine.lineNumber == line; } ) );
}

void paint( AbstractLogView& view )
{
    view.viewport()->grab();
    QCoreApplication::processEvents();
}

} // namespace

SCENARIO( "A text view showing Value Names selects and copies whole values", "[logviewvaluenames]" )
{
    const ScopedValueNames valueNames;
    const FakeLogData logData{ valueNamesLines() };
    const QuickFindPattern quickFindPattern;

    ValueNamesLogView view( &logData, &quickFindPattern, false );
    showForTest( view );
    view.valueNamesShownSet( true );
    REQUIRE( view.showsValueNames() );

    WHEN( "text is dragged from before a Named Value into it" )
    {
        drag( view, onText( 0, 3 ), onText( 0, 15 ) );

        THEN( "the copy holds the whole raw value" )
        {
            REQUIRE( view.getSelectedText() == QStringLiteral( " << ECU 0x15" ) );
        }

        THEN( "Copy as Shown holds the whole name" )
        {
            REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( " << ECU Beispiel(0x15)" ) );
        }

        THEN( "Add to search adds the whole raw value" )
        {
            QSignalSpy added( &view, qOverload<const QString&>( &AbstractLogView::addToSearch ) );
            chooseFromMenu( view, onText( 0, 5 ), QStringLiteral( "&Add to search" ) );
            REQUIRE( added.count() == 1 );
            REQUIRE( added.front().front().toString() == QStringLiteral( " << ECU 0x15" ) );
        }

        THEN( "Copy with line numbers copies the whole raw value" )
        {
            QApplication::clipboard()->clear();
            chooseFromMenu( view, onText( 0, 5 ), QStringLiteral( "Copy with line numbers" ) );
            REQUIRE( QApplication::clipboard()->text() == QStringLiteral( "0:  << ECU 0x15" ) );
        }
    }

    WHEN( "a Named Value is double-clicked" )
    {
        doubleClick( view, onText( 0, 30 ) );

        THEN( "the whole value is selected" )
        {
            REQUIRE( view.getSelectedText() == QStringLiteral( "0x14" ) );
            REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( "Sample(0x14)" ) );
        }
    }

    WHEN( "the word after a Named Value and a tab is double-clicked" )
    {
        doubleClick( view, onText( 1, 24 ) );

        THEN( "it is that word: the tab stops where the name puts it" )
        {
            REQUIRE( view.getSelectedText() == QStringLiteral( "b" ) );
        }
    }

    WHEN( "a Named Value between tabs is double-clicked" )
    {
        doubleClick( view, onText( 1, 13 ) );

        THEN( "the whole value is selected" )
        {
            REQUIRE( view.getSelectedText() == QStringLiteral( "7" ) );
            REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( "seven(7)" ) );
        }
    }

    WHEN( "a QuickFind selects part of a Named Value" )
    {
        // "15" of 0x15, in the display columns of the raw text.
        Access::quickFindFound( view, Portion{ NamedLine, LineColumn{ 13 }, LineColumn{ 14 } } );

        THEN( "the copy holds the whole raw value" )
        {
            REQUIRE( view.getSelectedText() == QStringLiteral( "0x15" ) );
        }
    }

    WHEN( "a QuickFind selects past the end of a Log Line with Named Values" )
    {
        // From the s of sonstiges to past the end of the Log Line.
        Access::quickFindFound( view, Portion{ NamedLine, LineColumn{ 25 }, LineColumn{ 100 } } );

        THEN( "the copy holds the raw text to the end of the Log Line" )
        {
            REQUIRE( view.getSelectedText() == QStringLiteral( "tiges" ) );
            REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( "tiges" ) );
        }
    }

    WHEN( "a portion from inside a tab on a Log Line with a Named Value is selected" )
    {
        // "a<tab>id=7<tab>b": from inside the first tab to the i.
        Access::quickFindFound( view, Portion{ 1_lnum, LineColumn{ 3 }, LineColumn{ 8 } } );

        THEN( "the copy holds the part of the tab selected, as with Value Names hidden (#747)" )
        {
            REQUIRE( view.getSelectedText() == QStringLiteral( "     i" ) );
        }
    }

    WHEN( "a portion from inside a tab to inside the tab after a Named Value is selected" )
    {
        // "a<tab>id=7<tab>b": from inside the first tab to inside the second,
        // which is four columns wide in the raw text and five in the text
        // shown "a<tab>id=seven(7)<tab>b".
        Access::quickFindFound( view, Portion{ 1_lnum, LineColumn{ 3 }, LineColumn{ 13 } } );

        THEN( "Copy as Shown holds the part of each tab selected (#748)" )
        {
            REQUIRE( view.getSelectedText() == QStringLiteral( "     id=7  " ) );
            REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( "     id=seven(7)  " ) );
        }
    }

    WHEN( "a whole Log Line is selected" )
    {
        drag( view, onText( 0, 3 ), onText( 1, 3 ) );

        THEN( "the copy holds the Log Lines as the Log File holds them" )
        {
            auto selected = view.getSelectedText();
            selected.remove( QChar::CarriageReturn );
            REQUIRE( selected == valueNamesLines().mid( 0, 2 ).join( QChar::LineFeed ) );
        }

        THEN( "Copy as Shown holds them as shown" )
        {
            auto selected = view.getSelectedTextAsShown();
            selected.remove( QChar::CarriageReturn );
            REQUIRE( selected
                     == QStringLiteral( "BAP << ECU Beispiel(0x15) Sample(0x14) sonstiges\n"
                                        "a\tid=seven(7)\tb" ) );
        }
    }

    WHEN( "the view is switched to show no Value Names" )
    {
        view.valueNamesShownSet( false );
        doubleClick( view, onText( 0, 11 ) );

        THEN( "a word is the raw text's, and Copy as Shown copies it as it is" )
        {
            REQUIRE_FALSE( view.showsValueNames() );
            REQUIRE( view.getSelectedText() == QStringLiteral( "0x15" ) );
            REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( "0x15" ) );
        }
    }
}

SCENARIO( "A text view shows where a Named Value's name came from", "[logviewvaluenames]" )
{
    const ScopedValueNames valueNames;
    const FakeLogData logData{ valueNamesLines() };
    const QuickFindPattern quickFindPattern;

    ValueNamesLogView view( &logData, &quickFindPattern, false );
    showForTest( view );
    view.valueNamesShownSet( true );

    THEN( "the tooltip over a Named Value tells its value, name, table, rule and group" )
    {
        REQUIRE( view.valueNameToolTipAt( onText( 0, 12 ).toPoint() )
                 == QStringLiteral( "0x15 → Beispiel · table ECU · rule BAP ECU · group BAP" ) );
        REQUIRE( view.valueNameToolTipAt( onText( 1, 17 ).toPoint() )
                 == QStringLiteral( "7 → seven · table Ids · rule Id · group BAP" ) );
    }

    THEN( "there is none beside it, past the end of the Log Line, or on an unnamed value" )
    {
        REQUIRE( view.valueNameToolTipAt( onText( 0, 10 ).toPoint() ).isEmpty() );
        REQUIRE( view.valueNameToolTipAt( onText( 0, 25 ).toPoint() ).isEmpty() );
        REQUIRE( view.valueNameToolTipAt( onText( 0, 52 ).toPoint() ).isEmpty() );
        REQUIRE( view.valueNameToolTipAt( onText( 3, 12 ).toPoint() ).isEmpty() );
    }
}

SCENARIO( "A Search over part of a Named Value colors all of what is shown", "[logviewvaluenames]" )
{
    const ScopedValueNames valueNames;
    const FakeLogData logData{ valueNamesLines() };
    const QuickFindPattern quickFindPattern;

    ValueNamesLogView view( &logData, &quickFindPattern, false );
    showForTest( view );
    const QColor searchBack{ 255, 200, 0 };
    view.setDecorationPolicy( DecorationPolicy{ .mainSearchHighlight = true,
                                                .variateMainSearchHighlight = false,
                                                .mainSearchBackColor = searchBack,
                                                .quickFindBackColor = QColor{ Qt::yellow } } );
    view.setSearchPattern( RegularExpressionPattern{ QStringLiteral( "x1" ) } );
    view.valueNamesShownSet( true );
    paint( view );

    THEN( "the Search's color covers both names, and nothing between them" )
    {
        const auto spans = Access::decorationSpans( view, NamedLine );
        std::vector<std::pair<int64_t, int64_t>> searched;
        for ( const auto& span : spans ) {
            if ( span.backColor() == searchBack ) {
                searched.emplace_back( span.startColumn().get(), span.size().get() );
            }
        }
        REQUIRE( searched == std::vector<std::pair<int64_t, int64_t>>{ { 11, 14 }, { 26, 12 } } );
    }

    THEN( "a name is not searched: the Search matches raw text only" )
    {
        view.setSearchPattern( RegularExpressionPattern{ QStringLiteral( "Beispiel" ) } );
        paint( view );
        const auto spans = Access::decorationSpans( view, NamedLine );
        REQUIRE( std::none_of( spans.begin(), spans.end(), [ &searchBack ]( const auto& span ) {
            return span.backColor() == searchBack;
        } ) );
    }
}

SCENARIO( "A text view wraps and scrolls the text it shows with Value Names",
          "[logviewvaluenames]" )
{
    const ScopedValueNames valueNames;
    const FakeLogData logData{ valueNamesLines() };
    const QuickFindPattern quickFindPattern;
    REQUIRE( 51 < Columns );
    REQUIRE( 69 > Columns );

    GIVEN( "text wrapping on" )
    {
        ValueNamesLogView view( &logData, &quickFindPattern, true );
        showForTest( view );

        THEN( "a Log Line narrow enough raw but not shown takes two Visual Lines only with Value "
              "Names" )
        {
            REQUIRE( visualLinesOf( view, WideLine ) == 1 );
            view.valueNamesShownSet( true );
            REQUIRE( visualLinesOf( view, WideLine ) == 2 );
            view.valueNamesShownSet( false );
            REQUIRE( visualLinesOf( view, WideLine ) == 1 );
        }
    }

    GIVEN( "text wrapping off" )
    {
        ValueNamesLogView view( &logData, &quickFindPattern, false );
        showForTest( view );
        paint( view );
        REQUIRE( view.horizontalScrollBar()->maximum() == 0 );

        // A view of Log Lines that are as wide raw as these are shown.
        const FakeLogData shownData{ { QStringLiteral(
            "BAP << ECU Beispiel(0x15) Sample(0x14) and thirty more characters." ) } };
        ValueNamesLogView shownView( &shownData, &quickFindPattern, false );
        showForTest( shownView );
        REQUIRE( shownView.horizontalScrollBar()->maximum() > 0 );

        WHEN( "Value Names are shown and painted" )
        {
            view.valueNamesShownSet( true );
            paint( view );

            THEN( "the horizontal scrollbar reaches the end of the widest Log Line shown" )
            {
                REQUIRE( view.horizontalScrollBar()->maximum()
                         == shownView.horizontalScrollBar()->maximum() );
            }

            AND_WHEN( "the view is scrolled to its right" )
            {
                view.horizontalScrollBar()->setValue( view.horizontalScrollBar()->maximum() );
                const auto offset = view.horizontalScrollBar()->value();

                THEN( "a point lands on the character shown there" )
                {
                    // "Beispiel(0x15)" starts at column 11 of the text shown.
                    doubleClick( view, onText( 0, 12 - offset ) );
                    REQUIRE( view.getSelectedText() == QStringLiteral( "0x15" ) );
                }
            }
        }
    }
}

SCENARIO( "Off, Value Names cost a text view nothing", "[logviewvaluenames]" )
{
    const FakeLogData logData{ valueNamesLines() };
    const QuickFindPattern quickFindPattern;

    GIVEN( "Naming Rules, and a view whose switch is off" )
    {
        const ScopedValueNames valueNames;
        ValueNamesLogView view( &logData, &quickFindPattern, false );
        showForTest( view );
        paint( view );

        THEN( "no Log Line is named" )
        {
            REQUIRE_FALSE( view.showsValueNames() );
            REQUIRE_FALSE( Access::namesAny( view ) );
        }
    }

    GIVEN( "the switch on, but every Naming Rule unchecked" )
    {
        auto group = valuenamesfixture::exampleGroup();
        auto rules = group.rules();
        for ( auto& rule : rules ) {
            rule.enabled = false;
        }
        group.setRules( rules );
        const ScopedValueNames valueNames{ { group } };
        ValueNamesLogView view( &logData, &quickFindPattern, false );
        showForTest( view );
        view.valueNamesShownSet( true );
        paint( view );

        THEN( "no Log Line is named either" )
        {
            REQUIRE_FALSE( view.showsValueNames() );
            REQUIRE_FALSE( Access::namesAny( view ) );
        }
    }

    GIVEN( "the switch on, and the Value Names changed afterwards" )
    {
        const ScopedValueNames valueNames;
        ValueNamesLogView view( &logData, &quickFindPattern, false );
        showForTest( view );
        view.valueNamesShownSet( true );
        paint( view );
        doubleClick( view, onText( 0, 12 ) );
        REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( "Beispiel(0x15)" ) );

        auto group = valuenamesfixture::exampleGroup();
        auto tables = group.tables();
        tables[ 0 ].rows[ 0 ].name = QStringLiteral( "Example" );
        group.setTables( tables );
        ValueNamesCollection::get().setGroups( { group } );
        view.applyValueNamesChange();
        paint( view );

        THEN( "the view shows the new names" )
        {
            REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( "Example(0x15)" ) );
            REQUIRE( view.valueNameToolTipAt( onText( 0, 12 ).toPoint() )
                         .startsWith( QStringLiteral( "0x15 → Example" ) ) );
        }
    }
}

SCENARIO( "A text view saves its Log Lines with Value Names when asked to",
          "[logviewvaluenames][linessaver]" )
{
    const ScopedValueNames valueNames;
    const FakeLogData logData{ valueNamesLines() };
    const QuickFindPattern quickFindPattern;
    ValueNamesLogView view( &logData, &quickFindPattern, false );
    showForTest( view );

    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto fileName = dir.filePath( QStringLiteral( "saved.log" ) );
    const auto saved = [ &fileName ]() {
        QFile file{ fileName };
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        return QString::fromUtf8( file.readAll() );
    };

    WHEN( "the view shows Value Names and saves with them" )
    {
        view.valueNamesShownSet( true );
        view.saveLinesTo( fileName, 0_lnum, 2_lnum, true );

        THEN( "the file holds the Log Lines as shown" )
        {
            REQUIRE( saved().contains(
                QStringLiteral( "BAP << ECU Beispiel(0x15) Sample(0x14) sonstiges" ) ) );
            REQUIRE( saved().contains( QStringLiteral( "a\tid=seven(7)\tb" ) ) );
        }
    }

    WHEN( "it saves without them" )
    {
        view.valueNamesShownSet( true );
        view.saveLinesTo( fileName, 0_lnum, 2_lnum, false );

        THEN( "the file holds the Log Lines as the Log File does" )
        {
            REQUIRE( saved().contains( QStringLiteral( "BAP << ECU 0x15 0x14 sonstiges" ) ) );
            REQUIRE_FALSE( saved().contains( QStringLiteral( "Beispiel" ) ) );
        }
    }

    WHEN( "it shows none and is asked to save with them" )
    {
        view.saveLinesTo( fileName, 0_lnum, 2_lnum, true );

        THEN( "there are none to save with" )
        {
            REQUIRE_FALSE( saved().contains( QStringLiteral( "Beispiel" ) ) );
        }
    }
}

SCENARIO( "The save dialog offers With Value Names only while the view shows them",
          "[logviewvaluenames][linessaver]" )
{
    const ScopedValueNames valueNames;
    const FakeLogData logData{ valueNamesLines() };
    const QuickFindPattern quickFindPattern;
    ValueNamesLogView view( &logData, &quickFindPattern, false );
    showForTest( view );

    GIVEN( "a view that does not show Value Names, though a Naming Rule is enabled" )
    {
        THEN( "it asks with the platform's own dialog" )
        {
            REQUIRE_FALSE( view.offersSaveWithValueNames() );
        }
    }

    GIVEN( "a view that shows Value Names" )
    {
        view.valueNamesShownSet( true );

        THEN( "it asks with the dialog that offers them" )
        {
            REQUIRE( view.offersSaveWithValueNames() );
        }
    }
}

// Below a dialog of the test's own, laid out on a grid as Qt's file dialog is:
// Qt's file dialog starts a thread that ThreadSanitizer sees race with the
// dialog itself (#634).
SCENARIO( "The save dialog's With Value Names is off at first and says what is saved",
          "[logviewvaluenames][linessaver]" )
{
    GIVEN( "a dialog laid out on a grid, given the check" )
    {
        QDialog dialog;
        auto* grid = new QGridLayout( &dialog );
        grid->addWidget( new QLabel( QStringLiteral( "a file name" ), &dialog ), 0, 0 );
        SaveLinesDialog::addWithValueNames( dialog );
        auto* check = dialog.findChild<QCheckBox*>( SaveLinesDialog::WithValueNamesName );

        THEN( "the check is below what was there, off and enabled" )
        {
            REQUIRE( check != nullptr );
            REQUIRE( grid->indexOf( check ) >= 0 );
            int row = 0;
            int column = 0;
            int rowSpan = 0;
            int columnSpan = 0;
            grid->getItemPosition( grid->indexOf( check ), &row, &column, &rowSpan, &columnSpan );
            REQUIRE( row == 1 );
            REQUIRE_FALSE( check->isChecked() );
            REQUIRE( check->isEnabled() );
            REQUIRE_FALSE( SaveLinesDialog::withValueNames( dialog ) );
        }

        THEN( "checked, the save is with Value Names" )
        {
            REQUIRE( check != nullptr );
            check->setChecked( true );
            REQUIRE( SaveLinesDialog::withValueNames( dialog ) );
        }
    }

    GIVEN( "a dialog laid out otherwise" )
    {
        QDialog dialog;
        new QVBoxLayout( &dialog );
        SaveLinesDialog::addWithValueNames( dialog );

        THEN( "it is left without the check, and the save is without Value Names" )
        {
            REQUIRE( dialog.findChild<QCheckBox*>( SaveLinesDialog::WithValueNamesName )
                     == nullptr );
            REQUIRE_FALSE( SaveLinesDialog::withValueNames( dialog ) );
        }
    }
}

// A Filtered View: its positions are not its Log Lines' numbers (#647).
SCENARIO( "A Filtered View shows Value Names at its positions", "[logviewvaluenames]" )
{
    const ScopedValueNames valueNames;
    // Log Lines 1, 3 and 4 are shown, at positions 0, 1 and 2.
    const QStringList logFileLines{
        QStringLiteral( "not shown" ), QStringLiteral( "BAP << ECU 0x15 0x14 sonstiges" ),
        QStringLiteral( "not shown either" ), QStringLiteral( "a\tid=7\tb" ),
        // Wrapped, Beispiel(0x15) starts the second Visual Line: the Viewport
        // is 58 columns wide and it is shown from column 52 on.
        QStringLiteral( "a Log Line wrapped inside a Named Value: BAP << ECU 0x15 0x14 end" )
    };
    const std::vector<uint64_t> shown{ 1, 3, 4 };
    const FakeLogData logFile{ logFileLines };
    QStringList shownLines;
    for ( const auto line : shown ) {
        shownLines << logFileLines[ static_cast<qsizetype>( line ) ];
    }
    const FakeLogData shownText{ shownLines };
    const QuickFindPattern quickFindPattern;

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a Filtered View showing Value Names, text wrapping "
               << ( textWrap ? "on" : "off" ) )
        {
            AbstractLogView view( &shownText, std::make_unique<VectorLines>( &logFile, shown ),
                                  &quickFindPattern, textWrap );
            showForTest( view );
            view.valueNamesShownSet( true );

            THEN( "a point on a Named Value tells where its name came from" )
            {
                REQUIRE(
                    view.valueNameToolTipAt( onText( 0, 12 ).toPoint() )
                    == QStringLiteral( "0x15 → Beispiel · table ECU · rule BAP ECU · group BAP" ) );
                REQUIRE( view.valueNameToolTipAt( onText( 1, 13 ).toPoint() )
                         == QStringLiteral( "7 → seven · table Ids · rule Id · group BAP" ) );
            }

            WHEN( "text is dragged into a Named Value" )
            {
                drag( view, onText( 0, 3 ), onText( 0, 15 ) );

                THEN( "the copy holds the raw value, Copy as Shown the name, of Log Line 1" )
                {
                    REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 1_lnum } );
                    REQUIRE( view.getSelectedText() == QStringLiteral( " << ECU 0x15" ) );
                    REQUIRE( view.getSelectedTextAsShown()
                             == QStringLiteral( " << ECU Beispiel(0x15)" ) );
                }
            }

            WHEN( "a Named Value between tabs is double-clicked" )
            {
                doubleClick( view, onText( 1, 13 ) );

                THEN( "the whole value of Log Line 3 is selected" )
                {
                    REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 3_lnum } );
                    REQUIRE( view.getSelectedText() == QStringLiteral( "7" ) );
                    REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( "seven(7)" ) );
                }
            }

            if ( textWrap ) {
                THEN( "a Named Value on the second Visual Line of a wrapped Log Line is found "
                      "there" )
                {
                    REQUIRE( view.valueNameToolTipAt( onText( 3, 2 ).toPoint() )
                                 .startsWith( QStringLiteral( "0x15 → Beispiel" ) ) );
                    doubleClick( view, onText( 3, 2 ) );
                    REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 4_lnum } );
                    REQUIRE( view.getSelectedText() == QStringLiteral( "0x15" ) );
                }
            }

            WHEN( "a Search matches part of a Named Value" )
            {
                const QColor searchBack{ 255, 200, 0 };
                view.setDecorationPolicy(
                    DecorationPolicy{ .mainSearchHighlight = true,
                                      .variateMainSearchHighlight = false,
                                      .mainSearchBackColor = searchBack,
                                      .quickFindBackColor = QColor{ Qt::yellow } } );
                view.setSearchPattern( RegularExpressionPattern{ QStringLiteral( "x15" ) } );
                paint( view );

                THEN( "its color covers the whole name at the position shown" )
                {
                    const auto spans = Access::decorationSpans( view, 0_lnum );
                    std::vector<std::pair<int64_t, int64_t>> searched;
                    for ( const auto& span : spans ) {
                        if ( span.backColor() == searchBack ) {
                            searched.emplace_back( span.startColumn().get(), span.size().get() );
                        }
                    }
                    REQUIRE( searched == std::vector<std::pair<int64_t, int64_t>>{ { 11, 14 } } );
                }
            }
        }
    }
}
