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

#include "decorationsetup.h"

#include <algorithm>
#include <cmath>

namespace {

// The WCAG 2 relative luminance of an opaque color.
double relativeLuminance( const QColor& color )
{
    const auto linear = []( float value ) {
        const auto channel = static_cast<double>( value );
        return channel <= 0.04045 ? channel / 12.92 : std::pow( ( channel + 0.055 ) / 1.055, 2.4 );
    };
    return 0.2126 * linear( color.redF() ) + 0.7152 * linear( color.greenF() )
           + 0.0722 * linear( color.blueF() );
}

// The WCAG 2 contrast ratio of two opaque colors, from 1 to 21.
double contrastRatio( const QColor& first, const QColor& second )
{
    const auto a = relativeLuminance( first );
    const auto b = relativeLuminance( second );
    return ( std::max( a, b ) + 0.05 ) / ( std::min( a, b ) + 0.05 );
}

// The least a foreground ANSI color has to contrast with what it is drawn on.
constexpr double MinimumAnsiContrast = 3.0;

// How many steps a foreground is blended toward the text color in, at most.
constexpr int ContrastBlendSteps = 10;

// foreground, blended toward target step by step until it reads on
// background; target at worst.
QColor readableOn( const QColor& foreground, const QColor& background, const QColor& target )
{
    if ( !background.isValid() || contrastRatio( foreground, background ) >= MinimumAnsiContrast ) {
        return foreground;
    }
    for ( int step = 1; step < ContrastBlendSteps; ++step ) {
        const auto weight = static_cast<float>( step ) / ContrastBlendSteps;
        const auto blend
            = [ weight ]( float from, float to ) { return from + ( to - from ) * weight; };
        const auto blended = QColor::fromRgbF( blend( foreground.redF(), target.redF() ),
                                               blend( foreground.greenF(), target.greenF() ),
                                               blend( foreground.blueF(), target.blueF() ) );
        if ( contrastRatio( blended, background ) >= MinimumAnsiContrast ) {
            return blended;
        }
    }
    return target;
}

// Black or white, whichever contrasts more with background. One of them
// always reaches 3:1 (at least about 4.6:1) on any opaque color.
QColor extremeOn( const QColor& background )
{
    const QColor black( Qt::black );
    const QColor white( Qt::white );
    return contrastRatio( black, background ) >= contrastRatio( white, background ) ? black
                                                                                    : white;
}

// One step of xterm's 6x6x6 color cube.
int xtermCubeLevel( int step )
{
    return step == 0 ? 0 : 55 + 40 * step;
}

// The color an ANSI color stands for; invalid for the line's own.
QColor resolvedAnsiColor( const AnsiColor& color, const std::array<QColor, 16>& basicColors )
{
    switch ( color.kind() ) {
    case AnsiColor::Kind::LineColor:
        return {};
    case AnsiColor::Kind::Rgb:
        return QColor::fromRgb( static_cast<QRgb>( color.rgbValue() ) );
    case AnsiColor::Kind::Indexed:
        break;
    }

    const int index = color.index();
    if ( index < 16 ) {
        return basicColors[ static_cast<std::size_t>( index ) ];
    }
    if ( index < 232 ) {
        const int cube = index - 16;
        return QColor( xtermCubeLevel( cube / 36 ), xtermCubeLevel( ( cube / 6 ) % 6 ),
                       xtermCubeLevel( cube % 6 ) );
    }
    const int gray = 8 + 10 * ( index - 232 );
    return QColor( gray, gray, gray );
}

} // namespace

void DecorationSetup::setPolicy( const DecorationPolicy& policy )
{
    policy_ = policy;
    rebuildMainSearch();
}

void DecorationSetup::setSearchPattern( const RegularExpressionPattern& pattern )
{
    searchPattern_ = pattern;
    rebuildMainSearch();
}

void DecorationSetup::setColorLabels( const std::vector<QStringList>& words,
                                      const std::vector<HighlightColor>& colors )
{
    colorLabelWords_ = words;
    colorLabelColors_ = colors;
    rebuildColorLabels();
}

void DecorationSetup::setAnsiColors( const std::array<QColor, 16>& basicColors )
{
    ansiBasicColors_ = basicColors;
}

logsquirl::vector<HighlightedMatch>
DecorationSetup::ansiColorsFor( const logsquirl::vector<AnsiColorSpan>& spans,
                                const LinePalette& palette ) const
{
    logsquirl::vector<HighlightedMatch> colors;
    if ( !policy_.showAnsiColors ) {
        return colors;
    }

    colors.reserve( spans.size() );
    for ( const auto& span : spans ) {
        const auto background = resolvedAnsiColor( span.background, ansiBasicColors_ );
        auto foreground = resolvedAnsiColor( span.foreground, ansiBasicColors_ );
        if ( foreground.isValid() ) {
            foreground = readableOn( foreground, background.isValid() ? background : palette.base,
                                     palette.text );
        }
        else if ( background.isValid()
                  && contrastRatio( palette.text, background ) < MinimumAnsiContrast ) {
            // A span with only a background leaves its text in the line's
            // own color, the Theme's text color -- so blending toward that
            // cannot help (ESC[47m under Dark: light text on near-white).
            // Instead the text color is blended toward black or white,
            // whichever reads better on the background, until it reaches
            // 3:1; at worst it becomes that black or white. A text color
            // that already reads on the background is left to the line.
            foreground = readableOn( palette.text, background, extremeOn( background ) );
        }
        colors.emplace_back( LineColumn{ span.start }, LineLength{ span.length }, foreground,
                             background );
    }
    return colors;
}

void DecorationSetup::setQuickFindPattern( const QuickFindPattern* pattern )
{
    quickFindPattern_ = pattern;
}

void DecorationSetup::rebuildMainSearch()
{
    cachedMainSearch_.reset();

    // A boolean or excluding pattern selects Log Lines, it does not point at
    // the text that made them match, so there is nothing in a line to color.
    if ( !policy_.mainSearchHighlight || searchPattern_.isBoolean || searchPattern_.isExclude
         || searchPattern_.pattern.isEmpty() ) {
        return;
    }

    cachedMainSearch_ = Highlighter{};
    cachedMainSearch_->setHighlightOnlyMatch( true );
    cachedMainSearch_->setVariateColors( policy_.variateMainSearchHighlight );
    cachedMainSearch_->setPattern( searchPattern_.pattern );
    cachedMainSearch_->setIgnoreCase( !searchPattern_.isCaseSensitive );
    cachedMainSearch_->setUseRegex( !searchPattern_.isPlainText );
    cachedMainSearch_->setBackColor( policy_.mainSearchBackColor );
    cachedMainSearch_->setForeColor( Qt::black );
}

void DecorationSetup::rebuildColorLabels()
{
    cachedColorLabels_.clear();

    // Only the slots that have both a color and at least one word produce a
    // Highlighter; a slot whose color the caller did not supply is skipped
    // rather than painted in some fallback color.
    const auto slots = std::min( colorLabelWords_.size(), colorLabelColors_.size() );
    for ( size_t slot = 0; slot < slots; ++slot ) {
        const auto& color = colorLabelColors_[ slot ];
        for ( const auto& word : colorLabelWords_[ slot ] ) {
            if ( word.isEmpty() ) {
                continue;
            }

            Highlighter highlighter{ word, false, true, color.foreColor, color.backColor };
            highlighter.setUseRegex( false );
            cachedColorLabels_.push_back( std::move( highlighter ) );
        }
    }
}

LineDecorator::Context DecorationSetup::context( const HighlighterSet& highlighterSet,
                                                 SearchLimits searchLimits,
                                                 const LinePalette& palette,
                                                 LineStatusDisplay lineStatus ) const
{
    return LineDecorator::Context{
        highlighterSet,
        cachedMainSearch_,
        cachedColorLabels_,
        quickFindPattern_ ? quickFindPattern_->getMatcher() : QuickFindMatcher{},
        policy_.quickFindBackColor,
        searchLimits,
        palette,
        lineStatus,
    };
}
