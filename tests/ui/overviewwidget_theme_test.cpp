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

// The overview beside the scroll bar shows Matches and Marks in every Theme:
// each line stands out against the overview background, the frame and the
// visible-area indicator are drawn in the Theme's colors, and a Theme switch
// repaints it (#255).

#include "logdata.h"
#include "logfiltereddata.h"
#include "overview.h"
#include "overviewwidget.h"
#include "regularexpressionpattern.h"
#include "test_policies.h"
#include "test_utils.h"
#include "theme.h"

#include <QImage>
#include <QTemporaryFile>
#include <QTest>

#include <algorithm>
#include <cmath>

#include <catch2/catch.hpp>

namespace {

// As many Log Lines as the overview is high, so that each Log Line is drawn
// on the pixel row of its own number.
constexpr int NbLogLines = 60;
constexpr int OverviewWidth = 30;

// Every fourth Log Line from 0 on is a Match, every fourth from 2 on a Mark.
bool isMatch( int line )
{
    return line % 4 == 0;
}
bool isMark( int line )
{
    return line % 4 == 2;
}

// The visible area spans Log Lines 1 to 3, rows no Match or Mark is drawn on.
const LineNumber FirstVisibleLine{ 1 };
const LineNumber LastVisibleLine{ 3 };

// A row that shows neither a Match, a Mark nor the visible-area indicator.
constexpr int EmptyRow = 5;

double contrast( const QColor& first, const QColor& second )
{
    const auto luminance = []( const QColor& color ) {
        const auto linear = []( double c ) {
            return c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 );
        };
        return 0.2126 * linear( static_cast<double>( color.redF() ) )
               + 0.7152 * linear( static_cast<double>( color.greenF() ) )
               + 0.0722 * linear( static_cast<double>( color.blueF() ) );
    };
    const auto darker = std::min( luminance( first ), luminance( second ) );
    const auto lighter = std::max( luminance( first ), luminance( second ) );
    return ( lighter + 0.05 ) / ( darker + 0.05 );
}

// A loaded Log File with Matches and Marks, and an overview widget showing
// them.
struct OverviewOfMarkedLogFile {
    OverviewOfMarkedLogFile()
        : logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
    {
        REQUIRE( file.open() );
        for ( int line = 0; line < NbLogLines; ++line ) {
            file.write( QStringLiteral( "%1 line %2\n" )
                            .arg( isMatch( line ) ? "match" : "other" )
                            .arg( line )
                            .toLatin1() );
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
        for ( int line = 0; line < NbLogLines; ++line ) {
            if ( isMark( line ) ) {
                filteredData->addMark(
                    LineNumber( static_cast<LineNumber::UnderlyingType>( line ) ) );
            }
        }

        overview.setFilteredData( filteredData.get() );
        overview.updateData( logData.getNbLine() );
        overview.setVisible( true );
        overview.updateCurrentPosition( FirstVisibleLine, LastVisibleLine );

        widget.setOverview( &overview );
        widget.resize( OverviewWidth, NbLogLines );
        widget.show();
        QTest::qWait( 20 );
    }

    ~OverviewOfMarkedLogFile()
    {
        widget.hide();
        Theme::apply( Theme::defaultTheme() );
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file{ "overview_theme_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
    Overview overview;
    OverviewWidget widget;
};

class PaintCounter : public QObject {
public:
    int count = 0;

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override
    {
        if ( event->type() == QEvent::Paint ) {
            ++count;
        }
        return QObject::eventFilter( watched, event );
    }
};

} // namespace

SCENARIO( "The overview shows Matches and Marks in every Theme", "[ui][theme][overview]" )
{
    OverviewOfMarkedLogFile fixture;

    const auto themeName
        = GENERATE( as<QString>{}, Theme::LightKey, Theme::DarkKey, Theme::HighContrastKey );
    INFO( themeName.toStdString() );
    Theme::apply( themeName );
    QCoreApplication::processEvents();
    const auto& theme = Theme::active();

    const auto image = fixture.widget.grab().toImage();
    REQUIRE( image.height() == NbLogLines );
    const auto middle = OverviewWidth / 2;
    const auto pixel = [ &image ]( int x, int y ) { return QColor( image.pixel( x, y ) ); };
    const auto background = pixel( middle, EmptyRow );

    THEN( "the overview background is the Theme's" )
    {
        REQUIRE( background == theme.color( ColorToken::Window ) );
    }

    THEN( "the frame and the visible-area indicator are drawn in the Theme's text color" )
    {
        const auto text = theme.color( ColorToken::Text );
        REQUIRE( pixel( 0, EmptyRow ) == text );
        REQUIRE( pixel( middle, static_cast<int>( FirstVisibleLine.get() ) ) == text );
        REQUIRE( pixel( middle, static_cast<int>( LastVisibleLine.get() ) ) == text );
    }

    THEN( "every Match and Mark line reaches 3:1 against the background, "
          "and a Match stays red and a Mark blue" )
    {
        for ( int row = 0; row < NbLogLines; ++row ) {
            if ( !isMatch( row ) && !isMark( row ) ) {
                continue;
            }
            INFO( "row " << row );
            const auto line = pixel( middle, row );
            CHECK( contrast( line, background ) >= 3.0 );
            if ( isMatch( row ) ) {
                CHECK( line.red() > line.blue() );
            }
            else {
                CHECK( line.blue() > line.red() );
            }
        }
    }
}

SCENARIO( "A highlighted line is flashed in the Theme's highlight color", "[ui][theme][overview]" )
{
    OverviewOfMarkedLogFile fixture;

    const auto themeName
        = GENERATE( as<QString>{}, Theme::LightKey, Theme::DarkKey, Theme::HighContrastKey );
    INFO( themeName.toStdString() );
    Theme::apply( themeName );
    QCoreApplication::processEvents();
    const auto& theme = Theme::active();

    WHEN( "a Log Line is highlighted" )
    {
        constexpr int HighlightedRow = 30;
        fixture.widget.highlightLine( LineNumber( HighlightedRow ) );
        const auto image = fixture.widget.grab().toImage();

        THEN( "the highlight frame is drawn in the Theme's highlight color, 3:1 against the "
              "background" )
        {
            // The first frame's top edge, two rows above the Log Line's row.
            const QColor frame( image.pixel( OverviewWidth / 2, HighlightedRow - 2 ) );
            REQUIRE( frame == theme.color( ColorToken::Highlight ) );
            REQUIRE( contrast( frame, theme.color( ColorToken::Window ) ) >= 3.0 );
        }
    }
}

SCENARIO( "A Theme switch repaints the overview", "[ui][theme][overview]" )
{
    Theme::apply( Theme::LightKey );
    OverviewOfMarkedLogFile fixture;
    PaintCounter paints;
    fixture.widget.installEventFilter( &paints );

    for ( const auto themeName : { Theme::DarkKey, Theme::HighContrastKey, Theme::LightKey } ) {
        INFO( QString( themeName ).toStdString() );
        const auto paintsBefore = paints.count;
        Theme::apply( themeName );
        REQUIRE( waitUiState( [ & ] { return paints.count > paintsBefore; } ) );
    }
}
