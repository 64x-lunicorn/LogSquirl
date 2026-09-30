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

#include "savelinesdialog.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QGridLayout>

std::unique_ptr<QFileDialog> SaveLinesDialog::create( QWidget* parent, bool valueNamesShown )
{
    auto dialog = std::make_unique<QFileDialog>( parent, QStringLiteral( "Save content" ) );
    dialog->setAcceptMode( QFileDialog::AcceptSave );
    dialog->setOption( QFileDialog::DontUseNativeDialog, true );

    auto* withValueNames = new QCheckBox( tr( "With Value Names" ), dialog.get() );
    withValueNames->setObjectName( WithValueNamesName );
    withValueNames->setChecked( false );
    withValueNames->setEnabled( valueNamesShown );
    withValueNames->setToolTip(
        tr( "Save the Log Lines as they are shown, with the names of their values" ) );

    // Qt's file dialog lays itself out on a grid: the check goes below it.
    if ( auto* grid = qobject_cast<QGridLayout*>( dialog->layout() ); grid != nullptr ) {
        grid->addWidget( withValueNames, grid->rowCount(), 0, 1, -1 );
    }
    else if ( dialog->layout() != nullptr ) {
        dialog->layout()->addWidget( withValueNames );
    }

    return dialog;
}

bool SaveLinesDialog::withValueNames( const QFileDialog& dialog )
{
    const auto* check = dialog.findChild<QCheckBox*>( WithValueNamesName );
    return check != nullptr && check->isEnabled() && check->isChecked();
}
