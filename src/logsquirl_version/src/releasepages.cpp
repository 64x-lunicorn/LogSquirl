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

#include "logsquirl_version.h"

#include <QRegularExpression>

namespace logsquirl::versioncheck {

namespace {

// A build YY.MM.PATCH.BUILD, or a release's name YY.MM.PATCH with an
// optional pre-release suffix, as .github/scripts/releases.py reads tags.
// Its month is 1 to 12, written with one digit or two.
QRegularExpressionMatch parse( const QString& version )
{
    static const QRegularExpression versionPattern( QStringLiteral(
        R"(^([0-9]{2})\.(0?[1-9]|1[0-2])\.[0-9]+(?:\.([0-9]+)|-(?:alpha|beta|rc)\.?[0-9]+)?$)" ) );
    return versionPattern.match( version );
}

QString monthOf( const QRegularExpressionMatch& match, QChar separator )
{
    return match.captured( 1 ) + separator + match.captured( 2 ).rightJustified( 2, u'0' );
}

} // namespace

QString versionMonth( const QString& version )
{
    const auto match = parse( version );
    if ( !match.hasMatch() ) {
        return version;
    }
    return monthOf( match, u'.' );
}

QString releaseNotesUrl( const QString& version )
{
    const auto match = parse( version );
    const bool localBuild = match.hasCaptured( 3 ) && match.captured( 3 ).toLongLong() == 0;
    if ( !match.hasMatch() || localBuild ) {
        return QLatin1String( ReleasesOverviewUrl );
    }
    return QLatin1String( WebsiteUrl ) + QStringLiteral( "/news/release-" ) + monthOf( match, u'-' )
           + u'/';
}

QString runningVersionMonth()
{
    return versionMonth( logsquirlVersion() );
}

QString runningReleaseNotesUrl()
{
    return releaseNotesUrl( logsquirlVersion() );
}

} // namespace logsquirl::versioncheck
