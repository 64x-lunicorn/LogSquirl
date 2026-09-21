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

#ifndef LOGSQUIRL_TEST_ISOLATED_SETTINGS_H
#define LOGSQUIRL_TEST_ISOLATED_SETTINGS_H

// Every test process runs beside a settings file of its own (#370).
//
// A test binary forces the portable settings, and those are the `logsquirl.conf`
// beside the executable -- so every test binary built into `<build>/output/`
// reads and writes the one file there, as do the application and the command
// line tool when they are run from the build directory. Several tests write to
// it. Since #217 each test case is its own process, so a case that dies between
// applying a setting and restoring it leaves that setting behind for every later
// case, in that run and in every run afterwards: exactly the leftover Encoding
// that #364 cost.
//
// The lever is the one the application already offers, and the one the grep CLI
// test (#364) and the E2E suite (#328) use: the portable settings live beside
// the executable, so the executable moves. Before anything reads a setting, the
// process hard-links itself into a scratch directory of its own and runs itself
// from there; the settings file is created there, and the directory goes when
// the process is done with it. Nothing restores anything, so nothing depends on
// a test case surviving: a case that dies takes its scratch directory with it,
// and the next case starts from the empty settings file it made itself.
//
// The scratch directory is a sibling of the directory the binaries are built
// into, so it is on the same file system and a hard link always works: a link
// costs a directory entry, while copying a Debug test binary per test case would
// not be affordable. The neighbours a test runs or loads (file_write_helper, the
// command line tool, the shared libraries Windows looks for beside the
// executable) are hard-linked along with it; the settings files deliberately are
// not.
//
// This is what a test binary run by hand does -- once, for the whole run.
// Under ctest, where every test case is a process of its own, the same scratch
// directory is made for the case by cmake/CatchTestDiscoveryRunTest.cmake
// before the binary starts, which says so through the environment below: a
// second start of a Debug test binary per case costs about a second, and there
// are several hundred cases.

#include <optional>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtGlobal>

#include <cstdio>
#include <filesystem>
#include <system_error>

#ifndef Q_OS_WIN
#include <csignal>
#endif

namespace isolated_settings {

// Says that the settings beside this executable are already this process's own:
// set for the process this one relaunches, and set by
// cmake/CatchTestDiscoveryRunTest.cmake, which does the same for a test case
// ctest runs and saves it a second start of the binary.
inline constexpr const char* RelaunchedMarker = "LOGSQUIRL_TEST_SETTINGS_ISOLATED";

namespace detail {

inline std::filesystem::path asPath( const QString& path )
{
    return std::filesystem::path( path.toStdU16String() );
}

// A hard link, which costs a directory entry and no bytes. Says whether the
// file is now there.
inline bool hardLink( const QString& source, const QString& target )
{
    std::error_code error;
    std::filesystem::create_hard_link( asPath( source ), asPath( target ), error );
    return !error;
}

// A copy, for the one file that has to be there even where a hard link is not
// to be had: the executable itself. Nothing else is copied -- a Debug test
// binary copied per test case would cost minutes a run.
inline bool copy( const QString& source, const QString& target )
{
    std::error_code error;
    std::filesystem::copy_file( asPath( source ), asPath( target ),
                                std::filesystem::copy_options::overwrite_existing, error );
    return !error;
}

} // namespace detail

// Relaunches this process beside a settings file of its own. Returns the exit
// code to end main() with when this process was the launcher, and nothing when
// this is the process that runs the test cases.
//
// Call it as the first thing in main(): once a setting has been read the
// settings file is already open, and once QApplication is up there is a GUI in
// the launcher that no one needs.
inline std::optional<int> relaunchWithOwnSettings( int argc, char* argv[] )
{
    if ( qEnvironmentVariableIsSet( RelaunchedMarker ) ) {
        return std::nullopt;
    }

    // Only to learn where this executable is: that directory is what decides
    // where the settings are written, so that is what has to move.
    QCoreApplication launcher( argc, argv );
    const QFileInfo executable{ QCoreApplication::applicationFilePath() };
    const QDir binaryDir = executable.absoluteDir();

    // Beside the built binaries, not among them: the same file system, so the
    // links below cost nothing, and a directory an interrupted run leaves
    // behind is in the build directory, where it is seen and thrown away, and
    // not in the user's temporary directory.
    const QString scratchRoot
        = QDir::cleanPath( binaryDir.absoluteFilePath( QStringLiteral( "../test_settings" ) ) );
    QDir{}.mkpath( scratchRoot );

    std::optional<QTemporaryDir> scratch;
    scratch.emplace( scratchRoot + QStringLiteral( "/XXXXXX" ) );
    if ( !scratch->isValid() ) {
        // A read-only build directory, say. The temporary directory may be on
        // another file system, and the executable is then copied, not linked.
        scratch.emplace( QDir::tempPath() + QStringLiteral( "/logsquirl-test-settings-XXXXXX" ) );
    }
    if ( !scratch->isValid() ) {
        std::fprintf( stderr, "%s: no scratch directory for a settings file of its own: %s\n",
                      qPrintable( executable.fileName() ), qPrintable( scratch->errorString() ) );
        return 1;
    }

    const QString isolatedExecutable = scratch->filePath( executable.fileName() );
    if ( !detail::hardLink( executable.absoluteFilePath(), isolatedExecutable )
         && !detail::copy( executable.absoluteFilePath(), isolatedExecutable ) ) {
        std::fprintf( stderr, "%s: could not put the executable beside its own settings in %s\n",
                      qPrintable( executable.fileName() ), qPrintable( scratch->path() ) );
        return 1;
    }

    // What a test runs or loads from beside its binary comes along: the helper
    // that writes a Log File, the command line tool a benchmark measures, the
    // libraries Windows looks for there. A link or nothing -- no run pays for a
    // copy of everything that was built. The settings files are what this is
    // about and are the one thing left behind.
    for ( const auto& neighbour :
          binaryDir.entryInfoList( QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden ) ) {
        if ( neighbour.fileName() == executable.fileName()
             || neighbour.suffix() == QLatin1String( "conf" ) ) {
            continue;
        }
        detail::hardLink( neighbour.absoluteFilePath(), scratch->filePath( neighbour.fileName() ) );
    }

    QStringList arguments;
    for ( int argument = 1; argument < argc; ++argument ) {
        arguments << QString::fromLocal8Bit( argv[ argument ] );
    }

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert( QString::fromLatin1( RelaunchedMarker ), QStringLiteral( "1" ) );

    QProcess isolated;
    isolated.setProgram( isolatedExecutable );
    isolated.setArguments( arguments );
    isolated.setProcessEnvironment( environment );
    // The test case's output is the launcher's output: ctest reads it as it
    // always did, and nothing is held back until the case is over.
    isolated.setProcessChannelMode( QProcess::ForwardedChannels );
    isolated.setWorkingDirectory( QDir::currentPath() );
    isolated.start();

    if ( !isolated.waitForStarted( -1 ) ) {
        std::fprintf( stderr, "%s: the isolated test process did not start: %s\n",
                      qPrintable( executable.fileName() ), qPrintable( isolated.errorString() ) );
        return 1;
    }
    isolated.waitForFinished( -1 );

    const int exitCode = isolated.exitCode();
    const bool crashed = isolated.exitStatus() == QProcess::CrashExit;

    // Before dying the same death below, so that the scratch directory goes
    // even then.
    scratch->remove();

#ifndef Q_OS_WIN
    if ( crashed && exitCode > 0 && exitCode <= 31 ) {
        // A test case that crashed crashes the launcher the same way, so ctest
        // and the JUnit report name what happened instead of an exit code
        // nobody can read. Qt promises exitCode() only for a process that ended
        // normally, which is why anything but a signal number is reported as
        // the plain failure below.
        ::signal( exitCode, SIG_DFL );
        ::raise( exitCode );
    }
#endif

    if ( crashed ) {
        std::fprintf( stderr, "%s: the isolated test process died (%d)\n",
                      qPrintable( executable.fileName() ), exitCode );
        return 1;
    }

    return exitCode;
}

} // namespace isolated_settings

#endif
