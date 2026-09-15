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

#include <map>
#include <vector>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "theme.h"

namespace {

const std::vector<QString>& builtInThemes()
{
    static const std::vector<QString> themes{ Theme::LightKey, Theme::DarkKey,
                                              Theme::HighContrastKey };
    return themes;
}

std::vector<ColorToken> allColorTokens()
{
    std::vector<ColorToken> tokens;
    for ( std::size_t i = 0; i < ColorTokenCount; ++i ) {
        tokens.push_back( static_cast<ColorToken>( i ) );
    }
    return tokens;
}

std::vector<StyleToken> allStyleTokens()
{
    std::vector<StyleToken> tokens;
    for ( std::size_t i = 0; i < StyleTokenCount; ++i ) {
        tokens.push_back( static_cast<StyleToken>( i ) );
    }
    return tokens;
}

struct RoleToken {
    QPalette::ColorGroup group;
    QPalette::ColorRole role;
    ColorToken token;
};

// Which Token each QPalette role of a Theme is derived from.
std::vector<RoleToken> paletteRoleTokens()
{
    const std::vector<std::pair<QPalette::ColorRole, ColorToken>> inEveryGroup{
        { QPalette::Window, ColorToken::Window },
        { QPalette::WindowText, ColorToken::WindowText },
        { QPalette::Base, ColorToken::Base },
        { QPalette::AlternateBase, ColorToken::AlternateBase },
        { QPalette::ToolTipBase, ColorToken::ToolTipBase },
        { QPalette::ToolTipText, ColorToken::ToolTipText },
        { QPalette::Text, ColorToken::Text },
        { QPalette::Button, ColorToken::Button },
        { QPalette::ButtonText, ColorToken::ButtonText },
        { QPalette::Link, ColorToken::Link },
        { QPalette::Highlight, ColorToken::Highlight },
        { QPalette::HighlightedText, ColorToken::HighlightedText },
        { QPalette::PlaceholderText, ColorToken::PlaceholderText },
        { QPalette::Light, ColorToken::Light },
        { QPalette::Midlight, ColorToken::Midlight },
        { QPalette::Mid, ColorToken::Mid },
        { QPalette::Shadow, ColorToken::Shadow },
    };
    const std::vector<RoleToken> perGroup{
        { QPalette::Active, QPalette::Button, ColorToken::ActiveButton },
        { QPalette::Disabled, QPalette::ButtonText, ColorToken::DisabledButtonText },
        { QPalette::Disabled, QPalette::WindowText, ColorToken::DisabledWindowText },
        { QPalette::Disabled, QPalette::Text, ColorToken::DisabledText },
        { QPalette::Disabled, QPalette::Light, ColorToken::DisabledLight },
    };

    std::vector<RoleToken> result = perGroup;
    for ( const auto group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled } ) {
        for ( const auto& [ role, token ] : inEveryGroup ) {
            const auto overridden
                = std::any_of( perGroup.begin(), perGroup.end(), [ group, role ]( const auto& r ) {
                      return r.group == group && r.role == role;
                  } );
            if ( !overridden ) {
                result.push_back( { group, role, token } );
            }
        }
    }
    return result;
}

} // namespace

TEST_CASE( "Every Token of every Theme is set", "[theme]" )
{
    for ( const auto& name : builtInThemes() ) {
        const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
        for ( const auto token : allColorTokens() ) {
            INFO( name.toStdString() << " " << Theme::tokenName( token ).toStdString() );
            REQUIRE( theme.color( token ).isValid() );
        }
        for ( const auto token : allStyleTokens() ) {
            INFO( name.toStdString() << " " << Theme::tokenName( token ).toStdString() );
            REQUIRE_FALSE( theme.value( token ).isEmpty() );
        }
    }
}

TEST_CASE( "The palette of a Theme is its Tokens", "[theme]" )
{
    for ( const auto& name : builtInThemes() ) {
        const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
        const auto palette = theme.palette();
        for ( const auto& [ group, role, token ] : paletteRoleTokens() ) {
            INFO( name.toStdString() << " group " << group << " role " << role << " "
                                     << Theme::tokenName( token ).toStdString() );
            REQUIRE( palette.color( group, role ) == theme.color( token ) );
        }
    }
}

TEST_CASE( "The stylesheet of a Theme is the template filled with its Tokens", "[theme]" )
{
    for ( const auto& name : builtInThemes() ) {
        const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
        const auto styleSheet = theme.styleSheet();
        INFO( name.toStdString() );

        REQUIRE( styleSheet.contains( "QPushButton" ) );
        REQUIRE_FALSE( styleSheet.contains( '@' ) );
        REQUIRE( styleSheet.contains( theme.color( ColorToken::Highlight ).name( QColor::HexRgb ),
                                      Qt::CaseInsensitive ) );
    }
}

TEST_CASE( "The stylesheet template names no color", "[theme]" )
{
    // Loads the template resource.
    REQUIRE_FALSE(
        Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light ).styleSheet().isEmpty() );

    QFile file( ":/themes/theme.qss" );
    REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );
    const auto templateText = QString::fromUtf8( file.readAll() );

    static const QRegularExpression hexColor( "#[0-9A-Fa-f]{3,8}\\b" );
    static const QRegularExpression functionalColor( "\\brgba?\\s*\\(" );
    REQUIRE_FALSE( templateText.contains( hexColor ) );
    REQUIRE_FALSE( templateText.contains( functionalColor ) );
}

TEST_CASE( "A Theme is chosen by its stored name", "[theme]" )
{
    REQUIRE( Theme::availableThemes()
             == QStringList{ Theme::DarkKey, Theme::HighContrastKey, Theme::LightKey,
                             Theme::SystemKey } );
    REQUIRE( Theme::defaultTheme() == Theme::LightKey );

    REQUIRE( Theme::fromName( Theme::LightKey, Qt::ColorScheme::Dark ).name() == Theme::LightKey );
    REQUIRE_FALSE( Theme::fromName( Theme::LightKey, Qt::ColorScheme::Dark ).isDark() );
    REQUIRE( Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light ).isDark() );
    REQUIRE( Theme::fromName( Theme::HighContrastKey, Qt::ColorScheme::Light ).isDark() );

    SECTION( "System follows the system color scheme" )
    {
        REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark ).name()
                 == Theme::DarkKey );
        REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Light ).name()
                 == Theme::LightKey );
        REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Unknown ).name()
                 == Theme::LightKey );
    }

    SECTION( "An unknown name is the default Theme" )
    {
        REQUIRE( Theme::fromName( "Fusion", Qt::ColorScheme::Dark ).name()
                 == Theme::defaultTheme() );
    }
}

TEST_CASE( "A dark Theme shows the inverse icons", "[theme]" )
{
    REQUIRE_FALSE( Theme::fromName( Theme::LightKey, Qt::ColorScheme::Dark ).usesInverseIcons() );
    REQUIRE( Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light ).usesInverseIcons() );
    REQUIRE( Theme::fromName( Theme::HighContrastKey, Qt::ColorScheme::Light ).usesInverseIcons() );

    SECTION( "System follows the system color scheme" )
    {
        REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark ).usesInverseIcons() );
        REQUIRE_FALSE(
            Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Light ).usesInverseIcons() );
    }
}

TEST_CASE( "A stored dark palette overrides Dark Tokens", "[theme]" )
{
    const std::map<QString, QString> overrides{ { "Base", "#101010" }, { "Text", "#C0C0C0" } };

    const auto dark = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light, overrides );

    REQUIRE( dark.color( ColorToken::Base ) == QColor( "#101010" ) );
    REQUIRE( dark.palette().color( QPalette::Base ) == QColor( "#101010" ) );
    REQUIRE( dark.styleSheet().contains( "#101010", Qt::CaseInsensitive ) );

    SECTION( "The placeholder text follows an overridden text color" )
    {
        auto expected = QColor( "#C0C0C0" );
        expected.setAlpha( 128 );
        REQUIRE( dark.color( ColorToken::PlaceholderText ) == expected );
    }

    SECTION( "System resolving to Dark uses them too" )
    {
        REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark, overrides )
                     .color( ColorToken::Base )
                 == QColor( "#101010" ) );
    }

    SECTION( "Other Themes ignore them" )
    {
        const auto plain = Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light );
        const auto light = Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light, overrides );
        REQUIRE( light.color( ColorToken::Base ) == plain.color( ColorToken::Base ) );
    }

    SECTION( "An invalid value or unknown name is ignored" )
    {
        const auto plain = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light );
        const auto odd
            = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light,
                               { { "Base", "not a color" }, { "NoSuchToken", "#fff" } } );
        REQUIRE( odd.color( ColorToken::Base ) == plain.color( ColorToken::Base ) );
    }

    SECTION( "The palette stored by earlier versions leaves Dark unchanged" )
    {
        // Every earlier version saved its full default dark palette.
        const std::map<QString, QString> storedDefaults{
            { "Window", "#121212" },
            { "WindowText", "#E0E0E0" },
            { "Base", "#1E1E1E" },
            { "AlternateBase", "#252526" },
            { "ToolTipBase", "#2D2D30" },
            { "ToolTipText", "#E0E0E0" },
            { "Text", "#E0E0E0" },
            { "Button", "#2D2D30" },
            { "ButtonText", "#E0E0E0" },
            { "Link", "#4D90FE" },
            { "Highlight", "#4D90FE" },
            { "HighlightedText", "#FFFFFF" },
            { "ActiveButton", "#252526" },
            { "DisabledButtonText", "#666666" },
            { "DisabledWindowText", "#666666" },
            { "DisabledText", "#666666" },
            { "DisabledLight", "#252526" },
        };
        const auto plain = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light );
        const auto stored
            = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light, storedDefaults );
        for ( const auto token : allColorTokens() ) {
            INFO( Theme::tokenName( token ).toStdString() );
            REQUIRE( stored.color( token ) == plain.color( token ) );
        }
    }
}

TEST_CASE( "A user stylesheet applies on top of the Theme's stylesheet", "[theme]" )
{
    QTemporaryDir userThemes;
    REQUIRE( userThemes.isValid() );
    const QString userRule = "QLabel { color: red; }";
    {
        QFile file( QDir( userThemes.path() ).filePath( "dark.qss" ) );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
        file.write( userRule.toUtf8() );
    }

    const auto dark = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light );
    const auto withUser = dark.styleSheetWithUserFile( userThemes.path() );
    REQUIRE( withUser.startsWith( dark.styleSheet() ) );
    REQUIRE( withUser.endsWith( userRule ) );

    REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark )
                 .styleSheetWithUserFile( userThemes.path() )
                 .endsWith( userRule ) );

    const auto light = Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light );
    REQUIRE( light.styleSheetWithUserFile( userThemes.path() ) == light.styleSheet() );
}
