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

#include "benchmarkscenario.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace logsquirl::benchmark {

ScenarioRegistry& ScenarioRegistry::instance()
{
    // Made on first use: registrations run while the program's statics are
    // initialized, in no defined order.
    static ScenarioRegistry registry;
    return registry;
}

bool ScenarioRegistry::add( const QString& name, const QString& description,
                            ScenarioFactory create )
{
    return entries_.try_emplace( name, ScenarioEntry{ name, description, std::move( create ) } )
        .second;
}

const ScenarioEntry* ScenarioRegistry::find( const QString& name ) const
{
    const auto found = entries_.find( name );
    return found == entries_.end() ? nullptr : &found->second;
}

std::vector<const ScenarioEntry*> ScenarioRegistry::entries() const
{
    std::vector<const ScenarioEntry*> entries;
    entries.reserve( entries_.size() );
    for ( const auto& entry : entries_ ) {
        entries.push_back( &entry.second );
    }
    return entries;
}

ScenarioRegistration::ScenarioRegistration( const QString& name, const QString& description,
                                            ScenarioFactory create )
{
    if ( !ScenarioRegistry::instance().add( name, description, std::move( create ) ) ) {
        std::fprintf( stderr, "logsquirl: two benchmark scenarios are named '%s'\n",
                      qPrintable( name ) );
        std::abort();
    }
}

} // namespace logsquirl::benchmark
