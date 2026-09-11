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

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <optional>

#include "fontutils.h"

namespace {

std::optional<QString> findFamily( bool fixedPitch )
{
    for ( const auto& family : QFontDatabase::families() ) {
        if ( QFontDatabase::isFixedPitch( family ) == fixedPitch ) {
            return family;
        }
    }
    return std::nullopt;
}

// A family name can claim fixed-pitch in QFontDatabase while itself being a
// generic alias (e.g. "Monospace") that Qt resolves to something else
// again -- possibly proportional -- once actually requested. What matters
// to validatedFixedPitchFont() is the family QFontInfo resolves to, so the
// fallback this test relies on must be self-consistent under that same
// resolution, or the assertion is really testing font substitution
// idiosyncrasies of the host rather than the function under test.
std::optional<QString> findStableFixedPitchFamily()
{
    for ( const auto& family : QFontDatabase::families() ) {
        if ( !QFontDatabase::isFixedPitch( family ) ) {
            continue;
        }
        const QFont font( family, 10 );
        const auto resolvedFamily = QFontInfo( font ).family();
        if ( QFontDatabase::isFixedPitch( resolvedFamily ) ) {
            return family;
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE( "FontUtils::validatedFixedPitchFont keeps a fixed-pitch font", "[fontutils]" )
{
    const auto family = findStableFixedPitchFamily();
    if ( !family.has_value() ) {
        SUCCEED( "No stable fixed-pitch font installed on this system to test with" );
        return;
    }

    const QFont font( *family, 10 );
    const QFont validated = FontUtils::validatedFixedPitchFont( font, *family );

    REQUIRE( QFontInfo( validated ).family() == QFontInfo( font ).family() );
}

TEST_CASE( "FontUtils::validatedFixedPitchFont substitutes a non-fixed-pitch font", "[fontutils]" )
{
    const auto family = findFamily( false );
    if ( !family.has_value() ) {
        SUCCEED( "No proportional font installed on this system to test with" );
        return;
    }

    const auto fallbackFamily = findStableFixedPitchFamily();
    if ( !fallbackFamily.has_value() ) {
        SUCCEED( "No stable fixed-pitch fallback font installed on this system to test with" );
        return;
    }

    const QFont font( *family, 10 );
    const QFont validated = FontUtils::validatedFixedPitchFont( font, *fallbackFamily );

    REQUIRE( QFontDatabase::isFixedPitch( QFontInfo( validated ).family() ) );
}
