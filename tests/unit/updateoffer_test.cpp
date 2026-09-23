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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "updateoffer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using logsquirl::versioncheck::findUpdateOffer;

namespace {

const auto ReleasesUrl
    = QStringLiteral( "https://github.com/64x-lunicorn/LogSquirl/releases/tag/" );

// The feed as CI Release writes it: the latest stable and beta, the build
// each was published from, every release name and its description.
QByteArray feed( const QString& stable, const QString& stableBuild, const QString& beta,
                 const QString& betaBuild, const QStringList& releases )
{
    QString releaseNames;
    QString changelog;
    for ( const auto& release : releases ) {
        releaseNames
            += QStringLiteral( "%1\"%2\"" ).arg( releaseNames.isEmpty() ? "" : ",", release );
        changelog += QStringLiteral( "%1{\"version\":\"%2\",\"description\":\"Notes of %2\"}" )
                         .arg( changelog.isEmpty() ? "" : ",", release );
    }
    return QStringLiteral( R"({"stable":"%1","stable_url":"%2v%1","stable_build":"%3",)"
                           R"("beta":"%4","beta_url":"%2v%4","beta_build":"%5",)"
                           R"("releases":[%6],"changelog":[%7]})" )
        .arg( stable, ReleasesUrl, stableBuild, beta, betaBuild, releaseNames, changelog )
        .toUtf8();
}

} // namespace

SCENARIO( "A stable user is offered a newer stable release", "[versioncheck][updateoffer]" )
{
    GIVEN( "A feed whose stable release was published from a later build" )
    {
        const auto current = feed( "26.10.0", "26.10.0.790", "26.10.0-beta2", "26.10.0.775",
                                   { "26.07.0", "26.10.0-beta1", "26.10.0-beta2", "26.10.0" } );

        WHEN( "LogSquirl 26.07.0 checks for updates without beta checking" )
        {
            const auto offer = findUpdateOffer( current, "26.07.0.741", false );

            THEN( "It is offered the stable release and the notes of every release it skips" )
            {
                REQUIRE( offer.has_value() );
                CHECK( offer->version == "26.10.0" );
                CHECK( offer->url == ReleasesUrl + "v26.10.0" );
                CHECK_FALSE( offer->isBeta );
                CHECK( offer->changes
                       == QStringList{ "26.10.0-beta1: Notes of 26.10.0-beta1",
                                       "26.10.0-beta2: Notes of 26.10.0-beta2",
                                       "26.10.0: Notes of 26.10.0" } );
            }
        }
    }

    GIVEN( "A feed that already lists a later beta" )
    {
        const auto current = feed( "26.10.0", "26.10.0.790", "26.11.0-beta1", "26.11.0.800",
                                   { "26.07.0", "26.10.0", "26.11.0-beta1" } );

        WHEN( "LogSquirl 26.07.0 checks for updates without beta checking" )
        {
            const auto offer = findUpdateOffer( current, "26.07.0.741", false );

            THEN( "The notes end with the offered stable release" )
            {
                REQUIRE( offer.has_value() );
                CHECK( offer->changes == QStringList{ "26.10.0: Notes of 26.10.0" } );
            }
        }

        WHEN( "It checks with beta checking on" )
        {
            const auto offer = findUpdateOffer( current, "26.07.0.741", true );

            THEN( "The newer stable release is offered before the newer beta" )
            {
                REQUIRE( offer.has_value() );
                CHECK( offer->version == "26.10.0" );
                CHECK_FALSE( offer->isBeta );
            }
        }
    }
}

SCENARIO( "A stable user is offered a newer beta only with beta checking on",
          "[versioncheck][updateoffer]" )
{
    GIVEN( "A feed whose only newer release is a beta" )
    {
        const auto betaOnly = feed( "26.07.0", "26.07.0.741", "26.10.0-beta1", "26.10.0.760",
                                    { "26.07.0", "26.10.0-beta1" } );

        WHEN( "LogSquirl 26.07.0 checks without beta checking" )
        {
            THEN( "It is offered nothing" )
            {
                CHECK_FALSE( findUpdateOffer( betaOnly, "26.07.0.741", false ).has_value() );
            }
        }

        WHEN( "It checks with beta checking on" )
        {
            const auto offer = findUpdateOffer( betaOnly, "26.07.0.741", true );

            THEN( "It is offered the beta" )
            {
                REQUIRE( offer.has_value() );
                CHECK( offer->version == "26.10.0-beta1" );
                CHECK( offer->url == ReleasesUrl + "v26.10.0-beta1" );
                CHECK( offer->isBeta );
                CHECK( offer->changes == QStringList{ "26.10.0-beta1: Notes of 26.10.0-beta1" } );
            }
        }
    }
}

SCENARIO( "A beta user is offered the next beta or the stable release",
          "[versioncheck][updateoffer]" )
{
    GIVEN( "A feed whose version 26.10.0 was only published as betas" )
    {
        const auto betas = feed( "26.07.0", "26.07.0.741", "26.10.0-beta2", "26.10.0.775",
                                 { "26.07.0", "26.10.0-beta1", "26.10.0-beta2" } );

        WHEN( "26.10.0-beta1 checks without beta checking" )
        {
            const auto offer = findUpdateOffer( betas, "26.10.0.760", false );

            THEN( "It is offered the next beta" )
            {
                REQUIRE( offer.has_value() );
                CHECK( offer->version == "26.10.0-beta2" );
                CHECK( offer->isBeta );
                CHECK( offer->changes == QStringList{ "26.10.0-beta2: Notes of 26.10.0-beta2" } );
            }
        }
    }

    GIVEN( "A feed whose stable 26.10.0 followed its betas" )
    {
        const auto stable = feed( "26.10.0", "26.10.0.790", "26.10.0-beta2", "26.10.0.775",
                                  { "26.07.0", "26.10.0-beta1", "26.10.0-beta2", "26.10.0" } );

        WHEN( "26.10.0-beta2 checks without beta checking" )
        {
            const auto offer = findUpdateOffer( stable, "26.10.0.775", false );

            THEN( "It is offered the stable release of its version" )
            {
                REQUIRE( offer.has_value() );
                CHECK( offer->version == "26.10.0" );
                CHECK( offer->url == ReleasesUrl + "v26.10.0" );
                CHECK_FALSE( offer->isBeta );
            }
        }
    }
}

SCENARIO( "A build no release is newer than is offered nothing", "[versioncheck][updateoffer]" )
{
    GIVEN( "A feed with the stable 26.10.0 and its betas" )
    {
        const auto current = feed( "26.10.0", "26.10.0.790", "26.10.0-beta2", "26.10.0.775",
                                   { "26.10.0-beta1", "26.10.0-beta2", "26.10.0" } );

        WHEN( "The stable 26.10.0 itself checks, with and without beta checking" )
        {
            THEN( "It is offered nothing" )
            {
                CHECK_FALSE( findUpdateOffer( current, "26.10.0.790", false ).has_value() );
                CHECK_FALSE( findUpdateOffer( current, "26.10.0.790", true ).has_value() );
            }
        }

        WHEN( "A build of the unreleased 26.11.0 checks" )
        {
            THEN( "It is offered nothing" )
            {
                CHECK_FALSE( findUpdateOffer( current, "26.11.0.0", false ).has_value() );
                CHECK_FALSE( findUpdateOffer( current, "26.11.0.812", true ).has_value() );
            }
        }
    }
}

SCENARIO( "A feed without builds offers only a newer version", "[versioncheck][updateoffer]" )
{
    GIVEN( "A feed written before CI Release recorded builds" )
    {
        const auto noBuilds
            = feed( "26.10.0", "", "26.10.0-beta1", "", { "26.07.0", "26.10.0-beta1", "26.10.0" } );

        WHEN( "A build of 26.10.0 checks with beta checking on" )
        {
            THEN( "It is offered nothing, since its own version cannot be told apart" )
            {
                CHECK_FALSE( findUpdateOffer( noBuilds, "26.10.0.760", true ).has_value() );
            }
        }

        WHEN( "LogSquirl 26.07.0 checks" )
        {
            const auto offer = findUpdateOffer( noBuilds, "26.07.0.741", false );

            THEN( "It is offered the newer version" )
            {
                REQUIRE( offer.has_value() );
                CHECK( offer->version == "26.10.0" );
            }
        }
    }
}

SCENARIO( "Only the stable and beta entries of the feed are offered",
          "[versioncheck][updateoffer]" )
{
    GIVEN( "A feed whose continuous build entry is newer than every release" )
    {
        const auto withCi = QStringLiteral( R"({"ci":"27.01.0","ci_url":"%1continuous",)"
                                            R"("stable":"26.07.0","stable_url":"%1v26.07.0",)"
                                            R"("releases":["26.07.0"],"changelog":[]})" )
                                .arg( ReleasesUrl )
                                .toUtf8();

        WHEN( "Released and unreleased builds check" )
        {
            THEN( "The continuous build is never offered" )
            {
                CHECK_FALSE( findUpdateOffer( withCi, "26.07.0.741", true ).has_value() );
                CHECK_FALSE( findUpdateOffer( withCi, "26.08.0.0", true ).has_value() );
            }
        }
    }

    GIVEN( "A reply that is not an update feed" )
    {
        THEN( "Nothing is offered" )
        {
            CHECK_FALSE(
                findUpdateOffer( "<html>rate limited</html>", "26.07.0.741", true ).has_value() );
            CHECK_FALSE( findUpdateOffer( "{}", "26.07.0.741", true ).has_value() );
        }
    }
}

SCENARIO( "Only a release page of LogSquirl is offered", "[versioncheck][updateoffer]" )
{
    // A feed whose newer stable release points at url (#222). Built as JSON
    // values, so every url arrives as written.
    const auto withStableUrl = []( const QString& url ) {
        return QJsonDocument( QJsonObject{ { "stable", "26.10.0" },
                                           { "stable_url", url },
                                           { "stable_build", "26.10.0.790" },
                                           { "releases", QJsonArray{ "26.07.0", "26.10.0" } },
                                           { "changelog", QJsonArray{} } } )
            .toJson();
    };

    GIVEN( "A feed whose newer stable release is a LogSquirl release page" )
    {
        const auto valid = withStableUrl( ReleasesUrl + "v26.10.0" );

        THEN( "It is offered" )
        {
            const auto offer = findUpdateOffer( valid, "26.07.0.741", false );
            REQUIRE( offer.has_value() );
            CHECK( offer->url == ReleasesUrl + "v26.10.0" );
        }
    }

    GIVEN( "A feed whose newer stable release points elsewhere" )
    {
        const auto url
            = GENERATE( as<QString>{},
                        // a foreign domain
                        "https://evil.example.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0",
                        // not https
                        "http://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0",
                        // another repository
                        "https://github.com/someone-else/LogSquirl/releases/tag/v26.10.0",
                        "https://github.com/64x-lunicorn/LogSquirl-fork/releases/tag/v26.10.0",
                        // the repository, but not its releases
                        "https://github.com/64x-lunicorn/LogSquirl/archive/v26.10.0.zip",
                        "https://github.com/64x-lunicorn/LogSquirl/releases",
                        // prefix tricks
                        "https://github.com/64x-lunicorn/LogSquirl/releases.evil.com/v26.10.0",
                        "https://github.com.evil.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0",
                        "https://github.com@evil.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0",
                        "https://evil.com#https://github.com/64x-lunicorn/LogSquirl/releases/",
                        " https://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0",
                        // case variants
                        "HTTPS://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0",
                        "https://GitHub.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0",
                        "https://github.com/64x-lunicorn/logsquirl/releases/tag/v26.10.0",
                        // a port
                        "https://github.com:8443/64x-lunicorn/LogSquirl/releases/tag/v26.10.0",
                        // leaving the releases by path traversal
                        "https://github.com/64x-lunicorn/LogSquirl/releases/../../../evil/repo",
                        "https://github.com/64x-lunicorn/LogSquirl/releases/./../archive",
                        "https://github.com/64x-lunicorn/LogSquirl/releases/%2e%2e/%2E%2E/evil",
                        "https://github.com/64x-lunicorn/LogSquirl/releases/..%2F..%2Fevil",
                        "https://github.com/64x-lunicorn/LogSquirl/releases/..\\..\\evil",
                        // not a URL
                        "https://github.com/64x-lunicorn/LogSquirl/releases/tag/v 26.10.0",
                        "https://github.com/64x-lunicorn/LogSquirl/releases/tag/v1\"><a "
                        "href=\"https://evil.com\">",
                        "" );

        WHEN( "LogSquirl 26.07.0 checks for updates" )
        {
            const auto feedJson = withStableUrl( url );

            THEN( "Nothing is offered" )
            {
                INFO( url.toStdString() );
                CHECK_FALSE( findUpdateOffer( feedJson, "26.07.0.741", true ).has_value() );
            }
        }
    }

    GIVEN( "A feed whose stable release points elsewhere and whose beta is a release page" )
    {
        const auto mixed
            = QStringLiteral(
                  R"({"stable":"26.10.0","stable_url":"https://evil.example.com/","stable_build":"26.10.0.790",)"
                  R"("beta":"26.11.0-beta1","beta_url":"%1v26.11.0-beta1","beta_build":"26.11.0.800",)"
                  R"("releases":["26.07.0","26.10.0","26.11.0-beta1"],"changelog":[]})" )
                  .arg( ReleasesUrl )
                  .toUtf8();

        WHEN( "LogSquirl 26.07.0 checks with beta checking on" )
        {
            const auto offer = findUpdateOffer( mixed, "26.07.0.741", true );

            THEN( "Only the beta is offered" )
            {
                REQUIRE( offer.has_value() );
                CHECK( offer->version == "26.11.0-beta1" );
                CHECK( offer->url == ReleasesUrl + "v26.11.0-beta1" );
            }
        }

        WHEN( "It checks without beta checking" )
        {
            THEN( "Nothing is offered" )
            {
                CHECK_FALSE( findUpdateOffer( mixed, "26.07.0.741", false ).has_value() );
            }
        }
    }
}
