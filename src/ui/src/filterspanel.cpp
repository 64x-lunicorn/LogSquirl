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

#include "filterspanel.h"

#include <QHBoxLayout>
#include <QLabel>

#include "log.h"
#include "persistentinfo.h"

static constexpr const char* PinnedSettingsKey = "PinnedFilters";

FiltersPanel::FiltersPanel( QWidget* parent )
    : QWidget( parent )
{
    auto* mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 6, 6, 6, 6 );

    // Search box to filter the list
    searchBox_ = new QLineEdit( this );
    searchBox_->setPlaceholderText( tr( "Search filters..." ) );
    searchBox_->setClearButtonEnabled( true );
    mainLayout->addWidget( searchBox_ );

    // Filter list with checkboxes
    filterList_ = new QListWidget( this );
    mainLayout->addWidget( filterList_ );

    // Buttons row
    auto* buttonsLayout = new QHBoxLayout;
    selectAllButton_ = new QPushButton( tr( "Select All" ), this );
    deselectAllButton_ = new QPushButton( tr( "Deselect All" ), this );
    buttonsLayout->addWidget( selectAllButton_ );
    buttonsLayout->addWidget( deselectAllButton_ );
    buttonsLayout->addStretch();
    mainLayout->addLayout( buttonsLayout );

    connect( searchBox_, &QLineEdit::textChanged, this, &FiltersPanel::onSearchTextChanged );
    connect( filterList_, &QListWidget::itemChanged, this, &FiltersPanel::onItemChanged );
    connect( selectAllButton_, &QPushButton::clicked, this, &FiltersPanel::selectAll );
    connect( deselectAllButton_, &QPushButton::clicked, this, &FiltersPanel::deselectAll );

    loadPinnedFilters();
    refreshFilters();
}

void FiltersPanel::showEvent( QShowEvent* event )
{
    QWidget::showEvent( event );
    refreshFilters();
}

void FiltersPanel::refreshFilters()
{
    auto& collection = PredefinedFiltersCollection::getSynced();
    allFilters_ = collection.getFilters();
    populateList( allFilters_ );
}

void FiltersPanel::populateList( const PredefinedFiltersCollection::Collection& filters )
{
    updatingList_ = true;
    filterList_->clear();

    const auto searchText = searchBox_ ? searchBox_->text().trimmed() : QString{};

    for ( const auto& filter : filters ) {
        // Apply search filter
        if ( !searchText.isEmpty()
             && !filter.name.contains( searchText, Qt::CaseInsensitive )
             && !filter.pattern.contains( searchText, Qt::CaseInsensitive ) ) {
            continue;
        }

        auto* item = new QListWidgetItem( filterList_ );

        // Show "Name  —  pattern" or just pattern if name equals pattern
        if ( filter.name != filter.pattern && !filter.name.isEmpty() ) {
            item->setText( filter.name + QString::fromUtf8( "  \u2014  " ) + filter.pattern );
        }
        else {
            item->setText( filter.pattern );
        }
        item->setToolTip( filter.pattern );

        item->setFlags( item->flags() | Qt::ItemIsUserCheckable );

        // Pinned filters start checked
        const bool isPinned = pinnedFilterNames_.contains( filter.name );
        item->setCheckState( isPinned ? Qt::Checked : Qt::Unchecked );

        // Store filter index for retrieval
        item->setData( Qt::UserRole, filter.name );
    }

    updatingList_ = false;

    // Emit current selection if any pinned filters were auto-checked
    emitCurrentSelection();
}

void FiltersPanel::onItemChanged( QListWidgetItem* item )
{
    if ( updatingList_ ) {
        return;
    }

    Q_UNUSED( item );
    emitCurrentSelection();
}

void FiltersPanel::onSearchTextChanged( const QString& text )
{
    Q_UNUSED( text );
    populateList( allFilters_ );
}

void FiltersPanel::selectAll()
{
    updatingList_ = true;
    for ( int i = 0; i < filterList_->count(); ++i ) {
        filterList_->item( i )->setCheckState( Qt::Checked );
    }
    updatingList_ = false;
    emitCurrentSelection();
}

void FiltersPanel::deselectAll()
{
    updatingList_ = true;
    for ( int i = 0; i < filterList_->count(); ++i ) {
        filterList_->item( i )->setCheckState( Qt::Unchecked );
    }
    updatingList_ = false;
    emitCurrentSelection();
}

void FiltersPanel::emitCurrentSelection()
{
    QList<PredefinedFilter> selected;

    // Collect currently checked filter names for pin tracking
    QSet<QString> checkedNames;

    for ( int i = 0; i < filterList_->count(); ++i ) {
        const auto* item = filterList_->item( i );
        if ( item->checkState() == Qt::Checked ) {
            const auto name = item->data( Qt::UserRole ).toString();
            checkedNames.insert( name );

            // Find the matching filter in allFilters_
            for ( const auto& filter : allFilters_ ) {
                if ( filter.name == name ) {
                    selected.append( filter );
                    break;
                }
            }
        }
    }

    // Update pinned filters to match current checked state
    pinnedFilterNames_ = checkedNames;
    savePinnedFilters();

    Q_EMIT filtersChanged( selected );
}

void FiltersPanel::savePinnedFilters()
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.beginGroup( PinnedSettingsKey );
    settings.remove( "" );
    settings.setValue( "count", pinnedFilterNames_.size() );

    int index = 0;
    for ( const auto& name : pinnedFilterNames_ ) {
        settings.setValue( QString( "filter%1" ).arg( index ), name );
        index++;
    }

    settings.endGroup();
    settings.sync();
}

void FiltersPanel::loadPinnedFilters()
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.beginGroup( PinnedSettingsKey );

    const auto count = settings.value( "count", 0 ).toInt();
    for ( int i = 0; i < count; ++i ) {
        const auto name = settings.value( QString( "filter%1" ).arg( i ) ).toString();
        if ( !name.isEmpty() ) {
            pinnedFilterNames_.insert( name );
        }
    }

    settings.endGroup();
}
