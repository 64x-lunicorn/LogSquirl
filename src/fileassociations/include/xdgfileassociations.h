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

#include <functional>
#include <optional>

#include "fileassociations.h"

// The file associations of Linux and the other freedesktop.org systems
// (#720). The deb and the rpm install LogSquirl's desktop entry, which lists
// every MIME type LogSquirl opens, so LogSquirl is offered for them (#717);
// making it the default is the user's choice, made with `xdg-mime default`.
// xdg-mime cannot unset a default, so giving a type back removes LogSquirl's
// entry for it from the user's mimeapps.list files, where xdg-mime wrote it,
// and leaves every other application's entry alone.
//
// It is compiled on every platform, so its tests run everywhere; only Linux
// creates it for the run.
class XdgFileAssociations : public FileAssociations {
    Q_OBJECT

public:
    // What the run finds on the system, from its environment variables
    // following the XDG Base Directory specification.
    struct Environment {
        // The name of LogSquirl's desktop entry.
        QString desktopId = QStringLiteral( "logsquirl.desktop" );
        // $XDG_CONFIG_HOME, or ~/.config.
        QString configHome;
        // $XDG_DATA_HOME, or ~/.local/share.
        QString dataHome;
        // $XDG_DATA_DIRS, or /usr/local/share and /usr/share.
        QStringList dataDirs;
        // $XDG_CURRENT_DESKTOP, lower case: each may have a mimeapps.list of
        // its own.
        QStringList currentDesktops;
        // An AppImage run, or a build without the file types: nothing
        // registered LogSquirl's types, and nothing must.
        bool appImage = false;
        // Whether xdg-mime is installed.
        bool hasXdgMime = false;

        // The environment of this run.
        static Environment ofThisRun();
    };

    // Runs xdg-mime with the arguments and returns what it wrote to its
    // standard output, or nothing when it could not run or failed.
    using XdgMime = std::function<std::optional<QString>( const QStringList& arguments )>;

    // Runs the xdg-mime installed.
    static std::optional<QString> runXdgMime( const QStringList& arguments );

    explicit XdgFileAssociations( Environment environment, XdgMime xdgMime = &runXdgMime );

    bool isAvailable() const override;
    QString unavailableReason() const override;
    FileAssociationState state( const FileType& type ) const override;
    FileAssociationResult apply( const std::vector<FileType>& makeDefault,
                                 const std::vector<FileType>& release ) override;

    // LogSquirl's desktop entry as the desktop finds it: the first of the
    // data directories that has it. Empty when none has.
    QString desktopEntryPath() const;

    // The user's mimeapps.list files a default of LogSquirl's can be in: the
    // ones of the current desktops and the general one in the configuration
    // directory, and the one older versions of xdg-mime wrote, in the data
    // directory. Whether they exist or not.
    QStringList userMimeAppsLists() const;

private:
    Environment environment_;
    XdgMime xdgMime_;
};

// Reading and editing the files of the freedesktop.org specifications as
// text, keeping everything else in them as it was written.
namespace XdgFiles {

// The desktop entries of the [Default Applications] group of a mimeapps.list
// for the MIME type, in their order.
QStringList defaultApplications( const QString& mimeAppsList, const QString& mimeType );

// The mimeapps.list without desktopId among the default applications of the
// MIME type. The other applications stay in their order; a type left without
// one loses its line. Every other line stays as it was.
QString withoutDefaultApplication( const QString& mimeAppsList, const QString& mimeType,
                                   const QString& desktopId );

// The MimeType list of the [Desktop Entry] group of a desktop entry.
QStringList desktopEntryMimeTypes( const QString& desktopEntry );

} // namespace XdgFiles
