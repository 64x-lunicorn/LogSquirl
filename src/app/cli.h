/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#ifndef LOGSQUIRL_CLI_H
#define LOGSQUIRL_CLI_H

#include <cstdint>
#include <iostream>
#include <vector>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>

#include "log.h"
#include "logsquirl_version.h"

struct CliParameters {
    bool new_session = false;
    bool load_session = false;
    bool multi_instance = false;
    bool log_to_file = false;
    bool follow_file = false;

    bool enable_logging = false;
    int log_level = 3;

    std::vector<QString> filenames;

    int window_width = 0;
    int window_height = 0;

    QString pattern;

    CliParameters( QCoreApplication& app, bool console = false )
    {
        QCommandLineParser parser;
        parser.setApplicationDescription( "LogSquirl log viewer" );
        const auto helpOption = parser.addHelpOption();
        // Not QCommandLineParser::addVersionOption(): that one is handled
        // inside process(), which prints applicationVersion() and exits before
        // the branch below can run. applicationVersion() is never set, so it
        // printed the application's name and nothing else -- except on macOS,
        // where Qt falls back to the bundle's Info.plist and hid it for the
        // desktop application alone (#368).
        const QCommandLineOption versionOption( QStringList() << "v"
                                                              << "version",
                                                "Displays version information." );
        parser.addOption( versionOption );

        const QCommandLineOption multiInstanceOption(
            QStringList() << "m"
                          << "multi",
            "allow multiple instance of logsquirl to run simultaneously (use together with -s)" );

        const QCommandLineOption loadSessionOption(
            QStringList() << "s"
                          << "load-session",
            "load the previous session (default when no file is passed)" );

        const QCommandLineOption newSessionOption(
            QStringList() << "n"
                          << "new-session",
            "do not load the previous session (default when a file is passed)" );

        const QCommandLineOption logToFileOption( QStringList() << "l"
                                                                << "log",
                                                  "save the log to a file" );

        const QCommandLineOption followOption( QStringList() << "f"
                                                             << "follow",
                                               "follow initial opened files" );

        const QCommandLineOption patternOption( QStringList() << "e"
                                                              << "pattern",
                                                "pattern to search for", "pattern" );

        const QCommandLineOption debugOption(
            QStringList() << "d"
                          << "debug",
            "output more debug (increase number for more verbosity)", "debug_level", "0" );

        parser.addOption( debugOption );

        if ( !console ) {
            const QCommandLineOption windowWidthOption( "window-width", "new window width",
                                                        "1024" );
            const QCommandLineOption windowHeightOption( "window-height", "new window height",
                                                         "768" );
            parser.addOption( multiInstanceOption );
            parser.addOption( loadSessionOption );
            parser.addOption( newSessionOption );
            parser.addOption( logToFileOption );
            parser.addOption( followOption );
            parser.addOption( windowWidthOption );
            parser.addOption( windowHeightOption );
        }
        else {
            parser.addOption( patternOption );
        }

        parser.process( app );

        if ( parser.isSet( helpOption ) ) {
            parser.showHelp( EXIT_SUCCESS );
        }

        if ( parser.isSet( versionOption ) ) {
            print_version();
            exit( EXIT_SUCCESS );
        }

        if ( parser.value( debugOption ).toInt() > 0 ) {
            enable_logging = true;
        }

        log_level += parser.value( debugOption ).toInt();

        if ( !console ) {
            if ( parser.isSet( multiInstanceOption ) ) {
                multi_instance = true;
            }

            if ( parser.isSet( loadSessionOption ) ) {
                load_session = true;
            }

            if ( parser.isSet( newSessionOption ) ) {
                new_session = true;
            }

            if ( parser.isSet( logToFileOption ) ) {
                log_to_file = true;
            }

            if ( parser.isSet( followOption ) ) {
                follow_file = true;
            }
        }
        else {

            if ( parser.isSet( patternOption ) ) {
                pattern = parser.value( patternOption );
            }
        }

        for ( const auto& file : parser.positionalArguments() ) {
            const auto fileInfo = QFileInfo( file );
            filenames.emplace_back( fileInfo.absoluteFilePath() );
        }
    }

    static void print_version()
    {
        // The name this binary was started as: logsquirl, or logsquirl_grep.
        std::cout << QCoreApplication::applicationName().toStdString() << " "
                  << logsquirlVersion().data() << "\n";
        std::cout << "Built " << logsquirlBuildDate().data() << " from " << logsquirlCommit().data()
                  << "(" << logsquirlGitVersion().data() << ")\n";

        std::cout
            << "Copyright (C) 2020 Nicolas Bonnefon, Anton Filimonov and other contributors\n";
        std::cout
            << "This is free software.  You may redistribute copies of it under the terms of\n";
        std::cout << "the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n";
        std::cout << "There is NO WARRANTY, to the extent permitted by law.\n";
    }
};

#endif