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

#ifndef LOGSQUIRL_TESTS_TEAMFOLDERTESTING_H
#define LOGSQUIRL_TESTS_TEAMFOLDERTESTING_H

// Helpers shared by the tests that run a Team Folder against real Git
// repositories with file:// remotes.

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QStandardPaths>
#include <QString>
#include <QTest>

#include <map>
#include <optional>
#include <string>

#include "settingspolicies.h"
#include "teamfolder.h"

namespace teamfolder_testing {

constexpr int SyncTimeoutMs = 60'000;

// Sets the environment every Git of this process runs in for as long as it
// lives, and puts back what was there.
class IsolatedGitEnvironment {
public:
    IsolatedGitEnvironment()
    {
#ifdef Q_OS_WIN
        const QByteArray nullDevice = "NUL";
#else
        const QByteArray nullDevice = "/dev/null";
#endif
        set( "GIT_CONFIG_GLOBAL", nullDevice );
        set( "GIT_CONFIG_NOSYSTEM", "1" );
        set( "GIT_TERMINAL_PROMPT", "0" );
        set( "GIT_AUTHOR_NAME", "Team Folder Test" );
        set( "GIT_AUTHOR_EMAIL", "team-folder-test@example.invalid" );
        set( "GIT_COMMITTER_NAME", "Team Folder Test" );
        set( "GIT_COMMITTER_EMAIL", "team-folder-test@example.invalid" );
    }

    ~IsolatedGitEnvironment()
    {
        for ( const auto& [ name, value ] : previous_ ) {
            if ( value.has_value() ) {
                qputenv( name.c_str(), *value );
            }
            else {
                qunsetenv( name.c_str() );
            }
        }
    }

    IsolatedGitEnvironment( const IsolatedGitEnvironment& ) = delete;
    IsolatedGitEnvironment& operator=( const IsolatedGitEnvironment& ) = delete;

private:
    void set( const char* name, const QByteArray& value )
    {
        previous_.emplace( name, qEnvironmentVariableIsSet( name )
                                     ? std::optional<QByteArray>( qgetenv( name ) )
                                     : std::nullopt );
        qputenv( name, value );
    }

    std::map<std::string, std::optional<QByteArray>> previous_;
};

inline bool gitInstalled()
{
    return !QStandardPaths::findExecutable( QStringLiteral( "git" ) ).isEmpty();
}

// Whether the Team Folder is done syncing, waiting for it as long as needed.
inline bool settled( const TeamFolder& folder )
{
    return QTest::qWaitFor( [ &folder ] { return !folder.isSyncing(); }, SyncTimeoutMs );
}

inline void syncNow( TeamFolder& folder )
{
    REQUIRE( settled( folder ) );
    folder.sync();
    REQUIRE( settled( folder ) );
}

inline TeamFolderPolicy policyFor( const QString& url, const QString& subfolder = {} )
{
    return TeamFolderPolicy{ .enabled = true, .repositoryUrl = url, .subfolder = subfolder };
}

} // namespace teamfolder_testing

#endif
