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

#ifdef Q_OS_WIN
#include <QDir>
#include <QFileInfo>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace logsquirl::plugins {

// ── PluginHandle ────────────────────────────────────────────────────────────

PluginHandle::PluginHandle( PluginMetadata meta, std::unique_ptr<QLibrary> lib,
                            LogSquirlPluginGetInfoFn getInfoFn, LogSquirlPluginInitFn initFn,
                            LogSquirlPluginInitExFn initExFn, LogSquirlPluginShutdownFn shutdownFn,
                            LogSquirlPluginConfigureFn configureFn,
                            LogSquirlConverterGetExtsFn converterGetExtsFn,
                            LogSquirlConverterConvertFn converterConvertFn )
    : metadata_( std::move( meta ) )
    , library_( std::move( lib ) )
    , getInfoFn_( getInfoFn )
    , initFn_( initFn )
    , initExFn_( initExFn )
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
    , initExFn_( other.initExFn_ )
    , shutdownFn_( other.shutdownFn_ )
    , configureFn_( other.configureFn_ )
    , converterGetExtsFn_( other.converterGetExtsFn_ )
    , converterConvertFn_( other.converterConvertFn_ )
    , initialised_( other.initialised_ )
{
    other.initialised_ = false;
    other.getInfoFn_ = nullptr;
    other.initFn_ = nullptr;
    other.initExFn_ = nullptr;
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
        initExFn_ = other.initExFn_;
        shutdownFn_ = other.shutdownFn_;
        configureFn_ = other.configureFn_;
        converterGetExtsFn_ = other.converterGetExtsFn_;
        converterConvertFn_ = other.converterConvertFn_;
        initialised_ = other.initialised_;

        other.initialised_ = false;
        other.getInfoFn_ = nullptr;
        other.initFn_ = nullptr;
        other.initExFn_ = nullptr;
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
    // A plugin that knows the table grows is told how far it goes here: the
    // whole table of this host (docs/adr/0017).
    const int rc
        = initExFn_ ? initExFn_( api, handle, sizeof( LogSquirlHostApi ) ) : initFn_( api, handle );
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
    return metadata_.type() == LOGSQUIRL_PLUGIN_CONVERTER && converterGetExtsFn_ != nullptr
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

// ── PluginLoader ────────────────────────────────────────────────────────────

namespace {

// Helper: resolve a symbol from QLibrary, returning nullptr on failure.
template <typename FnPtr>
FnPtr resolveSymbol( QLibrary& lib, const char* name )
{
    return reinterpret_cast<FnPtr>( lib.resolve( name ) );
}

#ifdef Q_OS_WIN
// QLibrary loads a plugin with LoadLibrary and its full path, and Windows then
// looks for the libraries the plugin imports in the application's directory,
// the system directories and PATH, but not in the plugin's own directory. A
// plugin that ships a library the application does not (Qt6SerialPort.dll next
// to the Serial Monitor) would fail to load with error 126.
//
// This loads the plugin first with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, which
// resolves its imports from its own directory as well, and holds it while
// QLibrary loads it: QLibrary then gets the module that is already loaded. The
// search path of the process stays as it is, unlike with SetDllDirectory, so a
// library another thread loads meanwhile (the delay-loaded Hyperscan DLL) is
// looked for as before. What the plugin delay-loads or loads itself later is
// looked for with the ordinary search order again.
//
// When this first load fails (the plugin needs a library found only through
// PATH, or the path is not a library), QLibrary loads the plugin as before and
// reports the error. Like QLibrary, it suppresses the system error dialogs, so
// a broken library next to a plugin is logged instead of stopping the start
// with a modal "Bad Image" box.
class LoadedFromOwnDirectory {
public:
    explicit LoadedFromOwnDirectory( const QString& libraryPath )
    {
        DWORD previousMode = 0;
        const auto modeSet
            = SetThreadErrorMode( SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previousMode );
        module_ = LoadLibraryExW(
            reinterpret_cast<const wchar_t*>(
                QDir::toNativeSeparators( QFileInfo( libraryPath ).absoluteFilePath() ).utf16() ),
            nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
        if ( modeSet ) {
            SetThreadErrorMode( previousMode, nullptr );
        }
    }

    ~LoadedFromOwnDirectory()
    {
        if ( module_ != nullptr ) {
            FreeLibrary( module_ );
        }
    }

    LoadedFromOwnDirectory( const LoadedFromOwnDirectory& ) = delete;
    LoadedFromOwnDirectory& operator=( const LoadedFromOwnDirectory& ) = delete;
    LoadedFromOwnDirectory( LoadedFromOwnDirectory&& ) = delete;
    LoadedFromOwnDirectory& operator=( LoadedFromOwnDirectory&& ) = delete;

private:
    HMODULE module_ = nullptr;
};
#endif

} // namespace

std::expected<PluginHandle, QString> PluginLoader::load( const PluginMetadata& metadata )
{
    const auto libPath = metadata.libraryPath();
    if ( libPath.isEmpty() ) {
        return std::unexpected( QStringLiteral( "Plugin library path is empty" ) );
    }

    LOG_INFO << "Loading plugin library: " << libPath;

    auto library = std::make_unique<QLibrary>( libPath );
#ifdef Q_OS_WIN
    // The libraries a plugin ships next to it are found (see above).
    const LoadedFromOwnDirectory loadedFromOwnDirectory( libPath );
#endif
    if ( !library->load() ) {
        return std::unexpected(
            QString( "Failed to load library '%1': %2" ).arg( libPath, library->errorString() ) );
    }

    // Resolve required symbols
    auto getInfoFn
        = resolveSymbol<LogSquirlPluginGetInfoFn>( *library, LOGSQUIRL_PLUGIN_ENTRY_GET_INFO );
    if ( !getInfoFn ) {
        return std::unexpected( QString( "Plugin '%1' missing symbol: %2" )
                                    .arg( metadata.id(), LOGSQUIRL_PLUGIN_ENTRY_GET_INFO ) );
    }

    auto initFn = resolveSymbol<LogSquirlPluginInitFn>( *library, LOGSQUIRL_PLUGIN_ENTRY_INIT );
    if ( !initFn ) {
        return std::unexpected( QString( "Plugin '%1' missing symbol: %2" )
                                    .arg( metadata.id(), LOGSQUIRL_PLUGIN_ENTRY_INIT ) );
    }

    auto shutdownFn
        = resolveSymbol<LogSquirlPluginShutdownFn>( *library, LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN );
    if ( !shutdownFn ) {
        return std::unexpected( QString( "Plugin '%1' missing symbol: %2" )
                                    .arg( metadata.id(), LOGSQUIRL_PLUGIN_ENTRY_SHUTDOWN ) );
    }

    // Optional symbols
    auto initExFn
        = resolveSymbol<LogSquirlPluginInitExFn>( *library, LOGSQUIRL_PLUGIN_ENTRY_INIT_EX );

    auto configureFn
        = resolveSymbol<LogSquirlPluginConfigureFn>( *library, LOGSQUIRL_PLUGIN_ENTRY_CONFIGURE );

    auto converterGetExtsFn = resolveSymbol<LogSquirlConverterGetExtsFn>(
        *library, LOGSQUIRL_CONVERTER_ENTRY_GET_EXTS );

    auto converterConvertFn
        = resolveSymbol<LogSquirlConverterConvertFn>( *library, LOGSQUIRL_CONVERTER_ENTRY_CONVERT );

    // Validate the plugin's own info against the manifest
    const auto* info = getInfoFn();
    if ( !info ) {
        return std::unexpected(
            QString( "Plugin '%1' get_info returned null" ).arg( metadata.id() ) );
    }

    if ( info->api_version != LOGSQUIRL_PLUGIN_API_VERSION ) {
        return std::unexpected( QString( "Plugin '%1' reports api_version %2, host supports %3" )
                                    .arg( metadata.id() )
                                    .arg( info->api_version )
                                    .arg( LOGSQUIRL_PLUGIN_API_VERSION ) );
    }

    LOG_INFO << "Plugin loaded successfully: " << metadata.id() << " v" << metadata.version();

    return PluginHandle( metadata, std::move( library ), getInfoFn, initFn, initExFn, shutdownFn,
                         configureFn, converterGetExtsFn, converterConvertFn );
}

} // namespace logsquirl::plugins
