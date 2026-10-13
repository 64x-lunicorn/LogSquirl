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
#include "datalocation.h"
#include "log.h"
#include "logformatparser.h"

#include <QDir>
#include <QFile>

#include <algorithm>
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

namespace {

// The Log Formats of one of a plugin's files, or why none could be read.
std::expected<QVector<LogFormatDefinition>, QString> readPluginFormatFile( const QString& path )
{
    QFile file( path );
    if ( !file.exists() ) {
        return std::unexpected( QStringLiteral( "the file does not exist" ) );
    }
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        return std::unexpected( QStringLiteral( "the file cannot be read" ) );
    }
    auto formats = LogFormatParser::parseJsonText( file.readAll() );
    if ( formats.has_value() && formats->isEmpty() ) {
        return std::unexpected( QStringLiteral( "the file holds no Log Format" ) );
    }
    return formats;
}

} // namespace

LogFormatCatalog::LogFormatCatalog( QString userFormatsDirectory )
    : userFormatsDirectory_( std::move( userFormatsDirectory ) )
{
}

QString LogFormatCatalog::defaultUserFormatsDirectory()
{
    const auto dataDir = DataLocation::current().dataDirectory();
    if ( dataDir.isEmpty() ) {
        return {};
    }
    return dataDir + "/formats";
}

void LogFormatCatalog::setPluginFormats( std::vector<PluginFormats> plugins )
{
    pluginFormats_ = std::move( plugins );
    std::ranges::stable_sort( pluginFormats_, {}, &PluginFormats::id );
}

void LogFormatCatalog::rebuild()
{
    formats_.clear();
    loadBuiltinFormats();
    loadPluginFormats();
    if ( !userFormatsDirectory_.isEmpty() ) {
        loadFromDirectory( userFormatsDirectory_ );
    }
}

void LogFormatCatalog::loadPluginFormats()
{
    pluginFormatWarnings_.clear();

    const auto warn = [ this ]( const QString& pluginId, const QString& message ) {
        LOG_WARNING << "Plugin " << pluginId << ": " << message;
        pluginFormatWarnings_.push_back( { pluginId, message } );
    };

    // Which plugin supplied each Log Format in this rebuild: between two
    // plugins naming the same one, the first by id wins. Within one plugin,
    // as within the user's folder, the later file wins.
    QHash<QString, QString> suppliedBy;
    for ( const auto& plugin : pluginFormats_ ) {
        for ( const auto& path : plugin.files ) {
            auto formats = readPluginFormatFile( path );
            if ( !formats.has_value() ) {
                warn( plugin.id, QStringLiteral( "Log Format file %1 skipped: %2" )
                                     .arg( QDir::toNativeSeparators( path ), formats.error() ) );
                continue;
            }
            for ( auto& format : *formats ) {
                const auto name = format.name();
                if ( const auto earlier = suppliedBy.find( name );
                     earlier != suppliedBy.end() && earlier.value() != plugin.id ) {
                    warn( plugin.id,
                          QStringLiteral( "Log Format '%1' is also shipped by plugin %2, "
                                          "whose Log Format is used" )
                              .arg( name, earlier.value() ) );
                    continue;
                }
                suppliedBy.insert( name, plugin.id );
                addFormat( std::move( format ), LogFormatSource::plugin( plugin.name ) );
            }
        }
    }
}

QStringList LogFormatCatalog::pluginFormatWarnings( const QString& pluginId ) const
{
    QStringList messages;
    for ( const auto& warning : pluginFormatWarnings_ ) {
        if ( warning.pluginId == pluginId ) {
            messages.append( warning.message );
        }
    }
    return messages;
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
            addFormat( std::move( format ), LogFormatSource::user() );
        }
    }
}

void LogFormatCatalog::addFormat( LogFormatDefinition format, LogFormatSource source )
{
    const auto name = format.name();
    format.setSource( std::move( source ) );
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
            addFormat( std::move( format ), LogFormatSource::builtIn() );
        }
    }
}
