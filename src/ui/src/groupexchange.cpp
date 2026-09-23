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

#include "groupexchange.h"

#include <QFileInfo>
#include <QSettings>

namespace logsquirl::groupexchange {

namespace {

QString& lastExportFolder()
{
    static QString folder;
    return folder;
}

template <typename Collection>
bool writeCollection( const QString& file, const Collection& collection )
{
    QSettings settings{ file, QSettings::IniFormat };
    // Start from an empty file: an existing one may hold other groups.
    settings.clear();
    collection.saveToStorage( settings );
    settings.sync();
    return settings.status() == QSettings::NoError;
}

} // namespace

QString suggestedFileName( const QString& groupName, GroupKind kind )
{
    QString name = groupName;
    for ( QChar& character : name ) {
        if ( QStringLiteral( "/\\:*?\"<>|" ).contains( character ) ) {
            character = QLatin1Char( '_' );
        }
    }
    return name
           + ( kind == GroupKind::Filter ? QStringLiteral( "_filter.conf" )
                                         : QStringLiteral( "_highlighter.conf" ) );
}

bool writeGroup( const QString& file, const PredefinedFilterSet& group )
{
    PredefinedFiltersCollection collection;
    collection.setFilterSets( { group } );
    return writeCollection( file, collection );
}

bool writeGroup( const QString& file, const HighlighterSet& group )
{
    // A fresh collection has no Color Labels and no active sets.
    HighlighterSetCollection collection;
    collection.setHighlighterSets( { group } );
    return writeCollection( file, collection );
}

QString exportFolder()
{
    return lastExportFolder();
}

void rememberExportFolder( const QString& file )
{
    lastExportFolder() = QFileInfo( file ).absolutePath();
}

QString withConfSuffix( const QString& file )
{
    return file.endsWith( QStringLiteral( ".conf" ) ) ? file : file + QStringLiteral( ".conf" );
}

} // namespace logsquirl::groupexchange
