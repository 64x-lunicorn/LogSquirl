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

#include "lostfileassociationshint.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

#include "filetypes.h"

namespace {

std::vector<FileType> typesOf( const QStringList& ids )
{
    std::vector<FileType> types;
    for ( const auto& type : FileTypes::choices() ) {
        if ( ids.contains( type.id ) ) {
            types.push_back( type );
        }
    }
    return types;
}

} // namespace

LostFileAssociationsHint::LostFileAssociationsHint( FileAssociations& fileAssociations,
                                                    const QStringList& lost,
                                                    const QString& movedFrom, QWidget* parent )
    : QWidget( parent )
    , fileAssociations_( &fileAssociations )
    , lost_( lost )
{
    QStringList shown;
    for ( const auto& type : typesOf( lost ) ) {
        shown << type.shownAs;
    }
    const auto types = shown.join( QStringLiteral( ", " ) );

    textLabel_ = new QLabel( this );
    if ( movedFrom.isEmpty() ) {
        textLabel_->setText( tr( "LogSquirl no longer opens %1 files." ).arg( types ) );
    }
    else {
        textLabel_->setText( tr( "The associations of %1 point at the old location of LogSquirl, "
                                 "%2." )
                                 .arg( types, QDir::toNativeSeparators( movedFrom ) ) );
    }
    textLabel_->setToolTip( tr( "You can choose the file types LogSquirl opens on the File "
                                "Associations page of the Options." ) );

    restoreButton_ = new QPushButton( tr( "Restore" ), this );
    restoreButton_->setToolTip( tr( "Make LogSquirl open these files again." ) );
    dismissButton_ = new QPushButton( tr( "Dismiss" ), this );
    dismissButton_->setToolTip( tr( "Do not tell again until you choose the file types again." ) );

    auto* layout = new QHBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->addWidget( textLabel_ );
    layout->addWidget( restoreButton_ );
    layout->addWidget( dismissButton_ );

    connect( restoreButton_, &QPushButton::clicked, this, &LostFileAssociationsHint::restore );
    connect( dismissButton_, &QPushButton::clicked, this, [ this ] {
        Q_EMIT dismissed( lost_ );
        Q_EMIT finished();
    } );
}

// Applies the choice again for the lost types: on Windows that leads to the
// Default apps page again, where the user confirms.
void LostFileAssociationsHint::restore()
{
    if ( fileAssociations_ && fileAssociations_->isAvailable() ) {
        const auto result = fileAssociations_->apply( typesOf( lost_ ), {} );
        if ( !result.succeeded() ) {
            QMessageBox::warning( this, tr( "File Associations" ), result.error );
        }
    }
    Q_EMIT finished();
}
