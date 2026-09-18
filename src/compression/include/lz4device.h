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

#ifndef LOGSQUIRL_LZ4DEVICE_H
#define LOGSQUIRL_LZ4DEVICE_H

#include <QFile>
#include <QIODevice>

#include <memory>
#include <vector>

struct LZ4F_dctx_s;

/// Read-only sequential QIODevice that decompresses a .lz4 file via liblz4 frame API.
///
/// A .lz4 file may hold several frames one after another (block-streamed
/// output, concatenated files); the device decompresses all of them. A
/// truncated or corrupt file makes read() fail with -1 instead of ending the
/// stream early.
class Lz4Device : public QIODevice {
public:
    explicit Lz4Device( const QString& filePath, QObject* parent = nullptr );
    ~Lz4Device() override;

    Lz4Device( const Lz4Device& ) = delete;
    Lz4Device& operator=( const Lz4Device& ) = delete;

    bool open( OpenMode mode ) override;
    void close() override;
    bool isSequential() const override;
    bool atEnd() const override;
    qint64 bytesAvailable() const override;

protected:
    qint64 readData( char* data, qint64 maxSize ) override;
    qint64 writeData( const char* data, qint64 maxSize ) override;

private:
    QString filePath_;
    QFile file_;

    LZ4F_dctx_s* dctx_ = nullptr;

    std::vector<char> inBuf_;
    std::size_t inPos_ = 0;
    std::size_t inSize_ = 0;

    bool fileExhausted_ = false;
    // A frame has been started but not yet fully decoded and flushed.
    bool frameInProgress_ = false;
    bool finished_ = false;
    bool failed_ = false;

    qint64 fail( const QString& reason, std::size_t decompressedBytes );
};

#endif // LOGSQUIRL_LZ4DEVICE_H
