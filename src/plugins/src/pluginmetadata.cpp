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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtGlobal>

#include <algorithm>

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

// An optional list of the files the plugin ships, as paths relative to the
// plugin directory that stay inside it: the Log Format files of "formats"
// (#793), the Filter Group files of "predefined_filters" and the Highlighter
// Set files of "highlighter_sets" (#865).
std::expected<QStringList, QString> parseFiles( const QJsonObject& obj, const QString& key )
{
    if ( !obj.contains( key ) ) {
        return QStringList{};
    }
    if ( !obj[ key ].isArray() ) {
        return std::unexpected( QString( "'%1' is not a list" ).arg( key ) );
    }

    QStringList files;
    for ( const auto& entry : obj[ key ].toArray() ) {
        if ( !entry.isString() || entry.toString().isEmpty() ) {
            return std::unexpected( QString( "'%1' entry is not a path" ).arg( key ) );
        }
        const auto path = entry.toString();
        // A leading slash counts as leaving it on every platform: on Windows
        // Qt holds such a path for relative, the drive being left out.
        const auto clean = QDir::cleanPath( QDir::fromNativeSeparators( path ) );
        if ( QDir::isAbsolutePath( path ) || clean.startsWith( QLatin1Char( '/' ) )
             || clean == QLatin1String( ".." ) || clean.startsWith( QLatin1String( "../" ) ) ) {
            return std::unexpected(
                QString( "'%1' entry '%2' leaves the plugin directory" ).arg( key, path ) );
        }
        files.append( path );
    }
    return files;
}

// The paths of files relative to the plugin directory, resolved against it;
// none without a directory.
QStringList pathsIn( const QString& directory, const QStringList& files )
{
    if ( directory.isEmpty() ) {
        return {};
    }
    QStringList paths;
    const QDir dir( directory );
    for ( const auto& file : files ) {
        paths.append( dir.filePath( file ) );
    }
    return paths;
}

// The optional "qt_version": the Qt the plugin was built against, as
// major.minor (#862).
std::expected<std::optional<QVersionNumber>, QString> parseQtVersion( const QJsonObject& obj )
{
    if ( !obj.contains( "qt_version" ) ) {
        return std::nullopt;
    }
    const auto invalid
        = std::unexpected( QStringLiteral( "'qt_version' is not a Qt version as major.minor" ) );
    const auto value = obj[ "qt_version" ];
    if ( !value.isString() ) {
        return invalid;
    }
    static const QRegularExpression majorMinor( QStringLiteral( R"(\A(\d+)\.(\d+)\z)" ) );
    const auto match = majorMinor.match( value.toString() );
    if ( !match.hasMatch() ) {
        return invalid;
    }
    // A number too big for an int is no Qt version either, not a 0.
    bool majorOk = false;
    bool minorOk = false;
    const auto major = match.captured( 1 ).toInt( &majorOk );
    const auto minor = match.captured( 2 ).toInt( &minorOk );
    if ( !majorOk || !minorOk ) {
        return invalid;
    }
    return QVersionNumber( major, minor );
}

} // namespace

bool PluginMetadata::isValidId( const QString& id )
{
    // ASCII only: the id becomes a folder name on every platform and a key
    // in the configuration, where nothing else is wanted (#819).
    const auto plain = []( QChar c ) {
        const auto u = c.unicode();
        return ( u >= 'a' && u <= 'z' ) || ( u >= 'A' && u <= 'Z' ) || ( u >= '0' && u <= '9' )
               || u == '.' || u == '-' || u == '_';
    };
    return !id.isEmpty() && !id.startsWith( QLatin1Char( '.' ) )
           && std::all_of( id.cbegin(), id.cend(), plain );
}

std::expected<PluginMetadata, QString> PluginMetadata::fromJsonFile( const QString& jsonPath )
{
    QFile file( jsonPath );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        return std::unexpected( QString( "Cannot open plugin manifest: %1" ).arg( jsonPath ) );
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
            QString( "%1: JSON parse error: %2" ).arg( context, parseError.errorString() ) );
    }

    if ( !doc.isObject() ) {
        return std::unexpected( QString( "%1: root is not a JSON object" ).arg( context ) );
    }

    const auto obj = doc.object();

    // Validate required string fields
    static constexpr const char* requiredStrings[] = { "id", "name", "version", "type", "library" };

    for ( const auto* field : requiredStrings ) {
        if ( !obj.contains( field ) || !obj[ field ].isString() ) {
            return std::unexpected(
                QString( "%1: missing or invalid required field '%2'" ).arg( context, field ) );
        }
    }

    // The id names the install folder: a plain name only (#819).
    if ( const auto id = obj[ "id" ].toString(); !isValidId( id ) ) {
        return std::unexpected(
            QString( "%1: 'id' '%2' is not a plain plugin name" ).arg( context, id ) );
    }

    // Validate api_version
    if ( !obj.contains( "api_version" ) || !obj[ "api_version" ].isDouble() ) {
        return std::unexpected( QString( "%1: missing or invalid 'api_version'" ).arg( context ) );
    }

    const int apiVer = obj[ "api_version" ].toInt();
    if ( apiVer != LOGSQUIRL_PLUGIN_API_VERSION ) {
        return std::unexpected( QString( "%1: incompatible api_version %2 (host supports %3)" )
                                    .arg( context )
                                    .arg( apiVer )
                                    .arg( LOGSQUIRL_PLUGIN_API_VERSION ) );
    }

    // Parse type
    auto typeResult = parsePluginType( obj[ "type" ].toString() );
    if ( !typeResult.has_value() ) {
        return std::unexpected( QString( "%1: %2" ).arg( context, typeResult.error() ) );
    }

    auto formatsResult = parseFiles( obj, QStringLiteral( "formats" ) );
    if ( !formatsResult.has_value() ) {
        return std::unexpected( QString( "%1: %2" ).arg( context, formatsResult.error() ) );
    }

    auto filtersResult = parseFiles( obj, QStringLiteral( "predefined_filters" ) );
    if ( !filtersResult.has_value() ) {
        return std::unexpected( QString( "%1: %2" ).arg( context, filtersResult.error() ) );
    }

    auto highlightersResult = parseFiles( obj, QStringLiteral( "highlighter_sets" ) );
    if ( !highlightersResult.has_value() ) {
        return std::unexpected( QString( "%1: %2" ).arg( context, highlightersResult.error() ) );
    }

    auto qtVersionResult = parseQtVersion( obj );
    if ( !qtVersionResult.has_value() ) {
        return std::unexpected( QString( "%1: %2" ).arg( context, qtVersionResult.error() ) );
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
    meta.formats_ = std::move( formatsResult.value() );
    meta.predefinedFilters_ = std::move( filtersResult.value() );
    meta.highlighterSets_ = std::move( highlightersResult.value() );
    meta.qtVersion_ = std::move( qtVersionResult.value() );
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

QString PluginMetadata::qtRefusal( const QVersionNumber& hostQt ) const
{
    if ( !qtVersion_.has_value() ) {
        return {};
    }
    const auto& pluginQt = *qtVersion_;
    if ( pluginQt.majorVersion() == hostQt.majorVersion()
         && pluginQt.minorVersion() <= hostQt.minorVersion() ) {
        return {};
    }
    // Shown on the plugin's card in Plugin Management, so translated.
    return QCoreApplication::translate( "PluginMetadata",
                                        "Plugin '%1' is built against Qt %2, but LogSquirl runs on "
                                        "Qt %3, which serves plugins built against Qt %4.0 to "
                                        "%4.%5 only" )
        .arg( id_, pluginQt.toString(), hostQt.toString() )
        .arg( hostQt.majorVersion() )
        .arg( hostQt.minorVersion() );
}

QString PluginMetadata::qtRefusal() const
{
    return qtRefusal( QVersionNumber::fromString( QString::fromLatin1( qVersion() ) ) );
}

QStringList PluginMetadata::formatPaths() const
{
    return pathsIn( directory_, formats_ );
}

QStringList PluginMetadata::predefinedFilterPaths() const
{
    return pathsIn( directory_, predefinedFilters_ );
}

QStringList PluginMetadata::highlighterSetPaths() const
{
    return pathsIn( directory_, highlighterSets_ );
}

} // namespace logsquirl::plugins
