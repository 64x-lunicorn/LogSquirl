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

std::unique_ptr<QFileDialog> SaveLinesDialog::create( QWidget* parent )
{
    auto dialog = std::make_unique<QFileDialog>( parent, tr( "Save content" ) );
    dialog->setAcceptMode( QFileDialog::AcceptSave );
    dialog->setOption( QFileDialog::DontUseNativeDialog, true );
    addWithValueNames( *dialog );
    return dialog;
}

void SaveLinesDialog::addWithValueNames( QDialog& dialog )
{
    // Qt's file dialog lays itself out on a grid: the check goes below it.
    // Laid out otherwise, the dialog is left as it is, without the check.
    auto* grid = qobject_cast<QGridLayout*>( dialog.layout() );
    if ( grid == nullptr ) {
        return;
    }

    auto* withValueNames = new QCheckBox( tr( "With Value Names" ), &dialog );
    withValueNames->setObjectName( WithValueNamesName );
    withValueNames->setChecked( false );
    withValueNames->setToolTip(
        tr( "Save the Log Lines as they are shown, with the names of their values" ) );
    grid->addWidget( withValueNames, grid->rowCount(), 0, 1, -1 );
}

bool SaveLinesDialog::withValueNames( const QDialog& dialog )
{
    const auto* check = dialog.findChild<QCheckBox*>( WithValueNamesName );
    return check != nullptr && check->isChecked();
}
