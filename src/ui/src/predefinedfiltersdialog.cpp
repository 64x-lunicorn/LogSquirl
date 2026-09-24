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

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>

#include "containers.h"
#include "dispatch_to.h"
#include "groupexchange.h"
#include "groupimportprompt.h"
#include "iconloader.h"
#include "log.h"
#include "predefinedfilters.h"
#include "theme.h"

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
    exportButton->setEnabled( false );

    connect( setListWidget, &QListWidget::itemSelectionChanged, this,
             &PredefinedFiltersDialog::updatePropertyFields );
    connect( filterSetEdit_, &PredefinedFilterSetEdit::changed, this,
             &PredefinedFiltersDialog::updateFilterSetProperties );
    connect( buttonBox, &QDialogButtonBox::clicked, this, &PredefinedFiltersDialog::resolveDialog );

    if ( !filterSets_.empty() ) {
        setCurrentRow( 0 );
    }

    loadIcons();
    Theme::whenApplied( this, [ this ] { loadIcons(); } );
}

void PredefinedFiltersDialog::loadIcons()
{
    loadListEditIcons( addSetButton, removeSetButton, upSetButton, downSetButton );
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
    if ( selectedRow_ < 0 && selectedTeamRow_ < 0 ) {
        return;
    }

    // The edit widget holds the group as the user sees it now.
    const auto group = filterSetEdit_->filterSet();
    using namespace logsquirl::groupexchange;

    const auto proposed
        = QDir( exportFolder() ).filePath( suggestedFileName( group.name(), GroupKind::Filter ) );
    auto file = QFileDialog::getSaveFileName( this, tr( "Export predefined filters" ), proposed,
                                              tr( "Predefined filters (*.conf)" ) );
    if ( file.isEmpty() ) {
        return;
    }
    file = withConfSuffix( file );

    if ( !writeGroup( file, group ) ) {
        QMessageBox::warning( this, tr( "Export predefined filters" ),
                              tr( "The file %1 could not be written." ).arg( file ) );
        return;
    }
    rememberExportFolder( file );
}

void PredefinedFiltersDialog::importFilters()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr( "Select one or more files to open" ), "", tr( "Predefined filters (*.conf)" ) );

    if ( files.isEmpty() ) {
        return;
    }

    using namespace logsquirl::groupexchange;
    const auto title = tr( "Import predefined filters" );
    ImportSession session( askUser( this, title ) );

    // The imported groups are only in this dialog's copy: OK / Apply take
    // them over, Cancel discards them.
    for ( const auto& file : files ) {
        LOG_INFO << "Loading filters from " << file;
        reportImportError( this, title, file, importFile( file, filterSets_, session ) );
    }

    // Show the list as it is now; a replaced group is read again from it.
    const int row = selectedRow_;
    populateSetList();
    setCurrentRow( row >= 0 ? row : setListWidget->count() - 1 );
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
        // One group is shown at a time: one of the user's own, or a Team group.
        if ( teamGroupsList_ ) {
            teamGroupsList_->clearSelection();
        }
        selectedTeamRow_ = -1;
        filterSetEdit_->setReadOnly( false );
        filterSetEdit_->setFilterSet( filterSets_.at( selectedRow_ ) );

        // The Default group cannot be removed or renamed.
        const bool isDefault = ( filterSets_[ selectedRow_ ].id() == defaultFilterSetId() );
        removeSetButton->setEnabled( !isDefault );
        upSetButton->setEnabled( selectedRow_ > 0 );
        downSetButton->setEnabled( selectedRow_ < setListWidget->count() - 1 );
        exportButton->setEnabled( true );
    }
    else if ( selectedTeamRow_ < 0 ) {
        exportButton->setEnabled( false );
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

// --- Team groups ---

void PredefinedFiltersDialog::showTeamGroups( const QList<PredefinedFilterSet>& groups )
{
    teamGroups_ = groups;

    if ( !teamGroupsList_ ) {
        teamGroupsLabel_ = new QLabel( tr( "Team groups" ), leftPanel );
        teamGroupsLabel_->setAlignment( Qt::AlignCenter );
        teamGroupsLabel_->setToolTip(
            tr( "Shared through the Team Folder: they change when the team changes them." ) );
        teamGroupsList_ = new QListWidget( leftPanel );
        teamGroupsList_->setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Expanding );
        leftLayout->addWidget( teamGroupsLabel_ );
        leftLayout->addWidget( teamGroupsList_ );
        connect( teamGroupsList_, &QListWidget::itemSelectionChanged, this,
                 &PredefinedFiltersDialog::showSelectedTeamGroup );
    }

    selectedTeamRow_ = -1;
    teamGroupsList_->clear();
    for ( const auto& group : teamGroups_ ) {
        teamGroupsList_->addItem( group.name() );
    }
}

void PredefinedFiltersDialog::showSelectedTeamGroup()
{
    const auto selected = teamGroupsList_->selectedItems();
    if ( selected.isEmpty() ) {
        return;
    }
    const auto row = teamGroupsList_->row( selected.at( 0 ) );

    // Leaves the user's own group; what was changed in it stays in this
    // dialog's copy until OK, Apply or Cancel.
    setListWidget->clearSelection();

    selectedTeamRow_ = row;
    filterSetEdit_->setReadOnly( true );
    filterSetEdit_->setFilterSet( teamGroups_.at( row ) );
    removeSetButton->setEnabled( false );
    upSetButton->setEnabled( false );
    downSetButton->setEnabled( false );
    exportButton->setEnabled( true );
}

// --- Helpers ---

void PredefinedFiltersDialog::populateSetList()
{
    setListWidget->clear();
    for ( const auto& set : filterSets_ ) {
        setListWidget->addItem( set.name() );
    }
}
