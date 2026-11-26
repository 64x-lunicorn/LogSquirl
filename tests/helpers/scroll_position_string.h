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

#ifndef SCROLL_POSITION_STRING_H
#define SCROLL_POSITION_STRING_H

// A failed comparison of two Scroll Positions shows both, not "{?} == {?}".
//
// Every test file that compares Scroll Positions includes this, before its
// first check. Catch's stringify<ScrollPosition> is an inline template: one
// file without this would instantiate it as "{?}", and the linker keeps one
// copy for the whole test executable, which may be that one.

#include <catch2/catch_tostring.hpp>

#include <string>

#include "viewportlayout.h"

template <>
struct Catch::StringMaker<ScrollPosition> {
    static std::string convert( const ScrollPosition& position )
    {
        return "{ line " + std::to_string( position.lineNumber.get() ) + ", visual line "
               + std::to_string( position.visualLineIndex ) + " }";
    }
};

#endif
