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

#ifndef LOGSQUIRL_STARREQUEST_H
#define LOGSQUIRL_STARREQUEST_H

#include <QDate>

#include "persistable.h"

class QSettings;

/// How long and how often LogSquirl has been used, and whether the user is
/// done with the Welcome Dashboard's request for a star on GitHub (#934).
/// Kept in the user's settings only; nothing of it leaves the machine.
struct StarRequestUse {
    /// The day of the first start counted; invalid before it.
    QDate firstStart;
    /// The application starts counted, the first one included.
    int starts = 0;
    /// Whether the user followed the link or closed the line.
    bool dismissed = false;
};

/// The days after the first start and the starts before the Dashboard asks.
inline constexpr qint64 StarRequestMinDays = 14;
inline constexpr int StarRequestMinStarts = 20;

/// Whether the Dashboard asks on today: at least StarRequestMinDays after the
/// first start and at least StarRequestMinStarts starts, and never once
/// dismissed.
bool starRequestDue( const StarRequestUse& use, QDate today );

/// The StarRequestUse in the settings store, in a group of its own
/// ("StarRequest" in the application's settings): no other setting writes
/// there, and no settings migration of an update removes it.
class StarRequest final : public Persistable<StarRequest> {
public:
    const StarRequestUse& use() const
    {
        return use_;
    }

    /// Replaces what is kept; save() stores it.
    void setUse( const StarRequestUse& use )
    {
        use_ = use;
    }

    /// Counts one start of the application on today, in the settings store.
    /// The first start counted is the first start. Called once per process,
    /// never per window or Dashboard.
    static void countApplicationStart( QDate today );

    /// Whether the Dashboard asks on today, as the settings store says now.
    static bool dueOn( QDate today );

    /// The user followed the link or closed the line: never again.
    static void dismissForGood();

    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

private:
    StarRequestUse use_;
};

#endif // LOGSQUIRL_STARREQUEST_H
