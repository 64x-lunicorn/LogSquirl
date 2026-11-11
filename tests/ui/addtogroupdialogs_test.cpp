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

// Add as Predefined Filter... and Name This Value share one layout and
// preview their result in one line (#958).

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

#include "addpredefinedfilterdialog.h"
#include "addtogrouplayout.h"
#include "crawlerwidget.h"
#include "filterpreviewsearch.h"
#include "groupchooser.h"
#include "logdata.h"
#include "logformatcatalog.h"
#include "namevaluedialog.h"
#include "naminggroup.h"
#include "open_log_file_of.h"
#include "openlogfile.h"
#include "session.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"
#include "theme.h"
#include "valuenamescollection.h"

using logsquirl::valuenames::LineRange;
using Section = AddToGroupLayout::Section;
using PreviewState = FilterPreviewSearch::Result::State;

namespace {

const auto EsimLine = QStringLiteral( "modem EsimState=9 done" );
const QChar Ellipsis( 0x2026 );
// The value, its name not typed yet, as the template shows it: OK names it so.
const auto Unnamed = QStringLiteral( "= (9)" );

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

// The labels of the rows of the form, top to bottom; empty for a row without.
QStringList rowLabels( const AddToGroupLayout& layout )
{
    QStringList labels;
    auto* form = layout.form();
    for ( int row = 0; row < form->rowCount(); ++row ) {
        const auto* item = form->itemAt( row, QFormLayout::LabelRole );
        const auto* label = item != nullptr ? qobject_cast<QLabel*>( item->widget() ) : nullptr;
        labels.append( label != nullptr ? label->text() : QString{} );
    }
    return labels;
}

// The row of the form whose field is widget, -1 for none.
int rowOf( const AddToGroupLayout& layout, const QWidget* widget )
{
    auto* form = layout.form();
    for ( int row = 0; row < form->rowCount(); ++row ) {
        for ( const auto role : { QFormLayout::FieldRole, QFormLayout::SpanningRole } ) {
            auto* item = form->itemAt( row, role );
            if ( item == nullptr ) {
                continue;
            }
            if ( item->widget() == widget ) {
                return row;
            }
            if ( auto* layoutItem = item->layout() ) {
                for ( int index = 0; index < layoutItem->count(); ++index ) {
                    if ( layoutItem->itemAt( index )->widget() == widget ) {
                        return row;
                    }
                }
            }
        }
    }
    return -1;
}

// Checks what both dialogs have the same: the sections in their order, the
// Group Chooser first where the thing goes, the preview last in the form, the
// warning line below it, then OK and Cancel; and the width.
void checkSharedLayout( QDialog& dialog, const AddToGroupLayout& layout,
                        const GroupChooser* chooser )
{
    const auto sections = layout.rowSections();
    REQUIRE( sections.size() == layout.form()->rowCount() );
    REQUIRE( std::is_sorted( sections.cbegin(), sections.cend() ) );
    REQUIRE( sections.front() == Section::What );
    REQUIRE( sections.back() == Section::Preview );
    REQUIRE( sections.count( Section::Preview ) == 1 );

    const auto chooserRow = rowOf( layout, chooser );
    REQUIRE( chooserRow == static_cast<int>( sections.indexOf( Section::Where ) ) );
    REQUIRE( rowOf( layout, layout.preview() ) == layout.form()->rowCount() - 1 );
    REQUIRE( rowLabels( layout ).back() == QStringLiteral( "Preview:" ) );

    auto* top = qobject_cast<QVBoxLayout*>( dialog.layout() );
    REQUIRE( top != nullptr );
    REQUIRE( top->itemAt( 0 )->layout() == layout.form() );
    REQUIRE( top->itemAt( 1 )->widget() == layout.warning() );
    REQUIRE( top->itemAt( top->count() - 1 )->widget() == layout.buttons() );
    REQUIRE( layout.buttons()->standardButtons()
             == ( QDialogButtonBox::Ok | QDialogButtonBox::Cancel ) );

    REQUIRE_FALSE( layout.preview()->wordWrap() );
    REQUIRE( dialog.width() == AddToGroupLayout::DialogWidth );
}

// The user's own Naming Groups are the ones given for as long as this lives.
class KeptNamingGroups {
public:
    explicit KeptNamingGroups( const QList<logsquirl::valuenames::NamingGroup>& groups = {} )
        : before_( ValueNamesCollection::get().ownGroups() )
    {
        store( groups );
    }

    ~KeptNamingGroups()
    {
        store( before_ );
    }

    KeptNamingGroups( const KeptNamingGroups& ) = delete;
    KeptNamingGroups& operator=( const KeptNamingGroups& ) = delete;

private:
    static void store( const QList<logsquirl::valuenames::NamingGroup>& groups )
    {
        auto& collection = ValueNamesCollection::get();
        collection.setTeamGroups( {} );
        collection.setGroups( groups );
        collection.save();
    }

    QList<logsquirl::valuenames::NamingGroup> before_;
};

// A Log File of the lines, loaded.
struct LoadedLogFile {
    explicit LoadedLogFile( const QStringList& lines )
    {
        REQUIRE( dir.isValid() );
        path = dir.filePath( QStringLiteral( "preview.log" ) );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        for ( const auto& line : lines ) {
            file.write( line.toUtf8() + '\n' );
        }
        file.close();

        const auto policies = testSettingsPolicies();
        logData = std::make_shared<LogData>( policies.indexing, policies.search,
                                             policies.fileAccess, policies.decoding );
        SafeQSignalSpy loaded( logData.get(), SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData->attachFile( path );
        REQUIRE( loaded.safeWait( 20000 ) );
        REQUIRE( logData->getNbLine().get() == static_cast<uint64_t>( lines.size() ) );
    }

    QTemporaryDir dir;
    QString path;
    std::shared_ptr<LogData> logData;
};

// Every seventh Log Line holds "a.c", every fifth "abc".
QStringList previewLines( int count )
{
    QStringList lines;
    for ( int index = 0; index < count; ++index ) {
        auto line = QStringLiteral( "10:00:00 INFO request %1 served" ).arg( index );
        if ( index % 7 == 3 ) {
            line += QStringLiteral( " a.c" );
        }
        if ( index % 5 == 1 ) {
            line += QStringLiteral( " abc" );
        }
        lines.append( line );
    }
    return lines;
}

int linesWith( const QStringList& lines, const QString& text )
{
    return static_cast<int>(
        std::count_if( lines.cbegin(), lines.cend(),
                       [ &text ]( const QString& line ) { return line.contains( text ); } ) );
}

// The buttons of a tab's Search Line that matches case and reads plain text.
SearchLine::Flags matchingCase()
{
    SearchLine::Flags flags;
    flags.matchCase = true;
    return flags;
}

bool previewDone( const AddPredefinedFilterDialog& dialog )
{
    return dialog.previewResult().state == PreviewState::Done;
}

} // namespace

SCENARIO( "Add as Predefined Filter and Name This Value share one layout",
          "[ui][addtogroup][addpredefinedfilter][namevalue]" )
{
    AddPredefinedFilterDialog filterDialog( QStringLiteral( "a.b[1]" ) );
    NameValueDialog valueDialog( EsimLine, rangeOf( EsimLine, QStringLiteral( "9" ) ) );
    showUntilExposed( filterDialog );
    showUntilExposed( valueDialog );

    THEN( "Add as Predefined Filter shows what, where, the preview, the warning and the buttons" )
    {
        const auto& layout = filterDialog.addToGroupLayout();
        REQUIRE( rowLabels( layout )
                 == QStringList{ QStringLiteral( "Name:" ), QStringLiteral( "Pattern:" ), QString{},
                                 QStringLiteral( "Filter Group:" ), QString{},
                                 QStringLiteral( "Preview:" ) } );
        REQUIRE( layout.rowSections()
                 == QList<Section>{ Section::What, Section::What, Section::What, Section::Where,
                                    Section::Where, Section::Preview } );
        checkSharedLayout( filterDialog, layout, filterDialog.groupChooser() );
    }

    THEN( "Name This Value shows what, where, the preview, the warning and the buttons" )
    {
        const auto& layout = valueDialog.addToGroupLayout();
        REQUIRE( rowLabels( layout )
                 == QStringList{ QStringLiteral( "Value:" ), QStringLiteral( "Naming Rule:" ),
                                 QStringLiteral( "Template:" ), QStringLiteral( "Naming Group:" ),
                                 QStringLiteral( "Name Table:" ), QStringLiteral( "Preview:" ) } );
        REQUIRE( layout.rowSections()
                 == QList<Section>{ Section::What, Section::What, Section::What, Section::Where,
                                    Section::Where, Section::Preview } );
        checkSharedLayout( valueDialog, layout, valueDialog.groupChooser() );
    }

    THEN( "both are as wide, and space their rows alike" )
    {
        REQUIRE( filterDialog.width() == valueDialog.width() );
        REQUIRE( filterDialog.addToGroupLayout().form()->verticalSpacing()
                 == valueDialog.addToGroupLayout().form()->verticalSpacing() );
        REQUIRE( filterDialog.layout()->contentsMargins()
                 == valueDialog.layout()->contentsMargins() );
    }
}

SCENARIO( "A preview shows its line on one line, elided around what it marks", "[ui][addtogroup]" )
{
    LinePreview preview;
    preview.setFixedWidth( 240 );
    showUntilExposed( preview );
    const auto metrics = preview.fontMetrics();

    GIVEN( "a short line" )
    {
        preview.setLine( QStringLiteral( "a\tshort line" ), { LinePreview::Mark{ 2, 5 } },
                         LinePreview::MarkStyle::Underline );

        THEN( "it shows whole, a tab as a blank, the mark marked" )
        {
            REQUIRE( preview.shownText() == QStringLiteral( "a short line" ) );
            REQUIRE( preview.markedTexts() == QStringList{ QStringLiteral( "short" ) } );
        }
    }

    GIVEN( "a long line, the mark in its middle" )
    {
        const auto line = QString( 200, u'x' ) + QStringLiteral( " VALUE " ) + QString( 200, u'y' );
        preview.setLine( line, { LinePreview::Mark{ line.indexOf( "VALUE" ), 5 } },
                         LinePreview::MarkStyle::Highlight );

        THEN( "it is cut on both sides, the mark in sight, and no wider than the label" )
        {
            const auto shown = preview.shownText();
            REQUIRE( shown.startsWith( Ellipsis ) );
            REQUIRE( shown.endsWith( Ellipsis ) );
            REQUIRE( shown.contains( QStringLiteral( "VALUE" ) ) );
            REQUIRE( preview.markedTexts() == QStringList{ QStringLiteral( "VALUE" ) } );
            REQUIRE( metrics.horizontalAdvance( shown ) <= preview.contentsRect().width() );
            REQUIRE( preview.height() < 2 * metrics.lineSpacing() );
        }

        AND_WHEN( "the label grows" )
        {
            const auto before = preview.shownText().size();
            preview.setFixedWidth( 480 );
            QTest::qWait( 20 );

            THEN( "it shows more of the line" )
            {
                REQUIRE( preview.shownText().size() > before );
            }
        }
    }

    GIVEN( "a long line, the mark at its end" )
    {
        const auto line = QString( 300, u'x' ) + QStringLiteral( " END" );
        preview.setLine( line, { LinePreview::Mark{ line.size() - 3, 3 } },
                         LinePreview::MarkStyle::Underline );

        THEN( "it is cut at its start only" )
        {
            REQUIRE( preview.shownText().startsWith( Ellipsis ) );
            REQUIRE( preview.shownText().endsWith( QStringLiteral( " END" ) ) );
        }
    }
}

SCENARIO( "A preview takes the colors of the Theme switched to", "[ui][addtogroup][theme]" )
{
    Theme::apply( Theme::LightKey );
    LinePreview preview;
    preview.setFixedWidth( 240 );
    showUntilExposed( preview );
    preview.setLine( QStringLiteral( "a VALUE here" ), { LinePreview::Mark{ 2, 5 } },
                     LinePreview::MarkStyle::Highlight );
    const auto highlightOf = []( const QWidget& widget ) {
        return widget.palette().color( QPalette::Highlight ).name();
    };
    const auto lightHighlight = highlightOf( preview );
    REQUIRE( preview.text().contains( lightHighlight ) );

    WHEN( "the Dark Theme is applied" )
    {
        Theme::apply( Theme::DarkKey );

        THEN( "the mark shows in the Dark Theme's highlight" )
        {
            REQUIRE( highlightOf( preview ) != lightHighlight );
            REQUIRE( preview.text().contains( highlightOf( preview ) ) );
            REQUIRE_FALSE( preview.text().contains( lightHighlight ) );
            REQUIRE( preview.markedTexts() == QStringList{ QStringLiteral( "VALUE" ) } );
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "Name This Value shows one row for the value, and More rows... the Name Table",
          "[ui][addtogroup][namevalue]" )
{
    GIVEN( "the dialog opened on a value of a new Name Table" )
    {
        const KeptNamingGroups kept;
        NameValueDialog dialog( EsimLine, rangeOf( EsimLine, QStringLiteral( "9" ) ) );
        showUntilExposed( dialog );
        auto* key = childOf<QLineEdit>( dialog, QStringLiteral( "valueKey" ) );
        auto* name = childOf<QLineEdit>( dialog, QStringLiteral( "valueName" ) );
        auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );

        THEN( "it shows the value as the key and an empty name, the focus in the name, not the "
              "table" )
        {
            REQUIRE_FALSE( dialog.showsRows() );
            REQUIRE( key->isVisible() );
            REQUIRE( key->text() == QStringLiteral( "9" ) );
            REQUIRE( name->isVisible() );
            REQUIRE( name->text().isEmpty() );
            REQUIRE( dialog.focusWidget() == name );
            REQUIRE_FALSE( rows->isVisible() );
            REQUIRE_FALSE(
                childOf<QPushButton>( dialog, QStringLiteral( "addRow" ) )->isVisible() );
        }

        WHEN( "a name is typed" )
        {
            QTest::keyClicks( name, QStringLiteral( "Ready" ) );

            THEN( "the rule OK adds names the value so, and the preview shows it" )
            {
                const auto added = dialog.ruleWithTable();
                REQUIRE( added.table.rows.size() == 1 );
                REQUIRE( added.table.rows.front().key == QStringLiteral( "9" ) );
                REQUIRE( added.table.rows.front().name == QStringLiteral( "Ready" ) );
                REQUIRE( dialog.shownPreview()
                         == QStringLiteral( "modem EsimState=Ready (9) done" ) );
            }

            AND_WHEN( "More rows... is clicked" )
            {
                QTest::mouseClick( childOf<QPushButton>( dialog, QStringLiteral( "moreRows" ) ),
                                   Qt::LeftButton );

                THEN( "the table shows with the row named, Add Row and Remove Row, instead of "
                      "the one row" )
                {
                    REQUIRE( dialog.showsRows() );
                    REQUIRE( rows->isVisible() );
                    REQUIRE(
                        childOf<QPushButton>( dialog, QStringLiteral( "addRow" ) )->isVisible() );
                    REQUIRE( childOf<QPushButton>( dialog, QStringLiteral( "removeRow" ) )
                                 ->isVisible() );
                    REQUIRE_FALSE( name->isVisible() );
                    REQUIRE( rows->rowCount() == 1 );
                    REQUIRE( rows->item( 0, 1 )->text() == QStringLiteral( "Ready" ) );
                    REQUIRE( dialog.focusWidget() == rows );
                }
            }
        }

        WHEN( "the table's row is named meanwhile" )
        {
            rows->item( 0, 1 )->setText( QStringLiteral( "Ready" ) );

            THEN( "the one row shows the name" )
            {
                REQUIRE( name->text() == QStringLiteral( "Ready" ) );
            }
        }
    }

    GIVEN( "a group with a Name Table of the name proposed, of a row already" )
    {
        auto modem
            = logsquirl::valuenames::NamingGroup::createNewGroup( QStringLiteral( "Modem" ) );
        logsquirl::valuenames::NameTable esim;
        esim.name = QStringLiteral( "EsimState" );
        esim.rows
            = { logsquirl::valuenames::NameRow{ QStringLiteral( "1" ), QStringLiteral( "Idle" ) } };
        modem.setTables( { esim } );
        const KeptNamingGroups kept( { modem } );

        NameValueDialog dialog( EsimLine, rangeOf( EsimLine, QStringLiteral( "9" ) ) );
        showUntilExposed( dialog );

        THEN( "the dialog opens with the one row of the value, the table's row and the value's "
              "in the table behind it" )
        {
            auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
            auto* name = childOf<QLineEdit>( dialog, QStringLiteral( "valueName" ) );
            REQUIRE_FALSE( dialog.showsRows() );
            REQUIRE_FALSE( rows->isVisible() );
            REQUIRE( rows->rowCount() == 2 );
            REQUIRE( name->isVisible() );
            REQUIRE( dialog.focusWidget() == name );
        }
    }

    GIVEN( "a group with a Name Table of the name proposed, of two rows already, and a group "
           "without tables" )
    {
        auto modem
            = logsquirl::valuenames::NamingGroup::createNewGroup( QStringLiteral( "Modem" ) );
        logsquirl::valuenames::NameTable esim;
        esim.name = QStringLiteral( "EsimState" );
        esim.rows
            = { logsquirl::valuenames::NameRow{ QStringLiteral( "1" ), QStringLiteral( "Idle" ) },
                logsquirl::valuenames::NameRow{ QStringLiteral( "2" ), QStringLiteral( "Busy" ) } };
        modem.setTables( { esim } );
        const auto other
            = logsquirl::valuenames::NamingGroup::createNewGroup( QStringLiteral( "Other" ) );
        const KeptNamingGroups kept( { modem, other } );

        NameValueDialog dialog( EsimLine, rangeOf( EsimLine, QStringLiteral( "9" ) ) );
        showUntilExposed( dialog );
        auto* rows = childOf<QTableWidget>( dialog, QStringLiteral( "nameRows" ) );
        auto* name = childOf<QLineEdit>( dialog, QStringLiteral( "valueName" ) );

        THEN( "the dialog opens with the table shown, the focus in it" )
        {
            REQUIRE( dialog.groupChooser()->choice().id == modem.id() );
            REQUIRE( dialog.showsRows() );
            REQUIRE( rows->isVisible() );
            REQUIRE( rows->rowCount() == 3 );
            REQUIRE( dialog.focusWidget() == rows );
            REQUIRE_FALSE( name->isVisible() );
        }

        WHEN( "the group without tables is chosen" )
        {
            dialog.groupChooser()->choose( other.id(), {} );

            THEN( "the one row of the value shows again" )
            {
                REQUIRE_FALSE( dialog.showsRows() );
                REQUIRE( rows->rowCount() == 1 );
                REQUIRE( name->isVisible() );
                REQUIRE( dialog.focusWidget() == name );
            }

            AND_WHEN( "the group of the table is chosen again" )
            {
                dialog.groupChooser()->choose( modem.id(), {} );

                THEN( "the table shows again" )
                {
                    REQUIRE( dialog.showsRows() );
                    REQUIRE( rows->rowCount() == 3 );
                }
            }
        }

        WHEN( "More rows... is clicked on the group without tables, and the groups are chosen in "
              "turn" )
        {
            dialog.groupChooser()->choose( other.id(), {} );
            REQUIRE_FALSE( dialog.showsRows() );
            QTest::mouseClick( childOf<QPushButton>( dialog, QStringLiteral( "moreRows" ) ),
                               Qt::LeftButton );
            REQUIRE( dialog.showsRows() );
            dialog.groupChooser()->choose( modem.id(), {} );
            dialog.groupChooser()->choose( other.id(), {} );

            THEN( "the table stays shown, as the user asked for it" )
            {
                REQUIRE( dialog.showsRows() );
                REQUIRE( rows->rowCount() == 1 );
                REQUIRE( rows->isVisible() );
            }
        }
    }
}

SCENARIO( "Name This Value previews the message of the Log Line, the value marked, on one line",
          "[ui][addtogroup][namevalue]" )
{
    const auto line
        = QStringLiteral( "2026-11-11 10:00:00.123 INFO [main] modem EsimState=9 done" );
    NameValueDialog dialog( line, rangeOf( line, QStringLiteral( "9" ) ) );
    showUntilExposed( dialog );
    auto* preview = dialog.addToGroupLayout().preview();

    THEN( "without a message the Log Line shows, the value marked" )
    {
        REQUIRE( dialog.shownPreview()
                 == QString( line ).replace( QStringLiteral( "=9" ), Unnamed ) );
        REQUIRE( preview->markedTexts() == QStringList{ QStringLiteral( " (9)" ) } );
    }

    WHEN( "the dialog is told where the message lies" )
    {
        dialog.showMessageOnly( rangeOf( line, QStringLiteral( "modem EsimState=9 done" ) ) );

        THEN( "only the message shows, the value marked" )
        {
            REQUIRE( dialog.shownPreview() == QStringLiteral( "modem EsimState= (9) done" ) );
            REQUIRE( preview->shownText() == QStringLiteral( "modem EsimState= (9) done" ) );
            REQUIRE( preview->markedTexts() == QStringList{ QStringLiteral( " (9)" ) } );
        }

        AND_WHEN( "the value is named" )
        {
            QTest::keyClicks( childOf<QLineEdit>( dialog, QStringLiteral( "valueName" ) ),
                              QStringLiteral( "Ready" ) );

            THEN( "the value shows by its name, marked" )
            {
                REQUIRE( preview->shownText()
                         == QStringLiteral( "modem EsimState=Ready (9) done" ) );
                REQUIRE( preview->markedTexts() == QStringList{ QStringLiteral( "Ready (9)" ) } );
            }
        }
    }

    WHEN( "the dialog is told of a message the value is not in" )
    {
        dialog.showMessageOnly( rangeOf( line, QStringLiteral( "INFO" ) ) );

        THEN( "the Log Line shows" )
        {
            REQUIRE( dialog.shownPreview()
                     == QString( line ).replace( QStringLiteral( "=9" ), Unnamed ) );
        }
    }

    GIVEN( "a Log Line too long for the preview" )
    {
        const auto longLine
            = QString( 300, u'-' ) + QStringLiteral( " EsimState=9 " ) + QString( 300, u'-' );
        NameValueDialog longDialog( longLine, rangeOf( longLine, QStringLiteral( "9" ) ) );
        showUntilExposed( longDialog );
        auto* longPreview = longDialog.addToGroupLayout().preview();

        THEN( "it is elided on one line, the value in sight" )
        {
            REQUIRE( longPreview->shownText().startsWith( Ellipsis ) );
            REQUIRE( longPreview->shownText().endsWith( Ellipsis ) );
            REQUIRE( longPreview->markedTexts() == QStringList{ QStringLiteral( " (9)" ) } );
            REQUIRE( longPreview->height() < 2 * longPreview->fontMetrics().lineSpacing() );
        }
    }
}

SCENARIO( "The message of a Log Line is the body field of the tab's Log Format",
          "[ui][addtogroup][namevalue]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto formats = directory.filePath( QStringLiteral( "formats" ) );
    REQUIRE( QDir().mkpath( formats ) );
    {
        QFile format( QDir( formats ).filePath( QStringLiteral( "message_test.json" ) ) );
        REQUIRE( format.open( QIODevice::WriteOnly | QIODevice::Text ) );
        format.write( R"({
    "message_test_log": {
        "title": "Message test",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>\\d{2}:\\d{2}:\\d{2}) (?<level>[A-Z]+) \\[(?<thread>\\w+)\\] (?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "sample": [{ "line": "10:00:00 INFO [main] modem EsimState=9 done" }]
    }
})" );
    }
    const auto logLine = QStringLiteral( "10:00:00 INFO [main] modem EsimState=9 done" );
    const auto path = directory.filePath( QStringLiteral( "message.log" ) );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        for ( int index = 0; index < 20; ++index ) {
            file.write( logLine.toUtf8() + '\n' );
        }
    }

    auto catalog = std::make_shared<LogFormatCatalog>( formats );
    catalog->rebuild();
    auto policies = testSettingsPolicies();
    policies.recognition.enabled = true;
    Session session( policies, catalog, nullptr );
    std::unique_ptr<CrawlerWidget> crawler(
        static_cast<CrawlerWidget*>( session.open( path, buildCrawlerWidget ) ) );
    showUntilExposed( *crawler );
    REQUIRE( waitUiState( [ & ] {
        return crawler->state().loadStatus == LoadingStatus::Successful
               && !crawler->state().logFormatName.isEmpty();
    } ) );

    THEN( "it lies after the timestamp, the level and the thread" )
    {
        const auto message = crawler->messageOf( logLine );
        REQUIRE( message.has_value() );
        REQUIRE( logLine.mid( message->start, message->length )
                 == QStringLiteral( "modem EsimState=9 done" ) );
    }

    THEN( "a Log Line the Log Format does not match has none" )
    {
        REQUIRE_FALSE( crawler->messageOf( QStringLiteral( "no format here" ) ).has_value() );
    }

    crawler.reset();
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
}

SCENARIO( "The preview's Search counts off the UI thread and follows the pattern",
          "[ui][addtogroup][addpredefinedfilter]" )
{
    const auto lines = previewLines( 2000 );
    const LoadedLogFile logFile( lines );
    FilterPreviewSearch search( logFile.logData );

    WHEN( "a pattern is set" )
    {
        search.setPattern(
            RegularExpressionPattern( QStringLiteral( "a.c" ), true, false, false, true ) );

        THEN( "nothing is counted yet: the count comes later, from the event loop" )
        {
            REQUIRE( search.result().state == PreviewState::Searching );
            REQUIRE( search.result().matchCount.get() == 0 );
            REQUIRE( waitUiState( [ & ] { return search.result().state == PreviewState::Done; } ) );
            REQUIRE( search.result().matchCount.get()
                     == static_cast<uint64_t>( linesWith( lines, QStringLiteral( "a.c" ) ) ) );
            REQUIRE( search.result().firstMatch == LineNumber( 3 ) );
            REQUIRE( search.result().firstLine == lines[ 3 ] );
        }

        AND_WHEN( "another pattern is set before the first one has settled" )
        {
            QList<FilterPreviewSearch::Result> told;
            QObject::connect( &search, &FilterPreviewSearch::resultChanged,
                              [ & ] { told.append( search.result() ); } );
            search.setPattern(
                RegularExpressionPattern( QStringLiteral( "abc" ), true, false, false, true ) );

            THEN( "only the last one is counted" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return search.result().state == PreviewState::Done; } ) );
                REQUIRE( search.result().matchCount.get()
                         == static_cast<uint64_t>( linesWith( lines, QStringLiteral( "abc" ) ) ) );
                for ( const auto& result : told ) {
                    if ( result.state == PreviewState::Done ) {
                        REQUIRE( result.matchCount == search.result().matchCount );
                    }
                }
            }
        }

        AND_WHEN( "it was counted, and the pattern changes" )
        {
            REQUIRE( waitUiState( [ & ] { return search.result().state == PreviewState::Done; } ) );
            search.setPattern(
                RegularExpressionPattern( QStringLiteral( "a.c" ), true, false, false, false ) );

            THEN( "the count of the pattern before is gone at once, and the new one comes" )
            {
                REQUIRE( search.result().state == PreviewState::Searching );
                REQUIRE_FALSE( search.result().firstMatch.has_value() );
                REQUIRE(
                    waitUiState( [ & ] { return search.result().state == PreviewState::Done; } ) );
                // As a regex, "a.c" finds "abc" too.
                REQUIRE( search.result().matchCount.get()
                         > static_cast<uint64_t>( linesWith( lines, QStringLiteral( "a.c" ) ) ) );
            }
        }

        AND_WHEN( "the pattern is emptied" )
        {
            search.setPattern( RegularExpressionPattern( QString{}, true, false, false, true ) );

            THEN( "nothing is searched for" )
            {
                REQUIRE( search.result().state == PreviewState::Idle );
                QTest::qWait( FilterPreviewSearch::SettleMilliseconds * 2 );
                REQUIRE( search.result().state == PreviewState::Idle );
            }
        }
    }
}

SCENARIO( "Add as Predefined Filter previews how many Log Lines the filter finds, and the first",
          "[ui][addtogroup][addpredefinedfilter]" )
{
    const auto lines = previewLines( 2000 );
    const LoadedLogFile logFile( lines );
    AddPredefinedFilterDialog dialog( QStringLiteral( "a.c" ) );
    showUntilExposed( dialog );
    auto* preview = dialog.addToGroupLayout().preview();
    auto* note = dialog.addToGroupLayout().previewNote();

    THEN( "without a Log File, the preview says there is none" )
    {
        REQUIRE( preview->shownText() == QStringLiteral( "No Log File to search in." ) );
    }

    WHEN( "it is given the Log File" )
    {
        dialog.previewIn( logFile.logData, matchingCase() );

        THEN( "it counts in the background, the dialog going on meanwhile" )
        {
            REQUIRE( dialog.previewResult().state == PreviewState::Searching );
            int ticks = 0;
            QTimer ticker;
            QObject::connect( &ticker, &QTimer::timeout, [ &ticks ] { ++ticks; } );
            ticker.start( 10 );
            REQUIRE( waitUiState( [ & ] { return previewDone( dialog ); } ) );
            REQUIRE( ticks > 0 );
        }

        THEN( "it shows the number of Log Lines found and the first, the match marked" )
        {
            REQUIRE( waitUiState( [ & ] { return previewDone( dialog ); } ) );
            const auto count = linesWith( lines, QStringLiteral( "a.c" ) );
            REQUIRE( dialog.previewResult().matchCount.get() == static_cast<uint64_t>( count ) );
            REQUIRE( note->isVisible() );
            REQUIRE( note->text().contains( QString::number( count ) ) );
            REQUIRE( preview->shownText().contains( QStringLiteral( "request 3 served" ) ) );
            REQUIRE( preview->markedTexts() == QStringList{ QStringLiteral( "a.c" ) } );
        }

        AND_WHEN( "the pattern is edited" )
        {
            REQUIRE( waitUiState( [ & ] { return previewDone( dialog ); } ) );
            auto* pattern = childOf<QLineEdit>( dialog, QStringLiteral( "filterPattern" ) );
            pattern->setText( QStringLiteral( "abc" ) );

            THEN( "the preview follows" )
            {
                REQUIRE( waitUiState( [ & ] {
                    return previewDone( dialog )
                           && dialog.previewResult().matchCount.get()
                                  == static_cast<uint64_t>(
                                      linesWith( lines, QStringLiteral( "abc" ) ) );
                } ) );
                REQUIRE( preview->shownText().contains( QStringLiteral( "request 1 served" ) ) );
                REQUIRE( preview->markedTexts() == QStringList{ QStringLiteral( "abc" ) } );
            }
        }

        AND_WHEN( "Regular expression is checked and the pattern edited into a regex" )
        {
            REQUIRE( waitUiState( [ & ] { return previewDone( dialog ); } ) );
            childOf<QCheckBox>( dialog, QStringLiteral( "filterRegex" ) )->setChecked( true );
            THEN( "checked, the escaped pattern finds the same Log Lines" )
            {
                REQUIRE( waitUiState( [ & ] { return previewDone( dialog ); } ) );
                REQUIRE( dialog.previewResult().matchCount.get()
                         == static_cast<uint64_t>( linesWith( lines, QStringLiteral( "a.c" ) ) ) );
            }

            AND_WHEN( "the regex is edited" )
            {
                REQUIRE( waitUiState( [ & ] { return previewDone( dialog ); } ) );
                childOf<QLineEdit>( dialog, QStringLiteral( "filterPattern" ) )
                    ->setText( QStringLiteral( "a.c$" ) );

                THEN( "the preview reads it as a regex" )
                {
                    const auto expected
                        = std::count_if( lines.cbegin(), lines.cend(), []( const QString& line ) {
                              return line.endsWith( QStringLiteral( " a.c" ) )
                                     || line.endsWith( QStringLiteral( " abc" ) );
                          } );
                    REQUIRE( waitUiState( [ & ] {
                        return previewDone( dialog )
                               && dialog.previewResult().matchCount.get()
                                      == static_cast<uint64_t>( expected );
                    } ) );
                }
            }
        }

        AND_WHEN( "the pattern finds nothing" )
        {
            REQUIRE( waitUiState( [ & ] { return previewDone( dialog ); } ) );
            childOf<QLineEdit>( dialog, QStringLiteral( "filterPattern" ) )
                ->setText( QStringLiteral( "nowhere to be found" ) );

            THEN( "the preview says so" )
            {
                REQUIRE( waitUiState( [ & ] {
                    return previewDone( dialog ) && dialog.previewResult().matchCount.get() == 0;
                } ) );
                REQUIRE( preview->shownText()
                         == QStringLiteral( "The pattern finds no Log Line in this Log File." ) );
            }
        }
    }
}

SCENARIO( "The preview of a large Log File does not hold the dialog up",
          "[ui][addtogroup][addpredefinedfilter]" )
{
    const auto lines = previewLines( 300'000 );
    const LoadedLogFile logFile( lines );
    AddPredefinedFilterDialog dialog( QStringLiteral( "abc" ) );
    showUntilExposed( dialog );
    dialog.previewIn( logFile.logData, matchingCase() );

    WHEN( "the pattern is typed while the Search runs" )
    {
        auto* pattern = childOf<QLineEdit>( dialog, QStringLiteral( "filterPattern" ) );
        QTest::qWait( FilterPreviewSearch::SettleMilliseconds + 20 );
        pattern->clear();
        QTest::keyClicks( pattern, QStringLiteral( "a.c" ) );

        THEN( "the keys reach the pattern at once, and the count is that of the pattern typed" )
        {
            REQUIRE( pattern->text() == QStringLiteral( "a.c" ) );
            REQUIRE( dialog.previewResult().state == PreviewState::Searching );
            REQUIRE( waitUiState( [ & ] { return previewDone( dialog ); } ) );
            REQUIRE( dialog.previewResult().matchCount.get()
                     == static_cast<uint64_t>( linesWith( lines, QStringLiteral( "a.c" ) ) ) );
        }
    }
}
