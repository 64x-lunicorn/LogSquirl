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

    // The Log Line rawText as it was named already: shown is what the namer
    // gave for it, or empty where it gave no Named Value. Neither read nor
    // named again (#745).
    ShownColumns( const QString& rawText, const logsquirl::valuenames::ShownLine& shown );

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

    // The Named Value at the column of position, as the Portion on the line of
    // position it takes in the raw Log Line: what a double-click selects.
    // None beside every Named Value -- inside a tab next to one, too -- past
    // the end of the Log Line, and for the identity, which reads nothing.
    std::optional<Portion> namedValueAt( const FilePosition& position ) const;

    // The display column of the text shown a raw display column is shown at.
    // One on a Named Value goes to the first column of what the value shows,
    // or to its last, as snap asks. One outside every Named Value keeps its
    // place inside its character -- a tab -- cut to the width that character
    // has in the text shown (#748). One past the end of the Log Line stays as
    // far past the end of the text shown. The identity gives the column as it
    // is and reads nothing.
    LineColumn shownColumn( LineColumn rawColumn, logsquirl::valuenames::Snap snap ) const;

    // The raw display column a display column of the text shown shows: the
    // reverse of shownColumn(), for what a click lands on (#743). One on what
    // a Named Value shows goes to the first column of its raw text, or to its
    // last, as snap asks. One outside every Named Value keeps its place inside
    // its character -- a tab -- cut to the width that character has in the
    // raw Log Line. One past the end of the text shown stays as far past the
    // end of the Log Line. The identity gives the column as it is and reads
    // nothing.
    LineColumn rawColumn( LineColumn shownColumn, logsquirl::valuenames::Snap snap ) const;

    // The Named Value whose text shown is drawn at a display column of the
    // text shown: what its tooltip tells of. None beside every Named Value --
    // inside a tab next to one, too -- past the end of the text shown, and for
    // the identity, which reads nothing.
    std::optional<logsquirl::valuenames::NamedValue>
    namedValueShownAt( LineColumn shownColumn ) const;

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
