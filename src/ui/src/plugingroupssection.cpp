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

#include "plugingroupssection.h"

#include <QBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>

PluginGroupsSection::PluginGroupsSection( QWidget* parent, QBoxLayout* layout,
                                          const QString& labelText )
    : QObject( parent )
    , label_( new QLabel( labelText, parent ) )
    , list_( new QListWidget( parent ) )
    , copyButton_( new QPushButton( tr( "Copy to my groups" ), parent ) )
{
    label_->setAlignment( Qt::AlignCenter );
    label_->setToolTip( tr( "Shipped by the enabled plugins: read-only, and gone when the plugin "
                            "is disabled." ) );
    list_->setObjectName( QStringLiteral( "pluginGroupList" ) );
    list_->setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Expanding );
    copyButton_->setObjectName( QStringLiteral( "pluginCopy" ) );
    copyButton_->setEnabled( false );
    layout->addWidget( label_ );
    layout->addWidget( list_ );
    layout->addWidget( copyButton_ );
}

void PluginGroupsSection::setNames( const QStringList& names )
{
    list_->clear();
    list_->addItems( names );
}

void PluginGroupsSection::updateButtons( bool pluginGroupSelected )
{
    copyButton_->setEnabled( pluginGroupSelected );
}
