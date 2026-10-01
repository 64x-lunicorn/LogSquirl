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

#include "benchmarkscenario.h"

using namespace logsquirl::benchmark;

namespace {

class NamedScenario : public Scenario {
public:
    explicit NamedScenario( int id )
        : id_( id )
    {
    }

    void start( ScenarioRun& ) override {}

    int id() const
    {
        return id_;
    }

private:
    int id_;
};

ScenarioFactory scenarioWithId( int id )
{
    return [ id ] { return std::make_unique<NamedScenario>( id ); };
}

} // namespace

TEST_CASE( "A scenario is found by the name it was added under", "[benchmark]" )
{
    ScenarioRegistry registry;
    REQUIRE( registry.add( "open-and-index", "Opens a Log File", scenarioWithId( 1 ) ) );
    REQUIRE( registry.add( "search", "Searches", scenarioWithId( 2 ) ) );

    const auto* found = registry.find( "search" );
    REQUIRE( found != nullptr );
    CHECK( found->description == "Searches" );
    const auto scenario = found->create();
    CHECK( static_cast<NamedScenario*>( scenario.get() )->id() == 2 );

    CHECK( registry.find( "scroll" ) == nullptr );
}

TEST_CASE( "A scenario name is taken once", "[benchmark]" )
{
    ScenarioRegistry registry;
    REQUIRE( registry.add( "open-and-index", "first", scenarioWithId( 1 ) ) );

    CHECK_FALSE( registry.add( "open-and-index", "second", scenarioWithId( 2 ) ) );
    CHECK( registry.find( "open-and-index" )->description == "first" );
}

TEST_CASE( "The scenarios are listed by name", "[benchmark]" )
{
    ScenarioRegistry registry;
    registry.add( "search", "", scenarioWithId( 1 ) );
    registry.add( "follow", "", scenarioWithId( 2 ) );
    registry.add( "open-and-index", "", scenarioWithId( 3 ) );

    const auto entries = registry.entries();
    REQUIRE( entries.size() == 3 );
    CHECK( entries[ 0 ]->name == "follow" );
    CHECK( entries[ 1 ]->name == "open-and-index" );
    CHECK( entries[ 2 ]->name == "search" );
}

TEST_CASE( "A registration adds its scenario to the application's registry", "[benchmark]" )
{
    const ScenarioRegistration registration{ "registration-test", "added by a test",
                                             scenarioWithId( 7 ) };

    const auto* found = ScenarioRegistry::instance().find( "registration-test" );
    REQUIRE( found != nullptr );
    CHECK( found->description == "added by a test" );
}
