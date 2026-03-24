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
#include "pluginmetadata.h"

#include <QString>

#include <expected>
#include <memory>

class QLibrary;

namespace logsquirl::plugins {

/**
 * A loaded plugin instance — holds the shared library and resolved symbols.
 *
 * Created by PluginLoader::load().  The library remains loaded until this
 * object is destroyed.  Call init() to start the plugin and shutdown() before
 * destruction.
 */
class PluginHandle {
  public:
    ~PluginHandle();

    PluginHandle( const PluginHandle& ) = delete;
    PluginHandle& operator=( const PluginHandle& ) = delete;
    PluginHandle( PluginHandle&& ) noexcept;
    PluginHandle& operator=( PluginHandle&& ) noexcept;

    const PluginMetadata& metadata() const { return metadata_; }

    /** True if the plugin has been successfully initialised. */
    bool isInitialised() const { return initialised_; }

    /**
     * Initialise the plugin by calling its init entry point.
     * @param api     Host API function table (must outlive the plugin).
     * @param handle  Opaque handle passed back through every host API call.
     * @return Empty string on success, error message on failure.
     */
    QString init( const LogSquirlHostApi* api, void* handle );

    /** Shut down the plugin (calls its shutdown entry point). */
    void shutdown();

    /** True if the plugin exports a configure entry point. */
    bool hasConfigureUi() const { return configureFn_ != nullptr; }

    /** Open the plugin's configuration dialog with the given parent widget. */
    void configure( void* parentWidget );

    /**
     * Create a minimal handle for script-based plugins (e.g. Lua).
     * No library is loaded; all function pointers are null.
     * The metadata is stored for identification purposes.
     */
    static PluginHandle createScriptHandle( PluginMetadata meta );

    /** True if this is a converter plugin with extension/convert entry points. */
    bool isConverter() const;

    /** Get converter file extensions (semicolon-separated).  Empty if not a converter. */
    QString converterExtensions() const;

    /**
     * Run the converter on a file.
     * @return 0 on success, non-zero on failure.
     */
    int convert( const QString& inputPath, const QString& outputPath ) const;

  private:
    friend class PluginLoader;

    PluginHandle( PluginMetadata meta, std::unique_ptr<QLibrary> lib,
                  LogSquirlPluginGetInfoFn getInfoFn,
                  LogSquirlPluginInitFn initFn,
                  LogSquirlPluginShutdownFn shutdownFn,
                  LogSquirlPluginConfigureFn configureFn,
                  LogSquirlConverterGetExtsFn converterGetExtsFn,
                  LogSquirlConverterConvertFn converterConvertFn );

    PluginMetadata metadata_;
    std::unique_ptr<QLibrary> library_;

    LogSquirlPluginGetInfoFn getInfoFn_ = nullptr;
    LogSquirlPluginInitFn initFn_ = nullptr;
    LogSquirlPluginShutdownFn shutdownFn_ = nullptr;
    LogSquirlPluginConfigureFn configureFn_ = nullptr;
    LogSquirlConverterGetExtsFn converterGetExtsFn_ = nullptr;
    LogSquirlConverterConvertFn converterConvertFn_ = nullptr;

    bool initialised_ = false;
};

/**
 * Loads plugin shared libraries and resolves their C ABI entry points.
 *
 * Uses QLibrary (not QPluginLoader) because plugins export plain C symbols
 * rather than Qt plugin metadata.
 */
class PluginLoader {
  public:
    /**
     * Load a plugin from disk.
     * Resolves all required symbols and validates the info returned by the
     * plugin's get_info entry point against the manifest metadata.
     *
     * @param metadata  Parsed plugin manifest.
     * @return A ready-to-init PluginHandle, or an error message.
     */
    static std::expected<PluginHandle, QString> load( const PluginMetadata& metadata );
};

} // namespace logsquirl::plugins
