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

// What the tests of a Command Source (#575) need to run commands on macOS and
// Linux: a plain shell, and a look at whether a process still runs.

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QtGlobal>

#include <cerrno>
#include <signal.h>
#include <sys/types.h>

#ifdef Q_OS_MACOS
#include <sys/sysctl.h>
#endif

// Commands run through /bin/sh -- or the shell given -- while this lives,
// rather than through the login shell of whoever runs the tests, whose profile
// may be slow or talk.
class ShellForTests {
public:
    explicit ShellForTests( const QByteArray& shell = "/bin/sh" )
        : previous_( qgetenv( "SHELL" ) )
        , hadShell_( qEnvironmentVariableIsSet( "SHELL" ) )
    {
        qputenv( "SHELL", shell );
    }

    ~ShellForTests()
    {
        if ( hadShell_ ) {
            qputenv( "SHELL", previous_ );
        }
        else {
            qunsetenv( "SHELL" );
        }
    }

    ShellForTests( const ShellForTests& ) = delete;
    ShellForTests& operator=( const ShellForTests& ) = delete;

private:
    QByteArray previous_;
    bool hadShell_;
};

// Whether the process is there and not a zombie: a process whose parent died
// is reaped by whoever adopts it, which in a container may never happen.
inline bool processIsRunning( qint64 pid )
{
    if ( ::kill( static_cast<pid_t>( pid ), 0 ) != 0 ) {
        return errno != ESRCH;
    }
#ifdef Q_OS_MACOS
    int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>( pid ) };
    kinfo_proc info{};
    size_t size = sizeof( info );
    if ( ::sysctl( name, 4, &info, &size, nullptr, 0 ) != 0 || size == 0 ) {
        return false;
    }
    return info.kp_proc.p_stat != SZOMB;
#else
    QFile stat( QStringLiteral( "/proc/%1/stat" ).arg( pid ) );
    if ( !stat.open( QIODevice::ReadOnly ) ) {
        return false;
    }
    // The state follows the command's name, which is in parentheses.
    const auto line = stat.readAll();
    const auto nameEnd = line.lastIndexOf( ')' );
    return nameEnd < 0 || nameEnd + 2 >= line.size() || line.at( nameEnd + 2 ) != 'Z';
#endif
}
