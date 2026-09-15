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

#include "logformatcatalog.h"
#include "logformatparser.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <utility>

// The built-in Log Formats are compiled into this static library. Nothing
// else in an executable references that resource object, so a linker may
// drop it -- and with it every built-in Log Format -- unless it is
// initialized explicitly. Q_INIT_RESOURCE must be used outside a namespace.
static void initBuiltinFormatsResource()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE( formats );
        return true;
    }();
    Q_UNUSED( initialized );
}

LogFormatCatalog::LogFormatCatalog( QString userFormatsDirectory )
    : userFormatsDirectory_( std::move( userFormatsDirectory ) )
{
}

QString LogFormatCatalog::defaultUserFormatsDirectory()
{
    const auto dataDir = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    if ( dataDir.isEmpty() ) {
        return {};
    }
    return dataDir + "/formats";
}

void LogFormatCatalog::rebuild()
{
    formats_.clear();
    loadBuiltinFormats();
    if ( !userFormatsDirectory_.isEmpty() ) {
        loadFromDirectory( userFormatsDirectory_ );
    }
}

void LogFormatCatalog::loadFromDirectory( const QString& directoryPath )
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

void LogFormatCatalog::addFormat( LogFormatDefinition format )
{
    const auto name = format.name();
    formats_.insert( name, std::make_shared<const LogFormatDefinition>( std::move( format ) ) );
}

std::shared_ptr<const LogFormatDefinition>
LogFormatCatalog::formatByName( const QString& name ) const
{
    return formats_.value( name );
}

int LogFormatCatalog::formatCount() const
{
    return static_cast<int>( formats_.size() );
}

QStringList LogFormatCatalog::formatNames() const
{
    return QStringList( formats_.keys() );
}

void LogFormatCatalog::loadBuiltinFormats()
{
    initBuiltinFormatsResource();

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
