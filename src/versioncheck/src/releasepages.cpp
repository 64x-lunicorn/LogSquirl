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

#include "releasepages.h"

#include <QRegularExpression>

namespace logsquirl::versioncheck {

QString releaseNotesUrl( const QString& version )
{
    // A release's name, as its tag names it without the v: YY.MM.PATCH and
    // a pre-release suffix, as .github/scripts/releases.py reads tags.
    static const QRegularExpression releaseName(
        QStringLiteral( R"(^[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc)\.?[0-9]+)?$)" ) );
    if ( !releaseName.match( version ).hasMatch() ) {
        return QLatin1String( ReleasesOverviewUrl );
    }
    return QLatin1String( ReleasePagesPrefix ) + QStringLiteral( "tag/v" ) + version;
}

} // namespace logsquirl::versioncheck
