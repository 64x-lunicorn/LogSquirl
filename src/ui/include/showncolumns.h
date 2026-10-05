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

#include <QString>

#include "containers.h"
#include "selection.h"
#include "shownline.h"
#include "valuenamer.h"

// The character of a text drawn at a display column, given the display
// column each of its characters starts at (rawToDisplayColumns()): a column
// inside an expanded tab is the tab's, and one past the end of the text is
// its length.
qsizetype characterAtDisplayColumn( const logsquirl::vector<int>& displayColumns,
                                    int displayColumn );

// The columns of one Log Line as the Text View and the Filtered View show it
// with its Named Values (#647), in the view's terms: a Portion and display
// columns, which count a tab as the spaces it is expanded to.
//
// The view keeps a Portion in the display columns of the raw Log Line.
// ShownLine maps the characters of the raw Log Line to those of the text
// shown, and knows no tabs; this takes the steps from display columns to
// characters and back, so no caller chains them itself. The display columns
// are computed only when an operation needs them.
//
// Without a namer it is the identity: it computes no columns, gives every
// Portion back as it is, in constant time, and reads the Log Line only for
// the text of a Portion.
class ShownColumns {
public:
    // The identity.
    ShownColumns() = default;

    // The Log Line readLine reads, named by namer. Without a namer -- the
    // view shows no Value Names, or none could name anything -- it is the
    // identity, and readLine is called only by textShown().
    ShownColumns( const logsquirl::valuenames::ValueNamer* namer,
                  const std::function<QString()>& readLine );

    // The Portion grown to cover every Named Value it takes part of, so that
    // a Selection, a Copy and the length of a Selection take its whole raw
    // text. A Portion that reaches past the end of the Log Line is cut at its
    // end. A Portion wholly past the end, one on a Log Line without Named
    // Values, and an invalid one, is given back as it is.
    Portion covering( const Portion& portion ) const;

    // The text shown for the Portion once it covers every Named Value it
    // takes part of, with its tabs expanded as the text shown expands them:
    // what Copy as Shown copies. An end of the Portion inside a tab outside
    // every Named Value keeps its place in the tab, cut to the width the tab
    // has in the text shown; on a Log Line without Named Values it is the text
    // Copy gives. A Portion past the end of the Log Line is cut at its end.
    //
    // The identity gives the raw text of the Portion with its tabs expanded,
    // as Copy gives it, reading the Log Line once; the default one, which
    // reads none, gives an empty text. An invalid Portion gives an empty
    // text.
    QString textShown( const Portion& portion ) const;

private:
    // The display column each character of the raw Log Line starts at, and
    // its display length last.
    const logsquirl::vector<int>& rawDisplayColumns() const;
    // The same for the text shown.
    const logsquirl::vector<int>& shownDisplayColumns() const;

    bool named_ = false;
    // Kept by the identity only, for textShown().
    std::function<QString()> readLine_;
    QString rawText_;
    logsquirl::valuenames::ShownLine shown_;
    mutable std::optional<logsquirl::vector<int>> rawDisplayColumns_;
    mutable std::optional<logsquirl::vector<int>> shownDisplayColumns_;
};
