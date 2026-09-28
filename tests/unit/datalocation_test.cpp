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

// Where a portable and an installed run keep their settings and data (#602).

#include <catch2/catch_test_macros.hpp>

#include "datalocation.h"
#include "logformatcatalog.h"
#include "plugincatalog.h"
#include "teamfolder.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

using logsquirl::plugins::PluginCatalog;

namespace {

// One spelling of a path, whether or not it exists yet.
QString sameSpelling( const QString& path )
{
    const QFileInfo info( path );
    return info.exists() ? info.canonicalFilePath() : QDir::cleanPath( path );
}

} // namespace

SCENARIO( "A run is portable when its build says so or it finds logsquirl.conf beside itself",
          "[datalocation]" )
{
    GIVEN( "A directory the executable is in" )
    {
        QTemporaryDir executableDir;
        REQUIRE( executableDir.isValid() );
        const auto executablePath = QDir::cleanPath( executableDir.path() );

        WHEN( "The build does not force portable and there is no logsquirl.conf" )
        {
            const DataLocation location{ false, executableDir.path() };

            THEN( "The run is installed and keeps its data in the platform's locations" )
            {
                CHECK_FALSE( location.isPortable() );
                CHECK( location.dataDirectory()
                       == QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) );
                CHECK( location.configDirectory()
                       == QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) );
            }

            THEN( "Its portable settings file would be logsquirl.conf beside the executable" )
            {
                CHECK( location.portableSettingsPath()
                       == QDir( executablePath ).filePath( "logsquirl.conf" ) );
            }
        }

        WHEN( "The build does not force portable but logsquirl.conf is beside the executable" )
        {
            QFile settings( QDir( executablePath ).filePath( "logsquirl.conf" ) );
            REQUIRE( settings.open( QIODevice::WriteOnly ) );
            settings.close();

            const DataLocation location{ false, executableDir.path() };

            THEN( "The run is portable and keeps its data beside the executable" )
            {
                CHECK( location.isPortable() );
                CHECK( location.dataDirectory() == executablePath );
                CHECK( location.configDirectory() == executablePath );
            }
        }

        WHEN( "The build forces portable" )
        {
            const DataLocation location{ true, executableDir.path() };

            THEN( "The run is portable without a logsquirl.conf" )
            {
                CHECK( location.isPortable() );
                CHECK( location.dataDirectory() == executablePath );
                CHECK( location.configDirectory() == executablePath );
            }
        }
    }
}

SCENARIO( "A portable run keeps its Log Formats, plugins, Team Folder and themes beside itself",
          "[datalocation]" )
{
    // Every test binary forces portable and runs from a scratch directory of
    // its own (isolated_settings.h), so what it stores stays out of the user's
    // application data.
    GIVEN( "This test binary, which runs portable" )
    {
        const auto& location = DataLocation::current();
        REQUIRE( location.isPortable() );

        const QDir executableDir( QCoreApplication::applicationDirPath() );
        REQUIRE( sameSpelling( location.executableDirectory() )
                 == sameSpelling( executableDir.path() ) );

        THEN( "Its data and configuration directories are the executable's directory" )
        {
            CHECK( location.dataDirectory() == location.executableDirectory() );
            CHECK( location.configDirectory() == location.executableDirectory() );
        }

        THEN( "Its Log Formats are in formats beside the executable" )
        {
            CHECK( sameSpelling( LogFormatCatalog::defaultUserFormatsDirectory() )
                   == sameSpelling( executableDir.filePath( "formats" ) ) );
        }

        THEN( "Its user plugin directory is plugins beside the executable" )
        {
            CHECK( sameSpelling( PluginCatalog::userPluginDirectory() )
                   == sameSpelling( executableDir.filePath( "plugins" ) ) );
        }

        THEN( "Every plugin directory is scanned once" )
        {
            QStringList spellings;
            for ( const auto& directory : PluginCatalog::defaultPluginDirectories() ) {
                spellings << sameSpelling( directory );
            }
            const auto scanned = spellings.size();
            spellings.removeDuplicates();
            CHECK( spellings.size() == scanned );
#ifndef Q_OS_MACOS
            // The user plugin directory is the application plugin directory.
            CHECK( scanned == 1 );
#endif
        }

        THEN( "Its Team Folder clone is in teamfolder beside the executable" )
        {
            CHECK( sameSpelling( TeamFolder::defaultCloneDirectory() )
                   == sameSpelling( executableDir.filePath( "teamfolder" ) ) );
        }

        THEN( "Its theme stylesheets are in themes beside the executable" )
        {
            CHECK( sameSpelling( Theme::userThemesDirectory() )
                   == sameSpelling( executableDir.filePath( "themes" ) ) );
        }
    }
}
