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

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include "configuration.h"

class QProcess;

namespace logsquirl::plugins {
class StreamWriter;
class StdinPump;
} // namespace logsquirl::plugins

// How what a Command Source was fed from came to its end.
struct CommandEnd {
    enum class Kind {
        // The command's shell exited on its own, with exitCode.
        Exited,
        // The command's shell was killed by a signal or crashed.
        Stopped,
        // Standard input was closed by its writer.
        InputClosed,
    };

    Kind kind = Kind::Exited;
    int exitCode = 0;

    // The exit code a shell gives a command it could not find: 127 on
    // macOS/Linux, 9009 from cmd.exe on Windows.
    bool commandNotFound() const;
};

// How a command line is run through the user's shell (#575): on macOS and
// Linux as `$SHELL -l -c "<command line>"` -- a login shell, so that an
// application started from the Finder or the Dock finds what the user's PATH
// finds -- with /bin/sh when `shell` is empty; on Windows as
// `cmd.exe /d /s /c "<command line>"`, given as native arguments so that
// nothing is quoted again.
struct ShellInvocation {
    QString program;
    QStringList arguments;
    QString nativeArguments;
};
ShellInvocation shellInvocation( const QString& commandLine, const QString& shell );

// The title of a command's tab: its command line, elided in the middle to 40
// characters.
QString commandTabTitle( const QString& commandLine );

// The Command Source of a tab: what feeds the Transient Log File the tab
// shows, and the owner of that file (#575). It is one of
//  - a command run through the user's shell (startCommand()), whose output
//    it writes to a spool file for as long as the command runs;
//  - standard input (readStandardInput()), copied into a spool file until
//    its writer closes it;
//  - a spool file somebody else writes and hands over (adoptSpoolFile()),
//    such as another LogSquirl process that reads standard input for this
//    one (#623).
//
// The window keeps one per tab, by the tab's path -- the spool file -- and
// destroys it with the tab. Destroying it stops what feeds the file and
// removes the file:
//  - a command runs in a session and process group of its own on macOS and
//    Linux; the group -- its shell with the rest -- gets SIGTERM, and SIGKILL
//    2 s later if any of it is still there. That wait happens off the calling
//    thread: stopping never blocks it. A command still running when the
//    application exits gets SIGKILL then.
//  - On Windows a command runs in a Job Object that kills every process of
//    the job when it is closed, so that no child process escapes.
// Once the command's shell has ended on its own, whatever it left running in
// its group is stopped the same way: one tab is one run.
class CommandSource : public QObject {
    Q_OBJECT

public:
    enum class Kind { Command, StandardInput, SpoolFile };

    // Runs the command line through the user's shell in its working folder
    // (the home folder when empty), its output -- and its standard error, if
    // asked -- written to a new spool file. Null, with why in `error`, when
    // the working folder does not exist, the spool file cannot be created or
    // the shell cannot be started. A command the shell does not know is no
    // failure here: it ends with commandNotFound().
    static std::unique_ptr<CommandSource> startCommand( const RecentCommand& command,
                                                        QString* error );

    // Copies what arrives on `fd` into a new spool file until its writer
    // closes it. Null, with why in `error`, when the spool file cannot be
    // created.
    static std::unique_ptr<CommandSource> readStandardInput( int fd, QString* error );

    // Takes over a spool file another process writes, and tells that process
    // so with the marker of its adoption (see spoolAdoptionMarker()): this
    // object removes the marker, then the file, with its folder when that is
    // left empty, when it is destroyed. It never ends by itself. A file that
    // cannot be removed then -- on Windows, while the other process still has
    // it open -- is removed again when the application exits (#623).
    static std::unique_ptr<CommandSource> adoptSpoolFile( const QString& path );

    // Removes the spool files that could not be removed with their Command
    // Source -- on Windows, while a tab or another process still has one open.
    // Runs by itself when the application exits.
    static void removeLeftoverSpoolFiles();

    ~CommandSource() override;

    CommandSource( const CommandSource& ) = delete;
    CommandSource& operator=( const CommandSource& ) = delete;

    Kind kind() const
    {
        return kind_;
    }

    // The file the tab reads.
    QString spoolPath() const
    {
        return spoolPath_;
    }

    // What was run; empty for anything but a command.
    const RecentCommand& command() const
    {
        return command_;
    }

    // Whether ended() was emitted.
    bool hasEnded() const
    {
        return ended_;
    }

    // Stops what feeds the spool file, as destroying this object does, but
    // keeps the file. Nothing is emitted after it.
    void stop();

    // The title a command's tab has once it ended: " [exit N]" or
    // " [stopped]" after `title`. Standard input's tab keeps its title, and
    // gets no tooltip line or message from these.
    static QString endedTitle( const QString& title, const CommandEnd& end );
    // The line its tooltip gets then.
    static QString endedToolTip( const CommandEnd& end );
    // What the status bar says then.
    static QString endedMessage( const QString& title, const CommandEnd& end );

Q_SIGNALS:
    // Emitted once, on this object's thread, when the command ended or
    // standard input was closed. Everything it wrote is in the spool file
    // by then.
    void ended( CommandEnd end );

private:
    explicit CommandSource( Kind kind );

    QString startProcess();
    void readOutput();
    void finish( const CommandEnd& end );
    void stopProcess();

    Kind kind_;
    RecentCommand command_;
    QString spoolPath_;
    bool ended_ = false;

    std::unique_ptr<logsquirl::plugins::StreamWriter> writer_;
    std::unique_ptr<logsquirl::plugins::StdinPump> pump_;
    QProcess* process_ = nullptr;
    // The command's process group (its shell's process id); 0 once it
    // needs no stopping.
    qint64 processGroup_ = 0;
#ifdef Q_OS_WIN
    // The Job Object every process of the command runs in.
    void* job_ = nullptr;
    // The process information CreateProcess filled in, only while start()
    // runs.
    void* creatingProcess_ = nullptr;
#endif
};
