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

#include "containers.h"
#include "groupexchange.h"
#include "groupimportprompt.h"
#include "log.h"
#include "plaintext.h"
#include "predefinedfilters.h"

static constexpr QLatin1String DEFAULT_SET_NAME = QLatin1String( "New filter group", 16 );

PredefinedFiltersDialog::PredefinedFiltersDialog( QWidget* parent )
    : QDialog( parent )
{
    setupUi( this );

    groupList_ = new GroupList( logsquirl::groupexchange::GroupKind::Filter, leftPanel );
    leftLayout->addWidget( groupList_ );

    filterSetEdit_ = new PredefinedFilterSetEdit( this );
    filterSetEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    filterSetEditLayout->addWidget( filterSetEdit_ );

    splitter->setStretchFactor( 0, 0 );
    splitter->setStretchFactor( 1, 1 );

    // Load a temporary copy of filter sets from disk.
    filterSets_ = PredefinedFiltersCollection::getSynced().filterSets();
    listOwnGroups();

    // The plugins' groups below the user's own, read-only: they are copied,
    // never edited, and OK or Apply stores none of them (#865).
    pluginGroups_ = PredefinedFiltersCollection::get().pluginFilterGroups();
    groupList_->setPluginGroups( pluginGroups_ );

    connect( groupList_, &GroupList::currentChanged, this, &PredefinedFiltersDialog::showGroup );
    connect( groupList_, &GroupList::newRequested, this, &PredefinedFiltersDialog::addGroup );
    connect( groupList_, &GroupList::deleteRequested, this, &PredefinedFiltersDialog::deleteGroup );
    connect( groupList_, &GroupList::moveRequested, this, &PredefinedFiltersDialog::moveGroup );
    connect( groupList_, &GroupList::shareRequested, this, &PredefinedFiltersDialog::shareGroup );
    connect( groupList_, &GroupList::copyRequested, this, &PredefinedFiltersDialog::copyGroup );
    connect( groupList_, &GroupList::exportRequested, this,
             &PredefinedFiltersDialog::exportFilters );
    connect( groupList_, &GroupList::importRequested, this,
             &PredefinedFiltersDialog::importFilters );

    connect( filterSetEdit_, &PredefinedFilterSetEdit::changed, this,
             &PredefinedFiltersDialog::updateFilterSetProperties );
    connect( buttonBox, &QDialogButtonBox::clicked, this, &PredefinedFiltersDialog::resolveDialog );

    if ( !filterSets_.empty() ) {
        groupList_->setCurrent( GroupList::Section::Own, 0 );
    }
}

PredefinedFiltersDialog::PredefinedFiltersDialog( const QString& newFilter, QWidget* parent )
    : PredefinedFiltersDialog( parent )
{
    if ( !newFilter.isEmpty() ) {
        // Add the filter to the Default set and select that row.
        for ( int i = 0; i < filterSets_.size(); ++i ) {
            if ( filterSets_[ i ].id() == defaultFilterSetId() ) {
                filterSets_[ i ].addFilter( { newFilter, newFilter, false } );
                groupList_->setCurrent( GroupList::Section::Own, i );
                break;
            }
        }
    }
}

// --- The Group List ---

void PredefinedFiltersDialog::listOwnGroups()
{
    QSet<int> undeletable;
    for ( int row = 0; row < filterSets_.size(); ++row ) {
        if ( filterSets_[ row ].id() == defaultFilterSetId() ) {
            undeletable.insert( row );
        }
    }
    groupList_->setOwnGroups( GroupList::namesOf( filterSets_ ), undeletable );
}

void PredefinedFiltersDialog::showGroup( GroupList::Section section, int row )
{
    // One group is shown at a time: one of the user's own, a Team group or a
    // plugin's group. What was changed in the one shown before stays in this
    // dialog's copy until OK, Apply or Cancel.
    selectedRow_ = section == GroupList::Section::Own ? row : -1;
    selectedTeamRow_ = section == GroupList::Section::Team ? row : -1;
    selectedPluginRow_ = section == GroupList::Section::Plugin ? row : -1;

    if ( selectedRow_ >= 0 ) {
        filterSetEdit_->setReadOnly( false );
        filterSetEdit_->setFilterSet( filterSets_.at( selectedRow_ ) );
    }
    else if ( selectedTeamRow_ >= 0 ) {
        filterSetEdit_->setReadOnly( !teamEdits_.isEditable() );
        filterSetEdit_->setFilterSet( teamEdits_.groups().at( selectedTeamRow_ ) );
    }
    else if ( selectedPluginRow_ >= 0 ) {
        filterSetEdit_->setReadOnly( true );
        filterSetEdit_->setFilterSet( pluginGroups_.at( selectedPluginRow_ ).group );
    }
    else {
        filterSetEdit_->setReadOnly( false );
        filterSetEdit_->reset();
    }
}

void PredefinedFiltersDialog::updateFilterSetProperties()
{
    if ( selectedRow_ >= 0 ) {
        filterSets_[ selectedRow_ ] = filterSetEdit_->filterSet();
        groupList_->setName( GroupList::Section::Own, selectedRow_,
                             filterSets_[ selectedRow_ ].name() );
    }
    else if ( selectedTeamRow_ >= 0 && teamEdits_.isEditable() ) {
        auto& group = teamEdits_.groups()[ selectedTeamRow_ ];
        group = filterSetEdit_->filterSet();
        groupList_->setName( GroupList::Section::Team, selectedTeamRow_, group.name() );
    }
}

void PredefinedFiltersDialog::addGroup( GroupList::Section section )
{
    if ( section == GroupList::Section::Team ) {
        if ( !teamEdits_.isEditable() ) {
            return;
        }
        teamEdits_.groups().append( PredefinedFilterSet::createNewSet( DEFAULT_SET_NAME ) );
        groupList_->setTeamGroups( teamEdits_.names(), true );
        groupList_->setCurrent( section, groupList_->count( section ) - 1 );
        return;
    }

    filterSets_.append( PredefinedFilterSet::createNewSet( DEFAULT_SET_NAME ) );
    listOwnGroups();
    groupList_->setCurrent( section, groupList_->count( section ) - 1 );
}

void PredefinedFiltersDialog::deleteGroup( GroupList::Section section, int row )
{
    if ( section == GroupList::Section::Team ) {
        // The Group List asked whether it goes for the whole team.
        if ( !teamEdits_.isEditable() || row < 0 || row >= teamEdits_.groups().size() ) {
            return;
        }
        teamEdits_.groups().removeAt( row );
        groupList_->setTeamGroups( teamEdits_.names(), true );
        return;
    }

    // The Default group is never deleted.
    if ( section != GroupList::Section::Own || row < 0 || row >= filterSets_.size()
         || filterSets_[ row ].id() == defaultFilterSetId() ) {
        return;
    }
    filterSets_.removeAt( row );
    listOwnGroups();
    const auto count = groupList_->count( section );
    groupList_->setCurrent( section, row < count ? row : count - 1 );
}

void PredefinedFiltersDialog::moveGroup( int row, int offset )
{
    const auto to = row + offset;
    if ( row < 0 || row >= filterSets_.size() || to < 0 || to >= filterSets_.size() ) {
        return;
    }
    filterSets_.move( row, to );
    listOwnGroups();
    groupList_->setCurrent( GroupList::Section::Own, to );
}

void PredefinedFiltersDialog::shareGroup( int row )
{
    if ( !teamEdits_.isEditable() || row < 0 || row >= filterSets_.size() ) {
        return;
    }
    // What was typed in the group so far is shared.
    if ( row == selectedRow_ ) {
        filterSets_[ row ] = filterSetEdit_->filterSet();
    }

    teamEdits_.share( filterSets_[ row ] );
    groupList_->setTeamGroups( teamEdits_.names(), true );
    // The Team copy is shown; the group of the user's own stays as it is.
    groupList_->setCurrent( GroupList::Section::Team,
                            groupList_->count( GroupList::Section::Team ) - 1 );
}

void PredefinedFiltersDialog::copyGroup( GroupList::Section section, int row )
{
    PredefinedFilterSet copy;
    if ( section == GroupList::Section::Team && row >= 0 && row < teamEdits_.groups().size() ) {
        if ( teamEdits_.isEditable() && row == selectedTeamRow_ ) {
            teamEdits_.groups()[ row ] = filterSetEdit_->filterSet();
        }
        copy = teamEdits_.copyFor( row, filterSets_ );
    }
    else if ( section == GroupList::Section::Plugin && row >= 0 && row < pluginGroups_.size() ) {
        copy = logsquirl::teamfolder::copyOfGroup( pluginGroups_.at( row ).group,
                                                   GroupList::namesOf( filterSets_ ) );
    }
    else {
        return;
    }

    filterSets_.append( copy );
    listOwnGroups();
    groupList_->setCurrent( GroupList::Section::Own,
                            groupList_->count( GroupList::Section::Own ) - 1 );
}

// --- Import / Export ---

void PredefinedFiltersDialog::exportFilters()
{
    if ( selectedRow_ < 0 && selectedTeamRow_ < 0 && selectedPluginRow_ < 0 ) {
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
        logsquirl::plaintext::warning( this, tr( "Export predefined filters" ),
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
    // A group of a Team group's id is a copy of it, not that group, and so is
    // one of a plugin's group's id.
    auto reserved = idsOf( teamEdits_.groups() );
    for ( const auto& group : logsquirl::as_const( pluginGroups_ ) ) {
        reserved.insert( group.group.id() );
    }
    ImportSession session( askUser( this, title ), reserved );

    // The imported groups are only in this dialog's copy: OK / Apply take
    // them over, Cancel discards them.
    for ( const auto& file : files ) {
        LOG_INFO << "Loading filters from " << file;
        reportImportError( this, title, file, importFile( file, filterSets_, session ) );
    }

    // Show the list as it is now; a replaced group is read again from it.
    const int row = selectedRow_;
    listOwnGroups();
    groupList_->setCurrent( GroupList::Section::Own,
                            row >= 0 ? row : groupList_->count( GroupList::Section::Own ) - 1 );
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
    else if ( selectedTeamRow_ >= 0 && teamEdits_.isEditable() ) {
        teamEdits_.groups()[ selectedTeamRow_ ] = filterSetEdit_->filterSet();
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

    // What was done to the Team groups goes to the team.
    if ( teamEdits_.isEditable() ) {
        const auto requests = teamEdits_.takeRequests();
        if ( !requests.isEmpty() ) {
            Q_EMIT publishRequested( requests );
        }
    }
}

// --- Team groups ---

void PredefinedFiltersDialog::setRegexLabAccess( RegexLabAccess access )
{
    filterSetEdit_->setRegexLabAccess( std::move( access ) );
}

void PredefinedFiltersDialog::updateTeamRevisions( const QStringList& ids,
                                                   const QHash<QString, QString>& revisions )
{
    teamEdits_.updateRevisions( ids, revisions );
}

void PredefinedFiltersDialog::showTeamGroups( const QList<PredefinedFilterSet>& groups,
                                              bool editable,
                                              const QHash<QString, QString>& revisions )
{
    teamEdits_.reset( groups, editable, revisions );
    // A Team group shown is not any more: the list says so.
    groupList_->setTeamGroups( teamEdits_.names(), editable );
}
