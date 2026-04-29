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

#include <memory>

#include <QFileInfo>
#include <QMimeDatabase>
#include <QtConcurrent>

#include <k7zip.h>
#include <kcompressiondevice.h>
#include <ktar.h>
#include <kzip.h>

#include "log.h"

#include "decompressor.h"
#include "lz4device.h"
#include "zstddevice.h"

namespace {

enum class Archive { None, Zip7, Tar, Zip, Gz, Bz2, Xz, Zstd, Lz4 };

Archive archiveTypeByExtension( const QString& archiveFilePath )
{
    const auto info = QFileInfo( archiveFilePath );
    const auto extension = info.suffix().toLower();
    LOG_INFO << "Suffix is " << extension;

    if ( extension == "zip" ) {
        return Archive::Zip;
    }
    else if ( extension == "7z" ) {
        return Archive::Zip7;
    }
    else if ( extension == "tgz" || extension == "tbz2" || extension == "txz"
              || extension == "tzst" ) {
        return Archive::Tar;
    }

    if ( extension == "gz" || extension == "bz2" || extension == "xz" || extension == "lzma"
         || extension == "zst" || extension == "zstd" || extension == "lz4" ) {
        const auto completeSuffix = info.completeSuffix().toLower();
        if ( completeSuffix.contains( "tar." ) ) {
            return Archive::Tar;
        }
        else if ( extension == "gz" ) {
            return Archive::Gz;
        }
        else if ( extension == "bz2" ) {
            return Archive::Bz2;
        }
        else if ( extension == "xz" || extension == "lzma" ) {
            return Archive::Xz;
        }
        else if ( extension == "zst" || extension == "zstd" ) {
            return Archive::Zstd;
        }
        else if ( extension == "lz4" ) {
            return Archive::Lz4;
        }
    }

    return Archive::None;
}

Archive archiveType( const QString& archiveFilePath )
{
    QMimeDatabase mimeDb;
    const auto mime = mimeDb.mimeTypeForFile( archiveFilePath );

    if ( mime.inherits( "application/x-7z-compressed" ) ) {
        return Archive::Zip7;
    }

    if ( mime.inherits( "application/zip" ) ) {
        return Archive::Zip;
    }

    if ( mime.inherits( "application/x-tar" ) ) {
        return Archive::Tar;
    }

    auto mimeArchiveType = Archive::None;

    if ( mime.inherits( "application/x-gzip" ) ) {
        mimeArchiveType = Archive::Gz;
    }
    else if ( mime.inherits( "application/x-bzip" ) ) {
        mimeArchiveType = Archive::Bz2;
    }
    else if ( mime.inherits( "application/x-lzma" ) || mime.inherits( "application/x-xz" ) ) {
        mimeArchiveType = Archive::Xz;
    }
    else if ( mime.inherits( "application/zstd" ) || mime.inherits( "application/x-zstd" ) ) {
        mimeArchiveType = Archive::Zstd;
    }
    else if ( mime.inherits( "application/x-lz4" ) ) {
        mimeArchiveType = Archive::Lz4;
    }

    if ( mimeArchiveType == Archive::None ) {
        return archiveTypeByExtension( archiveFilePath );
    }

    const auto info = QFileInfo( archiveFilePath );
    const auto extension = info.suffix().toLower();
    const auto completeSuffix = info.completeSuffix().toLower();
    if ( completeSuffix.contains( "tar.", Qt::CaseInsensitive )
         || extension.endsWith( "tgz", Qt::CaseInsensitive )
         || extension.endsWith( "tbz", Qt::CaseInsensitive )
         || extension.endsWith( "tbz2", Qt::CaseInsensitive )
         || extension.endsWith( "txz", Qt::CaseInsensitive )
         || extension.endsWith( "tzst", Qt::CaseInsensitive ) ) {

        return Archive::Tar;
    }

    return mimeArchiveType;
}

std::shared_ptr<KArchive> makeExtractor( Archive archiveType, const QString& archiveFilePath )
{
    switch ( archiveType ) {
    case Archive::Zip:
        return std::make_shared<KZip>( archiveFilePath );
    case Archive::Zip7:
        return std::make_shared<K7Zip>( archiveFilePath );
    case Archive::Tar: {
        // KTar handles .tar.gz/.tar.bz2/.tar.xz natively, but not zstd/lz4.
        // For those, provide a pre-decompressing QIODevice to KTar.
        const auto info = QFileInfo( archiveFilePath );
        const auto completeSuffix = info.completeSuffix().toLower();
        const auto extension = info.suffix().toLower();

        QIODevice* decompDevice = nullptr;
        if ( completeSuffix.contains( "tar.zst" ) || completeSuffix.contains( "tar.zstd" )
             || extension == "tzst" ) {
            decompDevice = new ZstdDevice( archiveFilePath );
        }
        else if ( completeSuffix.contains( "tar.lz4" ) ) {
            decompDevice = new Lz4Device( archiveFilePath );
        }

        if ( decompDevice ) {
            // KTar does not own the device — use a custom deleter to clean up both.
            return std::shared_ptr<KTar>( new KTar( decompDevice ),
                                          [ decompDevice ]( KTar* tar ) {
                                              delete tar;
                                              delete decompDevice;
                                          } );
        }

        return std::make_shared<KTar>( archiveFilePath );
    }
    default:
        return {};
    }
}

/// Creates a QIODevice for streaming decompression. Returns KCompressionDevice
/// for gz/bz2/xz and custom wrappers for zstd/lz4.
std::shared_ptr<QIODevice> makeDecompressor( Archive archiveType,
                                             const QString& archiveFilePath )
{
    switch ( archiveType ) {
    case Archive::Gz:
        return std::make_shared<KCompressionDevice>( archiveFilePath,
                                                     KCompressionDevice::GZip );
    case Archive::Bz2:
        return std::make_shared<KCompressionDevice>( archiveFilePath,
                                                     KCompressionDevice::BZip2 );
    case Archive::Xz:
        return std::make_shared<KCompressionDevice>( archiveFilePath,
                                                     KCompressionDevice::Xz );
    case Archive::Zstd:
        return std::make_shared<ZstdDevice>( archiveFilePath );
    case Archive::Lz4:
        return std::make_shared<Lz4Device>( archiveFilePath );
    default:
        return {};
    }
}

bool doExtract( std::shared_ptr<KArchive> archive, const QString& archiveFilePath,
                const QString& destination, AtomicFlag& interrupt )
{
    if ( !archive->open( QIODevice::ReadOnly ) ) {
        LOG_WARNING << "Cannot open " << archiveFilePath;
        return false;
    }

    const KArchiveDirectory* root = archive->directory();

    if ( !root ) {
        LOG_WARNING << "Cannot open root directory" << archiveFilePath;
        archive->close();
        return false;
    }

    auto result = false;
    try {
        const auto recursive = true;
#ifdef LOGSQUIRL_KARCHIVE
        result = root->copyTo( destination, interrupt, recursive );
#else
        result = root->copyTo( destination, recursive );
#endif
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Exception during extract: " << e.what();
    }

    if ( interrupt ) {
        result = false;
        LOG_INFO << "Interrupted extract of " << archiveFilePath;
    }

    archive->close();
    return result;
}

bool doDecompress( std::shared_ptr<QIODevice> input, const QString& archiveFilePath,
                   QFile* outputFile, AtomicFlag& interrupt )
{
    if ( !input->open( QIODevice::ReadOnly ) ) {
        LOG_WARNING << "Cannot open " << archiveFilePath;
        return false;
    }

    bool success = true;
    try {
        while ( !input->atEnd() ) {
            if ( interrupt ) {
                success = false;
                LOG_INFO << "Interrupted decompress of " << archiveFilePath;
                break;
            }

            QByteArray data = input->read( 4 * 1024 * 1024 );
            if ( data.isEmpty() ) {
                // No progress while not at end — device is in a bad state.
                // Break to avoid an infinite loop.
                if ( !input->atEnd() ) {
                    LOG_ERROR << "Decompressor stalled (read returned 0 but not atEnd) for "
                              << archiveFilePath;
                    success = false;
                }
                break;
            }

            // Detect short writes (e.g. disk full, quota exceeded) — these
            // would otherwise produce a silently truncated output file.
            const auto writtenBytes = outputFile->write( data );
            if ( writtenBytes < 0 ) {
                LOG_ERROR << "Error decompressing " << archiveFilePath
                          << ": " << outputFile->errorString();
                success = false;
                break;
            }
            if ( writtenBytes != data.size() ) {
                LOG_ERROR << "Short write while decompressing " << archiveFilePath
                          << ": wrote " << writtenBytes << " of " << data.size()
                          << " bytes (" << outputFile->errorString() << ")";
                success = false;
                break;
            }
        }
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Exception during decompress: " << e.what();
    }

    input->close();
    outputFile->close();

    return success;
}

} // namespace

Decompressor::Decompressor( QObject* parent )
    : QObject( parent )
{
    connect( &watcher_, &QFutureWatcher<bool>::finished, [ this ]() {
        LOG_INFO << "Decompressor finished " << watcher_.result();
        Q_EMIT finished( watcher_.result() );
    } );
}

bool Decompressor::waitForResult()
{
    return watcher_.result();
}

DecompressAction Decompressor::action( const QString& archiveFilePath )
{
    const auto archive = archiveType( archiveFilePath );

    switch ( archive ) {
    case Archive::Zip:
    case Archive::Zip7:
    case Archive::Tar:
        return DecompressAction::Extract;
    case Archive::Gz:
    case Archive::Bz2:
    case Archive::Xz:
    case Archive::Zstd:
    case Archive::Lz4:
        return DecompressAction::Decompress;
    default:
        return DecompressAction::None;
    }
}

bool Decompressor::decompress( const QString& archiveFilePath, QFile* outputFile,
                               AtomicFlag& interrupt )
{
    auto decompressor = makeDecompressor( archiveType( archiveFilePath ), archiveFilePath );
    if ( !decompressor ) {
        LOG_WARNING << "Unsupported archive " << archiveFilePath.constData();
        return false;
    }

    future_ = QtConcurrent::run(
        [ input = std::move( decompressor ), archiveFilePath, outputFile, &interrupt ] {
            return doDecompress( input, archiveFilePath, outputFile, interrupt );
        } );
    watcher_.setFuture( future_ );

    return true;
}

bool Decompressor::extract( const QString& archiveFilePath, const QString& destination,
                            AtomicFlag& interrupt )
{
    auto archive = makeExtractor( archiveType( archiveFilePath ), archiveFilePath );
    if ( !archive ) {
        LOG_WARNING << "Unsupported archive " << archiveFilePath;
        return false;
    }

    // Open the archive

    future_ = QtConcurrent::run(
        [ ar = std::move( archive ), archiveFilePath, destination, &interrupt ] {
            return doExtract( ar, archiveFilePath, destination, interrupt );
        } );
    watcher_.setFuture( future_ );

    return true;
}
