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

#include "tabbarstyle.h"

#include <QtGlobal>

#include "theme.h"

QString closableTabBarStyleSheet( const Theme& theme )
{
    const bool inverse = theme.usesInverseIcons();

    QString image;
    QString hoverImage;

    if ( inverse ) {
        image = QStringLiteral( ":/images/icons8-close-window-16_inverse.png" );
        hoverImage = QStringLiteral( ":/images/icons8-close-window-hover-16_inverse.png" );
    }
    else {
#if defined( Q_OS_MAC )
        // work around Qt macOS bug missing tab close icons
        // see: https://bugreports.qt.io/browse/QTBUG-61092
        image = QStringLiteral(
            ":/qt-project.org/styles/commonstyle/images/standardbutton-closetab-16.png" );
        hoverImage = QStringLiteral(
            ":/qt-project.org/styles/commonstyle/images/standardbutton-closetab-hover-16.png" );
#elif defined( Q_OS_WIN )
        image = QStringLiteral( ":/images/icons8-close-window-16.png" );
        hoverImage = QStringLiteral( ":/images/icons8-close-window-hover-16.png" );
#endif
    }

    QString styleSheet = QStringLiteral( "QTabBar::tab { height: 28px; }" );
    if ( image.isEmpty() ) {
        return styleSheet;
    }

    styleSheet += QStringLiteral( " QTabBar::close-button { image: url(%1); }" ).arg( image );
    if ( inverse ) {
        styleSheet
            += QStringLiteral( " QTabBar::close-button:hover { image: url(%1);"
                               " background-color: %2; border-radius: 3px; }" )
                   .arg( hoverImage,
                         theme.color( ColorToken::CloseButtonHover ).name( QColor::HexRgb ) );
    }
    else {
        styleSheet += QStringLiteral( " QTabBar::close-button:hover { image: url(%1); }" )
                          .arg( hoverImage );
    }
    return styleSheet;
}
