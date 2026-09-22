/*
 * Copyright (C) 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "versionchecker.h"
#include "configuration.h"
#include "log.h"

#include "logsquirl_version.h"
#include "updateoffer.h"
#include "updateschedule.h"

namespace {

static constexpr QLatin1String VERSION_URL = QLatin1String(
    "https://raw.githubusercontent.com/64x-lunicorn/LogSquirl/master/latest.json", 76 );

} // namespace

void VersionCheckerConfig::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "VersionCheckerConfig::retrieveFromStorage";

    if ( settings.contains( "VersionChecker/nextDeadline" ) )
        next_deadline_ = settings.value( "VersionChecker/nextDeadline" ).toLongLong();
}

void VersionCheckerConfig::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "VersionCheckerConfig::saveToStorage";

    settings.setValue( "VersionChecker/nextDeadline", static_cast<long long>( next_deadline_ ) );
}

UpdateCheckSettings UpdateCheckSettings::fromSettingsStore()
{
    return UpdateCheckSettings{
        [] { return Configuration::get().versionCheckingEnabled(); },
        [] { return Configuration::get().betaVersionCheckingEnabled(); },
        [] { return VersionCheckerConfig::getSynced().nextDeadline(); },
        []( std::time_t deadline ) {
            auto& config = VersionCheckerConfig::get();
            config.setNextDeadline( deadline );
            config.save();
        },
    };
}

QUrl VersionChecker::defaultFeedUrl()
{
    return QUrl( VERSION_URL );
}

VersionChecker::VersionChecker( UpdateCheckSettings settings, QUrl feedUrl )
    : QObject()
    , settings_( std::move( settings ) )
    , feedUrl_( std::move( feedUrl ) )
    , manager_( new QNetworkAccessManager( this ) )
{
    manager_->setRedirectPolicy( QNetworkRequest::NoLessSafeRedirectPolicy );

    // Connected once here, not on every start: a second start would otherwise
    // handle each reply once more (#389).
    connect( manager_, &QNetworkAccessManager::finished, this, &VersionChecker::downloadFinished );
}

void VersionChecker::startCheck()
{
    LOG_DEBUG << "VersionChecker::startCheck()";

    if ( !settings_.checkingEnabled() ) {
        return;
    }

    const auto nextDeadline = settings_.nextDeadline();
    if ( logsquirl::versioncheck::isCheckDue( std::time( nullptr ), nextDeadline,
                                              settings_.betaCheckingEnabled() ) ) {
        LOG_DEBUG << "Requesting new version info from " << feedUrl_.toString();

        QNetworkRequest request;
        request.setUrl( feedUrl_ );
        manager_->get( request );
    }
    else {
        LOG_DEBUG << "Deadline not reached yet, next check in "
                  << std::difftime( nextDeadline, std::time( nullptr ) );
    }
}

void VersionChecker::downloadFinished( QNetworkReply* reply )
{
    LOG_DEBUG << "VersionChecker::downloadFinished()";

    const bool downloadSucceeded = reply->error() == QNetworkReply::NoError;
    if ( downloadSucceeded ) {
        const auto rawReply = reply->readAll();
        checkVersionData( rawReply );
    }
    else {
        LOG_WARNING << "Download failed: err " << reply->error();
    }

    reply->deleteLater();

    settings_.saveNextDeadline( logsquirl::versioncheck::nextDeadlineAfterCheck(
        std::time( nullptr ), downloadSucceeded ) );
}

void VersionChecker::checkVersionData( QByteArray versionData )
{
    LOG_DEBUG << "Version reply: " << QString::fromUtf8( versionData );

    const QString currentVersion = logsquirlVersion();
    const auto offer = logsquirl::versioncheck::findUpdateOffer( versionData, currentVersion,
                                                                 settings_.betaCheckingEnabled() );
    if ( !offer ) {
        LOG_DEBUG << "Current version " << currentVersion << " is up to date";
        return;
    }

    LOG_INFO << "Sending new version notification: " << currentVersion << " -> " << offer->version
             << ", url " << offer->url << ( offer->isBeta ? " (beta)" : "" );
    const auto displayVersion
        = offer->isBeta ? QString( "%1 (Beta)" ).arg( offer->version ) : offer->version;
    Q_EMIT newVersionFound( displayVersion, offer->url, offer->changes );
}
