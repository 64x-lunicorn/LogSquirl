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

#include <catch2/catch.hpp>

#include "updateoffer.h"

using logsquirl::versioncheck::findUpdateOffer;

namespace {

const auto ReleasesUrl
    = QStringLiteral( "https://github.com/64x-lunicorn/LogSquirl/releases/tag/" );

// The feed as CI Release writes it: the latest stable and beta, the build
// each was published from, every release name and its description.
QByteArray feed( const QString& stable, const QString& stableBuild, const QString& beta,
                 const QString& betaBuild, const QStringList& releases )
{
    QString list;
    QString changelog;
    for ( const auto& release : releases ) {
        list += QStringLiteral( "%1\"%2\"" ).arg( list.isEmpty() ? "" : ",", release );
        changelog += QStringLiteral( "%1{\"version\":\"%2\",\"description\":\"Notes of %2\"}" )
                         .arg( changelog.isEmpty() ? "" : ",", release );
    }
    return QStringLiteral( R"({"stable":"%1","stable_url":"%2v%1","stable_build":"%3",)"
                           R"("beta":"%4","beta_url":"%2v%4","beta_build":"%5",)"
                           R"("releases":[%6],"changelog":[%7]})" )
        .arg( stable, ReleasesUrl, stableBuild, beta, betaBuild, list, changelog )
        .toUtf8();
}

} // namespace

TEST_CASE( "A stable user is offered a newer stable release" )
{
    const auto offer
        = findUpdateOffer( feed( "26.10.0", "26.10.0.790", "26.10.0-beta2", "26.10.0.775",
                                 { "26.07.0", "26.10.0-beta1", "26.10.0-beta2", "26.10.0" } ),
                           "26.07.0.741", false );

    REQUIRE( offer.has_value() );
    CHECK( offer->version == "26.10.0" );
    CHECK( offer->url == ReleasesUrl + "v26.10.0" );
    CHECK_FALSE( offer->isBeta );
    CHECK( offer->changes
           == QStringList{ "26.10.0-beta1: Notes of 26.10.0-beta1",
                           "26.10.0-beta2: Notes of 26.10.0-beta2", "26.10.0: Notes of 26.10.0" } );
}

TEST_CASE( "A stable user is offered a newer beta only with beta checking on" )
{
    const auto betaOnly = feed( "26.07.0", "26.07.0.741", "26.10.0-beta1", "26.10.0.760",
                                { "26.07.0", "26.10.0-beta1" } );

    CHECK_FALSE( findUpdateOffer( betaOnly, "26.07.0.741", false ).has_value() );

    const auto offer = findUpdateOffer( betaOnly, "26.07.0.741", true );
    REQUIRE( offer.has_value() );
    CHECK( offer->version == "26.10.0-beta1" );
    CHECK( offer->url == ReleasesUrl + "v26.10.0-beta1" );
    CHECK( offer->isBeta );
    CHECK( offer->changes == QStringList{ "26.10.0-beta1: Notes of 26.10.0-beta1" } );
}

TEST_CASE( "A newer stable release is offered before a newer beta" )
{
    const auto offer
        = findUpdateOffer( feed( "26.10.0", "26.10.0.790", "26.11.0-beta1", "26.11.0.800",
                                 { "26.07.0", "26.10.0", "26.11.0-beta1" } ),
                           "26.07.0.741", true );

    REQUIRE( offer.has_value() );
    CHECK( offer->version == "26.10.0" );
    CHECK_FALSE( offer->isBeta );
}

TEST_CASE( "A beta user is offered the next beta of the same version without beta checking" )
{
    const auto offer
        = findUpdateOffer( feed( "26.07.0", "26.07.0.741", "26.10.0-beta2", "26.10.0.775",
                                 { "26.07.0", "26.10.0-beta1", "26.10.0-beta2" } ),
                           "26.10.0.760", false );

    REQUIRE( offer.has_value() );
    CHECK( offer->version == "26.10.0-beta2" );
    CHECK( offer->isBeta );
    CHECK( offer->changes == QStringList{ "26.10.0-beta2: Notes of 26.10.0-beta2" } );
}

TEST_CASE( "A beta user is offered the stable release of the same version" )
{
    const auto offer
        = findUpdateOffer( feed( "26.10.0", "26.10.0.790", "26.10.0-beta2", "26.10.0.775",
                                 { "26.07.0", "26.10.0-beta1", "26.10.0-beta2", "26.10.0" } ),
                           "26.10.0.775", false );

    REQUIRE( offer.has_value() );
    CHECK( offer->version == "26.10.0" );
    CHECK( offer->url == ReleasesUrl + "v26.10.0" );
    CHECK_FALSE( offer->isBeta );
}

TEST_CASE( "An up-to-date user is offered nothing" )
{
    const auto current = feed( "26.10.0", "26.10.0.790", "26.10.0-beta2", "26.10.0.775",
                               { "26.10.0-beta1", "26.10.0-beta2", "26.10.0" } );

    CHECK_FALSE( findUpdateOffer( current, "26.10.0.790", false ).has_value() );
    CHECK_FALSE( findUpdateOffer( current, "26.10.0.790", true ).has_value() );
}

TEST_CASE( "A build of an unreleased version is offered nothing" )
{
    const auto current = feed( "26.10.0", "26.10.0.790", "26.10.0-beta2", "26.10.0.775",
                               { "26.10.0-beta1", "26.10.0-beta2", "26.10.0" } );

    CHECK_FALSE( findUpdateOffer( current, "26.11.0.0", false ).has_value() );
    CHECK_FALSE( findUpdateOffer( current, "26.11.0.812", true ).has_value() );
}

TEST_CASE( "Without the builds in the feed, only a newer version is offered" )
{
    const auto noBuilds
        = feed( "26.10.0", "", "26.10.0-beta1", "", { "26.07.0", "26.10.0-beta1", "26.10.0" } );

    CHECK_FALSE( findUpdateOffer( noBuilds, "26.10.0.760", true ).has_value() );

    const auto offer = findUpdateOffer( noBuilds, "26.07.0.741", false );
    REQUIRE( offer.has_value() );
    CHECK( offer->version == "26.10.0" );
}

TEST_CASE( "The continuous build entry of the feed is never offered" )
{
    const auto withCi = QByteArray(
        R"({"ci":"27.01.0","ci_url":"https://github.com/64x-lunicorn/LogSquirl/releases/tag/continuous",)"
        R"("stable":"26.07.0","stable_url":"https://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.07.0",)"
        R"("releases":["26.07.0"],"changelog":[]})" );

    CHECK_FALSE( findUpdateOffer( withCi, "26.07.0.741", true ).has_value() );
    CHECK_FALSE( findUpdateOffer( withCi, "26.08.0.0", true ).has_value() );
}

TEST_CASE( "A feed that is not an update feed offers nothing" )
{
    CHECK_FALSE( findUpdateOffer( "<html>rate limited</html>", "26.07.0.741", true ).has_value() );
    CHECK_FALSE( findUpdateOffer( "{}", "26.07.0.741", true ).has_value() );
}
