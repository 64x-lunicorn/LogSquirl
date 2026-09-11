
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
    // Falls back to a fixed-pitch family when the font that will actually
    // be used to paint is not fixed-pitch. Checked via QFontInfo rather
    // than the requested family directly, so a family Qt itself
    // substitutes (missing font, stale settings file) is validated after
    // that substitution and not before it -- the request can name a font
    // that does not exist; only what Qt resolves to actually gets painted.
    static QFont validatedFixedPitchFont( const QFont& font,
                                          const QString& fallbackFamily = "DejaVu Sans Mono" )
    {
        const QFontInfo resolvedInfo( font );
        if ( QFontDatabase::isFixedPitch( resolvedInfo.family() ) ) {
            return font;
        }

        LOG_WARNING << "Font \"" << resolvedInfo.family().toStdString()
                    << "\" is not fixed-pitch, falling back to \"" << fallbackFamily.toStdString()
                    << "\"";

        QFont fallback = font;
        fallback.setFamily( fallbackFamily );
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