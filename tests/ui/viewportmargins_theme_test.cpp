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

// The Viewport's margins -- the bullet zone and the line numbers -- are drawn
// in the Tokens of the active Theme, and follow a Theme switch (#254). The
// Match and Mark bullets keep their own colors, which the Theme does not set,
// so these check that they stay visible against every Theme's margin.
//
// The view inherits the application's palette, as every view in the
// application does; what it paints is read back with grab().

#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

#include <QCoreApplication>
#include <QImage>

#include "abstractlogview.h"
#include "fake_log_data.h"
#include "linemapping.h"
#include "quickfindpattern.h"
#include "test_policies.h"
#include "theme.h"
#include "viewportlayout.h"

namespace {

using LineTypeFlags = AbstractLogData::LineTypeFlags;

// The first Log Lines are a plain one, a Match, a Mark, and one that is both.
constexpr int PlainRow = 0;
constexpr int MatchRow = 1;
constexpr int MarkRow = 2;
constexpr int MarkedMatchRow = 3;
// A row below the last Log Line: nothing is drawn on its margins.
constexpr int EmptyRow = 5;

class StatusLineTypes : public EveryLogLine {
public:
    using EveryLogLine::EveryLogLine;

    LineType lineType( LineNumber lineNumber ) const override
    {
        switch ( lineNumber.get() ) {
        case MatchRow:
            return LineTypeFlags::Match;
        case MarkRow:
            return LineTypeFlags::Mark;
        case MarkedMatchRow:
            return LineTypeFlags::Mark | LineTypeFlags::Match;
        default:
            return LineTypeFlags::Plain;
        }
    }
};

class MarginsLogView : public AbstractLogView {
public:
    MarginsLogView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern )
        : AbstractLogView( logData, std::make_unique<StatusLineTypes>( logData ), quickFindPattern,
                           false )
    {
    }
};

// A view with line numbers, shown the way the application shows one.
struct ShownView {
    ShownView()
        : logData( QStringList{ "10:00:00 INFO  plain", "10:00:01 ERROR a Match",
                                "10:00:02 WARN  a Mark", "10:00:03 ERROR a marked Match" } )
        , view( &logData, &quickFindPattern )
    {
        view.setFrameShape( QFrame::NoFrame );
        view.setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        view.setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        view.resize( 400, 240 );
        view.show();
        QCoreApplication::processEvents();

        view.setPresentationPolicy( testSettingsPolicies().presentation );
        view.setLineNumbersVisible( true );
        view.updateData();
        QCoreApplication::processEvents();
    }

    QImage grab()
    {
        return view.viewport()->grab().toImage().convertToFormat( QImage::Format_RGB32 );
    }

    FakeLogData logData;
    QuickFindPattern quickFindPattern;
    MarginsLogView view;
};

double relativeLuminance( const QColor& color )
{
    const auto linear = []( int channel ) {
        const auto value = channel / 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow( ( value + 0.055 ) / 1.055, 2.4 );
    };
    return 0.2126 * linear( color.red() ) + 0.7152 * linear( color.green() )
           + 0.0722 * linear( color.blue() );
}

// The WCAG contrast ratio of two colors, from 1 to 21.
double contrast( const QColor& first, const QColor& second )
{
    const auto lighter = std::max( relativeLuminance( first ), relativeLuminance( second ) );
    const auto darker = std::min( relativeLuminance( first ), relativeLuminance( second ) );
    return ( lighter + 0.05 ) / ( darker + 0.05 );
}

// The highest contrast any pixel of area has against background.
double highestContrast( const QImage& image, const QRect& area, const QColor& background )
{
    double highest = 1.0;
    for ( int y = area.top(); y <= area.bottom(); ++y ) {
        for ( int x = area.left(); x <= area.right(); ++x ) {
            highest = std::max( highest, contrast( image.pixelColor( x, y ), background ) );
        }
    }
    return highest;
}

struct Margins {
    int rowHeight;
    // The bullet zone, without its separator.
    QRect bulletZone;
    // The line-number area, without its separators and padding.
    QRect lineNumbers;

    explicit Margins( const ViewportLayout& layout )
        : rowHeight( layout.input().charHeightPx )
        , bulletZone( 0, 0, ViewportLayout::BulletAreaWidth, layout.input().viewportHeightPx )
        , lineNumbers( layout.lineNumberAreaStartX() + ViewportLayout::LineNumberPadding, 0,
                       layout.lineNumberAreaWidthPx() - 2 * ViewportLayout::LineNumberPadding,
                       layout.input().viewportHeightPx )
    {
    }

    int middleOf( int row ) const
    {
        return row * rowHeight + rowHeight / 2;
    }

    // Where a row's bullet is drawn.
    QRect bulletOf( int row ) const
    {
        return QRect( bulletZone.left(), row * rowHeight, bulletZone.width(), rowHeight );
    }

    // A pixel inside a row's bullet, circle or arrow alike.
    QPoint bulletFillOf( int row ) const
    {
        return QPoint( ViewportLayout::BulletAreaWidth / 2 - 1, middleOf( row ) );
    }

    QRect lineNumberOf( int row ) const
    {
        return QRect( lineNumbers.left(), row * rowHeight, lineNumbers.width(), rowHeight );
    }
};

// Requires that the margins of the painted image are drawn in theme's Tokens.
void requireMarginsInTokens( const QImage& image, const Margins& margins, const Theme& theme )
{
    const auto marginColor = theme.color( ColorToken::ViewportMargin );
    const auto emptyRowY = margins.middleOf( EmptyRow );

    THEN( "the bullet zone and the line-number area have the Theme's margin color" )
    {
        REQUIRE( image.pixelColor( margins.bulletZone.left() + 1, emptyRowY ).rgb()
                 == marginColor.rgb() );
        REQUIRE( image.pixelColor( margins.lineNumbers.left() + 1, emptyRowY ).rgb()
                 == marginColor.rgb() );
    }

    THEN( "line numbers have at least WCAG AA contrast against the margin" )
    {
        REQUIRE( highestContrast( image, margins.lineNumberOf( PlainRow ), marginColor ) >= 4.5 );
    }
}

} // namespace

SCENARIO( "The Viewport's margins are drawn in the Tokens of every Theme", "[ui][theme][viewport]" )
{
    for ( const auto& name : { QString( Theme::LightKey ), QString( Theme::DarkKey ),
                               QString( Theme::HighContrastKey ), QString( Theme::SmyckKey ),
                               QString( Theme::SmyckLightKey ) } ) {
        GIVEN( "a view with line numbers shown under the " << name.toStdString() << " Theme" )
        {
            Theme::apply( name );
            ShownView shown;
            const Margins margins( shown.view.viewportLayout() );
            const auto image = shown.grab();
            const auto& theme = Theme::active();
            const auto marginColor = theme.color( ColorToken::ViewportMargin );

            requireMarginsInTokens( image, margins, theme );

            THEN( "every bullet stands out from the margin" )
            {
                for ( const auto row : { PlainRow, MatchRow, MarkRow, MarkedMatchRow } ) {
                    INFO( "row " << row );
                    REQUIRE( highestContrast( image, margins.bulletOf( row ), marginColor )
                             >= 3.0 );
                }
            }

            THEN( "the Match, Mark and plain bullets are told apart by their colors" )
            {
                const auto fill = [ & ]( int row ) {
                    return image.pixelColor( margins.bulletFillOf( row ) ).rgb();
                };
                const std::vector<QRgb> fills{ fill( PlainRow ), fill( MatchRow ), fill( MarkRow ),
                                               fill( MarkedMatchRow ) };
                for ( std::size_t i = 0; i < fills.size(); ++i ) {
                    for ( std::size_t j = i + 1; j < fills.size(); ++j ) {
                        INFO( "rows " << i << " and " << j );
                        REQUIRE( fills[ i ] != fills[ j ] );
                    }
                }
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "The Viewport's margins follow a Theme switch", "[ui][theme][viewport]" )
{
    GIVEN( "a view with line numbers painted under the Light Theme" )
    {
        Theme::apply( Theme::LightKey );
        ShownView shown;
        const Margins margins( shown.view.viewportLayout() );
        const auto lightImage = shown.grab();
        REQUIRE(
            lightImage.pixelColor( margins.bulletZone.left() + 1, margins.middleOf( EmptyRow ) )
                .rgb()
            == Theme::active().color( ColorToken::ViewportMargin ).rgb() );

        for ( const auto& name : { QString( Theme::DarkKey ), QString( Theme::HighContrastKey ),
                                   QString( Theme::SmyckKey ), QString( Theme::SmyckLightKey ) } ) {
            WHEN( "the " << name.toStdString() << " Theme is applied" )
            {
                Theme::apply( name );
                QCoreApplication::processEvents();

                requireMarginsInTokens( shown.grab(), margins, Theme::active() );
            }
        }

        Theme::apply( Theme::defaultTheme() );
    }
}
