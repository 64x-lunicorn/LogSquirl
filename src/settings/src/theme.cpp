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

#include "configuration.h"
#include "log.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTextStream>

#include <algorithm>
#include <vector>

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

// The stored name last applied: unlike the active Theme, it can be System.
QString& chosenName()
{
    static QString name = Theme::defaultTheme();
    return name;
}

struct Refresh {
    QPointer<QObject> context;
    std::function<void()> refresh;
};

std::vector<Refresh>& refreshes()
{
    static std::vector<Refresh> list;
    return list;
}

// The Fusion style apply() installed. Null before the first apply(), and
// again if something else replaced the application style since.
QPointer<QStyle>& installedFusion()
{
    static QPointer<QStyle> style;
    return style;
}

// Where System reads the operating system's color scheme from; empty for
// QStyleHints. Only tests replace it.
std::function<Qt::ColorScheme()>& systemColorSchemeSource()
{
    static std::function<Qt::ColorScheme()> source;
    return source;
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
        { Dark, "#6C757D" },
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
        { StatusOk, "#2EA043" },
        { StatusWarning, "#D29922" },
        { StatusInactive, "#6E7681" },
        { StatusInfo, "#388BFD" },
        { StatusText, "#FFFFFF" },
        // Shortcuts in a selected row; SecondaryText is on Base.
        { HighlightedSecondaryText, "#FFFFFF" },
        { BadgeBackground, "#DEE2E6" },
        { BadgeText, "#212529" },
        { ViewportMargin, "#E9ECEF" },
        { ViewportMarginBorder, "#CED4DA" },
        { LineNumberText, "#495057" },
        { Bullet, "#FFFFFF" },
        { BulletOutline, "#495057" },
        { ProgressChunk, "#0056B3" },
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
        { TabAddButtonPadding, "4px 0px" },
        { TabAddButtonMargin, "1px 1px" },
        { TabAddButtonMinWidth, "15px" },
        { ScrollBarExtent, "12px" },
        { HandleRadius, "4px" },
        { MenuBarItemRadius, "2px" },
        { MenuItemPadding, "5px 24px" },
        { MenuIconOffset, "0px" },
        { IndicatorSize, "12px" },
        { ArrowDownIcon, "url(:/icons/arrow-down-light.svg)" },
        { ArrowUpIcon, "url(:/icons/arrow-up-light.svg)" },
        { CheckIcon, "url(:/icons/check-light.svg)" },
        { DisabledCheckIcon, "url(:/icons/check-light.svg)" },
        { CloseIcon, "url(:/icons/close-light.svg)" },
        { DisabledBorderStyle, "solid" },
        { ProgressChunkBorderWidth, "0px" },
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
        // Fusion draws frames and tab-bar base lines in these: none is
        // brighter than Border. Mid is Border itself, for palette(mid) borders.
        { Light, "#333333" },
        { Midlight, "#2D2D30" },
        { Mid, "#333333" },
        { Dark, "#1A1A1A" },
        { Shadow, "#000000" },

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
        { CloseButtonHover, "#C42B1C" },
        { SecondaryText, "#A0A0A0" },
        { ScrollBarTrack, "#1A1A1A" },
        { Handle, "#555555" },
        { HandleHover, "#777777" },
        { Indicator, "#2D2D30" },
        { IndicatorBorder, "#777777" },
        { IndicatorIndeterminate, "#1E3A5F" },
        { IndicatorDisabled, "#252526" },
        { IndicatorDisabledBorder, "#555555" },
        { StatusOk, "#2EA043" },
        { StatusWarning, "#D29922" },
        { StatusInactive, "#6E7681" },
        { StatusInfo, "#388BFD" },
        { StatusText, "#FFFFFF" },
        // White on Highlight stays below 4.5:1, so dark text.
        { HighlightedSecondaryText, "#121212" },
        { BadgeBackground, "#3A3A3E" },
        { BadgeText, "#E0E0E0" },
        { ViewportMargin, "#252526" },
        { ViewportMarginBorder, "#333333" },
        { LineNumberText, "#A0A0A0" },
        { Bullet, "#1E1E1E" },
        { BulletOutline, "#A0A0A0" },
        // Highlight; follows an overridden Highlight (see fromName).
        { ProgressChunk, "#4D90FE" },
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
        { DisabledBorderStyle, "solid" },
        { ProgressChunkBorderWidth, "0px" },
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
        { Dark, "#FFFFFF" },
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
        { ButtonHover, "#1F1F1F" },
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
        { StatusOk, "#00FF00" },
        { StatusWarning, "#FFFF00" },
        { StatusInactive, "#A6A6A6" },
        { StatusInfo, "#00FFFF" },
        { StatusText, "#000000" },
        { HighlightedSecondaryText, "#000000" },
        { BadgeBackground, "#FFFFFF" },
        { BadgeText, "#000000" },
        { ViewportMargin, "#000000" },
        { ViewportMarginBorder, "#FFFFFF" },
        { LineNumberText, "#FFFFFF" },
        { Bullet, "#000000" },
        { BulletOutline, "#FFFFFF" },
        // No single text color reaches 4.5:1 on both black and yellow, so the
        // filled part is black too, outlined in Highlight.
        { ProgressChunk, "#000000" },
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
        { ArrowUpIcon, "url(:/icons/arrow-up-hc.svg)" },
        { CheckIcon, "url(:/icons/check-hc.svg)" },
        { DisabledCheckIcon, "url(:/icons/check-hc.svg)" },
        { CloseIcon, "url(:/icons/close-dark.svg)" },
        // A disabled button differs from an enabled one by more than a gray.
        { DisabledBorderStyle, "dashed" },
        { ProgressChunkBorderWidth, "2px" },
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
    if ( !overrides.contains( QStringLiteral( "ProgressChunk" ) ) ) {
        colors_[ indexOf( ColorToken::ProgressChunk ) ] = color( ColorToken::Highlight );
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

bool Theme::usesInverseIcons() const
{
    return isDark_;
}

bool Theme::usesInverseIconsWhenChecked() const
{
    return color( ColorToken::Checked ).lightness() < 128;
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
    inEveryGroup( QPalette::Dark, ColorToken::Dark );
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

namespace {

// The operating system's color scheme as Qt reports it now. Right after
// unsetColorScheme() a platform may still report the application's former
// override, and report the system's own scheme later with
// colorSchemeChanged; followSystemColorScheme() applies System again then.
// QPlatformTheme, which knows the system's scheme directly, is private API.
Qt::ColorScheme systemColorScheme()
{
    const auto& source = systemColorSchemeSource();
    return source ? source() : qApp->styleHints()->colorScheme();
}

void runRefreshes()
{
    auto& list = refreshes();
    list.erase( std::remove_if( list.begin(), list.end(),
                                []( const Refresh& entry ) { return entry.context.isNull(); } ),
                list.end() );

    // A refresh may register further refreshes or destroy other contexts.
    const auto current = list;
    for ( const auto& entry : current ) {
        if ( entry.context ) {
            entry.refresh();
        }
    }
}

void applyResolved( const QString& name, Qt::ColorScheme systemScheme )
{
    LOG_INFO << "Setting theme to " << name;

    auto& theme = activeTheme();
    theme = Theme::fromName( name, systemScheme, Configuration::get().darkPalette() );

    // Installed once: replacing the application style on every switch would
    // delete the style every widget is polished with.
    auto& fusion = installedFusion();
    if ( fusion.isNull() ) {
        fusion = QStyleFactory::create( QStringLiteral( "Fusion" ) );
        qApp->setStyle( fusion );
    }

    // Tells the platform which title bar / window chrome to use. System
    // leaves it to the operating system.
    if ( name != Theme::SystemKey ) {
        qApp->styleHints()->setColorScheme( theme.isDark() ? Qt::ColorScheme::Dark
                                                           : Qt::ColorScheme::Light );
    }

    qApp->setPalette( theme.palette() );
    qApp->setStyleSheet( theme.styleSheetWithUserFile(
        QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation )
        + QStringLiteral( "/themes/" ) ) );

    // Only now, with Qt's repolish over, may widgets touch their styles.
    runRefreshes();
}

// Applies System again if the operating system's color scheme now resolves
// to another Theme than the one shown. It reads the current state rather
// than a signal's argument: it runs queued, and a platform may deliver a
// change late, after a newer one -- including the application's own
// setColorScheme() calls, which also emit colorSchemeChanged.
void followSystemColorSchemeChange()
{
    if ( chosenName() != Theme::SystemKey ) {
        return;
    }
    const auto scheme = systemColorScheme();
    if ( Theme::fromName( Theme::SystemKey, scheme ).name() == Theme::active().name() ) {
        return;
    }
    applyResolved( Theme::SystemKey, scheme );
}

} // namespace

void Theme::apply( const QString& name )
{
    chosenName() = name;

    if ( name == SystemKey ) {
        // An earlier apply() set the application's color scheme, which hides
        // the system's until it is unset.
        qApp->styleHints()->unsetColorScheme();
    }

    applyResolved( name, systemColorScheme() );
}

void Theme::whenApplied( QObject* context, std::function<void()> refresh )
{
    refreshes().push_back( { context, std::move( refresh ) } );
}

void Theme::followSystemColorScheme()
{
    static const bool connected = [] {
        // Queued: Qt emits colorSchemeChanged from its own theme-change
        // handling, and applying a Theme repolishes every widget, which must
        // not run inside it.
        QObject::connect(
            qApp->styleHints(), &QStyleHints::colorSchemeChanged, qApp,
            [] { followSystemColorSchemeChange(); }, Qt::QueuedConnection );
        return true;
    }();
    Q_UNUSED( connected );
}

void Theme::setSystemColorSchemeSource( std::function<Qt::ColorScheme()> source )
{
    systemColorSchemeSource() = std::move( source );
}

const Theme& Theme::active()
{
    return activeTheme();
}
