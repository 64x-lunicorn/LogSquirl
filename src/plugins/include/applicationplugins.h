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

#include "plugincatalog.h"
#include "pluginhost.h"

#include <QObject>

#include <functional>

namespace logsquirl::plugins {

/**
 * The Application Plugins: the one Plugin Catalog and the one Plugin Host of
 * the application, shared by every window (#303).
 *
 * The plugins are discovered and loaded once, and not while a window is being
 * built: a window asks for loading when it is shown, and loading runs after
 * the events already queued, so a slow plugin no longer delays the first
 * window. Every further ask does nothing, so a second window neither rescans
 * the plugin directories nor loads a plugin again.
 *
 * What depends on the loaded plugins, such as the converter a Log File is
 * opened with, waits for them with whenLoaded().
 *
 * Must be created and used on the main (GUI) thread.
 */
class ApplicationPlugins : public QObject {
    Q_OBJECT

public:
    /**
     * Discovers and loads the plugins into the catalog and the host, as the
     * caller's configuration says. The plugin layer reads no settings, so the
     * application hands it in.
     */
    using LoadStep = std::function<void( PluginCatalog&, PluginHost& )>;

    /** Application Plugins loaded by loadStep; an empty step loads nothing. */
    explicit ApplicationPlugins( LoadStep loadStep = {}, QObject* parent = nullptr );

    ~ApplicationPlugins() override;

    ApplicationPlugins( const ApplicationPlugins& ) = delete;
    ApplicationPlugins& operator=( const ApplicationPlugins& ) = delete;

    PluginCatalog& catalog()
    {
        return catalog_;
    }

    PluginHost& host()
    {
        return host_;
    }

    /**
     * Load the plugins after the events already queued, unless that was
     * asked for before. Emits loaded() once they are.
     */
    void loadSoon();

    /** Whether the plugins have been loaded. */
    bool isLoaded() const
    {
        return state_ == State::Loaded;
    }

    /**
     * Run work once the plugins are loaded: right away if they are, otherwise
     * when they have been, unless context is gone by then. Does not ask for
     * loading itself.
     */
    void whenLoaded( const QObject* context, std::function<void()> work );

Q_SIGNALS:
    /** Emitted once, when the plugins have been loaded. */
    void loaded();

private:
    enum class State { NotAsked, Asked, Loaded };

    void load();

    LoadStep loadStep_;
    State state_ = State::NotAsked;

    // Declared before host_, which looks plugins up in it and must go first.
    PluginCatalog catalog_;
    PluginHost host_{ catalog_ };
};

} // namespace logsquirl::plugins
