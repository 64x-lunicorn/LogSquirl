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

#include "logformatregistry.h"
#include "logformatparser.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QStandardPaths>

void LogFormatRegistry::loadFromDirectory( const QString& directoryPath )
{
    QDir dir( directoryPath );
    if ( !dir.exists() ) {
        return;
    }

    const auto jsonFiles = dir.entryList( { "*.json" }, QDir::Files | QDir::Readable );
    for ( const auto& fileName : jsonFiles ) {
        const auto filePath = dir.filePath( fileName );
        auto formats = LogFormatParser::parseFile( filePath );
        for ( auto& format : formats ) {
            addFormat( std::move( format ) );
        }
    }
}

void LogFormatRegistry::addFormat( LogFormatDefinition format )
{
    const auto name = format.name();
    formats_.insert( name, std::move( format ) );
}

const LogFormatDefinition* LogFormatRegistry::formatByName( const QString& name ) const
{
    auto it = formats_.find( name );
    if ( it != formats_.end() ) {
        return &it.value();
    }
    return nullptr;
}

int LogFormatRegistry::formatCount() const
{
    return static_cast<int>( formats_.size() );
}

QStringList LogFormatRegistry::formatNames() const
{
    return QStringList( formats_.keys() );
}

void LogFormatRegistry::loadBuiltinFormats()
{
    QDir resourceDir( ":/formats" );
    const auto jsonFiles = resourceDir.entryList( { "*.json" }, QDir::Files );
    for ( const auto& fileName : jsonFiles ) {
        const auto filePath = resourceDir.filePath( fileName );
        QFile file( filePath );
        if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
            continue;
        }
        const auto data = file.readAll();
        file.close();

        auto formats = LogFormatParser::parseJsonString( data.constData() );
        for ( auto& format : formats ) {
            addFormat( std::move( format ) );
        }
    }
}

void LogFormatRegistry::loadUserFormats()
{
    const auto dataDir
        = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    if ( dataDir.isEmpty() ) {
        return;
    }
    const auto formatsDir = dataDir + "/formats";
    loadFromDirectory( formatsDir );
}
