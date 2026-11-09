/*
 * Copyright (C) 2011, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
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

// Implements PersistentInfo, a singleton class which store/retrieve objects
// to persistent storage.

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "datalocation.h"
#include "log.h"
#include "uuid.h"

#include "persistentinfo.h"

constexpr uint8_t AppSettingsVersion = 1;
constexpr uint8_t SessionSettingsVersion = 2;

constexpr const char ApplicationSessionFile[] = "logsquirl";
constexpr const char SessionSettingsFile[] = "logsquirl_session";
constexpr const char PortableExtension[] = ".conf";

namespace {
QString makeSessionSettingsPath( const QString& appConfigPath )
{
    return QFileInfo( appConfigPath )
        .absoluteDir()
        .filePath( QString( SessionSettingsFile ) + PortableExtension );
}
} // namespace

PersistentInfo::PersistentInfo()
{
    // A portable run keeps its settings beside the executable, as it keeps its
    // data there: DataLocation decides both (#602).
    const auto& location = DataLocation::current();
    const auto portableConfigPath = location.portableSettingsPath();

    LOG_INFO << "Portable config path " << portableConfigPath;

    if ( location.isPortable() ) {
        PreparePortableSettings( portableConfigPath );
    }
    else {
        PrepareOsSettings();
    }

    updateSettings( *appSettings_, *sessionSettings_ );
}

void PersistentInfo::PreparePortableSettings( const QString& portableConfigPath )
{
    const auto sessionSettingsPath = makeSessionSettingsPath( portableConfigPath );

    if ( !QFileInfo::exists( sessionSettingsPath ) && QFileInfo::exists( portableConfigPath ) ) {
        QFile::copy( portableConfigPath, sessionSettingsPath );
    }

    appSettings_ = std::make_unique<QSettings>( portableConfigPath, QSettings::IniFormat );
    sessionSettings_ = std::make_unique<QSettings>( sessionSettingsPath, QSettings::IniFormat );
}

void PersistentInfo::PrepareOsSettings()
{
#ifdef Q_OS_WIN
    const auto format = QSettings::IniFormat;
#else
    const auto format = QSettings::NativeFormat;
#endif

    appSettings_ = std::make_unique<QSettings>( format, QSettings::UserScope, "logsquirl",
                                                ApplicationSessionFile );
    sessionSettings_ = std::make_unique<QSettings>( format, QSettings::UserScope, "logsquirl",
                                                    SessionSettingsFile );

#ifndef Q_OS_MACOS
    const auto sessionSettingsPath = makeSessionSettingsPath( appSettings_->fileName() );

    if ( sessionSettings_->allKeys().isEmpty() ) {
        if ( QFile::exists( sessionSettingsPath ) ) {
            QSettings oldSessionSettings{ sessionSettingsPath, QSettings::IniFormat };
            for ( const auto& key : oldSessionSettings.allKeys() ) {
                sessionSettings_->setValue( key, oldSessionSettings.value( key ) );
            }
        }
        else {
            for ( const auto& key : appSettings_->allKeys() ) {
                sessionSettings_->setValue( key, appSettings_->value( key ) );
            }
        }
    }
#endif
}

void PersistentInfo::updateSettings( QSettings& appSettings, QSettings& sessionSettings )
{
    const auto oldAppSettingsVersion = appSettings.value( "version", 0 ).toUInt();
    LOG_INFO << "App settings version" << oldAppSettingsVersion;

    if ( oldAppSettingsVersion != AppSettingsVersion ) {
        appSettings.remove( "geometry" );
        appSettings.remove( "versionchecker.nextDeadline" );
        appSettings.remove( "OpenFiles" );
        appSettings.remove( "RecentFiles" );
        appSettings.remove( "SavedSearches" );
    }

    std::pair<QString, QString> keysToMoveAround[] = {
        { "DefaultConfigurationView.searchAutoRefresh", "defaultView.searchAutoRefresh" },
        { "DefaultConfigurationView.searchIgnoreCase", "defaultView.searchIgnoreCase" },
        { "DefaultConfigurationView.splitterSizes", "defaultView.splitterSizes" },
    };

    for ( const auto& keyToMove : keysToMoveAround ) {
        if ( appSettings.contains( keyToMove.first ) ) {
            appSettings.setValue( keyToMove.second, appSettings.value( keyToMove.first ) );
            appSettings.remove( keyToMove.first );
        }
    }

    const auto oldSessionSettingsVersion = sessionSettings.value( "version", 0 ).toUInt();
    LOG_INFO << "Session settings version" << oldSessionSettingsVersion;

    if ( oldSessionSettingsVersion < 1 ) {
        sessionSettings.setValue( "Window/geometry", sessionSettings.value( "geometry" ) );
        sessionSettings.setValue( "VersionChecker/nextDeadline",
                                  sessionSettings.value( "versionchecker.nextDeadline" ) );
        sessionSettings.remove( "HighlighterSet" );
        for ( const auto& key : sessionSettings.childKeys() ) {
            sessionSettings.remove( key );
        }
    }
    if ( oldSessionSettingsVersion < SessionSettingsVersion ) {
        const auto geometry = sessionSettings.value( "Window/geometry" );
        sessionSettings.remove( "Window/geometry" );

        if ( geometry.isValid() ) {
            const auto id = generateIdFromUuid();
            sessionSettings.beginGroup( "OpenFiles" );
            // The top line stored beside each is left behind (#559).
            std::vector<std::pair<QString, QString>> openFiles;
            int size = sessionSettings.beginReadArray( "openFiles" );
            LOG_INFO << "OpenFiles" << size;

            for ( int i = 0; i < size; ++i ) {
                sessionSettings.setArrayIndex( i );
                QString fileName = sessionSettings.value( "fileName" ).toString();
                QString viewContext = sessionSettings.value( "viewContext" ).toString();
                openFiles.emplace_back( fileName, viewContext );
            }
            sessionSettings.endArray();
            sessionSettings.endGroup(); // OpenFiles

            sessionSettings.beginGroup( "Window" );
            sessionSettings.setValue( "version", 1 );

            sessionSettings.beginWriteArray( "windows" );
            sessionSettings.setArrayIndex( 0 );
            sessionSettings.setValue( "id", id );
            sessionSettings.setValue( "geometry", geometry );

            sessionSettings.beginGroup( "OpenFiles" );
            sessionSettings.setValue( "version", 1 );
            sessionSettings.beginWriteArray( "openFiles" );
            for ( unsigned i = 0; i < openFiles.size(); ++i ) {
                sessionSettings.setArrayIndex( static_cast<int>( i ) );

                sessionSettings.setValue( "fileName", openFiles.at( i ).first );
                sessionSettings.setValue( "viewContext", openFiles.at( i ).second );
            }
            sessionSettings.endArray(); // openfiles
            sessionSettings.endGroup(); // OpenFiles
            sessionSettings.endArray(); // windows
            sessionSettings.endGroup(); // Window
        }
    }

    appSettings.setValue( "version", AppSettingsVersion );
    sessionSettings.setValue( "version", SessionSettingsVersion );
}

PersistentInfo& PersistentInfo::getInstance()
{
    static PersistentInfo pInfo;
    return pInfo;
}

QSettings& PersistentInfo::getSettings( app_settings )
{
    return *getInstance().appSettings_;
}

QSettings& PersistentInfo::getSettings( session_settings )
{
    return *getInstance().sessionSettings_;
}
