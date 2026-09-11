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

// Every assertion that a fallback *resolves to* a fixed-pitch family needs the
// host to own one in the first place. A system with no monospace font
// installed -- a minimal container is the usual case -- leaves
// validatedFixedPitchFont() nothing to fall back to, and the failure would
// report a host deficiency as a defect in the function.
bool hostHasFixedPitchFont()
{
    return findStableFixedPitchFamily().has_value();
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

TEST_CASE( "FontUtils::platformFixedPitchFamily resolves to a fixed-pitch family", "[fontutils]" )
{
    const auto family = FontUtils::platformFixedPitchFamily();

    REQUIRE( !family.isEmpty() );

    if ( !hostHasFixedPitchFont() ) {
        SUCCEED( "No fixed-pitch font installed on this system to resolve to" );
        return;
    }

    REQUIRE( QFontDatabase::isFixedPitch( QFontInfo( QFont( family, 10 ) ).family() ) );
}

TEST_CASE( "FontUtils::validatedFixedPitchFont falls back without a family named", "[fontutils]" )
{
    const auto family = findFamily( false );
    if ( !family.has_value() ) {
        SUCCEED( "No proportional font installed on this system to test with" );
        return;
    }

    if ( !hostHasFixedPitchFont() ) {
        SUCCEED( "No fixed-pitch font installed on this system to fall back to" );
        return;
    }

    const QFont font( *family, 10 );
    const QFont validated = FontUtils::validatedFixedPitchFont( font );

    REQUIRE( QFontDatabase::isFixedPitch( QFontInfo( validated ).family() ) );
}

TEST_CASE( "FontUtils::validatedFixedPitchFont rejects a fallback family that is absent",
           "[fontutils]" )
{
    const auto family = findFamily( false );
    if ( !family.has_value() ) {
        SUCCEED( "No proportional font installed on this system to test with" );
        return;
    }

    if ( !hostHasFixedPitchFont() ) {
        SUCCEED( "No fixed-pitch font installed on this system to fall back to" );
        return;
    }

    // The defect this covers: a fallback family that does not exist on the
    // host is substituted by Qt just as silently as the original font was,
    // and the substitute can itself be proportional.
    const QFont font( *family, 10 );
    const QFont validated
        = FontUtils::validatedFixedPitchFont( font, "LogSquirl No Such Font Family" );

    REQUIRE( QFontDatabase::isFixedPitch( QFontInfo( validated ).family() ) );
}
