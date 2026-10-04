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

#include "filetypechoices.h"

#include <QCoreApplication>
#include <QTreeWidget>

#include "filetypes.h"

namespace FileTypeChoices {

void fill( QTreeWidget& tree )
{
    tree.clear();
    for ( const auto group : { FileType::Group::Logs, FileType::Group::Optional } ) {
        auto* groupItem = new QTreeWidgetItem( &tree );
        groupItem->setText( 0, FileTypes::groupTitle( group ) );
        groupItem->setFlags( Qt::ItemIsEnabled );
        groupItem->setFirstColumnSpanned( true );
        auto groupFont = groupItem->font( 0 );
        groupFont.setBold( true );
        groupItem->setFont( 0, groupFont );

        for ( const auto& type : FileTypes::choices() ) {
            if ( type.group != group ) {
                continue;
            }
            auto* item = new QTreeWidgetItem( groupItem );
            item->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );
            item->setData( 0, Qt::UserRole, type.id );
            item->setCheckState( 0, Qt::Unchecked );
            item->setText( 0, type.shownAs );
            item->setText( 1, FileTypes::label( type ) );
        }
    }
    tree.expandAll();
    tree.resizeColumnToContents( 0 );
    tree.resizeColumnToContents( 1 );
}

QTreeWidgetItem* row( const QTreeWidget& tree, const QString& id )
{
    for ( int group = 0; group < tree.topLevelItemCount(); ++group ) {
        auto* groupItem = tree.topLevelItem( group );
        for ( int index = 0; index < groupItem->childCount(); ++index ) {
            if ( groupItem->child( index )->data( 0, Qt::UserRole ).toString() == id ) {
                return groupItem->child( index );
            }
        }
    }
    return nullptr;
}

QStringList checkedIds( const QTreeWidget& tree )
{
    QStringList ids;
    for ( const auto& type : FileTypes::choices() ) {
        if ( const auto* item = row( tree, type.id );
             item != nullptr && item->checkState( 0 ) == Qt::Checked ) {
            ids << type.id;
        }
    }
    return ids;
}

// The words are the File Associations page's, under its context, which the
// translations have them in.
StateLook lookOf( FileAssociationState state )
{
    switch ( state ) {
    case FileAssociationState::Default:
        return { QStyle::SP_DialogApplyButton,
                 QCoreApplication::translate( "OptionsDialog", "Default" ),
                 QCoreApplication::translate( "OptionsDialog", "LogSquirl opens these files." ) };
    case FileAssociationState::Registered:
        return { QStyle::SP_MessageBoxInformation,
                 QCoreApplication::translate( "OptionsDialog", "Registered" ),
                 QCoreApplication::translate( "OptionsDialog",
                                              "LogSquirl is offered for these files, but another "
                                              "application opens them." ) };
    case FileAssociationState::NotRegistered:
        return { QStyle::SP_DialogNoButton,
                 QCoreApplication::translate( "OptionsDialog", "Not registered" ),
                 QCoreApplication::translate( "OptionsDialog",
                                              "LogSquirl is not offered for these files." ) };
    }
    return { QStyle::SP_MessageBoxInformation, {}, {} };
}

void showState( QTreeWidgetItem& row, int column, FileAssociationState state,
                const QWidget& widget )
{
    const auto look = lookOf( state );
    row.setIcon( column, widget.style()->standardIcon( look.icon, nullptr, &widget ) );
    row.setText( column, look.text );
    row.setToolTip( column, look.toolTip );
}

} // namespace FileTypeChoices
