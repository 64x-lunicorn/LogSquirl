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
// argument, never as script or command text (#822).

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "openfilehelper.h"
#include "systemprogram.h"

namespace {

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
                    REQUIRE( action.url == QUrl::fromLocalFile( folder ) );
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
