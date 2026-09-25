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

#include <catch2/catch_test_macros.hpp>

#include "configuration.h"
#include "defaultencodingcheck.h"

// An unknown default Encoding MIB in the settings is reset to Auto once at
// startup, and the reset is stored (#488).

SCENARIO( "An unknown default encoding in the settings is reset to Auto", "[settings][encoding]" )
{
    GIVEN( "settings that store the encoding MIB 2013, which no encoding has" )
    {
        auto& config = Configuration::get();
        config.setDefaultEncodingMib( 2013 );
        config.save();

        WHEN( "the loaded configuration is checked" )
        {
            auto& loaded = Configuration::getSynced();
            REQUIRE( loaded.defaultEncodingMib() == 2013 );
            const auto wasReset = resetUnknownDefaultEncoding( loaded );

            THEN( "it is reset to -1 and the reset is saved" )
            {
                REQUIRE( wasReset );
                REQUIRE( loaded.defaultEncodingMib() == -1 );
                REQUIRE( Configuration::getSynced().defaultEncodingMib() == -1 );
            }
        }
    }

    GIVEN( "settings with a known encoding, or Auto" )
    {
        auto& config = Configuration::get();

        THEN( "they are left alone" )
        {
            config.setDefaultEncodingMib( 106 );
            REQUIRE_FALSE( resetUnknownDefaultEncoding( config ) );
            REQUIRE( config.defaultEncodingMib() == 106 );

            config.setDefaultEncodingMib( -1 );
            REQUIRE_FALSE( resetUnknownDefaultEncoding( config ) );
            REQUIRE( config.defaultEncodingMib() == -1 );
        }
    }
}
