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

#include <memory>

#include <QIODevice>

// A read-only QIODevice over another one that gives out at most
// `budgetBytes` of it: a read or a seek that would pass the budget fails
// instead, and budgetExceeded() says so (#816).
//
// A compressed tar is parsed and extracted through one. The tar reader
// reads and skips along the decompressed stream, so however far a hostile
// archive would expand, it is decompressed no further than the budget --
// where a tar reader given a path copies the whole decompressed tarball
// into a temporary file of its own first, which nothing bounds.
//
// Seekable, whatever the inner device: the tar reader keeps its place by
// pos(), seeks forward past an entry's bytes while parsing and back to each
// entry it extracts. A random-access inner device (a file, KArchive's
// gz/bz2/xz device) seeks itself. A sequential one (zstd, lz4) decompresses
// forward only: a seek forward reads and discards the bytes in between, a
// seek back reopens it at its start and reads up to the position, as the
// gz/bz2/xz device does over its own stream (#838). The budget counts the
// stream position, not the bytes read, so going back and reading the same
// bytes again costs nothing of it.
class BudgetedDevice : public QIODevice {
public:
    BudgetedDevice( std::unique_ptr<QIODevice> inner, qint64 budgetBytes,
                    QObject* parent = nullptr );
    ~BudgetedDevice() override;

    BudgetedDevice( const BudgetedDevice& ) = delete;
    BudgetedDevice& operator=( const BudgetedDevice& ) = delete;

    bool open( OpenMode mode ) override;
    void close() override;
    bool isSequential() const override;
    bool atEnd() const override;
    qint64 bytesAvailable() const override;
    bool seek( qint64 pos ) override;

    // Whether a read or a seek was refused for passing the budget.
    bool budgetExceeded() const;

protected:
    qint64 readData( char* data, qint64 maxSize ) override;
    qint64 writeData( const char* data, qint64 maxSize ) override;

private:
    // Reads and discards `bytes` from the stream, within the budget.
    bool skip( qint64 bytes );

    std::unique_ptr<QIODevice> inner_;
    qint64 budgetBytes_;
    bool exceeded_ = false;
};
