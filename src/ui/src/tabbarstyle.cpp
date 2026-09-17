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

#include "theme.h"

QString closableTabBarStyleSheet( const Theme& theme )
{
    // The same boxed close button in every Theme: neutral, red only on hover
    // (#264). The images are the application's own on every platform, which
    // also avoids the missing tab close icons of Qt's macOS style
    // (QTBUG-61092).
    const QString variant = theme.usesInverseIcons() ? QStringLiteral( "_inverse" ) : QString();
    const auto image = QStringLiteral( ":/images/icons8-close-window-16%1.png" ).arg( variant );
    const auto hoverImage
        = QStringLiteral( ":/images/icons8-close-window-hover-16%1.png" ).arg( variant );

    return QStringLiteral( "QTabBar::tab { height: 28px; }"
                           " QTabBar::close-button { image: url(%1); }"
                           " QTabBar::close-button:hover { image: url(%2);"
                           " background-color: %3; border-radius: 3px; }" )
        .arg( image, hoverImage,
              theme.color( ColorToken::CloseButtonHover ).name( QColor::HexRgb ) );
}
