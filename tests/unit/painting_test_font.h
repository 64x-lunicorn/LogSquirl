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

#ifndef PAINTING_TEST_FONT_H
#define PAINTING_TEST_FONT_H

// The font the log view tests draw with (#135): fixed-width, 8 x 16 px, with
// every glyph edge on a pixel boundary, loaded from
// data/painting/logsquirl-painting-test.ttf (see make_test_font.py there).
//
// With it, every platform paints the same pixels, and a test knows exactly
// which pixel lies on which character. Nothing needs to be installed on the
// host; if the platform cannot load the font, or does not honour its
// metrics, the test fails and says so: without its font it would verify
// nothing.

#include <catch2/catch.hpp>

#include <optional>

#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QString>

namespace paintingtestfont {

inline const QString PaintingTestDataDir = QStringLiteral( LOGSQUIRL_PAINTING_TEST_DATA_DIR );

constexpr int FontPixelSize = 16;
constexpr int CharWidth = 8;
constexpr int CharHeight = 16;

// The painting test's own font, or nothing when the platform cannot load it.
inline std::optional<QFont> loadPaintingTestFont()
{
    static const int fontId = QFontDatabase::addApplicationFont(
        PaintingTestDataDir + QStringLiteral( "/logsquirl-painting-test.ttf" ) );
    if ( fontId < 0 ) {
        return std::nullopt;
    }

    const auto families = QFontDatabase::applicationFontFamilies( fontId );
    if ( families.isEmpty() ) {
        return std::nullopt;
    }

    QFont font( families.first() );
    font.setPixelSize( FontPixelSize );
    // Each platform's text rasteriser has one mode that draws these
    // pixel-aligned glyphs exactly, and it is not the same mode everywhere.
#ifdef Q_OS_MACOS
    // CoreText smooths antialiased glyphs even where their edges sit exactly
    // on pixel boundaries, so macOS draws them unantialiased.
    font.setStyleStrategy( QFont::NoAntialias );
#else
    // Elsewhere an unantialiased glyph is a one-bit bitmap, which Qt copies
    // without the pen's transparency -- the dimmed Context Lines would not be
    // dimmed. Antialiased, a pixel-aligned glyph still covers every pixel
    // fully or not at all; subpixel rendering would color its edges.
    font.setStyleStrategy( QFont::NoSubpixelAntialias );
#endif
    font.setHintingPreference( QFont::PreferNoHinting );
    return font;
}

// The painting test's font, measuring CharWidth x CharHeight px. Fails the
// test when the platform cannot load it or measures it otherwise.
inline QFont requirePaintingTestFont()
{
    const auto font = loadPaintingTestFont();
    if ( !font.has_value() ) {
        FAIL( "The test could not load its own font from " << PaintingTestDataDir.toStdString()
                                                           << "; without it there is nothing "
                                                              "portable to compare against." );
    }

    const QFontMetrics metrics( *font );
    const auto charWidth = metrics.horizontalAdvance( QLatin1Char( 'm' ) );
    if ( charWidth != CharWidth || metrics.height() != CharHeight ) {
        FAIL( "The test font must measure " << CharWidth << "x" << CharHeight
                                            << " px, this platform measures it " << charWidth << "x"
                                            << metrics.height() );
    }

    return *font;
}

} // namespace paintingtestfont

#endif // PAINTING_TEST_FONT_H
