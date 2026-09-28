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

// The Table View has no gutter, so it colors the Row of a Match, a Mark and a
// Mark that is a Match. The colors are the active Theme's, read when a Row is
// painted, so a Theme switch recolors the Rows (#590).

#include "highlighterset.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logtablehighlightdelegate.h"
#include "regularexpressionpattern.h"
#include "test_policies.h"
#include "test_utils.h"
#include "theme.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTemporaryFile>

#include <catch2/catch_test_macros.hpp>

namespace {

// Log Line 0 is a Match, 1 a Mark, 2 a Mark that is a Match, 3 neither.
constexpr int NbLogLines = 4;
constexpr int MatchLine = 0;
constexpr int MarkLine = 1;
constexpr int MarkedMatchLine = 2;
constexpr int PlainLine = 3;

// A cell far wider than its text, so its right edge shows the Row's color.
const QRect CellRect( 0, 0, 200, 20 );

// A loaded Log File with a Search, its Matches and Marks, and the state a
// Table View hands its delegate. The Highlighter Sets are the developer's
// own and would color Rows: none is active while this lives.
struct TableOfMarkedLogFile {
    TableOfMarkedLogFile()
        : logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
        , activeHighlighterSets( HighlighterSetCollection::get().activeSetIds() )
        , model( NbLogLines, 1 )
    {
        HighlighterSetCollection::get().deactivateAll();

        REQUIRE( file.open() );
        for ( int line = 0; line < NbLogLines; ++line ) {
            const bool isMatch = line == MatchLine || line == MarkedMatchLine;
            file.write( QStringLiteral( "%1 %2\n" )
                            .arg( isMatch ? "match" : "other" )
                            .arg( line )
                            .toLatin1() );
            model.setData( model.index( line, 0 ), QStringLiteral( "x" ) );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();
        SafeQSignalSpy searchStateSpy{ filteredData.get(), &LogFilteredData::searchStateChanged };
        filteredData->request( RegularExpressionPattern( QStringLiteral( "match" ) ) );
        REQUIRE( waitUiState( [ & ]() {
            return searchStateSpy.count() > 0
                   && qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) ).progress
                          >= 100;
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
        filteredData->addMark( LineNumber( MarkLine ) );
        filteredData->addMark( LineNumber( MarkedMatchLine ) );

        state.currentSearch = filteredData.get();
    }

    ~TableOfMarkedLogFile()
    {
        for ( const auto& setId : activeHighlighterSets ) {
            HighlighterSetCollection::get().activateSet( setId );
        }
        Theme::apply( Theme::defaultTheme() );
    }

    TableOfMarkedLogFile( const TableOfMarkedLogFile& ) = delete;
    TableOfMarkedLogFile& operator=( const TableOfMarkedLogFile& ) = delete;

    // The color the delegate paints the right edge of the Row of line in,
    // with the application's palette as the Table View has it. The same
    // delegate paints before and after a Theme switch, as in a Table View.
    QColor rowColor( int line )
    {
        QImage image( CellRect.size(), QImage::Format_ARGB32 );
        image.fill( Qt::transparent );

        QStyleOptionViewItem option;
        option.rect = CellRect;
        option.state = QStyle::State_Enabled;
        option.palette = QApplication::palette();

        QPainter painter( &image );
        delegate.paint( &painter, option, model.index( line, 0 ) );
        painter.end();
        return image.pixelColor( CellRect.width() - 2, CellRect.height() / 2 );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "tableview_line_status_theme_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
    QStringList activeHighlighterSets;
    QStandardItemModel model;
    TableViewState state;
    LogTableHighlightDelegate delegate{ state, std::make_shared<OneRowPerLogLine>() };
};

} // namespace

SCENARIO( "The Table View colors a Match's and a Mark's Row in the Theme's colors",
          "[tableview][theme]" )
{
    GIVEN( "a Table View's delegate over a Log File with a Match, a Mark and a Marked Match" )
    {
        TableOfMarkedLogFile table;

        THEN( "in every built-in Theme each Row has that Theme's Row color" )
        {
            for ( const auto& name : Theme::builtInThemes() ) {
                INFO( name.toStdString() );
                Theme::apply( name );
                const auto& theme = Theme::active();
                REQUIRE( table.rowColor( MatchLine ) == theme.color( ColorToken::MatchRow ) );
                REQUIRE( table.rowColor( MarkLine ) == theme.color( ColorToken::MarkRow ) );
                REQUIRE( table.rowColor( MarkedMatchLine )
                         == theme.color( ColorToken::MarkedMatchRow ) );
                REQUIRE( table.rowColor( PlainLine ) == theme.color( ColorToken::Base ) );
            }
        }

        WHEN( "the Theme is switched" )
        {
            Theme::apply( Theme::LightKey );
            const auto before = table.rowColor( MatchLine );
            Theme::apply( Theme::SmyckKey );
            const auto after = table.rowColor( MatchLine );

            THEN( "the Match's Row takes the new Theme's color" )
            {
                REQUIRE( before
                         == Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light )
                                .color( ColorToken::MatchRow ) );
                REQUIRE( after == Theme::active().color( ColorToken::MatchRow ) );
                REQUIRE( before != after );
            }
        }
    }
}
