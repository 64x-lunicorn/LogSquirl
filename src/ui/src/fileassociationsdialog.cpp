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

#include "fileassociationsdialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "configuration.h"
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

} // namespace FileTypeChoices

FileAssociationChoice fileAssociationChoice( const Configuration& config )
{
    FileAssociationChoice choice;
    choice.ask = config.askForFileAssociations();
    choice.chosen = config.chosenFileAssociations();
    return choice;
}

void keepFileAssociationChoice( Configuration& config, const FileAssociationChoice& choice )
{
    config.setAskForFileAssociations( choice.ask );
    config.setChosenFileAssociations( choice.chosen );
    config.save();
}

FirstStartFileAssociationsDialog::FirstStartFileAssociationsDialog(
    FileAssociations& fileAssociations, const QStringList& checks, QWidget* parent )
    : QDialog( parent )
    , fileAssociations_( &fileAssociations )
{
    setWindowTitle( tr( "File Associations" ) );

    auto* layout = new QVBoxLayout( this );

    auto* heading = new QLabel( tr( "Make LogSquirl the default app" ), this );
    auto headingFont = heading->font();
    headingFont.setBold( true );
    headingFont.setPointSizeF( headingFont.pointSizeF() * 1.2 );
    heading->setFont( headingFont );
    layout->addWidget( heading );

    tree_ = new QTreeWidget( this );
    tree_->setColumnCount( 2 );
    tree_->setHeaderHidden( true );
    tree_->setRootIsDecorated( false );
    tree_->setSelectionMode( QAbstractItemView::NoSelection );
    tree_->setFocusPolicy( Qt::NoFocus );
    FileTypeChoices::fill( *tree_ );
    for ( const auto& id : checks ) {
        if ( auto* item = FileTypeChoices::row( *tree_, id ) ) {
            item->setCheckState( 0, Qt::Checked );
        }
    }
    layout->addWidget( tree_ );

    // What applying leads to outside LogSquirl, and in the portable build of
    // Windows the warning about moving it.
    noteLabel_ = new QLabel( fileAssociations.applyNote(), this );
    noteLabel_->setWordWrap( true );
    noteLabel_->setVisible( !noteLabel_->text().isEmpty() );
    layout->addWidget( noteLabel_ );

    pageLabel_ = new QLabel(
        tr( "You can change this later on the File Associations page of the Options." ), this );
    pageLabel_->setWordWrap( true );
    layout->addWidget( pageLabel_ );

    auto* buttons = new QDialogButtonBox( this );
    laterButton_ = buttons->addButton( tr( "Later" ), QDialogButtonBox::RejectRole );
    dontAskAgainButton_
        = buttons->addButton( tr( "Don't ask again" ), QDialogButtonBox::DestructiveRole );
    applyButton_ = buttons->addButton( tr( "Apply" ), QDialogButtonBox::AcceptRole );
    applyButton_->setDefault( true );
    layout->addWidget( buttons );

    connect( laterButton_, &QPushButton::clicked, this, [ this ] { finish( Answer::Later ); } );
    connect( dontAskAgainButton_, &QPushButton::clicked, this,
             [ this ] { finish( Answer::DontAskAgain ); } );
    connect( applyButton_, &QPushButton::clicked, this, [ this ] {
        applyChoice();
        finish( Answer::Applied );
    } );
    // Closed some other way, it asks again at the next start.
    connect( this, &QDialog::rejected, this, [ this ] {
        if ( answer_ == Answer::None ) {
            answer_ = Answer::Later;
            Q_EMIT answered( answer_ );
        }
    } );
}

QStringList FirstStartFileAssociationsDialog::checkedIds() const
{
    return FileTypeChoices::checkedIds( *tree_ );
}

void FirstStartFileAssociationsDialog::updateChoice( FileAssociationChoice& choice ) const
{
    switch ( answer_ ) {
    case Answer::None:
    case Answer::Later:
        break;
    case Answer::DontAskAgain:
        choice.ask = false;
        break;
    case Answer::Applied:
        choice.apply( checkedIds() );
        break;
    }
}

void FirstStartFileAssociationsDialog::finish( Answer answer )
{
    answer_ = answer;
    Q_EMIT answered( answer );
    if ( answer == Answer::Applied ) {
        accept();
    }
    else {
        reject();
    }
}

// Makes LogSquirl the default for each checked type it is not the default for
// yet, and gives back each unchecked one it is, as the page does.
void FirstStartFileAssociationsDialog::applyChoice()
{
    if ( !fileAssociations_ || !fileAssociations_->isAvailable() ) {
        return;
    }
    const auto plan = FileAssociationPlan::of( fileAssociations_->states(), checkedIds() );
    if ( plan.isEmpty() ) {
        return;
    }
    const auto result = fileAssociations_->apply( plan.makeDefault, plan.release );
    if ( !result.succeeded() ) {
        QMessageBox::warning( this, tr( "File Associations" ), result.error );
    }
}
