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

#include "soloselection.h"

#include <QAction>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <utility>

#include "plaintext.h"

SoloSelection::SoloSelection( QTreeWidget* tree, UnitKey unitKey )
    : QObject( tree )
    , tree_( tree )
    , unitKey_( std::move( unitKey ) )
{
    tree_->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( tree_, &QTreeWidget::customContextMenuRequested, this,
             &SoloSelection::showContextMenu );
}

void SoloSelection::forEachUnit(
    const std::function<void( QTreeWidgetItem*, const QString& )>& visit ) const
{
    for ( int g = 0; g < tree_->topLevelItemCount(); ++g ) {
        auto* groupItem = tree_->topLevelItem( g );
        const auto groupKey = unitKey_( groupItem );
        if ( !groupKey.isEmpty() ) {
            visit( groupItem, groupKey );
        }
        for ( int c = 0; c < groupItem->childCount(); ++c ) {
            auto* item = groupItem->child( c );
            const auto key = unitKey_( item );
            if ( !key.isEmpty() ) {
                visit( item, key );
            }
        }
    }
}

void SoloSelection::showOnly( QTreeWidgetItem* item )
{
    if ( item == nullptr ) {
        return;
    }

    QHash<QString, bool> before;
    {
        const QSignalBlocker blocker( tree_ );
        forEachUnit( [ &before, item ]( QTreeWidgetItem* unit, const QString& key ) {
            before.insert( key, unit->checkState( 0 ) == Qt::Checked );
            // A unit of the item: the item itself, or one of its group's.
            const bool solo = unit == item || unit->parent() == item;
            unit->setCheckState( 0, solo ? Qt::Checked : Qt::Unchecked );
        } );
    }
    checksBefore_ = std::move( before );
    canRestore_ = true;

    Q_EMIT checksChanged();
}

bool SoloSelection::canRestore() const
{
    return canRestore_;
}

void SoloSelection::restore()
{
    if ( !canRestore_ ) {
        return;
    }
    {
        const QSignalBlocker blocker( tree_ );
        forEachUnit( [ this ]( QTreeWidgetItem* unit, const QString& key ) {
            const auto it = checksBefore_.constFind( key );
            if ( it != checksBefore_.constEnd() ) {
                unit->setCheckState( 0, it.value() ? Qt::Checked : Qt::Unchecked );
            }
        } );
    }
    checksBefore_.clear();
    canRestore_ = false;

    Q_EMIT checksChanged();
}

std::unique_ptr<QMenu> SoloSelection::contextMenu( QTreeWidgetItem* item )
{
    auto menu = std::make_unique<QMenu>( tree_ );
    if ( item != nullptr ) {
        auto* showOnlyAction = menu->addAction( tr( "Show only this" ) );
        // The tree may be built again while the menu is open: the item is
        // followed by its index.
        const QPersistentModelIndex index( tree_->indexFromItem( item ) );
        connect( showOnlyAction, &QAction::triggered, this, [ this, index ] {
            if ( index.isValid() ) {
                showOnly( tree_->itemFromIndex( index ) );
            }
        } );
    }
    auto* restoreAction = menu->addAction( tr( "Restore previous selection" ) );
    restoreAction->setEnabled( canRestore() );
    connect( restoreAction, &QAction::triggered, this, &SoloSelection::restore );
    return menu;
}

void SoloSelection::showContextMenu( const QPoint& position )
{
    const auto menu = contextMenu( tree_->itemAt( position ) );
    menu->exec( tree_->viewport()->mapToGlobal( position ) );
}

QString SoloSelection::doubleClickHint()
{
    return tr( "Double-click: show only this" );
}

QString SoloSelection::toolTipWithHint( const QString& text )
{
    return logsquirl::plaintext::toolTip( text + QStringLiteral( "\n" ) + doubleClickHint() );
}
