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

#include "updateoffer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>

namespace logsquirl::versioncheck {

namespace {

// "26.10.0-beta1" and "26.10.0.760" both have the base version 26.10.0.
QVersionNumber baseOf( const QString& version )
{
    const auto number = QVersionNumber::fromString( version );
    return QVersionNumber( number.segments().mid( 0, 3 ) );
}

bool isPrerelease( const QString& releaseName )
{
    return releaseName.contains( u'-' );
}

struct Release {
    QString name;
    QString url;
    QString build; // YY.MM.PATCH.BUILD it was published from; empty in older feeds
    bool isBeta = false;
};

// A release is newer when it was published from a later build. Betas and the
// stable release of one version share its base, so without the build only a
// newer base counts.
bool isNewer( const Release& release, const QString& runningVersion )
{
    if ( !release.build.isEmpty() ) {
        return QVersionNumber::fromString( release.build )
               > QVersionNumber::fromString( runningVersion );
    }
    return baseOf( release.name ) > baseOf( runningVersion );
}

} // namespace

std::optional<UpdateOffer> findUpdateOffer( const QByteArray& feed, const QString& runningVersion,
                                            bool betaCheckingEnabled )
{
    const auto root = QJsonDocument::fromJson( feed ).object();
    const auto running = baseOf( runningVersion );
    if ( running.isNull() ) {
        return std::nullopt;
    }

    const auto releaseOf = [ &root ]( const QString& key, bool isBeta ) {
        return Release{ root.value( key ).toString(), root.value( key + "_url" ).toString(),
                        root.value( key + "_build" ).toString(), isBeta };
    };
    const auto stable = releaseOf( "stable", false );
    const auto beta = releaseOf( "beta", true );

    // A build whose version was only published as betas runs a beta.
    bool runsStable = false;
    bool runsBeta = false;
    for ( const auto& value : root.value( "releases" ).toArray() ) {
        const auto name = value.toString();
        if ( baseOf( name ) == running ) {
            ( isPrerelease( name ) ? runsBeta : runsStable ) = true;
        }
    }
    const bool offersBeta = betaCheckingEnabled || ( runsBeta && !runsStable );

    const auto offerable = [ &runningVersion ]( const Release& release ) {
        return !release.name.isEmpty() && !release.url.isEmpty()
               && isNewer( release, runningVersion );
    };
    const Release* chosen = nullptr;
    if ( offerable( stable ) ) {
        chosen = &stable;
    }
    else if ( offersBeta && offerable( beta ) ) {
        chosen = &beta;
    }
    if ( chosen == nullptr ) {
        return std::nullopt;
    }

    // The feed lists releases oldest first; the notes end with the offered one,
    // so a later release that is not offered is not listed.
    UpdateOffer offer{ chosen->name, chosen->url, chosen->isBeta, {} };
    for ( const auto& value : root.value( "changelog" ).toArray() ) {
        const auto entry = value.toObject();
        const auto version = entry.value( "version" ).toString();
        if ( baseOf( version ) > running || version == chosen->name ) {
            offer.changes << QStringLiteral( "%1: %2" )
                                 .arg( version, entry.value( "description" ).toString() );
        }
        if ( version == chosen->name ) {
            break;
        }
    }
    return offer;
}

} // namespace logsquirl::versioncheck
