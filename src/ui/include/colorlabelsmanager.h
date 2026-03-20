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

#ifndef LOGSQUIRL_COLORLABELSMANAGER_H
#define LOGSQUIRL_COLORLABELSMANAGER_H

#include "abstractlogview.h"
#include <optional>
#include <qobject.h>
#include <qobjectdefs.h>
#include <vector>

class ColorLabelsManager {
  public:
    using QuickHighlightersCollection = std::vector<AbstractLogView::QuickHighlighters>;

    QuickHighlightersCollection colorLabels() const;

    QuickHighlightersCollection setColorLabel( size_t label, const QString& text );
    QuickHighlightersCollection setNextColorLabel( const QString& text );

    QuickHighlightersCollection clear();

  private:
    QuickHighlightersCollection updateColorLabel( size_t label, const QString& text,
                                                  bool replaceCurrent );

    QuickHighlightersCollection quickHighlighters_ = QuickHighlightersCollection{9};
    std::optional<size_t> currentLabel_;
};

#endif