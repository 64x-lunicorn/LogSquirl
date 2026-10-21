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

#ifndef LOGSQUIRL_PLAINTEXT_H
#define LOGSQUIRL_PLAINTEXT_H

#include <QMessageBox>
#include <QString>

class QWidget;

// Text that is not the application's own -- a file name, a Log Line, a Named
// Value, a plugin catalog string -- shown as it is: markup in it reads
// literally and never changes the layout, hides content or inserts an image
// (#830). A QLabel shows such text with Qt::PlainText set; these are for the
// places that take no text format.
namespace logsquirl::plaintext {

// The text for a tool tip that shows `text` literally. QToolTip renders a
// text that might be rich text as such: that text is escaped, any other is
// shown as it is.
QString toolTip( const QString& text );

// QMessageBox::question and the others, with the text shown as plain text.
QMessageBox::StandardButton
question( QWidget* parent, const QString& title, const QString& text,
          QMessageBox::StandardButtons buttons
          = QMessageBox::StandardButtons( QMessageBox::Yes | QMessageBox::No ),
          QMessageBox::StandardButton defaultButton = QMessageBox::NoButton );
QMessageBox::StandardButton information( QWidget* parent, const QString& title, const QString& text,
                                         QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                         QMessageBox::StandardButton defaultButton
                                         = QMessageBox::NoButton );
QMessageBox::StandardButton warning( QWidget* parent, const QString& title, const QString& text,
                                     QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                     QMessageBox::StandardButton defaultButton
                                     = QMessageBox::NoButton );
QMessageBox::StandardButton critical( QWidget* parent, const QString& title, const QString& text,
                                      QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                      QMessageBox::StandardButton defaultButton
                                      = QMessageBox::NoButton );

} // namespace logsquirl::plaintext

#endif // LOGSQUIRL_PLAINTEXT_H
