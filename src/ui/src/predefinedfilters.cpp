/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
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

#include "predefinedfilters.h"

#include "log.h"
#include "uuid.h"

// ---------------------------------------------------------------------------
// PredefinedFilterSet
// ---------------------------------------------------------------------------

PredefinedFilterSet PredefinedFilterSet::createNewSet( const QString& name )
{
    return PredefinedFilterSet{ name };
}

PredefinedFilterSet::PredefinedFilterSet( const QString& name )
    : id_( generateIdFromUuid() )
    , name_( name )
{
}

QString PredefinedFilterSet::id() const
{
    return id_;
}

QString PredefinedFilterSet::name() const
{
    return name_;
}

void PredefinedFilterSet::setName( const QString& name )
{
    name_ = name;
}

QList<PredefinedFilter> PredefinedFilterSet::filters() const
{
    return filters_;
}

void PredefinedFilterSet::setFilters( const QList<PredefinedFilter>& filters )
{
    filters_ = filters;
}

void PredefinedFilterSet::addFilter( const PredefinedFilter& filter )
{
    filters_.append( filter );
}

bool PredefinedFilterSet::isEmpty() const
{
    return filters_.isEmpty();
}

void PredefinedFilterSet::saveToStorage( QSettings& settings ) const
{
    settings.beginGroup( "PredefinedFilterSet" );
    settings.setValue( "version", PredefinedFilterSet_VERSION );
    settings.setValue( "name", name_ );
    settings.setValue( "id", id_ );

    settings.remove( "filters" );
    settings.beginWriteArray( "filters" );
    for ( int i = 0; i < filters_.size(); ++i ) {
        settings.setArrayIndex( i );
        settings.setValue( "name", filters_[ i ].name );
        settings.setValue( "filter", filters_[ i ].pattern );
        settings.setValue( "regex", filters_[ i ].useRegex );
    }
    settings.endArray();
    settings.endGroup();
}

void PredefinedFilterSet::retrieveFromStorage( QSettings& settings )
{
    filters_.clear();

    if ( !settings.contains( "PredefinedFilterSet/version" ) ) {
        return;
    }

    settings.beginGroup( "PredefinedFilterSet" );
    if ( settings.value( "version" ).toInt() <= PredefinedFilterSet_VERSION ) {
        name_ = settings.value( "name", "Filters" ).toString();
        id_ = settings.value( "id", generateIdFromUuid() ).toString();

        const int size = settings.beginReadArray( "filters" );
        filters_.reserve( size );
        for ( int i = 0; i < size; ++i ) {
            settings.setArrayIndex( i );
            filters_.append( { settings.value( "name" ).toString(),
                               settings.value( "filter" ).toString(),
                               settings.value( "regex", true ).toBool() } );
        }
        settings.endArray();
    }
    else {
        LOG_ERROR << "Unknown PredefinedFilterSet version, ignoring";
    }
    settings.endGroup();
}

// ---------------------------------------------------------------------------
// PredefinedFiltersCollection
// ---------------------------------------------------------------------------

void PredefinedFiltersCollection::ensureDefaultSet()
{
    // Check if the Default set already exists (by its well-known id).
    for ( const auto& set : filterSets_ ) {
        if ( set.id() == defaultFilterSetId() ) {
            return;
        }
    }

    // Create the Default set with the well-known id.
    PredefinedFilterSet defaultSet;
    defaultSet.id_ = defaultFilterSetId();
    defaultSet.name_ = QStringLiteral( "Default" );
    filterSets_.prepend( defaultSet );
}

QList<PredefinedFilterSet> PredefinedFiltersCollection::filterSets() const
{
    return filterSets_;
}

void PredefinedFiltersCollection::setFilterSets( const QList<PredefinedFilterSet>& sets )
{
    filterSets_ = sets;
    ensureDefaultSet();
}

bool PredefinedFiltersCollection::hasSetByName( const QString& name ) const
{
    return std::any_of( filterSets_.cbegin(), filterSets_.cend(),
                        [ &name ]( const auto& s ) { return s.name() == name; } );
}

PredefinedFiltersCollection::Collection PredefinedFiltersCollection::getAllFilters() const
{
    Collection result;
    for ( const auto& set : filterSets_ ) {
        result.append( set.filters() );
    }
    return result;
}

PredefinedFiltersCollection::Collection PredefinedFiltersCollection::getFilters() const
{
    return getAllFilters();
}

PredefinedFiltersCollection::Collection PredefinedFiltersCollection::getSyncedFilters()
{
    filterSets_ = this->getSynced().filterSets();
    ensureDefaultSet();
    return getAllFilters();
}

void PredefinedFiltersCollection::setFilters( const Collection& filters )
{
    ensureDefaultSet();

    // Put filters into the Default set only.
    for ( auto& set : filterSets_ ) {
        if ( set.id() == defaultFilterSetId() ) {
            set.setFilters( filters );
            return;
        }
    }
}

void PredefinedFiltersCollection::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "PredefinedFiltersCollection::retrieveFromStorage";

    filterSets_.clear();

    if ( settings.contains( "PredefinedFiltersCollection/version" ) ) {
        settings.beginGroup( "PredefinedFiltersCollection" );
        const int storedVersion = settings.value( "version" ).toInt();

        if ( storedVersion == FLAT_FILTERS_VERSION ) {
            // --- v2 migration: flat array → Default group ---
            LOG_INFO << "Migrating v2 flat filters to v3 grouped format";

            QList<PredefinedFilter> oldFilters;
            const int size = settings.beginReadArray( "filters" );
            oldFilters.reserve( size );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );
                oldFilters.append( { settings.value( "name" ).toString(),
                                     settings.value( "filter" ).toString(),
                                     settings.value( "regex", true ).toBool() } );
            }
            settings.endArray();
            settings.endGroup();

            ensureDefaultSet();
            for ( auto& set : filterSets_ ) {
                if ( set.id() == defaultFilterSetId() ) {
                    set.setFilters( oldFilters );
                    break;
                }
            }
        }
        else if ( storedVersion <= PredefinedFiltersCollection_VERSION ) {
            // --- v3 format: array of PredefinedFilterSet ---
            const int size = settings.beginReadArray( "sets" );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );
                PredefinedFilterSet filterSet;
                filterSet.retrieveFromStorage( settings );
                filterSets_.append( std::move( filterSet ) );
            }
            settings.endArray();
            settings.endGroup();
        }
        else {
            LOG_ERROR << "Unknown PredefinedFiltersCollection version " << storedVersion;
            settings.endGroup();
        }
    }

    ensureDefaultSet();
}

void PredefinedFiltersCollection::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "PredefinedFiltersCollection::saveToStorage";

    settings.beginGroup( "PredefinedFiltersCollection" );
    settings.setValue( "version", PredefinedFiltersCollection_VERSION );

    // Remove legacy flat array if present.
    settings.remove( "filters" );

    settings.remove( "sets" );
    settings.beginWriteArray( "sets" );
    for ( int i = 0; i < filterSets_.size(); ++i ) {
        settings.setArrayIndex( i );
        filterSets_[ i ].saveToStorage( settings );
    }
    settings.endArray();
    settings.endGroup();
}

void PredefinedFiltersCollection::saveToStorage( const Collection& filters )
{
    setFilters( filters );
    this->save();
}
