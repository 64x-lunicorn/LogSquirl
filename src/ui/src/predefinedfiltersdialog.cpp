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

#include "predefinedfiltersdialog.h"

#include <QFileDialog>

#include "containers.h"
#include "dispatch_to.h"
#include "iconloader.h"
#include "log.h"
#include "predefinedfilters.h"

static constexpr QLatin1String DEFAULT_SET_NAME = QLatin1String( "New filter group", 16 );

PredefinedFiltersDialog::PredefinedFiltersDialog( QWidget* parent )
    : QDialog( parent )
{
    setupUi( this );

    filterSetEdit_ = new PredefinedFilterSetEdit( this );
    filterSetEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    filterSetEditLayout->addWidget( filterSetEdit_ );

    splitter->setStretchFactor( 0, 0 );
    splitter->setStretchFactor( 1, 1 );

    // Load a temporary copy of filter sets from disk.
    filterSets_ = PredefinedFiltersCollection::getSynced().filterSets();

    populateSetList();

    removeSetButton->setEnabled( false );
    upSetButton->setEnabled( false );
    downSetButton->setEnabled( false );

    connect( addSetButton, &QToolButton::clicked, this, &PredefinedFiltersDialog::addFilterSet );
    connect( removeSetButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::removeFilterSet );
    connect( upSetButton, &QToolButton::clicked, this, &PredefinedFiltersDialog::moveFilterSetUp );
    connect( downSetButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::moveFilterSetDown );
    connect( exportButton, &QPushButton::clicked, this, &PredefinedFiltersDialog::exportFilters );
    connect( importButton, &QPushButton::clicked, this, &PredefinedFiltersDialog::importFilters );

    selectedRow_ = -1;

    connect( setListWidget, &QListWidget::itemSelectionChanged, this,
             &PredefinedFiltersDialog::updatePropertyFields );
    connect( filterSetEdit_, &PredefinedFilterSetEdit::changed, this,
             &PredefinedFiltersDialog::updateFilterSetProperties );
    connect( buttonBox, &QDialogButtonBox::clicked, this, &PredefinedFiltersDialog::resolveDialog );

    if ( !filterSets_.empty() ) {
        setCurrentRow( 0 );
    }

    dispatchToMainThread( [ this ] {
        IconLoader iconLoader( this );
        addSetButton->setIcon( iconLoader.load( "icons8-plus-16" ) );
        removeSetButton->setIcon( iconLoader.load( "icons8-minus-16" ) );
        upSetButton->setIcon( iconLoader.load( "icons8-up-16" ) );
        downSetButton->setIcon( iconLoader.load( "icons8-down-arrow-16" ) );
    } );
}

PredefinedFiltersDialog::PredefinedFiltersDialog( const QString& newFilter, QWidget* parent )
    : PredefinedFiltersDialog( parent )
{
    if ( !newFilter.isEmpty() ) {
        // Add the filter to the Default set and select that row.
        for ( int i = 0; i < filterSets_.size(); ++i ) {
            if ( filterSets_[ i ].id() == defaultFilterSetId() ) {
                filterSets_[ i ].addFilter( { newFilter, newFilter, false } );
                setCurrentRow( i );
                updatePropertyFields();
                break;
            }
        }
    }
}

// --- Group list management ---

void PredefinedFiltersDialog::addFilterSet()
{
    filterSets_.append( PredefinedFilterSet::createNewSet( DEFAULT_SET_NAME ) );
    setListWidget->addItem( DEFAULT_SET_NAME );
    setCurrentRow( setListWidget->count() - 1 );
}

void PredefinedFiltersDialog::removeFilterSet()
{
    const int index = setListWidget->currentRow();
    if ( index < 0 || index >= filterSets_.size() ) {
        return;
    }

    // Prevent deletion of the Default group.
    if ( filterSets_[ index ].id() == defaultFilterSetId() ) {
        return;
    }

    setCurrentRow( -1 );
    dispatchToMainThread( [ this, index ] {
        filterSets_.removeAt( index );
        delete setListWidget->takeItem( index );

        const int count = setListWidget->count();
        if ( index < count ) {
            setCurrentRow( index );
        }
        else {
            setCurrentRow( count - 1 );
        }
    } );
}

void PredefinedFiltersDialog::moveFilterSetUp()
{
    const int index = setListWidget->currentRow();
    if ( index <= 0 ) {
        return;
    }

    filterSets_.move( index, index - 1 );
    dispatchToMainThread( [ this, index ] {
        auto* item = setListWidget->takeItem( index );
        setListWidget->insertItem( index - 1, item );
        setCurrentRow( index - 1 );
    } );
}

void PredefinedFiltersDialog::moveFilterSetDown()
{
    const int index = setListWidget->currentRow();
    if ( index < 0 || index >= setListWidget->count() - 1 ) {
        return;
    }

    filterSets_.move( index, index + 1 );
    dispatchToMainThread( [ this, index ] {
        auto* item = setListWidget->takeItem( index );
        setListWidget->insertItem( index + 1, item );
        setCurrentRow( index + 1 );
    } );
}

// --- Import / Export ---

void PredefinedFiltersDialog::exportFilters()
{
    auto file = QFileDialog::getSaveFileName( this, tr( "Export predefined filters" ), "",
                                              tr( "Predefined filters (*.conf)" ) );
    if ( file.isEmpty() ) {
        return;
    }
    if ( !file.endsWith( ".conf" ) ) {
        file += ".conf";
    }

    QSettings settings{ file, QSettings::IniFormat };
    PredefinedFiltersCollection collection;
    collection.setFilterSets( filterSets_ );
    collection.saveToStorage( settings );
}

void PredefinedFiltersDialog::importFilters()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr( "Select one or more files to open" ), "",
        tr( "Predefined filters (*.conf)" ) );

    for ( const auto& file : files ) {
        LOG_INFO << "Loading filters from " << file;
        QSettings settings{ file, QSettings::IniFormat };
        PredefinedFiltersCollection collection;
        collection.retrieveFromStorage( settings );

        for ( const auto& set : collection.filterSets() ) {
            // Skip duplicates by name or id.
            bool duplicate = false;
            for ( const auto& existing : filterSets_ ) {
                if ( existing.id() == set.id() || existing.name() == set.name() ) {
                    duplicate = true;
                    break;
                }
            }
            if ( duplicate ) {
                LOG_INFO << "Skipping duplicate set: " << set.name();
                continue;
            }

            filterSets_.append( set );
            setListWidget->addItem( set.name() );
        }
    }
}

// --- Apply / OK / Cancel ---

void PredefinedFiltersDialog::resolveDialog( QAbstractButton* button )
{
    const auto role = buttonBox->buttonRole( button );

    if ( role == QDialogButtonBox::RejectRole ) {
        reject();
        return;
    }

    // Write back to the temporary set if a row is selected.
    if ( selectedRow_ >= 0 ) {
        filterSets_[ selectedRow_ ] = filterSetEdit_->filterSet();
    }

    auto& persistent = PredefinedFiltersCollection::get();

    if ( role == QDialogButtonBox::AcceptRole ) {
        persistent.setFilterSets( filterSets_ );
        accept();
    }
    else if ( role == QDialogButtonBox::ApplyRole ) {
        persistent.setFilterSets( filterSets_ );
    }
    else {
        LOG_ERROR << "PredefinedFiltersDialog::resolveDialog unhandled role: " << role;
        return;
    }

    persistent.save();
    Q_EMIT optionsChanged();
}

// --- Selection / property sync ---

void PredefinedFiltersDialog::setCurrentRow( int row )
{
    dispatchToMainThread( [ this, row ]() { setListWidget->setCurrentRow( row ); } );
}

void PredefinedFiltersDialog::updatePropertyFields()
{
    if ( setListWidget->selectedItems().count() >= 1 ) {
        selectedRow_ = setListWidget->row( setListWidget->selectedItems().at( 0 ) );
    }
    else {
        selectedRow_ = -1;
    }

    if ( selectedRow_ >= 0 ) {
        filterSetEdit_->setFilterSet( filterSets_.at( selectedRow_ ) );

        // The Default group cannot be removed or renamed.
        const bool isDefault = ( filterSets_[ selectedRow_ ].id() == defaultFilterSetId() );
        removeSetButton->setEnabled( !isDefault );
        upSetButton->setEnabled( selectedRow_ > 0 );
        downSetButton->setEnabled( selectedRow_ < setListWidget->count() - 1 );
    }
    else {
        filterSetEdit_->reset();
        removeSetButton->setEnabled( false );
        upSetButton->setEnabled( false );
        downSetButton->setEnabled( false );
    }
}

void PredefinedFiltersDialog::updateFilterSetProperties()
{
    if ( selectedRow_ >= 0 ) {
        filterSets_[ selectedRow_ ] = filterSetEdit_->filterSet();
        setListWidget->currentItem()->setText( filterSets_[ selectedRow_ ].name() );
    }
}

// --- Helpers ---

void PredefinedFiltersDialog::populateSetList()
{
    setListWidget->clear();
    for ( const auto& set : filterSets_ ) {
        setListWidget->addItem( set.name() );
    }
}
