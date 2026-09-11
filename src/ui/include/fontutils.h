
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
#include <numeric>

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <qfontdatabase.h>
#include <vector>

class FontUtils {
public:
    // The fixed-pitch family Qt guarantees for this platform. Used as the
    // last resort, because a hard-coded family name is exactly the thing
    // that can be absent: "DejaVu Sans Mono" ships with most Linux
    // distributions and with neither stock Windows nor macOS.
    static QString platformFixedPitchFamily()
    {
        return QFontDatabase::systemFont( QFontDatabase::FixedFont ).family();
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
};

#endif