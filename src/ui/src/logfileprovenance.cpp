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

#include "logfileprovenance.h"

#include <QFileInfo>

#include "commandsource.h"

LogFileProvenance::LogFileProvenance() = default;
LogFileProvenance::~LogFileProvenance() = default;
LogFileProvenance::LogFileProvenance( LogFileProvenance&& ) noexcept = default;
LogFileProvenance& LogFileProvenance::operator=( LogFileProvenance&& ) noexcept = default;

LogFileProvenance LogFileProvenance::ordinary()
{
    return {};
}

LogFileProvenance LogFileProvenance::fromArchive( const ArchiveMember& archiveMember )
{
    LogFileProvenance provenance;
    provenance.origin = LogFileOrigin::fromArchive( archiveMember );
    return provenance;
}

LogFileProvenance LogFileProvenance::transient( const QString& title, const QString& titleToolTip )
{
    LogFileProvenance provenance;
    provenance.origin = LogFileOrigin::transient();
    provenance.openingTitle = title;
    provenance.toolTip = titleToolTip;
    return provenance;
}

LogFileProvenance LogFileProvenance::conversionOf( const QString& path,
                                                   const LogFileOrigin& source )
{
    LogFileProvenance provenance;
    provenance.origin = LogFileOrigin::conversionOf( path, source );
    return provenance;
}

LogFileProvenance LogFileProvenance::commandOutput( std::unique_ptr<CommandSource> source,
                                                    const QString& title,
                                                    const QString& titleToolTip )
{
    auto provenance = transient( title, titleToolTip );
    provenance.commandSource = std::move( source );
    return provenance;
}

QString LogFileProvenance::shownTitle( const QString& fileName ) const
{
    return openingTitle.isEmpty() ? QFileInfo( fileName ).fileName() : openingTitle;
}
