/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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
#ifndef LOGSQUIRL_DECOMPRESSOR_H
#define LOGSQUIRL_DECOMPRESSOR_H

#include <QFile>
#include <QFuture>
#include <QFutureWatcher>

#include "atomicflag.h"
#include "decompressionfailure.h"

enum class DecompressAction { None, Extract, Decompress };

// Decompresses a compressed Log File into a file, or extracts an archive
// into a folder, on a thread of its own. Either writes at most
// `budgetBytes` -- for an archive, over all its entries together -- and
// stops with DecompressionFailure::BudgetExceeded where it would write
// more (#816). The budget is a setting the caller hands in: this library
// does not know the settings store.
class Decompressor : public QObject {
    Q_OBJECT
public:
    explicit Decompressor( qint64 budgetBytes, QObject* parent = nullptr );

    bool decompress( const QString& path, QFile* outputFile, AtomicFlag& interrupt );
    bool extract( const QString& archiveFilePath, const QString& destination,
                  AtomicFlag& interrupt );

    bool waitForResult();

    // Why the decompression or extraction started last did not finish:
    // None when it did. Waits for it like waitForResult().
    DecompressionFailure failure();

    static DecompressAction action( const QString& archiveFilePath );

Q_SIGNALS:
    void finished( bool );

private:
    qint64 budgetBytes_;
    QFuture<DecompressionFailure> future_;
    QFutureWatcher<DecompressionFailure> watcher_;
};

#endif // LOGSQUIRL_DECOMPRESSOR_H
