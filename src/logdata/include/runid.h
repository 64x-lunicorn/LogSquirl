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

#ifndef LOGSQUIRL_RUN_ID_H
#define LOGSQUIRL_RUN_ID_H

#include <cstdint>

#include <QMetaType>

#include <type_safe/strong_typedef.hpp>

// Identifies one run of a Background Run. Every run started gets a fresh id,
// counting from 1; 0 is never one.
struct RunId : type_safe::strong_typedef<RunId, uint64_t>,
               type_safe::strong_typedef_op::equality_comparison<RunId> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = uint64_t;

    UnderlyingType get() const
    {
        return type_safe::get( *this );
    }
};
Q_DECLARE_METATYPE( RunId )

#endif
