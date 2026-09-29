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

#include "mergedialog.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int PathRole = Qt::UserRole;
} // namespace

MergeDialog::MergeDialog( const std::vector<MergeCandidate>& candidates, QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Merge Log Files" ) );

    auto* explanation = new QLabel(
        tr( "Check the Log Files to merge and put them in the order they are written in." ), this );
    explanation->setWordWrap( true );

    list_ = new QListWidget( this );
    list_->setSelectionMode( QAbstractItemView::SingleSelection );
    list_->setDragDropMode( QAbstractItemView::InternalMove );
    list_->setDefaultDropAction( Qt::MoveAction );
    for ( const auto& candidate : candidates ) {
        auto* item = new QListWidgetItem( candidate.name, list_ );
        item->setToolTip( candidate.toolTip );
        item->setData( PathRole, candidate.path );
        item->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable
                        | Qt::ItemIsDragEnabled );
        item->setCheckState( Qt::Checked );
    }

    moveUpButton_ = new QPushButton( tr( "Move Up" ), this );
    moveDownButton_ = new QPushButton( tr( "Move Down" ), this );
    // The Return key merges, whatever button has the focus last.
    moveUpButton_->setAutoDefault( false );
    moveDownButton_->setAutoDefault( false );

    auto* moveButtons = new QVBoxLayout;
    moveButtons->addWidget( moveUpButton_ );
    moveButtons->addWidget( moveDownButton_ );
    moveButtons->addStretch();

    auto* listRow = new QHBoxLayout;
    listRow->addWidget( list_ );
    listRow->addLayout( moveButtons );

    dropDuplicates_ = new QCheckBox( tr( "Drop duplicate lines" ), this );
    dropDuplicates_->setToolTip( tr( "Leaves out a line identical to one already written, from "
                                     "any of the files." ) );

    auto* buttons = new QDialogButtonBox( this );
    mergeButton_ = buttons->addButton( tr( "Merge" ), QDialogButtonBox::AcceptRole );
    buttons->addButton( QDialogButtonBox::Cancel );
    mergeButton_->setDefault( true );

    auto* layout = new QVBoxLayout( this );
    layout->addWidget( explanation );
    layout->addLayout( listRow );
    layout->addWidget( dropDuplicates_ );
    layout->addWidget( buttons );

    connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    connect( moveUpButton_, &QPushButton::clicked, this, [ this ] { moveCurrentRow( -1 ); } );
    connect( moveDownButton_, &QPushButton::clicked, this, [ this ] { moveCurrentRow( 1 ); } );
    connect( list_, &QListWidget::currentRowChanged, this, &MergeDialog::updateButtons );
    connect( list_, &QListWidget::itemChanged, this, &MergeDialog::updateButtons );
    // A drag moves the rows in the model.
    connect( list_->model(), &QAbstractItemModel::rowsMoved, this, &MergeDialog::updateButtons );

    if ( list_->count() > 0 ) {
        list_->setCurrentRow( 0 );
    }
    list_->setFocus();
    updateButtons();
}

QStringList MergeDialog::checkedPaths() const
{
    QStringList paths;
    for ( int row = 0; row < list_->count(); ++row ) {
        const auto* item = list_->item( row );
        if ( item->checkState() == Qt::Checked ) {
            paths.append( item->data( PathRole ).toString() );
        }
    }
    return paths;
}

bool MergeDialog::dropsDuplicates() const
{
    return dropDuplicates_->isChecked();
}

void MergeDialog::moveCurrentRow( int by )
{
    const auto row = list_->currentRow();
    const auto target = row + by;
    if ( row < 0 || target < 0 || target >= list_->count() ) {
        return;
    }
    auto* item = list_->takeItem( row );
    list_->insertItem( target, item );
    list_->setCurrentRow( target );
    updateButtons();
}

void MergeDialog::updateButtons()
{
    const auto row = list_->currentRow();
    moveUpButton_->setEnabled( row > 0 );
    moveDownButton_->setEnabled( row >= 0 && row < list_->count() - 1 );
    mergeButton_->setEnabled( checkedPaths().size() >= 2 );
}
