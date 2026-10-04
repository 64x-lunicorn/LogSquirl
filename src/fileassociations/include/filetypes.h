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

#include <vector>

// A file type LogSquirl opens: one choice for the user, with one or more
// extensions (#720). The list is declared once, in cmake/FileTypes.cmake,
// which generates every platform's packaging from it and this list too, so
// the ids, MIME types, type identifiers and ProgIDs the application uses are
// the ones the packages and the installer use.
struct FileType {
    // Where a choice is listed: under "Log files", under "More (optional)",
    // or nowhere, for a type only ever offered under "Open with".
    enum class Group { Logs, Optional, OpenWith };

    QString id;
    Group group = Group::OpenWith;
    // Whether the choice is offered checked, where nothing is chosen yet.
    bool checkedByDefault = false;
    // What the choice says, untranslated: "Android Logcat traces".
    QString label;
    // Its extensions as the choice shows them: ".adb, .adb0-.adb9".
    QString shownAs;
    // What a file of the type is called: "Android Logcat trace".
    QString name;
    // Without the dot.
    QStringList extensions;
    // The Linux MIME type.
    QString mimeType;
    // The macOS uniform type identifier.
    QString uti;
    // The Windows ProgID, LogSquirl.<id>.
    QString progId;
};

namespace FileTypes {

// The types the user chooses from, Log files first, in the order of the
// list.
const std::vector<FileType>& choices();

// The types LogSquirl is only offered for under "Open with" and never makes
// itself the default for: the compressed ones.
const std::vector<FileType>& openWithOnly();

// The type of the list with that id, of either kind; nullptr if none has it.
const FileType* find( const QString& id );

// The title of a group, translated: "Log files", "More (optional)".
QString groupTitle( FileType::Group group );

// What the choice of a type says, translated.
QString label( const FileType& type );

} // namespace FileTypes
