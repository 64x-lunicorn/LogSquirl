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

#include "teamgroupssection.h"

#include <QBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>

TeamGroupsSection::TeamGroupsSection( QWidget* parent, QBoxLayout* layout, const QString& labelText,
                                      const QString& addText )
    : QObject( parent )
    , parent_( parent )
    , label_( new QLabel( labelText, parent ) )
    , list_( new QListWidget( parent ) )
    , addButton_( new QPushButton( addText, parent ) )
    , shareButton_( new QPushButton( tr( "Share with team" ), parent ) )
    , copyButton_( new QPushButton( tr( "Copy to my groups" ), parent ) )
    , deleteButton_( new QPushButton( tr( "Delete for the team" ), parent ) )
{
    label_->setAlignment( Qt::AlignCenter );
    label_->setToolTip(
        tr( "Shared through the Team Folder: they change when the team changes them." ) );
    list_->setObjectName( QStringLiteral( "teamGroupList" ) );
    list_->setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Expanding );
    layout->addWidget( label_ );
    layout->addWidget( list_ );

    addButton_->setObjectName( QStringLiteral( "teamAdd" ) );
    shareButton_->setObjectName( QStringLiteral( "teamShare" ) );
    shareButton_->setToolTip( tr( "Adds a Team copy of the selected group of your own." ) );
    copyButton_->setObjectName( QStringLiteral( "teamCopy" ) );
    deleteButton_->setObjectName( QStringLiteral( "teamDelete" ) );
    auto* buttons = new QGridLayout;
    buttons->addWidget( addButton_, 0, 0 );
    buttons->addWidget( shareButton_, 0, 1 );
    buttons->addWidget( copyButton_, 1, 0 );
    buttons->addWidget( deleteButton_, 1, 1 );
    layout->addLayout( buttons );
}

void TeamGroupsSection::setEditable( bool editable )
{
    addButton_->setVisible( editable );
    shareButton_->setVisible( editable );
    deleteButton_->setVisible( editable );
}

void TeamGroupsSection::setNames( const QStringList& names )
{
    list_->clear();
    list_->addItems( names );
}

void TeamGroupsSection::updateButtons( bool editable, bool ownSelected, bool teamSelected )
{
    shareButton_->setEnabled( editable && ownSelected );
    copyButton_->setEnabled( teamSelected );
    deleteButton_->setEnabled( editable && teamSelected );
}

bool TeamGroupsSection::confirmDeletion( const QString& title ) const
{
    return QMessageBox::question( parent_, title,
                                  tr( "This deletes the group for the whole team." ),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No )
           == QMessageBox::Yes;
}
