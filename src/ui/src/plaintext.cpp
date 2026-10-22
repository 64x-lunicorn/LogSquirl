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

#include "plaintext.h"

#include <QTextDocument>

namespace logsquirl::plaintext {

QString toolTip( const QString& text )
{
    // Escaped only where QToolTip would render markup: a text it shows as it
    // is stays one, so it keeps its font and never wraps (#830).
    return Qt::mightBeRichText( text ) ? Qt::convertFromPlainText( text, Qt::WhiteSpacePre ) : text;
}

namespace {

QMessageBox::StandardButton show( QMessageBox::Icon icon, QWidget* parent, const QString& title,
                                  const QString& text, QMessageBox::StandardButtons buttons,
                                  QMessageBox::StandardButton defaultButton )
{
    // As QMessageBox's own static functions do it, with the format set.
    QMessageBox box( icon, title, text, QMessageBox::NoButton, parent );
    box.setTextFormat( Qt::PlainText );
    box.setStandardButtons( buttons );
    box.setDefaultButton( defaultButton );
    if ( box.exec() == -1 ) {
        return QMessageBox::Cancel;
    }
    return box.standardButton( box.clickedButton() );
}

} // namespace

QMessageBox::StandardButton question( QWidget* parent, const QString& title, const QString& text,
                                      QMessageBox::StandardButtons buttons,
                                      QMessageBox::StandardButton defaultButton )
{
    return show( QMessageBox::Question, parent, title, text, buttons, defaultButton );
}

QMessageBox::StandardButton information( QWidget* parent, const QString& title, const QString& text,
                                         QMessageBox::StandardButtons buttons,
                                         QMessageBox::StandardButton defaultButton )
{
    return show( QMessageBox::Information, parent, title, text, buttons, defaultButton );
}

QMessageBox::StandardButton warning( QWidget* parent, const QString& title, const QString& text,
                                     QMessageBox::StandardButtons buttons,
                                     QMessageBox::StandardButton defaultButton )
{
    return show( QMessageBox::Warning, parent, title, text, buttons, defaultButton );
}

QMessageBox::StandardButton critical( QWidget* parent, const QString& title, const QString& text,
                                      QMessageBox::StandardButtons buttons,
                                      QMessageBox::StandardButton defaultButton )
{
    return show( QMessageBox::Critical, parent, title, text, buttons, defaultButton );
}

} // namespace logsquirl::plaintext
