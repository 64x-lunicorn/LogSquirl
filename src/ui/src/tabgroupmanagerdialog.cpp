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

#include "tabgroupmanagerdialog.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "tabgroupinfo.h"

TabGroupManagerDialog::TabGroupManagerDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Manage Tab Groups" ) );
    setMinimumSize( 450, 300 );

    auto* mainLayout = new QVBoxLayout( this );

    // -- Table --
    table_ = new QTableWidget( this );
    table_->setColumnCount( 3 );
    table_->setHorizontalHeaderLabels( { tr( "Color" ), tr( "Name" ), tr( "Tabs" ) } );
    table_->setSelectionBehavior( QAbstractItemView::SelectRows );
    table_->setSelectionMode( QAbstractItemView::SingleSelection );
    table_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection( false );
    table_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
    table_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );
    table_->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::ResizeToContents );
    mainLayout->addWidget( table_ );

    // -- Button row --
    auto* buttonLayout = new QHBoxLayout();
    renameButton_ = new QPushButton( tr( "Rename…" ), this );
    changeColorButton_ = new QPushButton( tr( "Change Color…" ), this );
    deleteButton_ = new QPushButton( tr( "Delete" ), this );
    auto* closeButton = new QPushButton( tr( "Close" ), this );

    buttonLayout->addWidget( renameButton_ );
    buttonLayout->addWidget( changeColorButton_ );
    buttonLayout->addWidget( deleteButton_ );
    buttonLayout->addStretch();
    buttonLayout->addWidget( closeButton );
    mainLayout->addLayout( buttonLayout );

    // -- Connections --
    connect( renameButton_, &QPushButton::clicked, this,
             &TabGroupManagerDialog::renameSelectedGroup );
    connect( changeColorButton_, &QPushButton::clicked, this,
             &TabGroupManagerDialog::changeSelectedGroupColor );
    connect( deleteButton_, &QPushButton::clicked, this,
             &TabGroupManagerDialog::deleteSelectedGroup );
    connect( closeButton, &QPushButton::clicked, this, &QDialog::accept );

    connect( table_, &QTableWidget::itemSelectionChanged, this,
             &TabGroupManagerDialog::updateButtonStates );

    populateTable();
    updateButtonStates();
}

void TabGroupManagerDialog::populateTable()
{
    table_->setRowCount( 0 );

    const auto& groups = TabGroupInfo::getSynced().groups();
    table_->setRowCount( static_cast<int>( groups.size() ) );

    for ( int row = 0; row < static_cast<int>( groups.size() ); ++row ) {
        const auto& group = groups[ static_cast<size_t>( row ) ];

        // Color swatch
        auto* colorItem = new QTableWidgetItem();
        colorItem->setBackground( group.color );
        colorItem->setData( Qt::UserRole, group.id );
        colorItem->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEnabled );
        table_->setItem( row, 0, colorItem );

        // Group name
        auto* nameItem = new QTableWidgetItem( group.name );
        nameItem->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEnabled );
        table_->setItem( row, 1, nameItem );

        // Tab count
        auto* countItem
            = new QTableWidgetItem( QString::number( group.tabPaths.size() ) );
        countItem->setTextAlignment( Qt::AlignCenter );
        countItem->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEnabled );
        table_->setItem( row, 2, countItem );
    }
}

QString TabGroupManagerDialog::selectedGroupId() const
{
    const auto selected = table_->selectionModel()->selectedRows();
    if ( selected.isEmpty() ) {
        return {};
    }
    return table_->item( selected.first().row(), 0 )->data( Qt::UserRole ).toString();
}

void TabGroupManagerDialog::updateButtonStates()
{
    const bool hasSelection = !selectedGroupId().isEmpty();
    renameButton_->setEnabled( hasSelection );
    changeColorButton_->setEnabled( hasSelection );
    deleteButton_->setEnabled( hasSelection );
}

void TabGroupManagerDialog::renameSelectedGroup()
{
    const auto groupId = selectedGroupId();
    if ( groupId.isEmpty() ) {
        return;
    }

    const auto row = table_->selectionModel()->selectedRows().first().row();
    const auto currentName = table_->item( row, 1 )->text();

    bool ok = false;
    const auto newName = QInputDialog::getText( this, tr( "Rename Group" ), tr( "Group name:" ),
                                                QLineEdit::Normal, currentName, &ok );
    if ( ok && !newName.isEmpty() ) {
        TabGroupInfo::getSynced().renameGroup( groupId, newName ).save();
        populateTable();
    }
}

void TabGroupManagerDialog::changeSelectedGroupColor()
{
    const auto groupId = selectedGroupId();
    if ( groupId.isEmpty() ) {
        return;
    }

    const auto row = table_->selectionModel()->selectedRows().first().row();
    const auto currentColor = table_->item( row, 0 )->background().color();

    const auto color = QColorDialog::getColor( currentColor, this, tr( "Group Color" ) );
    if ( color.isValid() ) {
        TabGroupInfo::getSynced().setGroupColor( groupId, color ).save();
        populateTable();
    }
}

void TabGroupManagerDialog::deleteSelectedGroup()
{
    const auto groupId = selectedGroupId();
    if ( groupId.isEmpty() ) {
        return;
    }

    const auto row = table_->selectionModel()->selectedRows().first().row();
    const auto name = table_->item( row, 1 )->text();

    const auto answer = QMessageBox::question(
        this, tr( "Delete Group" ),
        tr( "Delete group \"%1\"? Tabs will be ungrouped." ).arg( name ),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

    if ( answer == QMessageBox::Yes ) {
        TabGroupInfo::getSynced().removeGroup( groupId ).save();
        populateTable();
        updateButtonStates();
    }
}
