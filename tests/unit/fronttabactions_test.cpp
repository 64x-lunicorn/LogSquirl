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

#include "fronttabactions.h"

namespace {

const QString NoTimestamp = QStringLiteral( "The Log Format has no timestamp field." );
const QString NoTimestampForLimits = QStringLiteral( "The Log Lines have no Timestamp." );

// A Log File loaded, without a Log Format recognized: what the Time
// Navigation says then.
CrawlerWidget::State loadedPlain()
{
    CrawlerWidget::State state;
    state.loadStatus = LoadingStatus::Successful;
    state.hasLoaded = true;
    state.lineCount = LinesCount( 100 );
    state.goToTimestampUnavailable = NoTimestamp;
    state.searchLimitsByTimeUnavailable = NoTimestampForLimits;
    return state;
}

// The same Log File with a Log Format whose timestamp field the Time
// Navigation goes by.
CrawlerWidget::State loadedWithTimestamps()
{
    auto state = loadedPlain();
    state.logFormatName = QStringLiteral( "spdlog" );
    state.hasTableView = true;
    state.goToTimestampUnavailable.clear();
    state.searchLimitsByTimeUnavailable.clear();
    return state;
}

} // namespace

SCENARIO( "The actions of the window follow the tab in front", "[fronttabactions]" )
{
    GIVEN( "no tab with a Log File in front: the Welcome Dashboard" )
    {
        const auto actions = frontTabActions( std::nullopt );

        THEN( "nothing that works on a Log File is enabled, and no toggle is checked" )
        {
            CHECK_FALSE( actions.logFileActionsEnabled );
            CHECK_FALSE( actions.exportAsCsvEnabled );
            CHECK_FALSE( actions.matchesOnlyEnabled );
            CHECK_FALSE( actions.matchesOnly );
            CHECK_FALSE( actions.goToTimestamp.enabled );
            CHECK_FALSE( actions.searchLimitsByTime.enabled );
            CHECK_FALSE( actions.stopEnabled );
            CHECK_FALSE( actions.reloadEnabled );
            CHECK_FALSE( actions.follows );
            CHECK_FALSE( actions.textWrap );
            CHECK_FALSE( actions.valueNamesShown );
            CHECK_FALSE( actions.encodingMib.has_value() );
        }
    }

    GIVEN( "a Log File loading for the first time" )
    {
        CrawlerWidget::State state;
        state.loadingProgress = 42;
        const auto actions = frontTabActions( state );

        THEN( "it can be stopped, not reloaded, and the actions on its file are there" )
        {
            CHECK( actions.stopEnabled );
            CHECK_FALSE( actions.reloadEnabled );
            CHECK( actions.logFileActionsEnabled );
            CHECK_FALSE( actions.exportAsCsvEnabled );
            CHECK_FALSE( actions.matchesOnlyEnabled );
        }
    }

    GIVEN( "a Log File loaded once and reloading" )
    {
        auto state = loadedPlain();
        state.loadStatus.reset();
        const auto actions = frontTabActions( state );

        THEN( "it can be stopped, not reloaded" )
        {
            CHECK( actions.stopEnabled );
            CHECK_FALSE( actions.reloadEnabled );
        }
    }

    GIVEN( "a Log File loaded without a Log Format" )
    {
        const auto actions = frontTabActions( loadedPlain() );

        THEN( "it can be reloaded, not stopped" )
        {
            CHECK( actions.reloadEnabled );
            CHECK_FALSE( actions.stopEnabled );
            CHECK( actions.logFileActionsEnabled );
        }
        THEN( "Matches Only and Export as CSV... are not there" )
        {
            CHECK_FALSE( actions.matchesOnlyEnabled );
            CHECK_FALSE( actions.exportAsCsvEnabled );
        }
        THEN( "Go to timestamp and the time Search Limits say why they are not there" )
        {
            CHECK_FALSE( actions.goToTimestamp.enabled );
            CHECK( actions.goToTimestamp.unavailable == NoTimestamp );
            CHECK_FALSE( actions.searchLimitsByTime.enabled );
            CHECK( actions.searchLimitsByTime.unavailable == NoTimestampForLimits );
        }
    }

    GIVEN( "a Log File loaded with a Log Format that has a timestamp field" )
    {
        const auto actions = frontTabActions( loadedWithTimestamps() );

        THEN( "Matches Only, Export as CSV..., Go to timestamp and the time Search Limits are "
              "there" )
        {
            CHECK( actions.matchesOnlyEnabled );
            CHECK_FALSE( actions.matchesOnly );
            CHECK( actions.exportAsCsvEnabled );
            CHECK( actions.goToTimestamp.enabled );
            CHECK( actions.goToTimestamp.unavailable.isEmpty() );
            CHECK( actions.searchLimitsByTime.enabled );
            CHECK( actions.searchLimitsByTime.unavailable.isEmpty() );
        }
    }

    GIVEN( "a Log File whose Table View shows Matches Only" )
    {
        auto state = loadedWithTimestamps();
        state.matchesOnly = true;
        const auto actions = frontTabActions( state );

        THEN( "Matches Only is enabled and checked" )
        {
            CHECK( actions.matchesOnlyEnabled );
            CHECK( actions.matchesOnly );
        }
    }

    GIVEN( "a Log File followed, wrapped and showing Value Names" )
    {
        auto state = loadedPlain();
        state.follows = true;
        state.textWrap = true;
        state.valueNamesShown = true;
        const auto actions = frontTabActions( state );

        THEN( "Follow, Text Wrap and Show Value Names are checked" )
        {
            CHECK( actions.follows );
            CHECK( actions.textWrap );
            CHECK( actions.valueNamesShown );
        }
    }

    GIVEN( "a Log File read in an Encoding the user chose" )
    {
        auto state = loadedPlain();
        state.encodingMib = 106;
        const auto actions = frontTabActions( state );

        THEN( "that Encoding is checked" )
        {
            REQUIRE( actions.encodingMib.has_value() );
            CHECK( *actions.encodingMib == 106 );
        }
    }

    GIVEN( "a Log File whose Encoding is detected" )
    {
        const auto actions = frontTabActions( loadedPlain() );

        THEN( "the detected Encoding is checked" )
        {
            CHECK_FALSE( actions.encodingMib.has_value() );
        }
    }

    GIVEN( "a loaded Log File whose reload a newer one interrupted" )
    {
        auto state = loadedPlain();
        state.loadStatus = LoadingStatus::Interrupted;
        const auto actions = frontTabActions( state );

        THEN( "it stays loaded: it can be reloaded, not stopped" )
        {
            CHECK( actions.reloadEnabled );
            CHECK_FALSE( actions.stopEnabled );
        }
    }

    GIVEN( "a Log File whose first load failed" )
    {
        CrawlerWidget::State state;
        state.loadStatus = LoadingStatus::Failed;
        const auto actions = frontTabActions( state );

        THEN( "it cannot be reloaded" )
        {
            CHECK_FALSE( actions.reloadEnabled );
            CHECK( actions.stopEnabled );
        }
    }
}
