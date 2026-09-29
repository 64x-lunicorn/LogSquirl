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

#include "commandoutputdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

CommandOutputDialog::CommandOutputDialog( std::vector<RecentCommand> recentCommands,
                                          QWidget* parent )
    : QDialog( parent )
    , recentCommands_( std::move( recentCommands ) )
    , commandBox_( new QComboBox( this ) )
    , workingFolderEdit_( new QLineEdit( this ) )
    , standardErrorBox_( new QCheckBox( tr( "Include standard error" ), this ) )
{
    setWindowTitle( tr( "Open Command Output" ) );

    commandBox_->setEditable( true );
    commandBox_->setInsertPolicy( QComboBox::NoInsert );
    commandBox_->setMinimumContentsLength( 50 );
    commandBox_->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    commandBox_->lineEdit()->setPlaceholderText( tr( "e.g. journalctl -f" ) );
    for ( const auto& recent : recentCommands_ ) {
        commandBox_->addItem( recent.commandLine );
    }

    workingFolderEdit_->setPlaceholderText( QDir::toNativeSeparators( QDir::homePath() ) );
    auto* browseButton = new QPushButton( tr( "Browse..." ), this );
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget( workingFolderEdit_, 1 );
    folderRow->addWidget( browseButton );

    standardErrorBox_->setChecked( true );

    auto* form = new QFormLayout;
    form->addRow( tr( "Command:" ), commandBox_ );
    form->addRow( tr( "Working folder:" ), folderRow );
    form->addRow( QString{}, standardErrorBox_ );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Open | QDialogButtonBox::Cancel, this );
    openButton_ = buttons->button( QDialogButtonBox::Open );

    auto* layout = new QVBoxLayout( this );
    layout->addLayout( form );
    layout->addWidget( buttons );

    connect( commandBox_, &QComboBox::activated, this, &CommandOutputDialog::showRecentCommand );
    connect( commandBox_, &QComboBox::editTextChanged, this,
             &CommandOutputDialog::updateOpenButton );
    connect( browseButton, &QPushButton::clicked, this, &CommandOutputDialog::chooseWorkingFolder );
    connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );

    if ( recentCommands_.empty() ) {
        commandBox_->setEditText( {} );
    }
    else {
        commandBox_->setCurrentIndex( 0 );
        showRecentCommand( 0 );
    }
    updateOpenButton();
}

RecentCommand CommandOutputDialog::command() const
{
    return RecentCommand{ commandBox_->currentText().trimmed(),
                          QDir::fromNativeSeparators( workingFolderEdit_->text().trimmed() ),
                          standardErrorBox_->isChecked() };
}

void CommandOutputDialog::showRecentCommand( int index )
{
    if ( index < 0 || static_cast<size_t>( index ) >= recentCommands_.size() ) {
        return;
    }
    const auto& recent = recentCommands_[ static_cast<size_t>( index ) ];
    commandBox_->setEditText( recent.commandLine );
    workingFolderEdit_->setText( QDir::toNativeSeparators( recent.workingFolder ) );
    standardErrorBox_->setChecked( recent.includeStandardError );
}

void CommandOutputDialog::chooseWorkingFolder()
{
    const auto start
        = workingFolderEdit_->text().isEmpty() ? QDir::homePath() : workingFolderEdit_->text();
    const auto folder = QFileDialog::getExistingDirectory( this, tr( "Working folder" ), start );
    if ( !folder.isEmpty() ) {
        workingFolderEdit_->setText( QDir::toNativeSeparators( folder ) );
    }
}

void CommandOutputDialog::updateOpenButton()
{
    openButton_->setEnabled( !commandBox_->currentText().trimmed().isEmpty() );
}
