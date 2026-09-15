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

SCENARIO( "A closable tab bar takes its close button from the Theme", "[theme]" )
{
    GIVEN( "a Theme showing inverse icons" )
    {
        WHEN( "its closable tab bar stylesheet is built" )
        {
            THEN( "it gets inverse close images and its hover color" )
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
        }
    }

    GIVEN( "System on a dark operating system" )
    {
        const auto theme = Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark );

        WHEN( "its closable tab bar stylesheet is built" )
        {
            const auto styleSheet = closableTabBarStyleSheet( theme );

            THEN( "it gets the inverse close images" )
            {
                REQUIRE( styleSheet.contains( "_inverse.png" ) );
            }
        }
    }

    GIVEN( "a light Theme" )
    {
        const auto theme = Theme::fromName( Theme::LightKey, Qt::ColorScheme::Dark );

        WHEN( "its closable tab bar stylesheet is built" )
        {
            const auto styleSheet = closableTabBarStyleSheet( theme );

            THEN( "it gets no inverse images and no hover background" )
            {
                REQUIRE_FALSE( styleSheet.contains( "_inverse" ) );
                REQUIRE_FALSE( styleSheet.contains( "background-color" ) );
            }
        }
    }

    GIVEN( "any built-in Theme" )
    {
        WHEN( "the hover color of its close button is removed from the stylesheet" )
        {
            THEN( "the stylesheet names no color of its own" )
            {
                for ( const auto& name :
                      { Theme::LightKey, Theme::DarkKey, Theme::HighContrastKey } ) {
                    const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                    auto styleSheet = closableTabBarStyleSheet( theme );
                    styleSheet.remove(
                        theme.color( ColorToken::CloseButtonHover ).name( QColor::HexRgb ),
                        Qt::CaseInsensitive );
                    INFO( name.data() );
                    REQUIRE_FALSE( styleSheet.contains( '#' ) );
                }
            }
        }
    }
}
