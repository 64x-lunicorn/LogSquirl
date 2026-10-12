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

#include "applicationplugins.h"
#include "logformatcatalog.h"

#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

// Hands the Log Format Catalog the Log Formats the enabled plugins ship
// (#793): the files every enabled plugin names in its manifest, whether or
// not its library is loaded. They are read once the plugins have been
// discovered and again whenever a plugin is enabled or disabled; a settings
// change rebuilds the Catalog with the same files.
//
// The Catalog knows no plugin and the plugin layer no Log Format: the
// application, which owns both, lets them meet here. Which plugins are
// enabled stands in the configuration, which the plugin layer does not read,
// so it is asked for.
class PluginLogFormats : public QObject {
    Q_OBJECT

public:
    using EnabledPlugins = std::function<QStringList()>;

    PluginLogFormats( logsquirl::plugins::ApplicationPlugins& plugins,
                      std::shared_ptr<LogFormatCatalog> catalog, EnabledPlugins enabledPlugins,
                      QObject* parent = nullptr );

    // Hands the Catalog the Log Format files of the plugins enabled now, as
    // their manifests name them, and rebuilds it -- unless they are the files
    // it was handed last time, when nothing changes.
    void refresh();

private:
    logsquirl::plugins::ApplicationPlugins& plugins_;
    std::shared_ptr<LogFormatCatalog> catalog_;
    EnabledPlugins enabledPlugins_;
    std::vector<LogFormatCatalog::PluginFormats> handedOver_;
};
