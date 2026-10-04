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

#include <QString>
#include <QStringList>

#include <optional>

#include "fileassociations.h"

// What the user chose for the file types LogSquirl opens, as the settings
// keep it: whether the first start still asks, and the types the user chose
// (#723).
struct FileAssociationChoice {
    // Whether the first start asks, until the user applies a choice or
    // answers "Don't ask again".
    bool ask = true;
    // The ids of the types the user chose for LogSquirl, on the File
    // Associations page or in the first-start dialog. Nothing until then.
    std::optional<QStringList> chosen;

    // The user applied a choice with the types of checkedIds checked.
    void apply( const QStringList& checkedIds );
};

// What LogSquirl does about the file associations as it starts, once the
// main window shows: ask the first-start question, or not.
struct FileAssociationsAtStart {
    // Ask which file types LogSquirl opens.
    bool ask = false;
    // The types the question offers checked: the suggested ones and every
    // one LogSquirl is the default for already.
    QStringList checks;

    // What to do with these file associations and the choice as kept; the
    // question is only asked when mayAsk, so not while a start opens a file.
    static FileAssociationsAtStart of( const FileAssociations& fileAssociations,
                                       const FileAssociationChoice& choice, bool mayAsk );
};
