/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014, 2015 Nicolas Bonnefon
 * and other contributors
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
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#ifndef PREDEFINEDFILTERS_H_
#define PREDEFINEDFILTERS_H_

#include <QList>
#include <QString>

#include "persistable.h"

struct PredefinedFilter {
    QString name;
    QString pattern;
    bool useRegex;
};

// Represents a named group of predefined filters, analogous to HighlighterSet.
// Each set has a unique UUID-based identifier and a display name.
class PredefinedFilterSet {
  public:
    // Create a new set with a generated UUID.
    static PredefinedFilterSet createNewSet( const QString& name );

    PredefinedFilterSet() = default;

    QString id() const;
    QString name() const;
    void setName( const QString& name );

    QList<PredefinedFilter> filters() const;
    void setFilters( const QList<PredefinedFilter>& filters );
    void addFilter( const PredefinedFilter& filter );

    bool isEmpty() const;

    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

  private:
    explicit PredefinedFilterSet( const QString& name );

    static constexpr int PredefinedFilterSet_VERSION = 1;

    QString id_;
    QString name_;
    QList<PredefinedFilter> filters_;

    // Allow dialog classes direct access for editing convenience.
    friend class PredefinedFilterSetEdit;
    friend class PredefinedFiltersDialog;
    friend class PredefinedFiltersCollection;
};

// Well-known ID for the non-deletable Default group.
inline const QString& defaultFilterSetId()
{
    static const QString id = QStringLiteral( "00000000-0000-0000-0000-000000000000" );
    return id;
}

// Represents collection of filter sets read from settings file.
class PredefinedFiltersCollection final : public Persistable<PredefinedFiltersCollection> {
  public:
    using Collection = QList<PredefinedFilter>;

    static const char* persistableName()
    {
        return "PredefinedFiltersCollection";
    }

    // --- Filter-set-level API ---

    QList<PredefinedFilterSet> filterSets() const;
    void setFilterSets( const QList<PredefinedFilterSet>& sets );

    bool hasSetByName( const QString& name ) const;

    // --- Backward-compatible flat API ---

    // Returns a flattened list of all filters across every set.
    Collection getAllFilters() const;

    // Legacy helpers — operate on the flat list inside the Default set.
    Collection getSyncedFilters();
    Collection getFilters() const;
    void setFilters( const Collection& filters );

    void retrieveFromStorage( QSettings& settings );
    void saveToStorage( QSettings& settings ) const;
    void saveToStorage( const Collection& filters );

  private:
    static constexpr int PredefinedFiltersCollection_VERSION = 3;
    // Version 2 stored a flat array of filters (no groups).
    static constexpr int FLAT_FILTERS_VERSION = 2;

    // Ensure the Default set exists; creates it if missing.
    void ensureDefaultSet();

    QList<PredefinedFilterSet> filterSets_;
};

Q_DECLARE_METATYPE( PredefinedFilter )

#endif