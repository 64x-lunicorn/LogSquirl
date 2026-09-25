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

#ifndef LOGSQUIRL_CRASHREPORTS_H
#define LOGSQUIRL_CRASHREPORTS_H

// What the crash handler decides about the crash reports of the last run,
// without Sentry or Crashpad: the Sentry build's CrashHandler hands in the
// actions that reach the crash database, the dialogs and the stack walker
// (#444). Built in every configuration, so it is tested without Sentry.

#include <QString>

#include <cstddef>
#include <functional>
#include <vector>

// A report in the crash database, as the handling needs it.
struct PendingCrashReport {
    QString id;       // The crash id, quoted in the issue the user may open.
    QString dumpFile; // The minidump the stack walker reads.
    bool uploaded = false;
};

enum class CrashReportChoice { Send, Discard };

// The effects of the handling. An index is that of the report in the list
// handed to handlePendingCrashReports.
struct CrashReportActions {
    std::function<QString( const QString& dumpFile )> symbolize;
    std::function<CrashReportChoice( const QString& formattedReport, const QString& dumpFile )>
        askUser;
    std::function<void( std::size_t index )> send;
    std::function<void( std::size_t index )> discard;
    std::function<void( const QString& crashId )> offerIssue;
};

// Shows each report not uploaded yet with its stack, sends or deletes it as
// the user chooses, and offers an issue for it either way. Returns whether a
// report was queued for upload, so the caller waits for the upload.
bool handlePendingCrashReports( const std::vector<PendingCrashReport>& reports,
                                const CrashReportActions& actions );

// The Crashpad database directory below the application's data directory.
QString crashDumpDirectory( const QString& dataDirectory );

// A Crashpad tool shipped next to the application binary.
QString crashpadToolPath( const QString& applicationDirectory, const QString& tool );

#endif
