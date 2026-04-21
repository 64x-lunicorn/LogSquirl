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

#ifndef LOGSQUIRL_ZSTDDEVICE_H
#define LOGSQUIRL_ZSTDDEVICE_H

#include <QFile>
#include <QIODevice>

#include <memory>
#include <vector>

struct ZSTD_DCtx_s;

/// Read-only sequential QIODevice that decompresses a .zst file via libzstd.
class ZstdDevice : public QIODevice {
  public:
    explicit ZstdDevice( const QString& filePath, QObject* parent = nullptr );
    ~ZstdDevice() override;

    ZstdDevice( const ZstdDevice& ) = delete;
    ZstdDevice& operator=( const ZstdDevice& ) = delete;

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

    ZSTD_DCtx_s* dctx_ = nullptr;

    std::vector<char> inBuf_;
    std::size_t inPos_ = 0;
    std::size_t inSize_ = 0;

    bool fileExhausted_ = false;
    bool finished_ = false;
};

#endif // LOGSQUIRL_ZSTDDEVICE_H
