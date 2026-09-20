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
                // A list of its own, not Theme::builtInThemes(): these are
                // the Themes that show the inverse icons, i.e. the dark ones.
                // A light Theme here would fail, and rightly (#358).
                for ( const auto& name :
                      { Theme::DarkKey, Theme::HighContrastKey, Theme::SmyckKey } ) {
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

            THEN( "it gets the same neutral close button as the dark Themes, red only on hover" )
            {
                REQUIRE_FALSE( styleSheet.contains( "_inverse" ) );
                REQUIRE( styleSheet.contains( "QTabBar::close-button { image: "
                                              "url(:/images/icons8-close-window-16.png); }" ) );
                REQUIRE( styleSheet.contains( "icons8-close-window-hover-16.png" ) );
                REQUIRE( styleSheet.contains(
                    "QTabBar::close-button:hover { image: "
                    "url(:/images/icons8-close-window-hover-16.png); background-color: "
                    + theme.color( ColorToken::CloseButtonHover ).name( QColor::HexRgb ) ) );
            }
        }
    }

    GIVEN( "any built-in Theme" )
    {
        WHEN( "its closable tab bar stylesheet is built" )
        {
            THEN( "the close button shows on the selected tab, and on another tab only under "
                  "the mouse" )
            {
                for ( const auto& name : Theme::builtInThemes() ) {
                    const auto styleSheet = closableTabBarStyleSheet(
                        Theme::fromName( name, Qt::ColorScheme::Light ) );
                    INFO( name.data() );
                    const auto hidden
                        = styleSheet.indexOf( "QTabBar::close-button:!selected { image: none; }" );
                    const auto hovered = styleSheet.indexOf( "QTabBar::close-button:hover {" );
                    REQUIRE( hidden >= 0 );
                    // Of two equally specific rules the later wins.
                    REQUIRE( hovered > hidden );
                }
            }
        }

        WHEN( "the hover color of its close button is removed from the stylesheet" )
        {
            THEN( "the stylesheet names no color of its own" )
            {
                for ( const auto& name : Theme::builtInThemes() ) {
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
