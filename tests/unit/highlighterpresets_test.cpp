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

#include <catch2/catch.hpp>

#include <cmath>

#include <QColorDialog>
#include <QSignalSpy>
#include <QToolButton>

#include "highlighteredit.h"
#include "highlighterpresets.h"
#include "theme.h"

namespace {

// Relative luminance and contrast ratio as WCAG 2 defines them.
double luminance( const QColor& color )
{
    const auto channel = []( double c ) {
        return c <= 0.03928 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 );
    };
    return 0.2126 * channel( static_cast<double>( color.redF() ) )
           + 0.7152 * channel( static_cast<double>( color.greenF() ) )
           + 0.0722 * channel( static_cast<double>( color.blueF() ) );
}

double contrast( const QColor& a, const QColor& b )
{
    const auto la = luminance( a );
    const auto lb = luminance( b );
    return ( std::max( la, lb ) + 0.05 ) / ( std::min( la, lb ) + 0.05 );
}

} // namespace

TEST_CASE( "Every Highlighter color preset is readable on its own background",
           "[highlighterpresets]" )
{
    for ( const auto& preset : highlighterColorPresets() ) {
        INFO( preset.name );
        CHECK( contrast( preset.foreColor, preset.backColor ) >= 4.5 );
    }
}

TEST_CASE( "Every Highlighter color preset stands out from the Log Lines of every Theme",
           "[highlighterpresets]" )
{
    // A Log Line takes its background from the Base Token.
    for ( const auto& name : Theme::builtInThemes() ) {
        const auto base = Theme::fromName( name, Qt::ColorScheme::Light ).color( ColorToken::Base );
        for ( const auto& preset : highlighterColorPresets() ) {
            INFO( name.toStdString() << " / " << preset.name );
            CHECK( contrast( preset.backColor, base ) >= 1.4 );
        }
    }
}

TEST_CASE( "The color dialog offers the Highlighter colors as its basic colors",
           "[highlighterpresets]" )
{
    installHighlighterDialogColors();

    const auto& colors = highlighterDialogColors();
    for ( int i = 0; i < static_cast<int>( colors.size() ); ++i ) {
        CHECK( QColorDialog::standardColor( i ) == colors[ static_cast<size_t>( i ) ] );
    }
}

TEST_CASE( "A color preset sets both colors of the Highlighter being edited",
           "[highlighterpresets]" )
{
    HighlighterEdit edit( Highlighter{ "", false, false, Qt::black, Qt::white } );
    edit.setHighlighter( Highlighter{ "ERROR", false, false, Qt::black, Qt::white } );
    QSignalSpy changed( &edit, &HighlighterEdit::changed );

    const int strongRed = SoftHighlighterColorPresetCount;
    auto* button
        = edit.findChild<QToolButton*>( QStringLiteral( "colorPresetButton%1" ).arg( strongRed ) );
    REQUIRE( button != nullptr );
    REQUIRE( button->isEnabled() );
    button->click();

    const auto& preset = highlighterColorPresets()[ static_cast<size_t>( strongRed ) ];
    CHECK( edit.highlighter().foreColor() == preset.foreColor );
    CHECK( edit.highlighter().backColor() == preset.backColor );
    CHECK( edit.highlighter().pattern() == QStringLiteral( "ERROR" ) );
    CHECK( changed.count() == 1 );
}

TEST_CASE( "Color presets are disabled while no Highlighter is being edited",
           "[highlighterpresets]" )
{
    HighlighterEdit edit( Highlighter{ "", false, false, Qt::black, Qt::white } );

    for ( auto* button : edit.findChildren<QToolButton*>() ) {
        CHECK_FALSE( button->isEnabled() );
    }
}
