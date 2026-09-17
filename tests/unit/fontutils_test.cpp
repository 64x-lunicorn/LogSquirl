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
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QStringList>
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
        if ( FontUtils::resolvesToFixedPitch( family ) ) {
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

TEST_CASE( "FontUtils::zoomedFontSize steps to the next offered size", "[fontutils]" )
{
    const QList<int> sizes{ 8, 9, 10, 12, 14 };

    SECTION( "from an offered size" )
    {
        REQUIRE( FontUtils::zoomedFontSize( sizes, 10, true ) == 12 );
        REQUIRE( FontUtils::zoomedFontSize( sizes, 10, false ) == 9 );
    }

    // The size a zoom starts from need not be offered: a settings file can
    // hold any size, and a font Qt cannot resolve reports one of its own.
    // Looking it up stepped past the end of the sizes (#220).
    SECTION( "from a size between two offered sizes" )
    {
        REQUIRE( FontUtils::zoomedFontSize( sizes, 11, true ) == 12 );
        REQUIRE( FontUtils::zoomedFontSize( sizes, 11, false ) == 10 );
    }

    SECTION( "from a size outside the offered sizes" )
    {
        REQUIRE( FontUtils::zoomedFontSize( sizes, 20, false ) == 14 );
        REQUIRE( FontUtils::zoomedFontSize( sizes, 4, true ) == 8 );
    }

    SECTION( "beyond the largest or smallest size, the size stays" )
    {
        REQUIRE( FontUtils::zoomedFontSize( sizes, 14, true ) == 14 );
        REQUIRE( FontUtils::zoomedFontSize( sizes, 20, true ) == 20 );
        REQUIRE( FontUtils::zoomedFontSize( sizes, 8, false ) == 8 );
        REQUIRE( FontUtils::zoomedFontSize( sizes, 4, false ) == 4 );
    }

    SECTION( "without offered sizes, the size stays" )
    {
        REQUIRE( FontUtils::zoomedFontSize( {}, 10, true ) == 10 );
        REQUIRE( FontUtils::zoomedFontSize( {}, 10, false ) == 10 );
    }
}

TEST_CASE( "FontUtils::uniformAsciiAdvance is the one advance of a fixed-pitch font",
           "[fontutils]" )
{
    const auto family = findStableFixedPitchFamily();
    if ( !family.has_value() ) {
        SUCCEED( "No stable fixed-pitch font installed on this system to test with" );
        return;
    }

    for ( const auto pointSize : { 9, 10, 11, 13 } ) {
        const QFontMetrics fm( QFont( *family, pointSize ) );
        const auto advance = FontUtils::uniformAsciiAdvance( fm );
        INFO( family->toStdString() << " " << pointSize << "pt" );
        REQUIRE( advance.has_value() );

        REQUIRE( *advance == QFontMetricsF( fm ).horizontalAdvance( QChar( 'x' ) ) );
        const QString line
            = QStringLiteral( "2026-09-17 12:00:00.123 INFO [main] a.b.C - x={y}; z" );
        REQUIRE( FontUtils::textWidth( fm, advance, line ) == fm.horizontalAdvance( line ) );
    }
}

TEST_CASE( "FontUtils::uniformAsciiAdvance is nothing for a proportional font", "[fontutils]" )
{
    const auto family = findFamily( false );
    if ( !family.has_value() ) {
        SUCCEED( "No proportional font installed on this system to test with" );
        return;
    }
    const QFont font( *family, 10 );
    const QFontMetrics fm( font );
    if ( fm.horizontalAdvance( QStringLiteral( "i" ) )
         == fm.horizontalAdvance( QStringLiteral( "W" ) ) ) {
        SUCCEED( "The proportional family resolved to a font with even advances" );
        return;
    }

    REQUIRE_FALSE( FontUtils::uniformAsciiAdvance( fm ).has_value() );
}

TEST_CASE( "FontUtils::textWidth measures what the font measures", "[fontutils]" )
{
    QStringList families;
    if ( const auto fixed = findStableFixedPitchFamily() ) {
        families << *fixed;
    }
    if ( const auto proportional = findFamily( false ) ) {
        families << *proportional;
    }

    const QStringList texts = {
        QString{},
        QStringLiteral( "plain ASCII log text 0123456789 !\"#$%&'()*+,-./:;<=>?@[]^_`{|}~" ),
        QString::fromUtf8( "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E wide" ),
        QString::fromUtf8( "caf\x65\xCC\x81 combining" ),
        QString::fromUtf8( "emoji \xF0\x9F\x98\x80 here" ),
        QStringLiteral( "control \x1b[0m" ),
    };

    for ( const auto& family : families ) {
        const QFontMetrics fm( QFont( family, 10 ) );
        const auto advance = FontUtils::uniformAsciiAdvance( fm );
        for ( const auto& text : texts ) {
            INFO( family.toStdString() << ": " << text.toStdString() );
            REQUIRE( FontUtils::textWidth( fm, advance, text ) == fm.horizontalAdvance( text ) );
        }
    }
}
