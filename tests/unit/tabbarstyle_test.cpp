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

#include "tabbarstyle.h"
#include "theme.h"

TEST_CASE( "A closable tab bar takes its close button from the Theme", "[theme]" )
{
    SECTION( "A Theme showing inverse icons gets inverse close images and its hover color" )
    {
        for ( const auto& name : { Theme::DarkKey, Theme::HighContrastKey } ) {
            const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
            const auto styleSheet = closableTabBarStyleSheet( theme );
            INFO( name.data() );

            REQUIRE( styleSheet.contains( "icons8-close-window-16_inverse.png" ) );
            REQUIRE( styleSheet.contains( "icons8-close-window-hover-16_inverse.png" ) );
            REQUIRE( styleSheet.contains(
                theme.color( ColorToken::CloseButtonHover ).name( QColor::HexRgb ),
                Qt::CaseInsensitive ) );
        }
    }

    SECTION( "System on a dark OS gets the inverse close images" )
    {
        const auto styleSheet = closableTabBarStyleSheet(
            Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark ) );
        REQUIRE( styleSheet.contains( "_inverse.png" ) );
    }

    SECTION( "A light Theme gets no inverse images and no hover background" )
    {
        const auto styleSheet
            = closableTabBarStyleSheet( Theme::fromName( Theme::LightKey, Qt::ColorScheme::Dark ) );
        REQUIRE_FALSE( styleSheet.contains( "_inverse" ) );
        REQUIRE_FALSE( styleSheet.contains( "background-color" ) );
    }

    SECTION( "The stylesheet names no color of its own" )
    {
        for ( const auto& name : { Theme::LightKey, Theme::DarkKey, Theme::HighContrastKey } ) {
            const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
            auto styleSheet = closableTabBarStyleSheet( theme );
            styleSheet.remove( theme.color( ColorToken::CloseButtonHover ).name( QColor::HexRgb ),
                               Qt::CaseInsensitive );
            INFO( name.data() );
            REQUIRE_FALSE( styleSheet.contains( '#' ) );
        }
    }
}
