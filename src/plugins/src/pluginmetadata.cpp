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

#include "pluginmetadata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace logsquirl::plugins {

namespace {

// Map the "type" string from plugin.json to our enum
std::expected<LogSquirlPluginType, QString> parsePluginType( const QString& typeStr )
{
    if ( typeStr == "datasource" ) {
        return LOGSQUIRL_PLUGIN_DATASOURCE;
    }
    if ( typeStr == "converter" ) {
        return LOGSQUIRL_PLUGIN_CONVERTER;
    }
    if ( typeStr == "ui" ) {
        return LOGSQUIRL_PLUGIN_UI;
    }
    return std::unexpected( QString( "Unknown plugin type: '%1'" ).arg( typeStr ) );
}

} // namespace

std::expected<PluginMetadata, QString> PluginMetadata::fromJsonFile( const QString& jsonPath )
{
    QFile file( jsonPath );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        return std::unexpected(
            QString( "Cannot open plugin manifest: %1" ).arg( jsonPath ) );
    }

    auto result = fromJson( file.readAll(), jsonPath );
    if ( result.has_value() ) {
        result->directory_ = QFileInfo( jsonPath ).absolutePath();
    }
    return result;
}

std::expected<PluginMetadata, QString> PluginMetadata::fromJson( const QByteArray& json,
                                                                  const QString& context )
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson( json, &parseError );
    if ( doc.isNull() ) {
        return std::unexpected(
            QString( "%1: JSON parse error: %2" )
                .arg( context, parseError.errorString() ) );
    }

    if ( !doc.isObject() ) {
        return std::unexpected( QString( "%1: root is not a JSON object" ).arg( context ) );
    }

    const auto obj = doc.object();

    // Validate required string fields
    static constexpr const char* requiredStrings[]
        = { "id", "name", "version", "type", "library" };

    for ( const auto* field : requiredStrings ) {
        if ( !obj.contains( field ) || !obj[ field ].isString() ) {
            return std::unexpected(
                QString( "%1: missing or invalid required field '%2'" )
                    .arg( context, field ) );
        }
    }

    // Validate api_version
    if ( !obj.contains( "api_version" ) || !obj[ "api_version" ].isDouble() ) {
        return std::unexpected(
            QString( "%1: missing or invalid 'api_version'" ).arg( context ) );
    }

    const int apiVer = obj[ "api_version" ].toInt();
    if ( apiVer != LOGSQUIRL_PLUGIN_API_VERSION ) {
        return std::unexpected(
            QString( "%1: incompatible api_version %2 (host supports %3)" )
                .arg( context )
                .arg( apiVer )
                .arg( LOGSQUIRL_PLUGIN_API_VERSION ) );
    }

    // Parse type
    auto typeResult = parsePluginType( obj[ "type" ].toString() );
    if ( !typeResult.has_value() ) {
        return std::unexpected(
            QString( "%1: %2" ).arg( context, typeResult.error() ) );
    }

    PluginMetadata meta;
    meta.id_ = obj[ "id" ].toString();
    meta.name_ = obj[ "name" ].toString();
    meta.version_ = obj[ "version" ].toString();
    meta.description_ = obj[ "description" ].toString();
    meta.author_ = obj[ "author" ].toString();
    meta.license_ = obj[ "license" ].toString();
    meta.library_ = obj[ "library" ].toString();
    meta.icon_ = obj[ "icon" ].toString();
    meta.type_ = typeResult.value();
    meta.apiVersion_ = apiVer;

    return meta;
}

QString PluginMetadata::libraryPath() const
{
    if ( directory_.isEmpty() || library_.isEmpty() ) {
        return {};
    }
    return QDir( directory_ ).filePath( library_ );
}

QString PluginMetadata::iconPath() const
{
    if ( directory_.isEmpty() || icon_.isEmpty() ) {
        return {};
    }
    return QDir( directory_ ).filePath( icon_ );
}

} // namespace logsquirl::plugins
