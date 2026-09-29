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

#pragma once

#include <QByteArray>
#include <QString>

#include <functional>
#include <optional>
#include <ostream>
#include <vector>

// What a secondary instance hands over to the primary instance (#302, #623):
// the Log Files given on its command line, and the spool file it writes what
// arrives on its standard input to, when it was given "-".
struct HandOver {
    std::vector<QString> files;
    // Empty when standard input is not handed over.
    QString standardInputSpool;
};

// The message a secondary instance sends: a CBOR map with "version", "files"
// and, only when standard input is handed over, "stdinSpool". A primary
// instance from before #623 ignores the key it does not know.
QByteArray handOverMessage( const HandOver& handOver, const QString& version );

// The hand-over a message carries; nothing when it is no hand-over message or
// comes from an instance of another version than `version`.
std::optional<HandOver> readHandOverMessage( const QByteArray& message, const QString& version );

// Whether `path` names a spool file of standard input as a secondary instance
// makes it: a file of its own in a folder of its own in the temporary folder.
// Only such a file is removed with its tab by the primary instance.
bool isStandardInputSpool( const QString& path );

// Sends a message to the primary instance; whether it was delivered.
using SendToPrimaryInstance = std::function<bool( const QByteArray& message )>;

// What a secondary instance given "-" does (#623): it creates the spool file
// of standard input, hands it over with `files` in one message, and then
// copies what arrives on `fd` into the spool file until its writer closes it.
// The primary instance owns the file from the hand-over on: it stays when this
// returns. When the message cannot be delivered, says so on `errors`, removes
// the spool file again and returns EXIT_FAILURE; EXIT_SUCCESS once `fd` is
// closed otherwise.
int handOverStandardInput( const std::vector<QString>& files, int fd, const QString& version,
                           const SendToPrimaryInstance& send, std::ostream& errors );
