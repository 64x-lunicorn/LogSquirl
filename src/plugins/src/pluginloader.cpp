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

#include "pluginloader.h"

#include "log.h"

#include <QLibrary>

#include <utility>

namespace logsquirl::plugins {

// ── PluginHandle ────────────────────────────────────────────────────────────

PluginHandle::PluginHandle( PluginMetadata meta, std::unique_ptr<QLibrary> lib,
                            LogSquirlPluginGetInfoFn getInfoFn,
                            LogSquirlPluginInitFn initFn,
                            LogSquirlPluginShutdownFn shutdownFn,
                            LogSquirlPluginConfigureFn configureFn,
                            LogSquirlConverterGetExtsFn converterGetExtsFn,
                            LogSquirlConverterConvertFn converterConvertFn )
    : metadata_( std::move( meta ) )
    , library_( std::move( lib ) )
    , getInfoFn_( getInfoFn )
    , initFn_( initFn )
    , shutdownFn_( shutdownFn )
    , configureFn_( configureFn )
    , converterGetExtsFn_( converterGetExtsFn )
    , converterConvertFn_( converterConvertFn )
{
}

PluginHandle::~PluginHandle()
{
    if ( initialised_ ) {
        shutdown();
    }
}

PluginHandle::PluginHandle( PluginHandle&& other ) noexcept
    : metadata_( std::move( other.metadata_ ) )
    , library_( std::move( other.library_ ) )
    , getInfoFn_( other.getInfoFn_ )
    , initFn_( other.initFn_ )
    , shutdownFn_( other.shutdownFn_ )
    , configureFn_( other.configureFn_ )
    , converterGetExtsFn_( other.converterGetExtsFn_ )
    , converterConvertFn_( other.converterConvertFn_ )
    , initialised_( other.initialised_ )
{
    other.initialised_ = false;
    other.getInfoFn_ = nullptr;
    other.initFn_ = nullptr;
    other.shutdownFn_ = nullptr;
    other.configureFn_ = nullptr;
    other.converterGetExtsFn_ = nullptr;
    other.converterConvertFn_ = nullptr;
}

PluginHandle& PluginHandle::operator=( PluginHandle&& other ) noexcept
{
    if ( this != &other ) {
        if ( initialised_ ) {
            shutdown();
        }
        metadata_ = std::move( other.metadata_ );
        library_ = std::move( other.library_ );
        getInfoFn_ = other.getInfoFn_;
        initFn_ = other.initFn_;
        shutdownFn_ = other.shutdownFn_;
        configureFn_ = other.configureFn_;
        converterGetExtsFn_ = other.converterGetExtsFn_;
        converterConvertFn_ = other.converterConvertFn_;
        initialised_ = other.initialised_;

        other.initialised_ = false;
        other.getInfoFn_ = nullptr;
        other.initFn_ = nullptr;
        other.shutdownFn_ = nullptr;
        other.configureFn_ = nullptr;
        other.converterGetExtsFn_ = nullptr;
        other.converterConvertFn_ = nullptr;
    }
    return *this;
}

QString PluginHandle::init( const LogSquirlHostApi* api, void* handle )
{
    if ( initialised_ ) {
        return QStringLiteral( "Plugin already initialised" );
    }
    if ( !initFn_ ) {
        return QStringLiteral( "No init entry point" );
    }

    LOG_INFO << "Initialising plugin: " << metadata_.id();
    const int rc = initFn_( api, handle );
    if ( rc != 0 ) {
        return QString( "Plugin init returned error code %1" ).arg( rc );
    }

    initialised_ = true;
    return {};
}

void PluginHandle::shutdown()
{
    if ( !initialised_ ) {
        return;
    }
    LOG_INFO << "Shutting down plugin: " << metadata_.id();
    if ( shutdownFn_ ) {
        shutdownFn_();
    }
    initialised_ = false;
}

void PluginHandle::configure( void* parentWidget )
{
    if ( configureFn_ ) {
        configureFn_( parentWidget );
    }
}

bool PluginHandle::isConverter() const
{
    return metadata_.type() == LOGSQUIRL_PLUGIN_CONVERTER
           && converterGetExtsFn_ != nullptr
           && converterConvertFn_ != nullptr;
}

QString PluginHandle::converterExtensions() const
{
    if ( converterGetExtsFn_ ) {
        return QString::fromUtf8( converterGetExtsFn_() );
    }
    return {};
}

int PluginHandle::convert( const QString& inputPath, const QString& outputPath ) const
{
    if ( !converterConvertFn_ ) {
        return -1;
    }
    const auto inUtf8 = inputPath.toUtf8();
    const auto outUtf8 = outputPath.toUtf8();
    return converterConvertFn_( inUtf8.constData(), outUtf8.constData() );
}

PluginHandle PluginHandle::createScriptHandle( PluginMetadata meta )
{
    return PluginHandle( std::move( meta ), nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr );
}

// ── PluginLoader ────────────────────────────────────────────────────────────

namespace {

// Helper: resolve a symbol from QLibrary, returning nullptr on failure.
template <typename FnPtr>
FnPtr resolveSymbol( QLibrary& lib, const char* name )
{
    return reinterpret_cast<FnPtr>( lib.resolve( name ) );
}

} // namespace

std::expected<PluginHandle, QString> PluginLoader::load( const PluginMetadata& metadata )
{
    const auto libPath = metadata.libraryPath();
    if ( libPath.isEmpty() ) {
        return std::unexpected( QStringLiteral( "Plugin library path is empty" ) );
    }

    LOG_INFO << "Loading plugin library: " << libPath;

    auto library = std::make_unique<QLibrary>( libPath );
    if ( !library->load() ) {
        return std::unexpected(
            QString( "Failed to load library '%1': %2" )
                .arg( libPath, library->errorString() ) );
    }

    // Resolve required symbols
    auto getInfoFn = resolveSymbol<LogSquirlPluginGetInfoFn>(
        *library, LOGSQUIRL_PLUGIN_ENTRY_GET_INFO );
    if ( !getInfoFn ) {
        return std::unexpected(
            QString( "Plugin '%1' missing symbol: %2" )
                .arg( metadata.id(), LOGSQUIRL_PLUGIN_ENTRY_GET_INFO ) );
    }

    auto initFn = resolveSymbol<LogSquirlPluginInitFn>(
        *library, LOGSQUIRL_PLUGIN_ENTRY_INIT );
    if ( !initFn ) {
        return std::unexpected(
            QString( "Plugin '%1' missing symbol: %2" )
                .arg( metadata.id(), LOGSQUIRL_PLUGIN_ENTRY_INIT ) );
    }

    auto shutdownFn = resolveSymbol<LogSquirlPluginShutdownFn>(
        *library, LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN );
    if ( !shutdownFn ) {
        return std::unexpected(
            QString( "Plugin '%1' missing symbol: %2" )
                .arg( metadata.id(), LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN ) );
    }

    // Optional symbols
    auto configureFn = resolveSymbol<LogSquirlPluginConfigureFn>(
        *library, LOGSQUIRL_PLUGIN_ENTRY_CONFIGURE );

    auto converterGetExtsFn = resolveSymbol<LogSquirlConverterGetExtsFn>(
        *library, LOGSQUIRL_CONVERTER_ENTRY_GET_EXTS );

    auto converterConvertFn = resolveSymbol<LogSquirlConverterConvertFn>(
        *library, LOGSQUIRL_CONVERTER_ENTRY_CONVERT );

    // Validate the plugin's own info against the manifest
    const auto* info = getInfoFn();
    if ( !info ) {
        return std::unexpected(
            QString( "Plugin '%1' get_info returned null" ).arg( metadata.id() ) );
    }

    if ( info->api_version != LOGSQUIRL_PLUGIN_API_VERSION ) {
        return std::unexpected(
            QString( "Plugin '%1' reports api_version %2, host supports %3" )
                .arg( metadata.id() )
                .arg( info->api_version )
                .arg( LOGSQUIRL_PLUGIN_API_VERSION ) );
    }

    LOG_INFO << "Plugin loaded successfully: " << metadata.id()
             << " v" << metadata.version();

    return PluginHandle(
        metadata, std::move( library ),
        getInfoFn, initFn, shutdownFn, configureFn,
        converterGetExtsFn, converterConvertFn );
}

} // namespace logsquirl::plugins
