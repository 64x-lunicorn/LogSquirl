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

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace logsquirl::versioncheck {

// A newer release the update check offers the user (#306).
struct UpdateOffer {
    QString version; // the release's name, e.g. "26.10.0-beta1"
    QString url;     // its release page
    bool isBeta = false;
    QStringList changes; // "version: description" of the releases the user skips
};

// Decides from the update feed (latest.json) which release, if any, a build
// running runningVersion (YY.MM.PATCH.BUILD) is offered. betaCheckingEnabled
// is the user's "check for beta versions" option.
std::optional<UpdateOffer> findUpdateOffer( const QByteArray& feed, const QString& runningVersion,
                                            bool betaCheckingEnabled );

} // namespace logsquirl::versioncheck
