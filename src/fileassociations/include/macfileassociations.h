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

#include <QSettings>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "fileassociations.h"

// The file associations of macOS (#721). The app bundle declares the types
// LogSquirl opens (#718), so LaunchServices offers LogSquirl for them; making
// it the default is the user's choice, made per content type: the system log
// type for .log, LogSquirl's own Logcat type, and the optional types.
//
// macOS asks the user to confirm each change of a default application, so
// applying returns at once and statesChanged() follows once every
// confirmation is answered; a declined one leaves the type as it was.
//
// macOS cannot unset a default. Giving a type back makes the application
// that opened it before LogSquirl took it its default again; for a type
// LogSquirl does not know that of, it says it cannot.
//
// It is compiled on every platform, so its tests run everywhere; only macOS
// creates it for the run.
class MacFileAssociations : public FileAssociations {
    Q_OBJECT

public:
    // An application LaunchServices knows.
    struct Application {
        // The path of its bundle.
        QString path;
        // Its bundle identifier.
        QString identifier;
    };

    // What LogSquirl asks LaunchServices; the macOS one calls NSWorkspace.
    class LaunchServices {
    public:
        virtual ~LaunchServices();

        // This run's app bundle; nothing for a run outside one.
        virtual std::optional<Application> thisApplication() const = 0;

        // The application that opens files of the content type; nothing if
        // none does or the type is unknown.
        virtual std::optional<Application> defaultApplication( const QString& contentType ) const
            = 0;

        // Every application that can open files of the content type.
        virtual std::vector<Application> applications( const QString& contentType ) const = 0;

        // Whether an application bundle is at the path.
        virtual bool isApplication( const QString& path ) const = 0;

        // Called with an empty string when the change went through, with why
        // not otherwise.
        using Done = std::function<void( const QString& error )>;

        // Makes the application at the path the default for the content
        // type. macOS may ask the user first, so it returns at once and calls
        // done later, in the thread of the caller, unless the LaunchServices
        // is gone by then.
        virtual void setDefaultApplication( const QString& applicationPath,
                                            const QString& contentType, Done done ) = 0;
    };

    // The LaunchServices of the system: NSWorkspace on macOS, nothing
    // elsewhere.
    static std::unique_ptr<LaunchServices> systemLaunchServices();

    // Where the applications that opened each content type before LogSquirl
    // are kept for this user.
    static std::unique_ptr<QSettings> userSettings();

    MacFileAssociations( std::unique_ptr<LaunchServices> launchServices,
                         std::unique_ptr<QSettings> settings );
    ~MacFileAssociations() override;

    bool isAvailable() const override;
    QString unavailableReason() const override;
    QString applyNote() const override;
    FileAssociationState state( const FileType& type ) const override;
    FileAssociationResult apply( const std::vector<FileType>& makeDefault,
                                 const std::vector<FileType>& release ) override;

    // The application that opened the content type before LogSquirl was made
    // its default, by the path of its bundle; empty if none is known.
    QString previousDefault( const QString& contentType ) const;

private:
    void setDefault( const QString& applicationPath, const QString& contentType,
                     std::function<void()> succeeded );

    std::unique_ptr<LaunchServices> launchServices_;
    std::unique_ptr<QSettings> settings_;
    // The changes asked for and not answered yet.
    int pending_ = 0;
};
