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

// Revealing a Log File in the file manager hands its path over as a process
// argument, never as script or command text (#822). A program started by a
// bare name is looked up on the PATH, never in the working directory (#823).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include "openfilehelper.h"
#include "systemprogram.h"

namespace {

// Makes a directory the working directory while it lives.
class WorkingDirectory {
public:
    explicit WorkingDirectory( const QString& path )
        : previous_( QDir::currentPath() )
    {
        REQUIRE( QDir::setCurrent( path ) );
    }
    ~WorkingDirectory()
    {
        QDir::setCurrent( previous_ );
    }
    WorkingDirectory( const WorkingDirectory& ) = delete;
    WorkingDirectory& operator=( const WorkingDirectory& ) = delete;

private:
    QString previous_;
};

// An executable file of this name in the directory: a program planted there.
QString plantProgram( const QDir& directory, const QString& name )
{
    const auto path = directory.filePath( name );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "#!/bin/sh\nexit 0\n" );
    file.close();
    REQUIRE( file.setPermissions( file.permissions() | QFileDevice::ExeOwner ) );
    return path;
}

// Every character a file system allows that would break out of a quoted
// AppleScript string or a command line: a quote, an ampersand, parentheses,
// a backslash and a newline. The name is an AppleScript injection as written.
const QString NameWithEveryHostileCharacter
    = QStringLiteral( "x\" & (do shell script \"id\") & \"\\\n.log" );

bool holdsScriptText( const QStringList& arguments )
{
    for ( const auto& argument : arguments ) {
        if ( argument.contains( QLatin1String( "tell application" ) )
             || argument.contains( QLatin1String( "osascript" ) )
             || argument.contains( QLatin1String( "-e" ) ) ) {
            return true;
        }
    }
    return false;
}

} // namespace

SCENARIO( "a Log File is revealed in the file manager by its path, never by script text",
          "[openfilehelper]" )
{
    GIVEN( "a Log File whose name has a quote, an ampersand, parentheses, a backslash and a "
           "newline" )
    {
        const QString folder = QStringLiteral( "/Users/someone/logs" );
        const QString path = folder + QLatin1Char( '/' ) + NameWithEveryHostileCharacter;

        WHEN( "it is revealed on macOS" )
        {
            const auto action = fileExplorerAction( path, FileExplorerPlatform::MacOS, QString() );

            THEN( "Finder is asked through open -R with the path as one argument" )
            {
                REQUIRE( action.program == QStringLiteral( "/usr/bin/open" ) );
                REQUIRE( action.arguments == QStringList{ QStringLiteral( "-R" ), path } );
                REQUIRE( !holdsScriptText( action.arguments ) );
            }
        }

        WHEN( "it is revealed on Windows with a SystemRoot" )
        {
            const QString systemRoot = QStringLiteral( "C:/Windows" );
            const auto action
                = fileExplorerAction( path, FileExplorerPlatform::Windows, systemRoot );

            THEN( "the Explorer below the system root selects the file by its native path" )
            {
                REQUIRE( action.program
                         == QDir::toNativeSeparators( systemRoot + "/explorer.exe" ) );
                REQUIRE( action.program != QStringLiteral( "explorer.exe" ) );
                REQUIRE( action.arguments
                         == QStringList{ QStringLiteral( "/select," )
                                         + QDir::toNativeSeparators( path ) } );
                REQUIRE( !holdsScriptText( action.arguments ) );
            }
        }

        WHEN( "it is revealed on Windows without a SystemRoot" )
        {
            const auto action
                = fileExplorerAction( path, FileExplorerPlatform::Windows, QString() );

            THEN( "the Explorer is never named by its bare name" )
            {
                REQUIRE( action.program != QStringLiteral( "explorer.exe" ) );
                if ( action.program.isEmpty() ) {
                    // The backslash of the name is a separator on Windows, so the
                    // folder is what QFileInfo makes of the path there too.
                    REQUIRE( action.url == QUrl::fromLocalFile( QFileInfo( path ).path() ) );
                }
                else {
                    REQUIRE( QDir::isAbsolutePath( action.program ) );
                    REQUIRE( action.arguments
                             == QStringList{ QStringLiteral( "/select," )
                                             + QDir::toNativeSeparators( path ) } );
                }
            }
        }

        WHEN( "it is revealed on another platform" )
        {
            const auto action = fileExplorerAction( path, FileExplorerPlatform::Other, QString() );

            THEN( "the folder is opened through the desktop services, no program is run" )
            {
                REQUIRE( action.program.isEmpty() );
                REQUIRE( action.arguments.isEmpty() );
                REQUIRE( action.url == QUrl::fromLocalFile( folder ) );
            }
        }
    }
}

SCENARIO( "a system program is resolved below the system root", "[openfilehelper]" )
{
    GIVEN( "a SystemRoot" )
    {
        const QString systemRoot = QStringLiteral( "C:/Windows" );

        THEN( "a program of the system folder is its absolute native path" )
        {
            REQUIRE( systemProgramPath( QStringLiteral( "explorer.exe" ), systemRoot )
                     == QDir::toNativeSeparators( QStringLiteral( "C:/Windows/explorer.exe" ) ) );
            REQUIRE(
                systemProgramPath( QStringLiteral( "System32/cmd.exe" ), systemRoot )
                == QDir::toNativeSeparators( QStringLiteral( "C:/Windows/System32/cmd.exe" ) ) );
        }
    }

    GIVEN( "no SystemRoot" )
    {
        THEN( "the program is found on PATH by its file name or not at all, never by a bare "
              "name" )
        {
            const auto program
                = systemProgramPath( QStringLiteral( "System32/cmd.exe" ), QString() );
            REQUIRE( ( program.isEmpty() || QDir::isAbsolutePath( program ) ) );
        }
    }
}

SCENARIO( "a program to start is resolved to an absolute path, never in the working directory",
          "[openfilehelper][systemprogram]" )
{
    GIVEN( "a program of a bare name planted in the working directory, which is not on PATH" )
    {
        const QTemporaryDir planted;
        REQUIRE( planted.isValid() );
        const QString name = QStringLiteral( "logsquirl-planted-823.exe" );
        const auto path = plantProgram( QDir( planted.path() ), name );
        const WorkingDirectory cwd( planted.path() );
        REQUIRE( QFileInfo::exists( name ) );

        THEN( "the bare name resolves to nothing on every platform" )
        {
            const auto platform = GENERATE( ProgramPlatform::Windows, ProgramPlatform::Other );
            CHECK( resolvedProgramPath( name, platform, QString() ).isEmpty() );
            CHECK(
                resolvedProgramPath( name, platform, QStringLiteral( "C:/Windows" ) ).isEmpty() );
            CHECK( resolvedProgramPath( name ).isEmpty() );
        }

        THEN( "a relative path with separators is refused" )
        {
            const auto platform = GENERATE( ProgramPlatform::Windows, ProgramPlatform::Other );
            CHECK( resolvedProgramPath( QStringLiteral( "./" ) + name, platform, QString() )
                       .isEmpty() );
            CHECK( resolvedProgramPath( QStringLiteral( ".\\" ) + name, platform, QString() )
                       .isEmpty() );
            CHECK( resolvedProgramPath( QStringLiteral( "bin/" ) + name, platform, QString() )
                       .isEmpty() );
        }

        THEN( "its absolute path is returned as it is" )
        {
            CHECK( resolvedProgramPath( path ) == path );
            CHECK( resolvedProgramPath( path, ProgramPlatform::Other, QString() ) == path );
        }

        THEN( "an absolute path that leads nowhere resolves to nothing" )
        {
            CHECK( resolvedProgramPath( planted.filePath( "not-there.exe" ) ).isEmpty() );
        }

        THEN( "a bare name on PATH is where PATH finds it, an absolute path" )
        {
#ifdef Q_OS_WIN
            const QString onPath = QStringLiteral( "where" );
#else
            const QString onPath = QStringLiteral( "sh" );
#endif
            const auto resolved = resolvedProgramPath( onPath );
            REQUIRE_FALSE( resolved.isEmpty() );
            CHECK( QDir::isAbsolutePath( resolved ) );
            CHECK( resolved == QStandardPaths::findExecutable( onPath ) );
        }
    }

    GIVEN( "the Windows command interpreter by its bare name" )
    {
        THEN( "it is the one of the system folder, whatever PATH or the working directory hold" )
        {
            const auto name = GENERATE( QStringLiteral( "cmd.exe" ), QStringLiteral( "CMD.EXE" ),
                                        QStringLiteral( "cmd" ) );
            CHECK( resolvedProgramPath( name, ProgramPlatform::Windows,
                                        QStringLiteral( "C:/Windows" ) )
                   == QDir::toNativeSeparators( QStringLiteral( "C:/Windows/System32/cmd.exe" ) ) );
        }

        THEN( "without a system root it is found on PATH by its name or not at all" )
        {
            const auto resolved = resolvedProgramPath( QStringLiteral( "cmd.exe" ),
                                                       ProgramPlatform::Windows, QString() );
            CHECK( ( resolved.isEmpty() || QDir::isAbsolutePath( resolved ) ) );
        }

        THEN( "elsewhere it is an ordinary program name" )
        {
            const auto resolved
                = resolvedProgramPath( QStringLiteral( "cmd.exe" ), ProgramPlatform::Other,
                                       QStringLiteral( "C:/Windows" ) );
            CHECK( resolved == QStandardPaths::findExecutable( QStringLiteral( "cmd.exe" ) ) );
        }
    }

    GIVEN( "no program name" )
    {
        THEN( "nothing is resolved" )
        {
            CHECK( resolvedProgramPath( QString() ).isEmpty() );
        }
    }
}
