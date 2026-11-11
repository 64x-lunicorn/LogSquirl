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

#include "crashreports.h"

#include <QChar>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <pthread.h>
#endif

bool handlePendingCrashReports( const std::vector<PendingCrashReport>& reports,
                                const CrashReportActions& actions )
{
    bool needWaitForUpload = false;

    for ( std::size_t index = 0; index < reports.size(); ++index ) {
        const auto& report = reports[ index ];
        if ( report.uploaded ) {
            continue;
        }

        QString formattedReport = report.dumpFile;
        formattedReport.append( QChar::LineFeed ).append( actions.symbolize( report.dumpFile ) );

        if ( actions.askUser( formattedReport, report.dumpFile ) == CrashReportChoice::Send ) {
            actions.send( index );
            needWaitForUpload = true;
        }
        else {
            actions.discard( index );
        }

        actions.offerIssue( report.id );
    }

    return needWaitForUpload;
}

QString crashDumpDirectory( const QString& dataDirectory )
{
    return dataDirectory + QStringLiteral( "/logsquirl_dump" );
}

QString crashpadToolPath( const QString& applicationDirectory, const QString& tool )
{
#ifdef Q_OS_WIN
    return applicationDirectory + QChar( '/' ) + tool + QStringLiteral( ".exe" );
#else
    return applicationDirectory + QChar( '/' ) + tool;
#endif
}

QString formatSentryMessage( const char* message, va_list args )
{
    return QString::vasprintf( message, args );
}

#ifdef Q_OS_MACOS
namespace {
// Runs in the child between fork and exec: a Mach call, no allocation. The
// exception types are those Crashpad's client takes; without a port of the
// task's own, the kernel hands them to the system's crash reporter.
void dropInheritedCrashExceptionPort()
{
    task_set_exception_ports( mach_task_self(), EXC_MASK_CRASH | EXC_MASK_RESOURCE | EXC_MASK_GUARD,
                              MACH_PORT_NULL, EXCEPTION_DEFAULT, THREAD_STATE_NONE );
}
} // namespace
#endif

void detachChildProcessesFromCrashHandler()
{
#ifdef Q_OS_MACOS
    // pthread_atfork has no way back, so it is registered once.
    static const bool registered
        = pthread_atfork( nullptr, nullptr, &dropInheritedCrashExceptionPort ) == 0;
    Q_UNUSED( registered );
#endif
}
