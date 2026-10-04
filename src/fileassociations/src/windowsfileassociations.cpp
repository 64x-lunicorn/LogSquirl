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

#include "windowsfileassociations.h"

#include <QCoreApplication>
#include <QDir>

#include <algorithm>

#include "datalocation.h"
#include "log.h"

namespace {

const QString ClassesKey = QStringLiteral( "Software\\Classes\\" );
const QString RegisteredApplicationsKey = QStringLiteral( "Software\\RegisteredApplications" );
const QString FileExtsKey
    = QStringLiteral( "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\." );

// How often, and how many times, the user's choice is read again after
// Default apps was opened: every two seconds for ten minutes.
constexpr int WatchIntervalMs = 2000;
constexpr int WatchTicks = 300;

QString fileAssociationsKey()
{
    return WindowsFileAssociations::CapabilitiesKey + QStringLiteral( "\\FileAssociations" );
}

QString extensionKey( const QString& extension )
{
    return ClassesKey + QLatin1Char( '.' ) + extension;
}

QString openWithKey( const QString& extension )
{
    return extensionKey( extension ) + QStringLiteral( "\\OpenWithProgids" );
}

QString shownAs( const std::vector<FileType>& types )
{
    QStringList shown;
    for ( const auto& type : types ) {
        shown << type.shownAs;
    }
    return shown.join( QStringLiteral( ", " ) );
}

} // namespace

const QString WindowsFileAssociations::RegisteredApplicationName = QStringLiteral( "LogSquirl" );
const QString WindowsFileAssociations::CapabilitiesKey
    = QStringLiteral( "Software\\LogSquirl\\Capabilities" );

WindowsFileAssociations::System::~System() = default;

#ifndef Q_OS_WIN
std::unique_ptr<WindowsFileAssociations::System> WindowsFileAssociations::windowsSystem()
{
    return nullptr;
}
#endif

WindowsFileAssociations::Environment WindowsFileAssociations::Environment::ofThisRun()
{
    Environment environment;
    environment.executable = QDir::toNativeSeparators( QCoreApplication::applicationFilePath() );
    environment.portable = DataLocation::current().isPortable();
    return environment;
}

WindowsFileAssociations::WindowsFileAssociations( Environment environment,
                                                  std::unique_ptr<System> system )
    : environment_( std::move( environment ) )
    , system_( std::move( system ) )
{
    watch_.setInterval( WatchIntervalMs );
    connect( &watch_, &QTimer::timeout, this, &WindowsFileAssociations::checkForChanges );
}

WindowsFileAssociations::~WindowsFileAssociations() = default;

bool WindowsFileAssociations::isAvailable() const
{
    return system_ != nullptr && !environment_.executable.isEmpty();
}

QString WindowsFileAssociations::unavailableReason() const
{
    if ( isAvailable() ) {
        return {};
    }
    return tr( "LogSquirl cannot choose the file types it opens on this system." );
}

QString WindowsFileAssociations::applyNote() const
{
    auto note = tr( "Windows does not let an application make itself the default. After Apply, "
                    "LogSquirl opens the Default apps page of the Windows settings, where you "
                    "choose LogSquirl for each file type." );
    if ( environment_.portable ) {
        note
            += QLatin1Char( ' ' ) + tr( "If you move LogSquirl, these associations stop working." );
    }
    return note;
}

QString WindowsFileAssociations::command() const
{
    return QLatin1Char( '"' ) + environment_.executable + QStringLiteral( "\" \"%1\"" );
}

QString WindowsFileAssociations::openingProgId( const QString& extension ) const
{
    // The user's choice: UserChoiceLatest on the newest Windows 11, which
    // keeps UserChoice beside it for older readers.
    for ( const auto* choice : { "\\UserChoiceLatest", "\\UserChoice" } ) {
        if ( const auto progId
             = system_->value( Hive::CurrentUser, FileExtsKey + extension + QLatin1String( choice ),
                               QStringLiteral( "ProgId" ) );
             progId && !progId->isEmpty() ) {
            return *progId;
        }
    }
    // Else the extension's default, the user's before the machine's.
    for ( const auto hive : { Hive::CurrentUser, Hive::LocalMachine } ) {
        if ( const auto progId = system_->value( hive, extensionKey( extension ), {} );
             progId && !progId->isEmpty() ) {
            return *progId;
        }
    }
    return {};
}

bool WindowsFileAssociations::isProgIdRegistered( const QString& progId ) const
{
    const auto commandKey = ClassesKey + progId + QStringLiteral( "\\shell\\open\\command" );
    return system_->hasKey( Hive::CurrentUser, commandKey )
           || system_->hasKey( Hive::LocalMachine, commandKey );
}

FileAssociationState WindowsFileAssociations::state( const FileType& type ) const
{
    if ( !isAvailable() || !isProgIdRegistered( type.progId ) ) {
        return FileAssociationState::NotRegistered;
    }
    for ( const auto& extension : type.extensions ) {
        if ( openingProgId( extension ).compare( type.progId, Qt::CaseInsensitive ) != 0 ) {
            return FileAssociationState::Registered;
        }
    }
    return FileAssociationState::Default;
}

bool WindowsFileAssociations::registerForUser( const FileType& type )
{
    const auto progIdKey = ClassesKey + type.progId;
    bool written = system_->setValue( progIdKey, {}, type.name )
                   && system_->setValue( progIdKey + QStringLiteral( "\\DefaultIcon" ), {},
                                         environment_.executable + QStringLiteral( ",1" ) )
                   && system_->setValue( progIdKey + QStringLiteral( "\\shell\\open\\command" ), {},
                                         command() );
    for ( const auto& extension : type.extensions ) {
        written = written && system_->setValue( openWithKey( extension ), type.progId, {} )
                  && system_->setValue( fileAssociationsKey(), QLatin1Char( '.' ) + extension,
                                        type.progId );
    }
    return written
           && system_->setValue( CapabilitiesKey, QStringLiteral( "ApplicationName" ),
                                 QStringLiteral( "LogSquirl" ) )
           && system_->setValue( CapabilitiesKey, QStringLiteral( "ApplicationDescription" ),
                                 tr( "A fast, smart log file explorer" ) )
           && system_->setValue( RegisteredApplicationsKey, RegisteredApplicationName,
                                 CapabilitiesKey );
}

void WindowsFileAssociations::removeKeyIfEmpty( const QString& key )
{
    if ( system_->hasKey( Hive::CurrentUser, key )
         && system_->valueNames( Hive::CurrentUser, key ).isEmpty()
         && system_->subkeys( Hive::CurrentUser, key ).isEmpty() ) {
        system_->removeKey( key );
    }
}

bool WindowsFileAssociations::unregisterForUser( const FileType& type )
{
    bool removed = system_->removeKey( ClassesKey + type.progId );
    for ( const auto& extension : type.extensions ) {
        removed = system_->removeValue( openWithKey( extension ), type.progId ) && removed;
        removeKeyIfEmpty( openWithKey( extension ) );
        removeKeyIfEmpty( extensionKey( extension ) );
        removed = system_->removeValue( fileAssociationsKey(), QLatin1Char( '.' ) + extension )
                  && removed;
    }
    // The last type takes LogSquirl's registration with it.
    if ( system_->valueNames( Hive::CurrentUser, fileAssociationsKey() ).isEmpty() ) {
        removed = system_->removeKey( CapabilitiesKey ) && removed;
        removed = system_->removeValue( RegisteredApplicationsKey, RegisteredApplicationName )
                  && removed;
    }
    return removed;
}

FileAssociationResult WindowsFileAssociations::apply( const std::vector<FileType>& makeDefault,
                                                      const std::vector<FileType>& release )
{
    FileAssociationResult result;
    if ( !isAvailable() ) {
        for ( const auto* types : { &makeDefault, &release } ) {
            for ( const auto& type : *types ) {
                result.failed << type.id;
            }
        }
        result.error = unavailableReason();
        return result;
    }

    QStringList errors;
    std::vector<FileType> notRegistered;
    for ( const auto& type : makeDefault ) {
        if ( !registerForUser( type ) ) {
            result.failed << type.id;
            notRegistered.push_back( type );
        }
    }
    if ( !notRegistered.empty() ) {
        errors << tr( "LogSquirl could not register %1 in the registry." )
                      .arg( shownAs( notRegistered ) );
    }

    std::vector<FileType> notReleased;
    std::vector<FileType> stillDefault;
    for ( const auto& type : release ) {
        if ( !unregisterForUser( type ) ) {
            result.failed << type.id;
            notReleased.push_back( type );
        }
    }
    system_->associationsChanged();

    for ( const auto& type : release ) {
        if ( !result.failed.contains( type.id )
             && state( type ) == FileAssociationState::Default ) {
            result.failed << type.id;
            stillDefault.push_back( type );
        }
    }
    if ( !notReleased.empty() ) {
        errors << tr( "LogSquirl could not remove its registration of %1 from the registry." )
                      .arg( shownAs( notReleased ) );
    }
    if ( !stillDefault.empty() ) {
        errors << tr( "LogSquirl still opens %1: the installer made it the default for every user "
                      "of this computer, or you chose it in Windows. Choose another app for them "
                      "on the Default apps page of the Windows settings." )
                      .arg( shownAs( stillDefault ) );
    }

    // The user confirms the default on the Default apps page.
    QStringList awaited;
    for ( const auto& type : makeDefault ) {
        if ( !result.failed.contains( type.id )
             && state( type ) != FileAssociationState::Default ) {
            awaited << type.id;
        }
    }
    if ( !awaited.isEmpty() ) {
        if ( system_->openDefaultApps( RegisteredApplicationName ) ) {
            awaited_ = awaited;
            watched_ = states();
            watchTicks_ = 0;
            watch_.start();
        }
        else {
            LOG_WARNING << "Could not open the Default apps page of the settings";
            errors << tr( "LogSquirl could not open the Default apps page of the Windows "
                          "settings. Open it yourself and choose LogSquirl there." );
        }
    }

    result.error = errors.join( QLatin1Char( ' ' ) );
    return result;
}

void WindowsFileAssociations::checkForChanges()
{
    const auto current = states();
    const auto changed = current != watched_;
    watched_ = current;
    // Until the user chose LogSquirl for every type applied, or gave up.
    const auto confirmed
        = std::all_of( awaited_.begin(), awaited_.end(), [ &current ]( const QString& id ) {
              const auto it = current.find( id );
              return it != current.end() && it->second == FileAssociationState::Default;
          } );
    if ( confirmed || ++watchTicks_ >= WatchTicks ) {
        watch_.stop();
    }
    if ( changed ) {
        Q_EMIT statesChanged();
    }
}

bool WindowsFileAssociations::isWatching() const
{
    return watch_.isActive();
}
