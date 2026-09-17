
/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LOGSQUIRL_FONTUTILS
#define LOGSQUIRL_FONTUTILS

#include "log.h"
#include <algorithm>
#include <iterator>
#include <numeric>

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QString>
#include <QStringView>
#include <cmath>
#include <optional>
#include <qfontdatabase.h>
#include <vector>

class FontUtils {
public:
    // The advance every printable ASCII character has in this font, when they
    // all have the same one, as in a fixed-pitch font; nothing otherwise. It
    // can be fractional: Qt adds advances up in 1/64 pixels and rounds the
    // sum to whole pixels only for the whole text.
    static std::optional<qreal> uniformAsciiAdvance( const QFontMetrics& fm )
    {
        const QFontMetricsF fmF( fm );
        const auto advance = fmF.horizontalAdvance( QChar( 'm' ) );
        if ( advance <= 0 ) {
            return std::nullopt;
        }

        QString printable;
        printable.reserve( LastPrintableAscii - FirstPrintableAscii + 1 );
        for ( char16_t c = FirstPrintableAscii; c <= LastPrintableAscii; ++c ) {
            if ( fmF.horizontalAdvance( QChar( c ) ) != advance ) {
                return std::nullopt;
            }
            printable.append( QChar( c ) );
        }

        // Counting must give what the font measures, rounding included, for
        // short and long text alike.
        const auto longText = printable.repeated( 11 );
        for ( const auto& text :
              { QStringView( printable ).first( 1 ), QStringView( printable ).first( 7 ),
                QStringView( printable ), QStringView( longText ) } ) {
            if ( countedWidth( advance, text.size() )
                 != fm.horizontalAdvance( QString::fromRawData( text.data(), text.size() ) ) ) {
                return std::nullopt;
            }
        }
        return advance;
    }

    // The width the font gives the text. With the font's uniform ASCII
    // advance, text of printable ASCII only is measured by counting, which
    // is much cheaper than shaping it; anything else -- wide or combining
    // characters, control characters -- is measured by the font.
    static int textWidth( const QFontMetrics& fm, std::optional<qreal> asciiAdvance,
                          QStringView text )
    {
        if ( text.isEmpty() ) {
            return 0;
        }
        if ( asciiAdvance.has_value() && isPrintableAscii( text ) ) {
            return countedWidth( *asciiAdvance, text.size() );
        }
        return fm.horizontalAdvance( QString::fromRawData( text.data(), text.size() ) );
    }

    // Whether a family name still names a fixed-pitch font *after* Qt has
    // resolved it. A name can claim fixed-pitch in QFontDatabase, or be a
    // generic alias, and resolve to something proportional once requested --
    // what gets painted is what QFontInfo reports, so that is what counts.
    static bool resolvesToFixedPitch( const QString& family )
    {
        if ( family.isEmpty() ) {
            return false;
        }
        return QFontDatabase::isFixedPitch( QFontInfo( QFont( family ) ).family() );
    }

    // The fixed-pitch family to fall back on for this platform. Used as the
    // last resort, because a hard-coded family name is exactly the thing
    // that can be absent: "DejaVu Sans Mono" ships with most Linux
    // distributions and with neither stock Windows nor macOS.
    //
    // What the platform *names* is not automatically what it paints.
    // QFontDatabase::systemFont(FixedFont) answers the generic "monospace"
    // on hosts that resolve it no further -- macOS among them -- and that
    // name is then substituted as silently as any other missing family, so
    // it is held to the rule it exists to enforce. Only when the platform's
    // own answer does not survive that check is the database searched for a
    // family that does; the scan costs a font enumeration, and it runs only
    // on the path where the alternative is painting Log Lines proportional.
    static QString platformFixedPitchFamily()
    {
        const auto systemFamily = QFontDatabase::systemFont( QFontDatabase::FixedFont ).family();
        if ( resolvesToFixedPitch( systemFamily ) ) {
            return systemFamily;
        }

        const auto families = QFontDatabase::families();
        for ( const auto& family : families ) {
            if ( resolvesToFixedPitch( family ) ) {
                return family;
            }
        }

        // Nothing on this host is fixed-pitch once resolved. The platform's
        // own answer is no worse than any other, and the caller logs it.
        return systemFamily;
    }

    // Falls back to a fixed-pitch family when the font that will actually
    // be used to paint is not fixed-pitch. Checked via QFontInfo rather
    // than the requested family directly, so a family Qt itself
    // substitutes (missing font, stale settings file) is validated after
    // that substitution and not before it -- the request can name a font
    // that does not exist; only what Qt resolves to actually gets painted.
    //
    // The fallback is held to the same rule: a family that is missing here
    // is substituted just as silently as the original was, so what it
    // resolves to is validated too, and the platform's own fixed font
    // takes over when it does not hold up.
    static QFont validatedFixedPitchFont( const QFont& font, const QString& fallbackFamily = {} )
    {
        const QFontInfo resolvedInfo( font );
        if ( QFontDatabase::isFixedPitch( resolvedInfo.family() ) ) {
            return font;
        }

        QFont fallback = font;
        if ( !fallbackFamily.isEmpty() ) {
            fallback.setFamily( fallbackFamily );
            if ( QFontDatabase::isFixedPitch( QFontInfo( fallback ).family() ) ) {
                LOG_WARNING << "Font \"" << resolvedInfo.family().toStdString()
                            << "\" is not fixed-pitch, falling back to \""
                            << fallbackFamily.toStdString() << "\"";
                return fallback;
            }
            LOG_WARNING << "Fallback font \"" << fallbackFamily.toStdString()
                        << "\" is not fixed-pitch either";
        }

        const auto platformFamily = platformFixedPitchFamily();
        fallback.setFamily( platformFamily );
        LOG_WARNING << "Font \"" << resolvedInfo.family().toStdString()
                    << "\" is not fixed-pitch, falling back to the platform fixed font \""
                    << platformFamily.toStdString() << "\"";
        return fallback;
    }

    static QStringList availableFonts()
    {
        // We only show the fixed fonts
        QStringList fixedFamilies;

        const auto families = QFontDatabase::families();
        for ( const auto& family : families ) {
            if ( QFontDatabase::isFixedPitch( family ) )
                fixedFamilies << family;
        }

        return fixedFamilies;
    }

    static QList<int> availableFontSizes( const QString& family )
    {
        auto sizes = QFontDatabase::pointSizes( family, "" );

        if ( sizes.empty() ) {
            sizes = QFontDatabase::standardSizes();
        }

        std::vector<int> additionalSizes( 10 );
        std::iota( additionalSizes.begin(), additionalSizes.end(), 10 );
        for ( int size : additionalSizes ) {
            if ( !sizes.contains( size ) ) {
                sizes.append( size );
            }
        }

        std::sort( sizes.begin(), sizes.end() );
        return sizes;
    }

    // The size a zoom steps to from currentSize: the nearest of the ascending
    // sizes above it, or below it, whether or not currentSize is one of them.
    // Where there is none in that direction the size stays as it is.
    static int zoomedFontSize( const QList<int>& sizes, int currentSize, bool increase )
    {
        if ( increase ) {
            const auto larger = std::upper_bound( sizes.cbegin(), sizes.cend(), currentSize );
            return larger != sizes.cend() ? *larger : currentSize;
        }

        const auto notSmaller = std::lower_bound( sizes.cbegin(), sizes.cend(), currentSize );
        return notSmaller != sizes.cbegin() ? *std::prev( notSmaller ) : currentSize;
    }

private:
    static constexpr char16_t FirstPrintableAscii = u' ';
    static constexpr char16_t LastPrintableAscii = u'~';

    // The whole pixels count characters of the advance come to, rounded
    // half up like the font's own measure.
    static int countedWidth( qreal advance, qsizetype count )
    {
        return static_cast<int>( std::floor( advance * static_cast<qreal>( count ) + 0.5 ) );
    }

    static bool isPrintableAscii( QStringView text )
    {
        return std::all_of( text.begin(), text.end(), []( QChar c ) {
            return c.unicode() >= FirstPrintableAscii && c.unicode() <= LastPrintableAscii;
        } );
    }
};

#endif