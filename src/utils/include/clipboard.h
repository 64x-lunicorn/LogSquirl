/*
 * Copyright (C) 2023 Anton Filimonov
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LOGSQUIRL_CLIPBOARD_H
#define LOGSQUIRL_CLIPBOARD_H

#include <exception>
#include <utility>

#include "log.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QTimer>

static inline void sendTextToClipboard( QString text, bool updateSelection = false )
{
    auto clipboard = QApplication::clipboard();
    if ( !clipboard ) {
        LOG_WARNING << "Unable to access the clipboard.";
        return;
    }

    try {
        auto* mime = new QMimeData;
        mime->setText( text );
        clipboard->setMimeData( mime, QClipboard::Clipboard );
        if ( updateSelection && clipboard->supportsSelection() ) {
            clipboard->setMimeData( mime, QClipboard::Selection );
        }
    } catch ( const std::exception& ex ) {
        LOG_ERROR << "Failed to send text to clipboard: " << ex.what();
    }
}

// Copy what a Presentation has selected, the one way every Presentation
// copies: a null character in a Log Line is copied as a space, a selection
// that cannot be read is logged and copies nothing, and nor does an empty one.
template <typename ReadSelection>
static inline void sendSelectionToClipboard( ReadSelection&& readSelection )
{
    try {
        QString text = readSelection();
        if ( text.isEmpty() ) {
            return;
        }
        text.replace( QChar::Null, QChar::Space );
        sendTextToClipboard( std::move( text ) );
    } catch ( const std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}
#endif
