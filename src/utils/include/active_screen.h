/*
 * Copyright (C) 2023 Anton Filimonov
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

#ifndef LOGSQUIRL_ACTIVE_SCREEN_H

#include <QWidget>
#include <QWindow>
#include <QScreen>

static inline QScreen* activeScreen(QWidget* widget) {
    if (widget == nullptr) return nullptr;
    
    QScreen* screen = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
        screen = widget->screen();
#else
        (void) widget->winId(); // make Qt create native window
        QWindow* window = widget->windowHandle();
        screen = window ? window->screen() : nullptr;
#endif
    return screen;
}

#endif
