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
#include <QTimer>

#include <memory>
#include <optional>

#include "fileassociations.h"

// The file associations of Windows, of the installed and the portable build
// (#722). Applying registers LogSquirl for the current user only, under
// HKEY_CURRENT_USER\Software\Classes, so it needs no administrator: the
// ProgIDs the installer uses (LogSquirl.<id>, #719), with the document icon
// and the running executable, LogSquirl under each extension's
// OpenWithProgids, and LogSquirl's capabilities under RegisteredApplications,
// which the "Default apps" page of the Windows settings lists.
//
// Windows does not let an application make itself the default: the user's
// choice (UserChoice) is protected. After registering, LogSquirl opens the
// Default apps page for LogSquirl, where the user confirms, and watches the
// user's choice for a while so the states follow. A type the current user
// registered but did not choose LogSquirl for there is Unconfirmed; one only
// the installer registered for the machine is Registered.
//
// Giving a type back removes the current user's registration only; what the
// installer registered for the machine stays.
//
// It is compiled on every platform, so its tests run everywhere; only Windows
// creates it for the run.
class WindowsFileAssociations : public FileAssociations {
    Q_OBJECT

public:
    // The registry and the shell, as LogSquirl uses them; the Windows one
    // calls the Win32 API.
    class System {
    public:
        enum class Hive {
            // HKEY_CURRENT_USER
            CurrentUser,
            // HKEY_LOCAL_MACHINE
            LocalMachine,
        };

        virtual ~System();

        // The text of a value, the default value for an empty name; an empty
        // text for a value that is no text; nothing if there is no value.
        virtual std::optional<QString> value( Hive hive, const QString& key,
                                              const QString& name ) const = 0;
        virtual bool hasKey( Hive hive, const QString& key ) const = 0;
        // The names of the values of a key, the default value as an empty one.
        virtual QStringList valueNames( Hive hive, const QString& key ) const = 0;
        virtual QStringList subkeys( Hive hive, const QString& key ) const = 0;

        // Writes a text value under HKEY_CURRENT_USER, creating the key.
        virtual bool setValue( const QString& key, const QString& name, const QString& value ) = 0;
        // Removes a value under HKEY_CURRENT_USER; true if it is gone.
        virtual bool removeValue( const QString& key, const QString& name ) = 0;
        // Removes a key under HKEY_CURRENT_USER and everything under it; true
        // if it is gone.
        virtual bool removeKey( const QString& key ) = 0;

        // Tells Explorer that file associations changed (SHCNE_ASSOCCHANGED).
        virtual void associationsChanged() = 0;
        // Opens the Default apps page of the settings for the application
        // registered under RegisteredApplications with that name.
        virtual bool openDefaultApps( const QString& registeredApplication ) = 0;
    };

    using Hive = System::Hive;

    // The run.
    struct Environment {
        // The executable, with native separators.
        QString executable;
        // A portable run, which associations stop working for once it is
        // moved.
        bool portable = false;

        // The environment of this run.
        static Environment ofThisRun();
    };

    // The System of Windows; nothing elsewhere.
    static std::unique_ptr<System> windowsSystem();

    // The name LogSquirl is registered under in RegisteredApplications, and
    // where its capabilities are, under HKEY_CURRENT_USER.
    static const QString RegisteredApplicationName;
    static const QString CapabilitiesKey;

    WindowsFileAssociations( Environment environment, std::unique_ptr<System> system );
    ~WindowsFileAssociations() override;

    bool isAvailable() const override;
    QString unavailableReason() const override;
    QString applyNote() const override;
    FileAssociationState state( const FileType& type ) const override;
    FileAssociationResult apply( const std::vector<FileType>& makeDefault,
                                 const std::vector<FileType>& release ) override;

    // The entry "Open with LogSquirl" of every file's context menu (#724),
    // under HKEY_CURRENT_USER for the page and HKEY_LOCAL_MACHINE for the
    // installer, both named ContextMenuKey. Removing the entry where the
    // installer added it for the machine hides it for the current user.
    static const QString ContextMenuKey;
    bool offersContextMenuEntry() const override;
    bool hasContextMenuEntry() const override;
    FileAssociationResult setContextMenuEntry( bool shown ) override;

    // A portable run whose registration for the current user opens another
    // executable: the portable LogSquirl that applied it was moved since
    // (#725).
    QString movedFrom() const override;

    // The ProgID that opens files with the extension (without the dot) for
    // the current user: the user's choice, or else the current user's and
    // then the machine's default of the extension. Empty if none.
    QString openingProgId( const QString& extension ) const;

    // Reads the states again and emits statesChanged() if they changed since
    // they were last read: what the watch after apply() does.
    void checkForChanges();

    // Whether apply() watches the user's choice, until the user chose
    // LogSquirl for every type it applied, or a while passed.
    bool isWatching() const;

private:
    QString command() const;
    bool isProgIdRegisteredForUser( const QString& progId ) const;
    bool isProgIdRegistered( const QString& progId ) const;
    bool registerForUser( const FileType& type );
    bool unregisterForUser( const FileType& type );
    void removeKeyIfEmpty( const QString& key );

    Environment environment_;
    std::unique_ptr<System> system_;

    QTimer watch_;
    int watchTicks_ = 0;
    // The states as last read, and the types the user is to choose LogSquirl
    // for.
    FileAssociationStates watched_;
    QStringList awaited_;
};
