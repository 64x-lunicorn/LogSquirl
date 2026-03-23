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

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>

#include "log.h"
#include "persistentinfo.h"

static constexpr const char* PinnedSettingsKey = "PinnedFilters";

// Composite key for pinning: "groupId/filterName".
static QString pinKey( const QString& groupId, const QString& filterName )
{
    return groupId + QStringLiteral( "/" ) + filterName;
}

FiltersPanel::FiltersPanel( QWidget* parent )
    : QWidget( parent )
{
    auto* mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 6, 6, 6, 6 );

    // Search box
    searchBox_ = new QLineEdit( this );
    searchBox_->setPlaceholderText( tr( "Search filters..." ) );
    searchBox_->setClearButtonEnabled( true );
    mainLayout->addWidget( searchBox_ );

    // Tree widget with groups
    filterTree_ = new QTreeWidget( this );
    filterTree_->setHeaderHidden( true );
    filterTree_->setRootIsDecorated( true );

    applyCurrentPalette();

    mainLayout->addWidget( filterTree_ );

    // Buttons row
    auto* buttonsLayout = new QHBoxLayout;
    selectAllButton_ = new QPushButton( tr( "Select All" ), this );
    deselectAllButton_ = new QPushButton( tr( "Deselect All" ), this );
    editFiltersButton_ = new QPushButton( tr( "Edit..." ), this );
    buttonsLayout->addWidget( selectAllButton_ );
    buttonsLayout->addWidget( deselectAllButton_ );
    buttonsLayout->addStretch();
    buttonsLayout->addWidget( editFiltersButton_ );
    mainLayout->addLayout( buttonsLayout );

    // Debounce timer: when a group checkbox is toggled, Qt fires itemChanged
    // for every child individually. This timer coalesces those into one emission.
    debounceTimer_ = new QTimer( this );
    debounceTimer_->setSingleShot( true );
    debounceTimer_->setInterval( 0 );
    connect( debounceTimer_, &QTimer::timeout, this, &FiltersPanel::emitCurrentSelection );

    connect( searchBox_, &QLineEdit::textChanged, this, &FiltersPanel::onSearchTextChanged );
    connect( filterTree_, &QTreeWidget::itemChanged, this, &FiltersPanel::onItemChanged );
    connect( selectAllButton_, &QPushButton::clicked, this, &FiltersPanel::selectAll );
    connect( deselectAllButton_, &QPushButton::clicked, this, &FiltersPanel::deselectAll );
    connect( editFiltersButton_, &QPushButton::clicked, this,
             &FiltersPanel::editFiltersRequested );

    loadPinnedFilters();
    refreshFilters();
}

void FiltersPanel::showEvent( QShowEvent* event )
{
    QWidget::showEvent( event );
    refreshFilters();
}

void FiltersPanel::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::ApplicationPaletteChange
         || event->type() == QEvent::PaletteChange ) {
        applyCurrentPalette();
    }
    QWidget::changeEvent( event );
}

void FiltersPanel::refreshFilters()
{
    allFilterSets_ = PredefinedFiltersCollection::getSynced().filterSets();
    populateTree( allFilterSets_ );
}

void FiltersPanel::populateTree( const QList<PredefinedFilterSet>& sets )
{
    updatingTree_ = true;
    filterTree_->clear();

    const auto searchText = searchBox_ ? searchBox_->text().trimmed() : QString{};

    for ( const auto& set : sets ) {
        auto* groupItem = new QTreeWidgetItem( filterTree_ );
        groupItem->setText( 0, set.name() );
        groupItem->setFlags( groupItem->flags() | Qt::ItemIsAutoTristate
                             | Qt::ItemIsUserCheckable );
        // Store group id for later retrieval.
        groupItem->setData( 0, Qt::UserRole, set.id() );

        bool anyChildVisible = false;

        for ( const auto& filter : set.filters() ) {
            // Apply search filter across group name, filter name, and pattern.
            if ( !searchText.isEmpty()
                 && !set.name().contains( searchText, Qt::CaseInsensitive )
                 && !filter.name.contains( searchText, Qt::CaseInsensitive )
                 && !filter.pattern.contains( searchText, Qt::CaseInsensitive ) ) {
                continue;
            }

            auto* childItem = new QTreeWidgetItem( groupItem );

            if ( filter.name != filter.pattern && !filter.name.isEmpty() ) {
                childItem->setText( 0, filter.name + QString::fromUtf8( "  \u2014  " )
                                           + filter.pattern );
            }
            else {
                childItem->setText( 0, filter.pattern );
            }
            childItem->setToolTip( 0, filter.pattern );
            childItem->setFlags( childItem->flags() | Qt::ItemIsUserCheckable );

            // Store filter name and group id for retrieval.
            childItem->setData( 0, Qt::UserRole, filter.name );
            childItem->setData( 0, Qt::UserRole + 1, set.id() );

            const bool isPinned = pinnedFilterKeys_.contains( pinKey( set.id(), filter.name ) );
            childItem->setCheckState( 0, isPinned ? Qt::Checked : Qt::Unchecked );

            anyChildVisible = true;
        }

        // Hide the group entirely if no children matched the search.
        if ( !anyChildVisible && !searchText.isEmpty() ) {
            delete filterTree_->takeTopLevelItem( filterTree_->indexOfTopLevelItem( groupItem ) );
            continue;
        }

        groupItem->setExpanded( true );
    }

    updatingTree_ = false;

    // Emit current selection if any pinned filters were auto-checked.
    emitCurrentSelection();
}

void FiltersPanel::onItemChanged( QTreeWidgetItem* item, int column )
{
    Q_UNUSED( column );
    Q_UNUSED( item );
    if ( updatingTree_ ) {
        return;
    }

    // Don't emit immediately — start/restart the debounce timer so that
    // a group toggle (which fires once per child) results in a single emission.
    debounceTimer_->start();
}

void FiltersPanel::onSearchTextChanged( const QString& text )
{
    Q_UNUSED( text );
    populateTree( allFilterSets_ );
}

void FiltersPanel::selectAll()
{
    updatingTree_ = true;
    for ( int g = 0; g < filterTree_->topLevelItemCount(); ++g ) {
        auto* groupItem = filterTree_->topLevelItem( g );
        for ( int c = 0; c < groupItem->childCount(); ++c ) {
            groupItem->child( c )->setCheckState( 0, Qt::Checked );
        }
    }
    updatingTree_ = false;
    emitCurrentSelection();
}

void FiltersPanel::deselectAll()
{
    updatingTree_ = true;
    for ( int g = 0; g < filterTree_->topLevelItemCount(); ++g ) {
        auto* groupItem = filterTree_->topLevelItem( g );
        for ( int c = 0; c < groupItem->childCount(); ++c ) {
            groupItem->child( c )->setCheckState( 0, Qt::Unchecked );
        }
    }
    updatingTree_ = false;
    emitCurrentSelection();
}

void FiltersPanel::emitCurrentSelection()
{
    QList<PredefinedFilter> selected;
    QSet<QString> checkedKeys;

    for ( int g = 0; g < filterTree_->topLevelItemCount(); ++g ) {
        const auto* groupItem = filterTree_->topLevelItem( g );
        const auto groupId = groupItem->data( 0, Qt::UserRole ).toString();

        for ( int c = 0; c < groupItem->childCount(); ++c ) {
            const auto* childItem = groupItem->child( c );
            if ( childItem->checkState( 0 ) == Qt::Checked ) {
                const auto filterName = childItem->data( 0, Qt::UserRole ).toString();
                checkedKeys.insert( pinKey( groupId, filterName ) );

                // Resolve the actual PredefinedFilter from allFilterSets_.
                for ( const auto& set : allFilterSets_ ) {
                    if ( set.id() == groupId ) {
                        for ( const auto& filter : set.filters() ) {
                            if ( filter.name == filterName ) {
                                selected.append( filter );
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    pinnedFilterKeys_ = checkedKeys;
    savePinnedFilters();

    Q_EMIT filtersChanged( selected );
}

void FiltersPanel::savePinnedFilters()
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.beginGroup( PinnedSettingsKey );
    settings.remove( "" );
    settings.setValue( "count", pinnedFilterKeys_.size() );

    int index = 0;
    for ( const auto& key : pinnedFilterKeys_ ) {
        settings.setValue( QString( "filter%1" ).arg( index ), key );
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
        const auto key = settings.value( QString( "filter%1" ).arg( i ) ).toString();
        if ( !key.isEmpty() ) {
            pinnedFilterKeys_.insert( key );
        }
    }

    settings.endGroup();
}

void FiltersPanel::applyCurrentPalette()
{
    auto pal = qApp->palette();

    // Fusion draws checkbox borders using Mid/Dark/Shadow palette roles.
    // The app's dark palette doesn't set these explicitly, so they
    // default to nearly-black — invisible on a dark background.
    // Derive visible border colours from the Text role so the
    // checkboxes always contrast against Base.
    const auto baseColor = pal.color( QPalette::Base );
    const bool isDark = baseColor.lightnessF() < 0.5f;
    if ( isDark ) {
        const auto borderColor = pal.color( QPalette::Text ).darker( 150 );
        pal.setColor( QPalette::Mid, borderColor );
        pal.setColor( QPalette::Dark, borderColor );
        pal.setColor( QPalette::Shadow, borderColor );
        // Light role is used for the inner highlight of the checkbox frame.
        pal.setColor( QPalette::Light, baseColor.lighter( 130 ) );
    }

    filterTree_->setPalette( pal );
    filterTree_->viewport()->setPalette( pal );
    searchBox_->setPalette( qApp->palette() );

    // Sync the widget-level style with the app style so that
    // Fusion (used by the dark theme) draws palette-aware checkboxes.
    filterTree_->setStyle( qApp->style() );
    filterTree_->viewport()->update();
}
