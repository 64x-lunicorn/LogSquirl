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

#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <QString>

#include "containers.h"
#include "linetypes.h"
#include "pluginuiport.h"
#include "presentationmenu.h"

class QObject;

// What the plugins offer in the context menu of a view of Log Lines (#859):
// their items, and the Log File the view shows, by the path its tab was
// added under.
struct PluginContextMenuOffer {
    QString logFile;
    std::vector<logsquirl::plugins::PluginContextMenuAction> actions;
};

// Asked each time a context menu opens, and again when a chosen item is
// delivered: the items then are the ones the Plugin UI Port has.
using PluginContextMenuSource = std::function<PluginContextMenuOffer()>;

// The plugin entries of a context menu opened in view, from what source
// offers now, for what was selected as it opened: selectedLogLines, in any
// order -- the plugin is handed the first of them in the order of the Log
// File -- and the text selected within one Log Line or cell, none for whole
// Log Lines.
//
// A chosen entry calls its item from the event loop, once the menu has
// closed, on view's thread. It calls nothing when view is gone by then, or
// the item is: removed, or its plugin unloaded.
std::vector<PresentationMenu::PluginEntry> pluginContextMenuEntries(
    QObject& view, const PluginContextMenuSource& source, logsquirl::plugins::PluginLogView kind,
    const logsquirl::vector<LineNumber>& selectedLogLines, std::optional<QString> selectedText );
