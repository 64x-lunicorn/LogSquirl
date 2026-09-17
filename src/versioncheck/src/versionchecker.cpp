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

namespace {

static constexpr QLatin1String VERSION_URL = QLatin1String(
    "https://raw.githubusercontent.com/64x-lunicorn/LogSquirl/master/latest.json", 76 );
static constexpr std::time_t CHECK_INTERVAL_S = 3600 * 24 * 7; /* 7 days */

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

VersionChecker::VersionChecker()
    : QObject()
    , manager_( new QNetworkAccessManager( this ) )
{
    manager_->setRedirectPolicy( QNetworkRequest::NoLessSafeRedirectPolicy );
}

void VersionChecker::startCheck()
{
    LOG_DEBUG << "VersionChecker::startCheck()";

    const auto& deadlineConfig = VersionCheckerConfig::getSynced();
    const auto& appConfig = Configuration::get();

    if ( !appConfig.versionCheckingEnabled() ) {
        return;
    }

    // Beta checks bypass the 7-day deadline and run on every app start
    const bool deadlineReached = deadlineConfig.nextDeadline() < std::time( nullptr );
    if ( deadlineReached || appConfig.betaVersionCheckingEnabled() ) {
        connect( manager_, &QNetworkAccessManager::finished, this,
                 &VersionChecker::downloadFinished );

        LOG_DEBUG << "Requesting new version info from " << VERSION_URL;

        QNetworkRequest request;
        request.setUrl( QUrl( VERSION_URL ) );
        manager_->get( request );
    }
    else {
        LOG_DEBUG << "Deadline not reached yet, next check in "
                  << std::difftime( deadlineConfig.nextDeadline(), std::time( nullptr ) );
    }
}

void VersionChecker::downloadFinished( QNetworkReply* reply )
{
    LOG_DEBUG << "VersionChecker::downloadFinished()";

    if ( reply->error() == QNetworkReply::NoError ) {
        const auto rawReply = reply->readAll();
        checkVersionData( rawReply );
    }
    else {
        LOG_WARNING << "Download failed: err " << reply->error();
    }

    reply->deleteLater();

    // Extend the deadline
    auto& config = VersionCheckerConfig::get();

    config.setNextDeadline( std::time( nullptr ) + CHECK_INTERVAL_S );

    config.save();
}

void VersionChecker::checkVersionData( QByteArray versionData )
{
    LOG_DEBUG << "Version reply: " << QString::fromUtf8( versionData );

    const QString currentVersion = logsquirlVersion();
    const auto offer = logsquirl::versioncheck::findUpdateOffer(
        versionData, currentVersion, Configuration::get().betaVersionCheckingEnabled() );
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
