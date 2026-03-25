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

#include "luapluginwrapper.h"

#include "log.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace logsquirl::plugins {

namespace {

/**
 * Expose a subset of the LogSquirlHostApi to Lua.
 * Functions are wrapped so the opaque handle is captured automatically.
 */
void registerHostApi( sol::state& lua, const LogSquirlHostApi* api, void* handle )
{
    auto hostTable = lua.create_named_table( "host" );

    // push_line(data_string)
    hostTable.set_function( "push_line", [ api, handle ]( const std::string& line ) {
        if ( api && api->push_line ) {
            api->push_line( handle, line.c_str(), line.size() );
        }
    } );

    // signal_eos()
    hostTable.set_function( "signal_eos", [ api, handle ]() {
        if ( api && api->signal_eos ) {
            api->signal_eos( handle );
        }
    } );

    // signal_error(message)
    hostTable.set_function( "signal_error", [ api, handle ]( const std::string& msg ) {
        if ( api && api->signal_error ) {
            api->signal_error( handle, msg.c_str() );
        }
    } );

    // log_message(level, message) — level: 0=debug, 1=info, 2=warning, 3=error
    hostTable.set_function( "log_message",
                            [ api, handle ]( int level, const std::string& msg ) {
                                if ( api && api->log_message ) {
                                    api->log_message( handle, level, msg.c_str() );
                                }
                            } );

    // show_notification(message)
    hostTable.set_function( "show_notification",
                            [ api, handle ]( const std::string& msg ) {
                                if ( api && api->show_notification ) {
                                    api->show_notification( handle, msg.c_str() );
                                }
                            } );

    // open_file(path, follow)
    hostTable.set_function( "open_file",
                            [ api, handle ]( const std::string& path, bool follow ) {
                                if ( api && api->open_file ) {
                                    api->open_file( handle, path.c_str(),
                                                    follow ? 1 : 0 );
                                }
                            } );

    // get_config_dir() → string
    hostTable.set_function( "get_config_dir", [ api, handle ]() -> std::string {
        if ( api && api->get_config_dir ) {
            const char* dir = api->get_config_dir( handle );
            return dir ? std::string( dir ) : std::string{};
        }
        return {};
    } );
}

} // namespace

std::unique_ptr<LuaPluginWrapper> LuaPluginWrapper::load( const QString& scriptPath )
{
    auto lua = std::make_unique<sol::state>();
    lua->open_libraries( sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math,
                         sol::lib::io, sol::lib::os );

    const auto result = lua->safe_script_file( scriptPath.toStdString(),
                                                sol::script_pass_on_error );
    if ( !result.valid() ) {
        sol::error err = result;
        LOG_ERROR << "Failed to load Lua plugin " << scriptPath << ": " << err.what();
        return nullptr;
    }

    return std::unique_ptr<LuaPluginWrapper>( new LuaPluginWrapper( std::move( lua ) ) );
}

LuaPluginWrapper::LuaPluginWrapper( std::unique_ptr<sol::state> state )
    : lua_( std::move( state ) )
{
}

LuaPluginWrapper::~LuaPluginWrapper() = default;

int LuaPluginWrapper::init( const LogSquirlHostApi* hostApi, void* hostHandle )
{
    registerHostApi( *lua_, hostApi, hostHandle );

    sol::protected_function initFn = ( *lua_ )[ "plugin_init" ];
    if ( !initFn.valid() ) {
        lastError_ = "Lua script does not define plugin_init()";
        return -1;
    }

    // Pass the host table to plugin_init
    auto result = initFn( ( *lua_ )[ "host" ] );
    if ( !result.valid() ) {
        sol::error err = result;
        lastError_ = QString::fromStdString( err.what() );
        return -1;
    }
    return 0;
}

void LuaPluginWrapper::shutdown()
{
    sol::protected_function shutdownFn = ( *lua_ )[ "plugin_shutdown" ];
    if ( shutdownFn.valid() ) {
        auto result = shutdownFn();
        if ( !result.valid() ) {
            sol::error err = result;
            LOG_WARNING << "Lua plugin_shutdown error: " << err.what();
        }
    }
}

int LuaPluginWrapper::startData()
{
    sol::protected_function fn = ( *lua_ )[ "start_data" ];
    if ( !fn.valid() ) {
        lastError_ = "Lua script does not define start_data()";
        return -1;
    }

    auto result = fn();
    if ( !result.valid() ) {
        sol::error err = result;
        lastError_ = QString::fromStdString( err.what() );
        return -1;
    }
    return 0;
}

int LuaPluginWrapper::convertFile( const char* inputPath, const char* outputPath )
{
    sol::protected_function fn = ( *lua_ )[ "convert_file" ];
    if ( !fn.valid() ) {
        lastError_ = "Lua script does not define convert_file()";
        return -1;
    }

    auto result = fn( std::string( inputPath ), std::string( outputPath ) );
    if ( !result.valid() ) {
        sol::error err = result;
        lastError_ = QString::fromStdString( err.what() );
        return -1;
    }

    return result.get<int>();
}

} // namespace logsquirl::plugins
