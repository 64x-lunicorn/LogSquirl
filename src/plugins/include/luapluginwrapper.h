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

#pragma once

#include "logsquirl_plugin_api.h"

#include <QString>

#include <memory>

// Forward declare sol::state to avoid leaking sol2 headers into every consumer.
namespace sol {
class state;
} // namespace sol

namespace logsquirl::plugins {

/**
 * Wraps a Lua script as a C ABI-compatible plugin.
 *
 * A Lua plugin consists of a plugin.json manifest and a .lua script file
 * in the same directory.  The manifest specifies `"entry_point": "script.lua"`.
 * This wrapper creates a sol2 Lua state, loads the script, and thunks the
 * C ABI plugin interface to Lua function calls.
 *
 * Expected Lua API:
 * @code
 * -- Required: called once when the plugin is loaded
 * function plugin_init(host_api)
 *   -- host_api provides push_line, log_message, etc.
 * end
 *
 * -- Optional: called when the plugin is shut down
 * function plugin_shutdown()
 * end
 *
 * -- For DataSource plugins: called to start producing data
 * function start_data()
 * end
 *
 * -- For Converter plugins: called to convert a file
 * function convert_file(input_path, output_path)
 *   return 0  -- 0 = success
 * end
 * @endcode
 */
class LuaPluginWrapper {
  public:
    /**
     * Load a Lua plugin from the given script path.
     * @param scriptPath  Absolute path to the .lua file.
     * @return non-null wrapper on success, nullptr on load failure.
     */
    static std::unique_ptr<LuaPluginWrapper> load( const QString& scriptPath );

    ~LuaPluginWrapper();

    LuaPluginWrapper( const LuaPluginWrapper& ) = delete;
    LuaPluginWrapper& operator=( const LuaPluginWrapper& ) = delete;

    /** Initialise the Lua plugin with the host API. */
    int init( const LogSquirlHostApi* hostApi, void* hostHandle );

    /** Shut down the Lua plugin. */
    void shutdown();

    /** Call the Lua start_data() function. */
    int startData();

    /** Call the Lua convert_file() function. */
    int convertFile( const char* inputPath, const char* outputPath );

    /** Return the last error message from Lua execution. */
    QString lastError() const { return lastError_; }

  private:
    explicit LuaPluginWrapper( std::unique_ptr<sol::state> state );

    std::unique_ptr<sol::state> lua_;
    QString lastError_;
};

} // namespace logsquirl::plugins
