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

#ifndef LOGSQUIRL_REGEXLABSOURCE_H
#define LOGSQUIRL_REGEXLABSOURCE_H

#include <QString>

#include "regexlabwindow.h"

class CrawlerWidget;

// Where the Regex Lab takes its sample from when it is tied to this tab: its
// selected Log Lines and those around its current line, read off the UI
// thread. The same for whoever opens the Lab -- the menu, and the editors,
// the Search Line and plugins to come (#659). Once the tab is gone, the
// source gives no sample, and the Lab lets go of it.
RegexLabSampleSource regexLabSampleSource( CrawlerWidget& tab, const QString& name );

#endif
