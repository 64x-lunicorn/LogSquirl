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

#ifndef LOGSQUIRL_FRONTTABACTIONS_H
#define LOGSQUIRL_FRONTTABACTIONS_H

#include <optional>

#include <QString>

#include "crawlerwidget.h"

// What the window's actions show for the tab in front, as its state says it
// (#946): each action takes what the tab decides of it -- whether it is
// enabled, whether it is checked, its tooltip -- and nothing else. Follow is
// enabled by the Watch Policy, whichever tab is in front.
struct FrontTabActions {
    // An action the Log File in front may not offer, which then says why.
    struct Offered {
        bool enabled = false;
        // Why it is not, empty when it is: the action's status tip is then
        // its tooltip.
        QString unavailable;
    };

    // The Encoding checked in the menu: the one chosen for the Log File,
    // none for the detected one.
    std::optional<int> encodingMib;

    // The View menu's toggles, checked as the tab shows them.
    bool follows = false;
    bool textWrap = false;
    bool valueNamesShown = false;

    // Matches Only and Export as CSV... are there for a Table View, that is
    // a Log Format recognized (#798, #890).
    bool matchesOnlyEnabled = false;
    bool matchesOnly = false;
    bool exportAsCsvEnabled = false;

    // Go to timestamp and the time Search Limits need a timestamp field in
    // the Log Format: the Time Navigation says why they are not available.
    Offered goToTimestamp;
    Offered searchLimitsByTime;

    // Reload once the Log File is loaded and no load is under way, Stop
    // otherwise: a reload a newer one interrupted leaves it loaded (#621).
    bool stopEnabled = false;
    bool reloadEnabled = false;

    // The Edit menu and the File menu's actions on the file -- Copy path,
    // Open containing folder, Open in editor, Clear log -- need a Log File.
    bool logFileActionsEnabled = false;
};

// state is that of the tab in front, none when it holds no Log File: the
// Welcome Dashboard.
FrontTabActions frontTabActions( const std::optional<CrawlerWidget::State>& state );

#endif
