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

// Radio buttons and sliders are styled from Tokens in every Theme: the ring
// of a radio button and the handle of a slider stand out against the window
// (WCAG non-text contrast, 3:1), a checked radio button and the filled part
// of a slider show the Theme's Highlight, and disabled ones look different
// from enabled ones (#258).

#include "theme.h"

#include <QApplication>
#include <QImage>
#include <QRadioButton>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionSlider>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

namespace {

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
    const auto a = luminance( first );
    const auto b = luminance( second );
    return ( std::max( a, b ) + 0.05 ) / ( std::min( a, b ) + 0.05 );
}

// The highest contrast any pixel of area reaches against background.
double bestContrast( const QImage& image, const QRect& area, const QColor& background )
{
    double best = 1.0;
    for ( int y = area.top(); y <= area.bottom(); ++y ) {
        for ( int x = area.left(); x <= area.right(); ++x ) {
            best = std::max( best, contrast( image.pixelColor( x, y ), background ) );
        }
    }
    return best;
}

bool contains( const QImage& image, const QRect& area, const QColor& color )
{
    for ( int y = area.top(); y <= area.bottom(); ++y ) {
        for ( int x = area.left(); x <= area.right(); ++x ) {
            if ( image.pixelColor( x, y ).rgb() == color.rgb() ) {
                return true;
            }
        }
    }
    return false;
}

class RadioButton : public QRadioButton {
public:
    RadioButton( bool checked, bool enabled )
        : QRadioButton( QStringLiteral( "Radio button" ) )
    {
        setAutoExclusive( false );
        setChecked( checked );
        setEnabled( enabled );
    }

    // Where the indicator is drawn, in window coordinates.
    QRect indicator() const
    {
        QStyleOptionButton option;
        initStyleOption( &option );
        const auto rect = style()->subElementRect( QStyle::SE_RadioButtonIndicator, &option, this );
        return rect.translated( mapTo( window(), QPoint( 0, 0 ) ) );
    }
};

class Slider : public QSlider {
public:
    Slider( Qt::Orientation orientation, bool enabled )
        : QSlider( orientation )
    {
        setRange( 0, 100 );
        setValue( 40 );
        setEnabled( enabled );
        if ( orientation == Qt::Horizontal ) {
            setFixedWidth( 200 );
        }
        else {
            setFixedHeight( 200 );
        }
    }

    // Where subControl is drawn, in window coordinates.
    QRect rect( QStyle::SubControl subControl ) const
    {
        QStyleOptionSlider option;
        initStyleOption( &option );
        const auto area = style()->subControlRect( QStyle::CC_Slider, &option, subControl, this );
        return area.translated( mapTo( window(), QPoint( 0, 0 ) ) );
    }
};

} // namespace

SCENARIO( "Radio buttons and sliders are styled from Tokens in every Theme",
          "[ui][theme][radiobutton][slider]" )
{
    GIVEN( "enabled and disabled radio buttons and sliders in a window" )
    {
        Theme::apply( Theme::LightKey );
        QWidget window;
        auto* layout = new QVBoxLayout( &window );
        auto* unchecked = new RadioButton( false, true );
        auto* checked = new RadioButton( true, true );
        auto* disabledUnchecked = new RadioButton( false, false );
        auto* disabledChecked = new RadioButton( true, false );
        auto* horizontal = new Slider( Qt::Horizontal, true );
        auto* disabledHorizontal = new Slider( Qt::Horizontal, false );
        auto* vertical = new Slider( Qt::Vertical, true );
        for ( QWidget* widget : std::initializer_list<QWidget*>{
                  unchecked, checked, disabledUnchecked, disabledChecked, horizontal,
                  disabledHorizontal, vertical } ) {
            layout->addWidget( widget );
        }
        window.show();
        QTest::qWait( 20 );
        // Hover rings in Highlight too: keep the pointer, which an earlier test
        // may have left anywhere, in the layout's margin, off every widget.
        QTest::mouseMove( &window, QPoint( 1, 1 ) );

        for ( const auto& name : Theme::builtInThemes() ) {
            WHEN( "the " + name.toStdString() + " Theme is applied" )
            {
                Theme::apply( name );
                QCoreApplication::processEvents();
                const auto& theme = Theme::active();
                const auto image = window.grab().toImage();
                const auto background = image.pixelColor( 0, 0 );

                THEN( "an unchecked radio button shows a ring that stands out against the window" )
                {
                    const auto indicator = unchecked->indicator();
                    REQUIRE( indicator.width() > 0 );
                    // The left of the ring, at mid height.
                    const QRect ring( indicator.left(), indicator.center().y() - 1, 3, 3 );
                    CHECK( bestContrast( image, ring, background ) >= 3.0 );
                    // Round: the corners of its box show the window.
                    CHECK( image.pixelColor( indicator.topLeft() ) == background );
                    CHECK( image.pixelColor( indicator.bottomRight() ) == background );
                }

                THEN( "a checked radio button shows the Highlight, as a checked check box does" )
                {
                    const auto indicator = checked->indicator();
                    CHECK( contains( image, indicator, theme.color( ColorToken::Highlight ) ) );
                    CHECK_FALSE( contains( image, unchecked->indicator(),
                                           theme.color( ColorToken::Highlight ) ) );
                    // Checked and unchecked differ in their centre, not only the ring.
                    const auto centre = indicator.center();
                    CHECK( image.pixelColor( centre )
                           != image.pixelColor( centre.x() + 1,
                                                centre.y() - indicator.height() / 2 + 3 ) );
                }

                THEN( "disabled radio buttons look different from enabled ones" )
                {
                    const auto size = unchecked->indicator().size();
                    CHECK( image.copy( disabledUnchecked->indicator() )
                           != image.copy( unchecked->indicator() ) );
                    CHECK( image.copy( disabledChecked->indicator() )
                           != image.copy( checked->indicator() ) );
                    CHECK( disabledUnchecked->indicator().size() == size );
                }

                THEN( "a slider's filled part shows the Highlight, its groove is visible and "
                      "its handle stands out against the window and the groove" )
                {
                    const auto handle = horizontal->rect( QStyle::SC_SliderHandle );
                    const auto groove = horizontal->rect( QStyle::SC_SliderGroove );
                    REQUIRE( handle.width() > 0 );
                    const int grooveY = groove.center().y();

                    const auto filled = image.pixelColor( groove.left() + 4, grooveY );
                    const auto empty = image.pixelColor( groove.right() - 4, grooveY );
                    CHECK( filled.rgb() == theme.color( ColorToken::Highlight ).rgb() );
                    CHECK( contrast( filled, background ) >= 3.0 );
                    CHECK( empty != background );
                    CHECK( empty != filled );

                    // The handle's top, above the groove.
                    const QRect handleTop( handle.left(), handle.top(), handle.width(), 3 );
                    CHECK( bestContrast( image, handleTop, background ) >= 3.0 );
                    CHECK( image.pixelColor( handle.center().x(), grooveY ) != empty );
                    CHECK( image.pixelColor( handle.center().x(), grooveY ) != filled );
                }

                THEN( "a vertical slider is filled from the bottom and its handle stands out" )
                {
                    const auto handle = vertical->rect( QStyle::SC_SliderHandle );
                    const auto groove = vertical->rect( QStyle::SC_SliderGroove );
                    REQUIRE( handle.height() > 0 );
                    const int grooveX = groove.center().x();

                    const auto filled = image.pixelColor( grooveX, groove.bottom() - 4 );
                    const auto empty = image.pixelColor( grooveX, groove.top() + 4 );
                    CHECK( filled.rgb() == theme.color( ColorToken::Highlight ).rgb() );
                    CHECK( empty != background );
                    CHECK( empty != filled );

                    const QRect handleLeft( handle.left(), handle.top(), 3, handle.height() );
                    CHECK( bestContrast( image, handleLeft, background ) >= 3.0 );
                }

                THEN( "a disabled slider looks different from an enabled one and still shows "
                      "its value" )
                {
                    const auto groove = disabledHorizontal->rect( QStyle::SC_SliderGroove );
                    CHECK( image.copy( groove )
                           != image.copy( horizontal->rect( QStyle::SC_SliderGroove ) ) );
                    CHECK( image.copy( disabledHorizontal->rect( QStyle::SC_SliderHandle ) )
                           != image.copy( horizontal->rect( QStyle::SC_SliderHandle ) ) );
                    const auto filled = image.pixelColor( groove.left() + 4, groove.center().y() );
                    const auto empty = image.pixelColor( groove.right() - 4, groove.center().y() );
                    CHECK( filled != theme.color( ColorToken::Highlight ) );
                    CHECK( filled != empty );
                }
            }
        }

        Theme::apply( Theme::defaultTheme() );
    }
}
