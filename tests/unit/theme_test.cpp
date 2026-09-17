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

#include <algorithm>
#include <cmath>
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
        { QPalette::Dark, ColorToken::Dark },
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

// The WCAG 2 contrast ratio of two opaque colors, from 1:1 to 21:1.
double contrastRatio( const QColor& a, const QColor& b )
{
    const auto luminance = []( const QColor& color ) {
        const auto linear = []( float value ) {
            const double channel = static_cast<double>( value );
            return channel <= 0.04045 ? channel / 12.92
                                      : std::pow( ( channel + 0.055 ) / 1.055, 2.4 );
        };
        return 0.2126 * linear( color.redF() ) + 0.7152 * linear( color.greenF() )
               + 0.0722 * linear( color.blueF() );
    };
    const auto first = luminance( a );
    const auto second = luminance( b );
    return ( std::max( first, second ) + 0.05 ) / ( std::min( first, second ) + 0.05 );
}

} // namespace

SCENARIO( "Every Token of every Theme is set", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        WHEN( "its Tokens are read" )
        {
            THEN( "every color is valid and every style value is non-empty" )
            {
                for ( const auto& name : builtInThemes() ) {
                    const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                    for ( const auto token : allColorTokens() ) {
                        INFO( name.toStdString()
                              << " " << Theme::tokenName( token ).toStdString() );
                        REQUIRE( theme.color( token ).isValid() );
                    }
                    for ( const auto token : allStyleTokens() ) {
                        INFO( name.toStdString()
                              << " " << Theme::tokenName( token ).toStdString() );
                        REQUIRE_FALSE( theme.value( token ).isEmpty() );
                    }
                }
            }
        }
    }
}

SCENARIO( "The palette of a Theme is its Tokens", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        WHEN( "its palette is derived" )
        {
            THEN( "every palette role has the color of its Token" )
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
        }
    }
}

SCENARIO( "The stylesheet of a Theme is the template filled with its Tokens", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        WHEN( "its stylesheet is built" )
        {
            THEN( "it has rules, no placeholder left, and the Theme's highlight color" )
            {
                for ( const auto& name : builtInThemes() ) {
                    const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                    const auto styleSheet = theme.styleSheet();
                    INFO( name.toStdString() );

                    REQUIRE( styleSheet.contains( "QPushButton" ) );
                    REQUIRE_FALSE( styleSheet.contains( '@' ) );
                    REQUIRE( styleSheet.contains(
                        theme.color( ColorToken::Highlight ).name( QColor::HexRgb ),
                        Qt::CaseInsensitive ) );
                }
            }
        }
    }
}

SCENARIO( "The stylesheet template names no color", "[theme]" )
{
    GIVEN( "the stylesheet template resource" )
    {
        // Loads the template resource.
        REQUIRE_FALSE(
            Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light ).styleSheet().isEmpty() );

        QFile file( ":/themes/theme.qss" );
        REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );

        WHEN( "its text is read" )
        {
            const auto templateText = QString::fromUtf8( file.readAll() );

            THEN( "it contains neither hex nor functional colors" )
            {
                static const QRegularExpression hexColor( "#[0-9A-Fa-f]{3,8}\\b" );
                static const QRegularExpression functionalColor( "\\brgba?\\s*\\(" );
                REQUIRE_FALSE( templateText.contains( hexColor ) );
                REQUIRE_FALSE( templateText.contains( functionalColor ) );
            }
        }
    }
}

SCENARIO( "A Theme is chosen by its stored name", "[theme]" )
{
    GIVEN( "the stored names of the Themes" )
    {
        THEN( "every Theme can be chosen and Light is the default" )
        {
            REQUIRE( Theme::availableThemes()
                     == QStringList{ Theme::DarkKey, Theme::HighContrastKey, Theme::LightKey,
                                     Theme::SystemKey } );
            REQUIRE( Theme::defaultTheme() == Theme::LightKey );
        }

        WHEN( "a Theme other than System is chosen" )
        {
            THEN( "the system color scheme does not matter" )
            {
                REQUIRE( Theme::fromName( Theme::LightKey, Qt::ColorScheme::Dark ).name()
                         == Theme::LightKey );
                REQUIRE_FALSE( Theme::fromName( Theme::LightKey, Qt::ColorScheme::Dark ).isDark() );
                REQUIRE( Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light ).isDark() );
                REQUIRE(
                    Theme::fromName( Theme::HighContrastKey, Qt::ColorScheme::Light ).isDark() );
            }
        }

        WHEN( "System is chosen" )
        {
            THEN( "it follows the system color scheme" )
            {
                REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark ).name()
                         == Theme::DarkKey );
                REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Light ).name()
                         == Theme::LightKey );
                REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Unknown ).name()
                         == Theme::LightKey );
            }
        }

        WHEN( "an unknown name is chosen" )
        {
            THEN( "it is the default Theme" )
            {
                REQUIRE( Theme::fromName( "Fusion", Qt::ColorScheme::Dark ).name()
                         == Theme::defaultTheme() );
            }
        }
    }
}

SCENARIO( "A dark Theme shows the inverse icons", "[theme]" )
{
    GIVEN( "the built-in Themes" )
    {
        WHEN( "a Theme other than System is chosen" )
        {
            THEN( "only the dark ones show inverse icons" )
            {
                REQUIRE_FALSE(
                    Theme::fromName( Theme::LightKey, Qt::ColorScheme::Dark ).usesInverseIcons() );
                REQUIRE(
                    Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light ).usesInverseIcons() );
                REQUIRE( Theme::fromName( Theme::HighContrastKey, Qt::ColorScheme::Light )
                             .usesInverseIcons() );
            }
        }

        WHEN( "System is chosen" )
        {
            THEN( "it follows the system color scheme" )
            {
                REQUIRE(
                    Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark ).usesInverseIcons() );
                REQUIRE_FALSE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Light )
                                   .usesInverseIcons() );
            }
        }
    }
}

SCENARIO( "A stored dark palette overrides Dark Tokens", "[theme]" )
{
    GIVEN( "stored overrides for Base and Text" )
    {
        const std::map<QString, QString> overrides{ { "Base", "#101010" }, { "Text", "#C0C0C0" } };

        WHEN( "the Dark Theme is built with them" )
        {
            const auto dark = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light, overrides );

            THEN( "the Token, palette and stylesheet use the override" )
            {
                REQUIRE( dark.color( ColorToken::Base ) == QColor( "#101010" ) );
                REQUIRE( dark.palette().color( QPalette::Base ) == QColor( "#101010" ) );
                REQUIRE( dark.styleSheet().contains( "#101010", Qt::CaseInsensitive ) );
            }

            THEN( "the placeholder text follows an overridden text color" )
            {
                auto expected = QColor( "#C0C0C0" );
                expected.setAlpha( 128 );
                REQUIRE( dark.color( ColorToken::PlaceholderText ) == expected );
            }
        }

        WHEN( "System resolves to Dark" )
        {
            THEN( "it uses them too" )
            {
                REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark, overrides )
                             .color( ColorToken::Base )
                         == QColor( "#101010" ) );
            }
        }

        WHEN( "another Theme is built with them" )
        {
            THEN( "it ignores them" )
            {
                const auto plain = Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light );
                const auto light
                    = Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light, overrides );
                REQUIRE( light.color( ColorToken::Base ) == plain.color( ColorToken::Base ) );
            }
        }
    }

    GIVEN( "an invalid value and an unknown name" )
    {
        WHEN( "the Dark Theme is built with them" )
        {
            THEN( "they are ignored" )
            {
                const auto plain = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light );
                const auto odd
                    = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light,
                                       { { "Base", "not a color" }, { "NoSuchToken", "#fff" } } );
                REQUIRE( odd.color( ColorToken::Base ) == plain.color( ColorToken::Base ) );
            }
        }
    }

    GIVEN( "the palette stored by earlier versions" )
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

        WHEN( "the Dark Theme is built with it" )
        {
            THEN( "Dark is unchanged" )
            {
                const auto plain = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light );
                const auto stored
                    = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light, storedDefaults );
                for ( const auto token : allColorTokens() ) {
                    INFO( Theme::tokenName( token ).toStdString() );
                    REQUIRE( stored.color( token ) == plain.color( token ) );
                }
            }
        }
    }
}

SCENARIO( "A user stylesheet applies on top of the Theme's stylesheet", "[theme]" )
{
    GIVEN( "a user stylesheet for the Dark Theme" )
    {
        QTemporaryDir userThemes;
        REQUIRE( userThemes.isValid() );
        const QString userRule = "QLabel { color: red; }";
        {
            QFile file( QDir( userThemes.path() ).filePath( "dark.qss" ) );
            REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
            file.write( userRule.toUtf8() );
        }

        WHEN( "the Dark Theme's stylesheet is built with the user file" )
        {
            const auto dark = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light );
            const auto withUser = dark.styleSheetWithUserFile( userThemes.path() );

            THEN( "the user rule follows the Theme's stylesheet" )
            {
                REQUIRE( withUser.startsWith( dark.styleSheet() ) );
                REQUIRE( withUser.endsWith( userRule ) );
            }
        }

        WHEN( "System resolves to Dark" )
        {
            THEN( "the user rule applies too" )
            {
                REQUIRE( Theme::fromName( Theme::SystemKey, Qt::ColorScheme::Dark )
                             .styleSheetWithUserFile( userThemes.path() )
                             .endsWith( userRule ) );
            }
        }

        WHEN( "the Light Theme's stylesheet is built with the user file" )
        {
            THEN( "the Dark user rule does not apply" )
            {
                const auto light = Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light );
                REQUIRE( light.styleSheetWithUserFile( userThemes.path() ) == light.styleSheet() );
            }
        }
    }
}

SCENARIO( "A Theme's arrow icons point in their own direction", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        THEN( "its up-arrow and down-arrow Tokens name different icons" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                REQUIRE( theme.value( StyleToken::ArrowUpIcon )
                         != theme.value( StyleToken::ArrowDownIcon ) );
                REQUIRE( theme.value( StyleToken::ArrowUpIcon ).contains( "arrow-up" ) );
                REQUIRE( theme.value( StyleToken::ArrowDownIcon ).contains( "arrow-down" ) );
            }
        }
    }
}

SCENARIO( "Check boxes show every state distinctly and at one size in every Theme", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        THEN( "its check box indicator is 16px" )
        {
            for ( const auto& name : builtInThemes() ) {
                INFO( name.toStdString() );
                REQUIRE( Theme::fromName( name, Qt::ColorScheme::Light )
                             .value( StyleToken::IndicatorSize )
                         == "16px" );
            }
        }

        THEN( "a disabled check box shows a check mark and a dash of its own" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                REQUIRE(
                    theme.value( StyleToken::DisabledCheckIcon ).contains( "check-disabled" ) );
                REQUIRE( theme.value( StyleToken::DisabledIndeterminateIcon )
                             .contains( "dash-disabled" ) );
            }
        }

        THEN( "an indeterminate check box shows a dash, not the check mark" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                REQUIRE( theme.value( StyleToken::IndeterminateIcon ).contains( "dash" ) );
                REQUIRE( theme.value( StyleToken::IndeterminateIcon )
                         != theme.value( StyleToken::CheckIcon ) );
            }
        }
    }
}

SCENARIO( "Every size Token is a stylesheet length with a unit", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        // An image Token names an icon or none, the border style Token a border
        // style; every other style Token is a size: one to four lengths, each
        // with a unit unless it is 0.
        static const QRegularExpression image( "^(url\\(.+\\)|none)$" );
        static const QRegularExpression borderStyle( "^(solid|dashed|dotted)$" );
        static const QRegularExpression length( "^(-?\\d+(\\.\\d+)?(px|pt|em|ex)|-?0)$" );

        THEN( "every length of every size Token carries a unit" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                for ( const auto token : allStyleTokens() ) {
                    const auto value = theme.value( token );
                    if ( token == StyleToken::DisabledBorderStyle ) {
                        INFO( name.toStdString()
                              << " DisabledBorderStyle = " << value.toStdString() );
                        REQUIRE( borderStyle.match( value ).hasMatch() );
                        continue;
                    }
                    if ( image.match( value ).hasMatch() ) {
                        continue;
                    }
                    INFO( name.toStdString() << " " << Theme::tokenName( token ).toStdString()
                                             << " = " << value.toStdString() );
                    const auto lengths = value.split( ' ', Qt::SkipEmptyParts );
                    REQUIRE( lengths.size() >= 1 );
                    REQUIRE( lengths.size() <= 4 );
                    for ( const auto& part : lengths ) {
                        INFO( part.toStdString() );
                        REQUIRE( length.match( part ).hasMatch() );
                    }
                }
            }
        }
    }
}

SCENARIO( "Dark draws no frame line brighter than its border", "[theme]" )
{
    GIVEN( "the Dark Theme" )
    {
        const auto dark = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light );

        THEN( "none of the palette roles Fusion draws frames with is lighter than Border" )
        {
            const auto border = dark.color( ColorToken::Border ).lightness();
            for ( const auto token : { ColorToken::Light, ColorToken::Midlight, ColorToken::Mid,
                                       ColorToken::Dark, ColorToken::Shadow } ) {
                INFO( Theme::tokenName( token ).toStdString() );
                REQUIRE( dark.color( token ).lightness() <= border );
            }
        }
    }
}

SCENARIO( "The Command Palette's badge and shortcut Tokens are readable in every Theme", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        THEN( "badge text on the badge and shortcut text on a row, selected or not, reach 4.5:1" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                REQUIRE( contrastRatio( theme.color( ColorToken::BadgeText ),
                                        theme.color( ColorToken::BadgeBackground ) )
                         >= 4.5 );
                REQUIRE( contrastRatio( theme.color( ColorToken::SecondaryText ),
                                        theme.color( ColorToken::Base ) )
                         >= 4.5 );
                REQUIRE( contrastRatio( theme.color( ColorToken::HighlightedSecondaryText ),
                                        theme.color( ColorToken::Highlight ) )
                         >= 4.5 );
            }
        }
    }
}

SCENARIO( "A checked button's icon contrasts with the Checked color in every Theme", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        THEN( "the icon variant for the checked state is light on a dark Checked color and dark "
              "on a light one, reaching 3:1" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                // The inverse variants are drawn in white, the others in black.
                const QColor icon( theme.usesInverseIconsWhenChecked() ? Qt::white : Qt::black );
                REQUIRE( contrastRatio( icon, theme.color( ColorToken::Checked ) ) >= 3.0 );
            }
        }

        THEN( "Light and Dark show the same variant checked as unchecked" )
        {
            for ( const auto& name : { QString( Theme::LightKey ), QString( Theme::DarkKey ) } ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                REQUIRE( theme.usesInverseIconsWhenChecked() == theme.usesInverseIcons() );
            }
        }
    }
}

SCENARIO( "High Contrast's progress bar text is readable over the filled and the empty part",
          "[theme]" )
{
    GIVEN( "the High Contrast Theme" )
    {
        const auto theme = Theme::fromName( Theme::HighContrastKey, Qt::ColorScheme::Light );

        THEN( "the text reaches 4.5:1 on the chunk and on the bar" )
        {
            const auto text = theme.color( ColorToken::Text );
            REQUIRE( contrastRatio( text, theme.color( ColorToken::ProgressChunk ) ) >= 4.5 );
            REQUIRE( contrastRatio( text, theme.color( ColorToken::Panel ) ) >= 4.5 );
        }

        THEN( "the chunk is outlined in a color that reaches 3:1 on the bar" )
        {
            REQUIRE( theme.value( StyleToken::ProgressChunkBorderWidth )
                     != QStringLiteral( "0px" ) );
            REQUIRE( contrastRatio( theme.color( ColorToken::Highlight ),
                                    theme.color( ColorToken::Panel ) )
                     >= 3.0 );
        }
    }

    GIVEN( "a stored Dark override of Highlight" )
    {
        THEN( "Dark's progress chunk follows it" )
        {
            const auto dark = Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light,
                                               { { "Highlight", "#FF8800" } } );
            REQUIRE( dark.color( ColorToken::ProgressChunk ) == QColor( "#FF8800" ) );
        }
    }
}

SCENARIO( "High Contrast shows hovered and disabled push buttons differently", "[theme]" )
{
    GIVEN( "the High Contrast Theme" )
    {
        const auto theme = Theme::fromName( Theme::HighContrastKey, Qt::ColorScheme::Light );

        THEN( "a hovered push button changes its background, border and text" )
        {
            REQUIRE( theme.color( ColorToken::ButtonHover ) != theme.color( ColorToken::Button ) );
            REQUIRE( theme.color( ColorToken::InputHoverBorder )
                     != theme.color( ColorToken::InputBorder ) );
            REQUIRE( theme.color( ColorToken::HoverText )
                     != theme.color( ColorToken::ButtonText ) );
        }

        THEN( "a disabled push button has a gray, dashed border and gray text" )
        {
            REQUIRE( theme.value( StyleToken::DisabledBorderStyle ) == QStringLiteral( "dashed" ) );
            REQUIRE( theme.color( ColorToken::DisabledBorder ).saturation() == 0 );
            REQUIRE( theme.color( ColorToken::DisabledBorder ).lightness()
                     < theme.color( ColorToken::InputBorder ).lightness() );
            REQUIRE( theme.color( ColorToken::DisabledButtonText ).lightness()
                     < theme.color( ColorToken::ButtonText ).lightness() );
        }
    }

    GIVEN( "Light and Dark" )
    {
        THEN( "their hover text is their button text, and disabled borders stay solid" )
        {
            for ( const auto& name : { QString( Theme::LightKey ), QString( Theme::DarkKey ) } ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                REQUIRE( theme.color( ColorToken::HoverText )
                         == theme.color( ColorToken::ButtonText ) );
                REQUIRE( theme.value( StyleToken::DisabledBorderStyle )
                         == QStringLiteral( "solid" ) );
            }
        }
    }
}

SCENARIO( "Every Theme's line numbers read against its Viewport margin", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        THEN( "its line-number text has at least WCAG AA contrast against its margin" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                REQUIRE( contrastRatio( theme.color( ColorToken::LineNumberText ),
                                        theme.color( ColorToken::ViewportMargin ) )
                         >= 4.5 );
            }
        }
    }
}

SCENARIO( "A radio button indicator is round in every Theme", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        // Qt ignores a border radius larger than half the indicator's box, so
        // the radius is exactly half of it: the indicator and its 2px border.
        static const QRegularExpression pixels( "^(\\d+)px$" );

        THEN( "the radio indicator radius is half the indicator size plus its border" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                const auto size = pixels.match( theme.value( StyleToken::IndicatorSize ) );
                const auto radius = pixels.match( theme.value( StyleToken::RadioIndicatorRadius ) );
                REQUIRE( size.hasMatch() );
                REQUIRE( radius.hasMatch() );
                REQUIRE( radius.captured( 1 ).toInt() == size.captured( 1 ).toInt() / 2 + 2 );
            }
        }
    }
}

SCENARIO( "An error marker's text is readable in every Theme", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        THEN( "its error text has at least WCAG AA contrast against the error background" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                REQUIRE( contrastRatio( theme.color( ColorToken::ErrorText ),
                                        theme.color( ColorToken::ErrorBackground ) )
                         >= 4.5 );
            }
        }
    }
}

SCENARIO( "The pull-to-follow bar's stripes stand out in every Theme", "[theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        THEN( "the stripes have at least 3:1 contrast against the window color they are drawn on" )
        {
            for ( const auto& name : builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                INFO( name.toStdString() );
                REQUIRE( contrastRatio( theme.color( ColorToken::PullToFollowStripe ),
                                        theme.color( ColorToken::Window ) )
                         >= 3.0 );
            }
        }
    }
}
