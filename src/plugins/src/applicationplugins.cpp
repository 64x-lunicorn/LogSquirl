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

#include "applicationplugins.h"

#include "log.h"

#include <QElapsedTimer>
#include <QTimer>

namespace logsquirl::plugins {

ApplicationPlugins::ApplicationPlugins( LoadStep loadStep, QObject* parent )
    : QObject( parent )
    , loadStep_( std::move( loadStep ) )
{
    host_.setUiPort( &uiPort_ );

    // Once for the application, rather than once per window listening.
    connect( &host_, &PluginHost::notificationRequested, this,
             []( const QString& message ) { LOG_INFO << "Plugin notification: " << message; } );
    connect( &host_, &PluginHost::dataSourceStopped, this,
             []( const QString& pluginId ) { LOG_INFO << "DataSource stopped: " << pluginId; } );
}

ApplicationPlugins::~ApplicationPlugins() = default;

void ApplicationPlugins::loadSoon()
{
    if ( state_ != State::NotAsked ) {
        return;
    }
    state_ = State::Asked;
    QTimer::singleShot( 0, this, &ApplicationPlugins::load );
}

void ApplicationPlugins::load()
{
    QElapsedTimer timer;
    timer.start();
    if ( loadStep_ ) {
        loadStep_( catalog_, host_ );
    }
    state_ = State::Loaded;
    LOG_INFO << "Plugins loaded in " << timer.elapsed() << " ms: " << host_.loadedPluginIds();
    Q_EMIT loaded();
}

void ApplicationPlugins::whenLoaded( const QObject* context, std::function<void()> work )
{
    if ( isLoaded() ) {
        work();
        return;
    }
    connect( this, &ApplicationPlugins::loaded, context, std::move( work ),
             Qt::SingleShotConnection );
}

} // namespace logsquirl::plugins
