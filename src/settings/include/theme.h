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

#ifndef LOGSQUIRL_THEME_H
#define LOGSQUIRL_THEME_H

#include <array>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <utility>

#include <QColor>
#include <QLatin1String>
#include <QPalette>
#include <QString>
#include <QStringList>

// The Tokens of a Theme. Each entry is one name: the enumerator, the
// placeholder @Name@ in the stylesheet template, and the key under which a
// stored Dark override is found. The first group are the QPalette roles.
#define LOGSQUIRL_COLOR_TOKENS( X )                                                                \
    X( Window )                                                                                    \
    X( WindowText )                                                                                \
    X( Base )                                                                                      \
    X( AlternateBase )                                                                             \
    X( ToolTipBase )                                                                               \
    X( ToolTipText )                                                                               \
    X( Text )                                                                                      \
    X( Button )                                                                                    \
    X( ButtonText )                                                                                \
    X( Link )                                                                                      \
    X( Highlight )                                                                                 \
    X( HighlightedText )                                                                           \
    X( ActiveButton )                                                                              \
    X( DisabledButtonText )                                                                        \
    X( DisabledWindowText )                                                                        \
    X( DisabledText )                                                                              \
    X( DisabledLight )                                                                             \
    X( PlaceholderText )                                                                           \
    X( Light )                                                                                     \
    X( Midlight )                                                                                  \
    X( Mid )                                                                                       \
    X( Shadow )                                                                                    \
    X( Chrome )                                                                                    \
    X( Pane )                                                                                      \
    X( Panel )                                                                                     \
    X( Menu )                                                                                      \
    X( Border )                                                                                    \
    X( InputBorder )                                                                               \
    X( PopupBorder )                                                                               \
    X( ToolTipBorder )                                                                             \
    X( DisabledBorder )                                                                            \
    X( DisabledBackground )                                                                        \
    X( Hover )                                                                                     \
    X( HoverText )                                                                                 \
    X( HoverBorder )                                                                               \
    X( InputHoverBorder )                                                                          \
    X( HeaderHover )                                                                               \
    X( ButtonHover )                                                                               \
    X( ToolButtonHover )                                                                           \
    X( ButtonPressed )                                                                             \
    X( ButtonPressedBorder )                                                                       \
    X( PressedText )                                                                               \
    X( Checked )                                                                                   \
    X( CheckedBorder )                                                                             \
    X( TabAddButtonHover )                                                                         \
    X( TabAddButtonPressed )                                                                       \
    X( TabAddButtonPressedBorder )                                                                 \
    X( TabUnderline )                                                                              \
    X( CloseButtonHover )                                                                          \
    X( SecondaryText )                                                                             \
    X( ScrollBarTrack )                                                                            \
    X( Handle )                                                                                    \
    X( HandleHover )                                                                               \
    X( Indicator )                                                                                 \
    X( IndicatorBorder )                                                                           \
    X( IndicatorIndeterminate )                                                                    \
    X( IndicatorDisabled )                                                                         \
    X( IndicatorDisabledBorder )                                                                   \
    X( StatusOk )                                                                                  \
    X( StatusWarning )                                                                             \
    X( StatusInactive )                                                                            \
    X( StatusInfo )                                                                                \
    X( StatusText )

// Non-color Tokens: sizes, paddings and icon images, as stylesheet values.
#define LOGSQUIRL_STYLE_TOKENS( X )                                                                \
    X( BorderWidth )                                                                               \
    X( OutlineWidth )                                                                              \
    X( ButtonPadding )                                                                             \
    X( ToolButtonPadding )                                                                         \
    X( InputPadding )                                                                              \
    X( ComboArrowSize )                                                                            \
    X( TabPaneOffset )                                                                             \
    X( TabAddButtonPadding )                                                                       \
    X( TabAddButtonMargin )                                                                        \
    X( TabAddButtonMinWidth )                                                                      \
    X( ScrollBarExtent )                                                                           \
    X( HandleRadius )                                                                              \
    X( MenuBarItemRadius )                                                                         \
    X( MenuItemPadding )                                                                           \
    X( MenuIconOffset )                                                                            \
    X( IndicatorSize )                                                                             \
    X( ArrowDownIcon )                                                                             \
    X( ArrowUpIcon )                                                                               \
    X( CheckIcon )                                                                                 \
    X( DisabledCheckIcon )                                                                         \
    X( CloseIcon )

#define LOGSQUIRL_TOKEN_ENUMERATOR( name ) name,
#define LOGSQUIRL_TOKEN_COUNT( name ) +1

enum class ColorToken { LOGSQUIRL_COLOR_TOKENS( LOGSQUIRL_TOKEN_ENUMERATOR ) };
enum class StyleToken { LOGSQUIRL_STYLE_TOKENS( LOGSQUIRL_TOKEN_ENUMERATOR ) };

inline constexpr std::size_t ColorTokenCount = 0 LOGSQUIRL_COLOR_TOKENS( LOGSQUIRL_TOKEN_COUNT );
inline constexpr std::size_t StyleTokenCount = 0 LOGSQUIRL_STYLE_TOKENS( LOGSQUIRL_TOKEN_COUNT );

#undef LOGSQUIRL_TOKEN_ENUMERATOR
#undef LOGSQUIRL_TOKEN_COUNT

// The look of the application: one set of Tokens, from which both the
// QPalette and the application stylesheet are derived.
class Theme {
public:
    // The values of the stored `style` setting.
    static constexpr QLatin1String LightKey = QLatin1String( "Light" );
    static constexpr QLatin1String DarkKey = QLatin1String( "Dark" );
    static constexpr QLatin1String HighContrastKey = QLatin1String( "High Contrast" );
    static constexpr QLatin1String SystemKey = QLatin1String( "System" );

    static QStringList availableThemes();
    static QString defaultTheme();

    // The Theme a stored `style` setting stands for. System becomes Dark when
    // the system color scheme is dark, and Light otherwise; an unknown name
    // becomes the default Theme. Dark overrides replace Dark Tokens by name
    // (e.g. "Window" -> "#101010") and do not apply to any other Theme.
    static Theme fromName( const QString& name, Qt::ColorScheme systemScheme,
                           const std::map<QString, QString>& darkOverrides = {} );

    // Light, Dark or High Contrast -- never System.
    QString name() const;
    bool isDark() const;

    // Whether two-tone icons show their inverse (light) variant, which a dark
    // Theme needs. Every choice between the two icon variants asks this.
    bool usesInverseIcons() const;

    QColor color( ColorToken token ) const;
    QString value( StyleToken token ) const;

    QPalette palette() const;

    // The stylesheet template with every Token filled in.
    QString styleSheet() const;

    // styleSheet() followed by the user's stylesheet for this Theme from
    // userThemesDirectory, if there is one.
    QString styleSheetWithUserFile( const QString& userThemesDirectory ) const;

    static QString tokenName( ColorToken token );
    static QString tokenName( StyleToken token );

    // Makes the Theme a stored `style` setting stands for the application's
    // look: Fusion style, platform color scheme, palette, and stylesheet with
    // the user's stylesheet from AppConfigLocation/themes/ on top. Call once
    // at startup, before widgets are created.
    static void apply( const QString& name );

    // The Theme last applied, or the default Theme before apply().
    static const Theme& active();

private:
    Theme() = default;

    static Theme light();
    static Theme dark();
    static Theme highContrast();

    void setColors( std::initializer_list<std::pair<ColorToken, const char*>> colors );
    void setValues( std::initializer_list<std::pair<StyleToken, const char*>> values );
    void applyOverrides( const std::map<QString, QString>& overrides );

    QString name_;
    bool isDark_ = false;
    QString userStyleSheetFileName_;
    std::array<QColor, ColorTokenCount> colors_;
    std::array<QString, StyleTokenCount> values_;
};

#endif
