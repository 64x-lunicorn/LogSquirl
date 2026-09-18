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
#include <functional>
#include <initializer_list>
#include <map>
#include <utility>

#include <QColor>
#include <QLatin1String>
#include <QPalette>
#include <QString>
#include <QStringList>

class QObject;

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
    X( Dark )                                                                                      \
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
    X( StatusText )                                                                                \
    X( HighlightedSecondaryText )                                                                  \
    X( BadgeBackground )                                                                           \
    X( BadgeText )                                                                                 \
    X( ViewportMargin )                                                                            \
    X( ViewportMarginBorder )                                                                      \
    X( LineNumberText )                                                                            \
    X( Bullet )                                                                                    \
    X( BulletOutline )                                                                             \
    X( ProgressChunk )                                                                             \
    X( SliderGroove )                                                                              \
    X( ErrorBackground )                                                                           \
    X( ErrorText )                                                                                 \
    X( PullToFollowStripe )                                                                        \
    X( DefaultButton )                                                                             \
    X( DefaultButtonText )                                                                         \
    X( DefaultButtonBorder )                                                                       \
    X( DefaultButtonHover )                                                                        \
    X( DefaultButtonFocusBorder )

// Non-color Tokens: sizes, paddings, border styles and icon images, as
// stylesheet values.
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
    X( CloseIcon )                                                                                 \
    X( DisabledBorderStyle )                                                                       \
    X( ProgressChunkBorderWidth )                                                                  \
    X( RadioIndicatorRadius )                                                                      \
    X( IndeterminateIcon )                                                                         \
    X( DisabledIndeterminateIcon )                                                                 \
    X( ControlRadius )                                                                             \
    X( PopupRadius )                                                                               \
    X( BoxBorderWidth )

#define LOGSQUIRL_TOKEN_ENUMERATOR( name ) name,
#define LOGSQUIRL_TOKEN_COUNT( name ) +1

enum class ColorToken { LOGSQUIRL_COLOR_TOKENS( LOGSQUIRL_TOKEN_ENUMERATOR ) };
enum class StyleToken { LOGSQUIRL_STYLE_TOKENS( LOGSQUIRL_TOKEN_ENUMERATOR ) };

inline constexpr std::size_t ColorTokenCount = 0 LOGSQUIRL_COLOR_TOKENS( LOGSQUIRL_TOKEN_COUNT );
inline constexpr std::size_t StyleTokenCount = 0 LOGSQUIRL_STYLE_TOKENS( LOGSQUIRL_TOKEN_COUNT );

#undef LOGSQUIRL_TOKEN_ENUMERATOR
#undef LOGSQUIRL_TOKEN_COUNT

// The number of Color Label slots a Theme colors.
inline constexpr std::size_t ColorLabelCount = 9;

// The colors a Theme gives one Color Label: the text and the background it
// paints a labelled word in. An invalid text color leaves the Log Line's own
// color in place.
struct ColorLabelColors {
    QColor foreColor;
    QColor backColor;
};

// The look of the application: one set of Tokens, from which both the
// QPalette and the application stylesheet are derived.
class Theme {
public:
    // The values of the stored `style` setting.
    static constexpr QLatin1String LightKey = QLatin1String( "Light" );
    static constexpr QLatin1String DarkKey = QLatin1String( "Dark" );
    static constexpr QLatin1String HighContrastKey = QLatin1String( "High Contrast" );
    static constexpr QLatin1String SmyckKey = QLatin1String( "Smyck" );
    static constexpr QLatin1String SystemKey = QLatin1String( "System" );

    // Every value the `style` setting can take, sorted by name.
    static QStringList availableThemes();

    // The `style` setting used when none or an unknown one is stored.
    static QString defaultTheme();

    // The Theme a stored `style` setting stands for. System becomes Dark when
    // the system color scheme is dark, and Light otherwise; an unknown name
    // becomes the default Theme. Dark overrides replace Dark Tokens by name
    // (e.g. "Window" -> "#101010") and do not apply to any other Theme.
    static Theme fromName( const QString& name, Qt::ColorScheme systemScheme,
                           const std::map<QString, QString>& darkOverrides = {} );

    // Light, Dark, High Contrast or Smyck -- never System.
    QString name() const;

    // Whether the Theme has dark backgrounds and light text.
    bool isDark() const;

    // Whether two-tone icons show their inverse (light) variant, which a dark
    // Theme needs. Every choice between the two icon variants asks this.
    bool usesInverseIcons() const;

    // Whether a checked button shows the inverse (light) variant of its icon:
    // whether the Checked Token is dark. Differs from usesInverseIcons() where
    // a dark Theme checks buttons with a light color, as High Contrast does.
    bool usesInverseIconsWhenChecked() const;

    // The color this Theme gives token.
    QColor color( ColorToken token ) const;

    // The stylesheet value (a size, padding or icon image) this Theme gives
    // token.
    QString value( StyleToken token ) const;

    // The colors this Theme gives the Color Labels, in slot order. Unlike a
    // Token, these color Log Lines: a Color Label follows the Theme unless
    // the user chose its colors (ADR-0006).
    const std::array<ColorLabelColors, ColorLabelCount>& colorLabels() const;

    // Whether these are the colors a built-in Theme gives the Color Label of
    // slot, and the Color Label may therefore follow the Theme. Colors the
    // user chose are those of no Theme, and are kept.
    static bool isBuiltInColorLabel( std::size_t slot, const QColor& foreColor,
                                     const QColor& backColor );

    // Whether two Color Label colors are the same as the settings store keeps
    // them: a Theme that gives a Color Label no text color of its own leaves
    // an invalid color, which the store writes and reads back as opaque
    // black.
    static bool sameColorLabelColor( const QColor& lhs, const QColor& rhs );

    // The QPalette derived from the palette-role Tokens, for every color group.
    QPalette palette() const;

    // The stylesheet template with every Token filled in.
    QString styleSheet() const;

    // styleSheet() followed by the user's stylesheet for this Theme from
    // userThemesDirectory, if there is one.
    QString styleSheetWithUserFile( const QString& userThemesDirectory ) const;

    // The name of token: its placeholder in the stylesheet template without
    // the @ signs, and its key in the stored Dark overrides.
    static QString tokenName( ColorToken token );
    static QString tokenName( StyleToken token );

    // Makes the Theme a stored `style` setting stands for the application's
    // look: Fusion style, platform color scheme, palette, and stylesheet with
    // the user's stylesheet from AppConfigLocation/themes/ on top. Can be
    // called again at any time, with windows open; afterwards every refresh
    // registered with whenApplied() runs.
    static void apply( const QString& name );

    // Runs refresh after every apply() for as long as context lives, once the
    // palette and stylesheet are in place. For what a widget derives from the
    // Theme and Qt does not update by itself: icons, stylesheets built from
    // Tokens, and palette roles set from colors read off a palette. A
    // stylesheet that names palette(role) needs none: Qt resolves it again
    // on every repolish. Never refresh from a StyleChange or PaletteChange
    // handler instead: those run while Qt repolishes (#173).
    static void whenApplied( QObject* context, std::function<void()> refresh );

    // Applies System again whenever the operating system's color scheme
    // changes while System is chosen, once Qt's event handling for the change
    // is over. Call once at startup.
    static void followSystemColorScheme();

    // Replaces where System reads the operating system's color scheme from,
    // QStyleHints::colorScheme() by default; an empty source restores that.
    // Exists for tests: no platform lets a test change the operating system's
    // color scheme.
    static void setSystemColorSchemeSource( std::function<Qt::ColorScheme()> source );

    // The Theme last applied, or the default Theme before apply().
    static const Theme& active();

private:
    Theme() = default;

    static Theme light();
    static Theme dark();
    static Theme highContrast();
    static Theme smyck();

    void setColors( std::initializer_list<std::pair<ColorToken, const char*>> colors );
    void setValues( std::initializer_list<std::pair<StyleToken, const char*>> values );
    void applyOverrides( const std::map<QString, QString>& overrides );

    QString name_;
    bool isDark_ = false;
    QString userStyleSheetFileName_;
    std::array<QColor, ColorTokenCount> colors_;
    std::array<QString, StyleTokenCount> values_;
    std::array<ColorLabelColors, ColorLabelCount> colorLabels_;
};

#endif
