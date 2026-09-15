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

#include "theme.h"

#include <algorithm>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTextStream>

#include "configuration.h"
#include "log.h"

// The stylesheet template is compiled into this static library. Nothing else
// references that resource object, so a linker may drop it unless it is
// initialized explicitly. Q_INIT_RESOURCE must be used outside a namespace.
static void initStyleSheetTemplateResource()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE( theme );
        return true;
    }();
    Q_UNUSED( initialized );
}

namespace {

#define LOGSQUIRL_TOKEN_NAME( name ) QStringLiteral( #name ),

const std::array<QString, ColorTokenCount>& colorTokenNames()
{
    static const std::array<QString, ColorTokenCount> names{ LOGSQUIRL_COLOR_TOKENS(
        LOGSQUIRL_TOKEN_NAME ) };
    return names;
}

const std::array<QString, StyleTokenCount>& styleTokenNames()
{
    static const std::array<QString, StyleTokenCount> names{ LOGSQUIRL_STYLE_TOKENS(
        LOGSQUIRL_TOKEN_NAME ) };
    return names;
}

#undef LOGSQUIRL_TOKEN_NAME

std::size_t indexOf( ColorToken token )
{
    return static_cast<std::size_t>( token );
}

std::size_t indexOf( StyleToken token )
{
    return static_cast<std::size_t>( token );
}

const QString& styleSheetTemplate()
{
    static const QString text = [] {
        initStyleSheetTemplateResource();
        QFile file( QStringLiteral( ":/themes/theme.qss" ) );
        if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
            LOG_ERROR << "Stylesheet template not found";
            return QString{};
        }
        return QString::fromUtf8( file.readAll() );
    }();
    return text;
}

QString styleSheetValue( const QColor& color )
{
    if ( color.alpha() == 255 ) {
        return color.name( QColor::HexRgb ).toUpper();
    }
    if ( color.alpha() == 0 ) {
        return QStringLiteral( "transparent" );
    }
    return QStringLiteral( "rgba(%1, %2, %3, %4)" )
        .arg( color.red() )
        .arg( color.green() )
        .arg( color.blue() )
        .arg( color.alpha() );
}

Theme& activeTheme()
{
    static Theme theme = Theme::fromName( Theme::defaultTheme(), Qt::ColorScheme::Light );
    return theme;
}

} // namespace

// ---------------------------------------------------------------------------
//  Token definitions -- the only place where a Theme's values are written.
// ---------------------------------------------------------------------------

Theme Theme::light()
{
    Theme theme;
    theme.name_ = LightKey;
    theme.isDark_ = false;
    theme.userStyleSheetFileName_ = QStringLiteral( "fusion-light.qss" );

    using enum ColorToken;
    theme.setColors( {
        { Window, "#F8F9FA" },
        { WindowText, "#212529" },
        { Base, "#FFFFFF" },
        { AlternateBase, "#F8F9FA" },
        { ToolTipBase, "#343A40" },
        { ToolTipText, "#F8F9FA" },
        { Text, "#212529" },
        { Button, "#E9ECEF" },
        { ButtonText, "#212529" },
        { Link, "#0056B3" },
        { Highlight, "#0056B3" },
        { HighlightedText, "#FFFFFF" },
        { ActiveButton, "#DEE2E6" },
        { DisabledButtonText, "#868E96" },
        { DisabledWindowText, "#868E96" },
        { DisabledText, "#868E96" },
        { DisabledLight, "#FFFFFF" },
        { PlaceholderText, "#868E96" },
        { Light, "#FFFFFF" },
        { Midlight, "#E9ECEF" },
        { Mid, "#ADB5BD" },
        { Shadow, "#868E96" },

        { Chrome, "#F8F9FA" },
        { Pane, "#F8F9FA" },
        { Panel, "#E9ECEF" },
        { Menu, "#F1F3F5" },
        { Border, "#DEE2E6" },
        { InputBorder, "#CED4DA" },
        { PopupBorder, "#CED4DA" },
        { ToolTipBorder, "#495057" },
        { DisabledBorder, "#E9ECEF" },
        { DisabledBackground, "#F1F3F5" },
        { Hover, "#DEE2E6" },
        { HoverText, "#212529" },
        { HoverBorder, "#DEE2E6" },
        { InputHoverBorder, "#CED4DA" },
        { HeaderHover, "#DEE2E6" },
        { ButtonHover, "#D8DDE3" },
        { ToolButtonHover, "#D8DDE3" },
        { ButtonPressed, "#C8CED6" },
        { ButtonPressedBorder, "#ADB5BD" },
        { PressedText, "#212529" },
        { Checked, "#CFE2FF" },
        { CheckedBorder, "#86B7FE" },
        { TabAddButtonHover, "#DEE2E6" },
        { TabAddButtonPressed, "#C8CED6" },
        { TabAddButtonPressedBorder, "#ADB5BD" },
        { TabUnderline, "transparent" },
        { CloseButtonHover, "#DC3545" },
        { SecondaryText, "#495057" },
        { ScrollBarTrack, "#F1F3F5" },
        { Handle, "#ADB5BD" },
        { HandleHover, "#868E96" },
        { Indicator, "#FFFFFF" },
        { IndicatorBorder, "#868E96" },
        { IndicatorIndeterminate, "#CFE2FF" },
        { IndicatorDisabled, "#F1F3F5" },
        { IndicatorDisabledBorder, "#CED4DA" },
    } );

    using enum StyleToken;
    theme.setValues( {
        { BorderWidth, "1px" },
        { OutlineWidth, "0px" },
        { ButtonPadding, "4px 12px" },
        { ToolButtonPadding, "3px" },
        { InputPadding, "3px 6px" },
        { ComboArrowSize, "10px" },
        { TabPaneOffset, "-1px" },
        { TabAddButtonPadding, "4 0px" },
        { TabAddButtonMargin, "1px 1px" },
        { TabAddButtonMinWidth, "15px" },
        { ScrollBarExtent, "12px" },
        { HandleRadius, "4px" },
        { MenuBarItemRadius, "2px" },
        { MenuItemPadding, "5px 24px" },
        { MenuIconOffset, "0px" },
        { IndicatorSize, "12px" },
        { ArrowDownIcon, "url(:/icons/arrow-down-light.svg)" },
        { ArrowUpIcon, "url(:/icons/arrow-down-light.svg)" },
        { CheckIcon, "url(:/icons/check-light.svg)" },
        { DisabledCheckIcon, "url(:/icons/check-light.svg)" },
        { CloseIcon, "url(:/icons/close-light.svg)" },
    } );
    return theme;
}

Theme Theme::dark()
{
    Theme theme;
    theme.name_ = DarkKey;
    theme.isDark_ = true;
    theme.userStyleSheetFileName_ = QStringLiteral( "dark.qss" );

    using enum ColorToken;
    theme.setColors( {
        { Window, "#121212" },
        { WindowText, "#E0E0E0" },
        { Base, "#1E1E1E" },
        { AlternateBase, "#252526" },
        { ToolTipBase, "#2D2D30" },
        { ToolTipText, "#E0E0E0" },
        { Text, "#E0E0E0" },
        { Button, "#2D2D30" },
        { ButtonText, "#E0E0E0" },
        { Link, "#4D90FE" },
        { Highlight, "#4D90FE" },
        { HighlightedText, "#FFFFFF" },
        { ActiveButton, "#252526" },
        { DisabledButtonText, "#666666" },
        { DisabledWindowText, "#666666" },
        { DisabledText, "#666666" },
        { DisabledLight, "#252526" },
        // Text at half opacity; follows an overridden Text (see fromName).
        { PlaceholderText, "#80E0E0E0" },
        { Light, "#FFFFFF" },
        { Midlight, "#CACACA" },
        { Mid, "#B8B8B8" },
        { Shadow, "#767676" },

        { Chrome, "#171717" },
        { Pane, "#1E1E1E" },
        { Panel, "#252526" },
        { Menu, "#252526" },
        { Border, "#333333" },
        { InputBorder, "#444444" },
        { PopupBorder, "#333333" },
        { ToolTipBorder, "#444444" },
        { DisabledBorder, "#2A2A2A" },
        { DisabledBackground, "#1A1A1A" },
        { Hover, "#2D2D30" },
        { HoverText, "#E0E0E0" },
        { HoverBorder, "#333333" },
        { InputHoverBorder, "#444444" },
        { HeaderHover, "#2D2D30" },
        { ButtonHover, "#3A3A3E" },
        { ToolButtonHover, "#3A3A3E" },
        { ButtonPressed, "#1A1A1E" },
        { ButtonPressedBorder, "#555555" },
        { PressedText, "#E0E0E0" },
        { Checked, "#1E3A5F" },
        { CheckedBorder, "#2D5A8F" },
        { TabAddButtonHover, "#3A3A3E" },
        { TabAddButtonPressed, "#1A1A1E" },
        { TabAddButtonPressedBorder, "#555555" },
        { TabUnderline, "#333333" },
        { CloseButtonHover, "#E74C3C" },
        { SecondaryText, "#A0A0A0" },
        { ScrollBarTrack, "#1A1A1A" },
        { Handle, "#555555" },
        { HandleHover, "#777777" },
        { Indicator, "#2D2D30" },
        { IndicatorBorder, "#777777" },
        { IndicatorIndeterminate, "#1E3A5F" },
        { IndicatorDisabled, "#252526" },
        { IndicatorDisabledBorder, "#555555" },
    } );

    using enum StyleToken;
    theme.setValues( {
        { BorderWidth, "1px" },
        { OutlineWidth, "0px" },
        { ButtonPadding, "4px 12px" },
        { ToolButtonPadding, "3px" },
        { InputPadding, "3px 6px" },
        { ComboArrowSize, "12px" },
        { TabPaneOffset, "-1px" },
        { TabAddButtonPadding, "0 6px" },
        { TabAddButtonMargin, "2px 4px" },
        { TabAddButtonMinWidth, "20px" },
        { ScrollBarExtent, "12px" },
        { HandleRadius, "4px" },
        { MenuBarItemRadius, "2px" },
        { MenuItemPadding, "5px 24px 5px 28px" },
        { MenuIconOffset, "6px" },
        { IndicatorSize, "16px" },
        { ArrowDownIcon, "url(:/icons/arrow-down-dark.svg)" },
        { ArrowUpIcon, "url(:/icons/arrow-up-dark.svg)" },
        { CheckIcon, "url(:/icons/check-dark.svg)" },
        { DisabledCheckIcon, "none" },
        { CloseIcon, "url(:/icons/close-dark.svg)" },
    } );
    return theme;
}

Theme Theme::highContrast()
{
    // Follows WCAG AAA contrast where possible and the Windows "Contrast
    // Themes" conventions: pure colors, 2px borders, yellow for selection and
    // focus, red for destructive actions.
    Theme theme;
    theme.name_ = HighContrastKey;
    theme.isDark_ = true;
    theme.userStyleSheetFileName_ = QStringLiteral( "high-contrast.qss" );

    using enum ColorToken;
    theme.setColors( {
        { Window, "#000000" },
        { WindowText, "#FFFFFF" },
        { Base, "#000000" },
        { AlternateBase, "#000000" },
        { ToolTipBase, "#000000" },
        { ToolTipText, "#FFFF00" },
        { Text, "#FFFFFF" },
        { Button, "#000000" },
        { ButtonText, "#FFFFFF" },
        { Link, "#00FFFF" },
        { Highlight, "#FFFF00" },
        { HighlightedText, "#000000" },
        { ActiveButton, "#000000" },
        { DisabledButtonText, "#A6A6A6" },
        { DisabledWindowText, "#A6A6A6" },
        { DisabledText, "#A6A6A6" },
        { DisabledLight, "#FFFFFF" },
        { PlaceholderText, "#808080" },
        { Light, "#FFFFFF" },
        { Midlight, "#CACACA" },
        { Mid, "#B8B8B8" },
        { Shadow, "#767676" },

        { Chrome, "#000000" },
        { Pane, "#000000" },
        { Panel, "#000000" },
        { Menu, "#000000" },
        { Border, "#FFFFFF" },
        { InputBorder, "#FFFFFF" },
        { PopupBorder, "#FFFFFF" },
        { ToolTipBorder, "#FFFFFF" },
        { DisabledBorder, "#808080" },
        { DisabledBackground, "#000000" },
        { Hover, "#1F1F1F" },
        { HoverText, "#FFFF00" },
        { HoverBorder, "#FFFF00" },
        { InputHoverBorder, "#FFFF00" },
        { HeaderHover, "#000000" },
        { ButtonHover, "#000000" },
        { ToolButtonHover, "#1F1F1F" },
        { ButtonPressed, "#FFFF00" },
        { ButtonPressedBorder, "#FFFF00" },
        { PressedText, "#000000" },
        { Checked, "#FFFF00" },
        { CheckedBorder, "#FFFF00" },
        { TabAddButtonHover, "#1F1F1F" },
        { TabAddButtonPressed, "#000000" },
        { TabAddButtonPressedBorder, "#FFFFFF" },
        { TabUnderline, "transparent" },
        { CloseButtonHover, "#FF0000" },
        { SecondaryText, "#FFFFFF" },
        { ScrollBarTrack, "#000000" },
        { Handle, "#FFFFFF" },
        { HandleHover, "#FFFF00" },
        { Indicator, "#000000" },
        { IndicatorBorder, "#FFFFFF" },
        { IndicatorIndeterminate, "#1F1F1F" },
        { IndicatorDisabled, "#000000" },
        { IndicatorDisabledBorder, "#808080" },
    } );

    using enum StyleToken;
    theme.setValues( {
        { BorderWidth, "2px" },
        { OutlineWidth, "1px" },
        { ButtonPadding, "3px 11px" },
        { ToolButtonPadding, "2px" },
        { InputPadding, "2px 5px" },
        { ComboArrowSize, "10px" },
        { TabPaneOffset, "-2px" },
        { TabAddButtonPadding, "0 6px" },
        { TabAddButtonMargin, "2px 4px" },
        { TabAddButtonMinWidth, "20px" },
        { ScrollBarExtent, "14px" },
        { HandleRadius, "3px" },
        { MenuBarItemRadius, "0px" },
        { MenuItemPadding, "5px 24px" },
        { MenuIconOffset, "0px" },
        { IndicatorSize, "12px" },
        { ArrowDownIcon, "url(:/icons/arrow-down-hc.svg)" },
        { ArrowUpIcon, "url(:/icons/arrow-down-hc.svg)" },
        { CheckIcon, "url(:/icons/check-hc.svg)" },
        { DisabledCheckIcon, "url(:/icons/check-hc.svg)" },
        { CloseIcon, "url(:/icons/close-dark.svg)" },
    } );
    return theme;
}

// ---------------------------------------------------------------------------

QStringList Theme::availableThemes()
{
    QStringList themes{ LightKey, DarkKey, HighContrastKey, SystemKey };
    std::sort( themes.begin(), themes.end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.compare( rhs, Qt::CaseInsensitive ) < 0;
    } );
    return themes;
}

QString Theme::defaultTheme()
{
    return LightKey;
}

Theme Theme::fromName( const QString& name, Qt::ColorScheme systemScheme,
                       const std::map<QString, QString>& darkOverrides )
{
    QString resolved = name;
    if ( name == SystemKey ) {
        resolved = systemScheme == Qt::ColorScheme::Dark ? QString( DarkKey ) : QString( LightKey );
    }

    if ( resolved == DarkKey ) {
        auto theme = dark();
        theme.applyOverrides( darkOverrides );
        return theme;
    }
    if ( resolved == HighContrastKey ) {
        return highContrast();
    }
    return light();
}

void Theme::setColors( std::initializer_list<std::pair<ColorToken, const char*>> colors )
{
    for ( const auto& [ token, value ] : colors ) {
        colors_[ indexOf( token ) ] = QColor::fromString( QLatin1String( value ) );
    }
}

void Theme::setValues( std::initializer_list<std::pair<StyleToken, const char*>> values )
{
    for ( const auto& [ token, value ] : values ) {
        values_[ indexOf( token ) ] = QString::fromLatin1( value );
    }
}

void Theme::applyOverrides( const std::map<QString, QString>& overrides )
{
    for ( const auto& [ key, value ] : overrides ) {
        const auto& names = colorTokenNames();
        const auto found = std::find( names.begin(), names.end(), key );
        const auto color = QColor::fromString( value );
        if ( found == names.end() || !color.isValid() ) {
            LOG_WARNING << "Ignoring dark palette override " << key << "=" << value;
            continue;
        }
        colors_[ static_cast<std::size_t>( found - names.begin() ) ] = color;
    }

    if ( !overrides.contains( QStringLiteral( "PlaceholderText" ) ) ) {
        auto placeholder = color( ColorToken::Text );
        placeholder.setAlpha( 128 );
        colors_[ indexOf( ColorToken::PlaceholderText ) ] = placeholder;
    }
}

QString Theme::name() const
{
    return name_;
}

bool Theme::isDark() const
{
    return isDark_;
}

QColor Theme::color( ColorToken token ) const
{
    return colors_[ indexOf( token ) ];
}

QString Theme::value( StyleToken token ) const
{
    return values_[ indexOf( token ) ];
}

QString Theme::tokenName( ColorToken token )
{
    return colorTokenNames()[ indexOf( token ) ];
}

QString Theme::tokenName( StyleToken token )
{
    return styleTokenNames()[ indexOf( token ) ];
}

QPalette Theme::palette() const
{
    QPalette palette;
    const auto inEveryGroup = [ & ]( QPalette::ColorRole role, ColorToken token ) {
        palette.setColor( role, color( token ) );
    };

    inEveryGroup( QPalette::Window, ColorToken::Window );
    inEveryGroup( QPalette::WindowText, ColorToken::WindowText );
    inEveryGroup( QPalette::Base, ColorToken::Base );
    inEveryGroup( QPalette::AlternateBase, ColorToken::AlternateBase );
    inEveryGroup( QPalette::ToolTipBase, ColorToken::ToolTipBase );
    inEveryGroup( QPalette::ToolTipText, ColorToken::ToolTipText );
    inEveryGroup( QPalette::Text, ColorToken::Text );
    inEveryGroup( QPalette::Button, ColorToken::Button );
    inEveryGroup( QPalette::ButtonText, ColorToken::ButtonText );
    inEveryGroup( QPalette::Link, ColorToken::Link );
    inEveryGroup( QPalette::Highlight, ColorToken::Highlight );
    inEveryGroup( QPalette::HighlightedText, ColorToken::HighlightedText );
    inEveryGroup( QPalette::PlaceholderText, ColorToken::PlaceholderText );
    inEveryGroup( QPalette::Light, ColorToken::Light );
    inEveryGroup( QPalette::Midlight, ColorToken::Midlight );
    inEveryGroup( QPalette::Mid, ColorToken::Mid );
    inEveryGroup( QPalette::Shadow, ColorToken::Shadow );

    palette.setColor( QPalette::Active, QPalette::Button, color( ColorToken::ActiveButton ) );
    palette.setColor( QPalette::Disabled, QPalette::ButtonText,
                      color( ColorToken::DisabledButtonText ) );
    palette.setColor( QPalette::Disabled, QPalette::WindowText,
                      color( ColorToken::DisabledWindowText ) );
    palette.setColor( QPalette::Disabled, QPalette::Text, color( ColorToken::DisabledText ) );
    palette.setColor( QPalette::Disabled, QPalette::Light, color( ColorToken::DisabledLight ) );

    return palette;
}

QString Theme::styleSheet() const
{
    static const QRegularExpression placeholder( QStringLiteral( "@([A-Za-z]+)@" ) );

    const auto& text = styleSheetTemplate();
    const auto tokenValue = [ this ]( const QString& name ) -> std::optional<QString> {
        const auto& colorNames = colorTokenNames();
        if ( const auto it = std::find( colorNames.begin(), colorNames.end(), name );
             it != colorNames.end() ) {
            return styleSheetValue(
                colors_[ static_cast<std::size_t>( it - colorNames.begin() ) ] );
        }
        const auto& styleNames = styleTokenNames();
        if ( const auto it = std::find( styleNames.begin(), styleNames.end(), name );
             it != styleNames.end() ) {
            return values_[ static_cast<std::size_t>( it - styleNames.begin() ) ];
        }
        return std::nullopt;
    };

    QString result;
    result.reserve( text.size() );
    qsizetype copiedUpTo = 0;
    for ( auto matches = placeholder.globalMatch( text ); matches.hasNext(); ) {
        const auto match = matches.next();
        result += QStringView( text ).mid( copiedUpTo, match.capturedStart() - copiedUpTo );
        if ( const auto value = tokenValue( match.captured( 1 ) ) ) {
            result += *value;
        }
        else {
            LOG_WARNING << "Stylesheet template names unknown token " << match.captured( 1 );
            result += match.captured( 0 );
        }
        copiedUpTo = match.capturedEnd();
    }
    result += QStringView( text ).mid( copiedUpTo );
    return result;
}

QString Theme::styleSheetWithUserFile( const QString& userThemesDirectory ) const
{
    auto result = styleSheet();

    QFile file( QDir( userThemesDirectory ).filePath( userStyleSheetFileName_ ) );
    if ( file.exists() && file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        LOG_INFO << "Applying user stylesheet " << file.fileName();
        QTextStream stream( &file );
        result += QChar( '\n' );
        result += stream.readAll();
    }
    return result;
}

void Theme::apply( const QString& name )
{
    LOG_INFO << "Setting theme to " << name;

    // Read before anything below sets the application's color scheme.
    const auto systemScheme = qApp->styleHints()->colorScheme();

    auto& theme = activeTheme();
    theme = fromName( name, systemScheme, Configuration::get().darkPalette() );

    qApp->setStyle( QStyleFactory::create( QStringLiteral( "Fusion" ) ) );

    // Tells the platform which title bar / window chrome to use.
    qApp->styleHints()->setColorScheme( theme.isDark() ? Qt::ColorScheme::Dark
                                                       : Qt::ColorScheme::Light );

    qApp->setPalette( theme.palette() );
    qApp->setStyleSheet( theme.styleSheetWithUserFile(
        QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation )
        + QStringLiteral( "/themes/" ) ) );
}

const Theme& Theme::active()
{
    return activeTheme();
}
