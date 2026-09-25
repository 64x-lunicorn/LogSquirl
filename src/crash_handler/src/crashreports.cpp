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
