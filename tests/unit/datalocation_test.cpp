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

// Where a portable and an installed run keep their settings and data (#602),
// and how a portable run takes over what it kept before (#613).

#include <catch2/catch_test_macros.hpp>

#include "datalocation.h"
#include "logformatcatalog.h"
#include "plugincatalog.h"
#include "teamfolder.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
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

namespace {

void writeFile( const QString& path, const QByteArray& content )
{
    REQUIRE( QDir().mkpath( QFileInfo( path ).path() ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( content ) == content.size() );
}

QByteArray readFile( const QString& path )
{
    QFile file( path );
    return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray{};
}

// Every file under a directory, relative to it, with its content.
QMap<QString, QByteArray> filesUnder( const QString& directory )
{
    QMap<QString, QByteArray> files;
    QDirIterator it( directory, QDir::Files | QDir::Hidden | QDir::System,
                     QDirIterator::Subdirectories );
    while ( it.hasNext() ) {
        const auto path = it.next();
        files.insert( QDir( directory ).relativeFilePath( path ), readFile( path ) );
    }
    return files;
}

} // namespace

SCENARIO( "A portable run takes over the data the portable package kept in the user profile",
          "[datalocation]" )
{
    // All three stand in for the real locations, which this never touches.
    QTemporaryDir executableDir;
    QTemporaryDir oldDataDir;
    QTemporaryDir oldConfigDir;
    REQUIRE( executableDir.isValid() );
    REQUIRE( oldDataDir.isValid() );
    REQUIRE( oldConfigDir.isValid() );

    const QDir beside( executableDir.path() );
    const QDir oldData( oldDataDir.path() );
    const QDir oldConfig( oldConfigDir.path() );

    GIVEN( "Log Formats, plugins, plugin configuration, a Team Folder clone and themes in the "
           "old locations" )
    {
        writeFile( oldData.filePath( "formats/mine.conf" ), "format" );
        writeFile( oldData.filePath( "plugins/installed/plugin.json" ), "installed" );
        writeFile( oldData.filePath( "plugins/shipped/plugin.json" ), "user's copy" );
        writeFile( oldData.filePath( "plugin_config/installed/settings.ini" ), "config" );
        writeFile( oldData.filePath( "teamfolder/.hidden/HEAD" ), "ref: refs/heads/main" );
        writeFile( oldData.filePath( "teamfolder/groups/team.json" ), "groups" );
        writeFile( oldConfig.filePath( "themes/dark.qss" ), "stylesheet" );
        const auto oldDataBefore = filesUnder( oldData.path() );
        const auto oldConfigBefore = filesUnder( oldConfig.path() );

        AND_GIVEN( "A portable run whose executable directory holds only the plugins its "
                   "package ships" )
        {
            writeFile( beside.filePath( "plugins/shipped/plugin.json" ), "shipped" );
            const DataLocation location{ true, executableDir.path() };

            WHEN( "It starts for the first time" )
            {
                const auto takeOver
                    = location.takeOverOldPortableData( oldData.path(), oldConfig.path() );

                THEN( "The data is beside the executable" )
                {
                    CHECK( takeOver.failed.isEmpty() );
                    CHECK( readFile( beside.filePath( "formats/mine.conf" ) ) == "format" );
                    CHECK( readFile( beside.filePath( "plugins/installed/plugin.json" ) )
                           == "installed" );
                    CHECK( readFile( beside.filePath( "plugin_config/installed/settings.ini" ) )
                           == "config" );
                    CHECK( readFile( beside.filePath( "teamfolder/.hidden/HEAD" ) )
                           == "ref: refs/heads/main" );
                    CHECK( readFile( beside.filePath( "teamfolder/groups/team.json" ) )
                           == "groups" );
                    CHECK( readFile( beside.filePath( "themes/dark.qss" ) ) == "stylesheet" );
                }

                THEN( "The plugin the package ships is kept" )
                {
                    CHECK( readFile( beside.filePath( "plugins/shipped/plugin.json" ) )
                           == "shipped" );
                    CHECK( takeOver.skipped.size() == 1 );
                }

                THEN( "The old locations are left as they were" )
                {
                    CHECK( filesUnder( oldData.path() ) == oldDataBefore );
                    CHECK( filesUnder( oldConfig.path() ) == oldConfigBefore );
                }

                THEN( "It leaves the marker" )
                {
                    CHECK( QFileInfo::exists( location.takeOverMarkerPath() ) );
                }

                AND_WHEN( "It starts again after the user removed what it took over" )
                {
                    REQUIRE( QDir( beside.filePath( "formats" ) ).removeRecursively() );
                    REQUIRE( QDir( beside.filePath( "plugins/installed" ) ).removeRecursively() );
                    REQUIRE( QDir( beside.filePath( "plugin_config" ) ).removeRecursively() );
                    REQUIRE( QDir( beside.filePath( "teamfolder" ) ).removeRecursively() );
                    REQUIRE( QDir( beside.filePath( "themes" ) ).removeRecursively() );

                    const auto again
                        = location.takeOverOldPortableData( oldData.path(), oldConfig.path() );

                    THEN( "Nothing is copied twice" )
                    {
                        CHECK( again.copied.isEmpty() );
                        CHECK_FALSE( QFileInfo::exists( beside.filePath( "formats" ) ) );
                        CHECK_FALSE( QFileInfo::exists( beside.filePath( "plugins/installed" ) ) );
                    }
                }
            }
        }

        AND_GIVEN( "A portable run with Log Formats of its own beside the executable" )
        {
            writeFile( beside.filePath( "formats/own.conf" ), "own" );
            const DataLocation location{ true, executableDir.path() };

            WHEN( "It starts" )
            {
                const auto takeOver
                    = location.takeOverOldPortableData( oldData.path(), oldConfig.path() );

                THEN( "Nothing is copied and the old locations are left as they were" )
                {
                    CHECK( takeOver.copied.isEmpty() );
                    CHECK( takeOver.failed.isEmpty() );
                    const auto marker = QFileInfo( location.takeOverMarkerPath() ).fileName();
                    CHECK( filesUnder( beside.path() ).keys()
                           == QStringList{ "formats/own.conf", marker } );
                    CHECK( filesUnder( oldData.path() ) == oldDataBefore );
                    CHECK( filesUnder( oldConfig.path() ) == oldConfigBefore );
                }
            }
        }

        AND_GIVEN( "An installed run" )
        {
            const DataLocation location{ false, executableDir.path() };

            WHEN( "It starts" )
            {
                const auto takeOver
                    = location.takeOverOldPortableData( oldData.path(), oldConfig.path() );

                THEN( "Nothing is copied and no marker is left" )
                {
                    CHECK( takeOver.copied.isEmpty() );
                    CHECK( takeOver.skipped.isEmpty() );
                    CHECK( filesUnder( beside.path() ).isEmpty() );
                }
            }
        }
    }

    GIVEN( "A portable run and nothing in the old locations" )
    {
        const DataLocation location{ true, executableDir.path() };

        WHEN( "It starts" )
        {
            const auto takeOver
                = location.takeOverOldPortableData( oldData.path(), oldConfig.path() );

            THEN( "Nothing is copied and no marker is left, so the next start looks again" )
            {
                CHECK( takeOver.copied.isEmpty() );
                CHECK( takeOver.skipped.isEmpty() );
                CHECK( takeOver.failed.isEmpty() );
                CHECK_FALSE( QFileInfo::exists( location.takeOverMarkerPath() ) );
            }
        }
    }
}

SCENARIO( "A Benchmark Run keeps everything it stores in a directory of its own", "[datalocation]" )
{
    QTemporaryDir isolatedDir;
    QTemporaryDir oldDataDir;
    QTemporaryDir oldConfigDir;
    REQUIRE( isolatedDir.isValid() );
    REQUIRE( oldDataDir.isValid() );
    REQUIRE( oldConfigDir.isValid() );
    const auto isolatedPath = QDir::cleanPath( isolatedDir.path() );

    GIVEN( "A location isolated in a directory" )
    {
        const auto location = DataLocation::isolatedIn( isolatedDir.path() );

        THEN( "Its settings, Session and data are in that directory" )
        {
            CHECK( location.isPortable() );
            CHECK( location.portableSettingsPath()
                   == QDir( isolatedPath ).filePath( "logsquirl.conf" ) );
            CHECK( location.dataDirectory() == isolatedPath );
            CHECK( location.configDirectory() == isolatedPath );
        }

        THEN( "It takes over nothing from the user's locations" )
        {
            writeFile( QDir( oldDataDir.path() ).filePath( "formats/mine.conf" ), "format" );
            writeFile( QDir( oldConfigDir.path() ).filePath( "themes/dark.qss" ), "stylesheet" );

            const auto takeOver
                = location.takeOverOldPortableData( oldDataDir.path(), oldConfigDir.path() );

            CHECK( takeOver.copied.isEmpty() );
            CHECK( takeOver.skipped.isEmpty() );
            CHECK( takeOver.failed.isEmpty() );
            CHECK( filesUnder( isolatedPath ).isEmpty() );
        }
    }

    GIVEN( "A run whose location is decided already" )
    {
        const auto decided = DataLocation::current().dataDirectory();

        THEN( "It can no longer be isolated" )
        {
            CHECK_FALSE( DataLocation::isolateCurrentIn( isolatedDir.path() ) );
            CHECK( DataLocation::current().dataDirectory() == decided );
        }
    }
}
