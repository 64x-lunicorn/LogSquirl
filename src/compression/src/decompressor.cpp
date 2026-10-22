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
#include <vector>

#include <QFileInfo>
#include <QMimeDatabase>
#include <QtConcurrent>

#include <k7zip.h>
#include <kcompressiondevice.h>
#include <ktar.h>
#include <kzip.h>

#include "log.h"

#include "archiveextraction.h"
#include "budgeteddevice.h"
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

// The compression a file is wrapped in, a compressed single file's or a
// compressed tar's, by its content first and its name second: None for a
// plain file, a plain tar, a zip or a 7z.
Archive compressionLayer( const QString& archiveFilePath )
{
    QMimeDatabase mimeDb;
    const auto mime = mimeDb.mimeTypeForFile( archiveFilePath );

    if ( mime.inherits( "application/x-gzip" ) ) {
        return Archive::Gz;
    }
    else if ( mime.inherits( "application/x-bzip" ) ) {
        return Archive::Bz2;
    }
    else if ( mime.inherits( "application/x-lzma" ) || mime.inherits( "application/x-xz" ) ) {
        return Archive::Xz;
    }
    else if ( mime.inherits( "application/zstd" ) || mime.inherits( "application/x-zstd" ) ) {
        return Archive::Zstd;
    }
    else if ( mime.inherits( "application/x-lz4" ) ) {
        return Archive::Lz4;
    }

    const auto extension = QFileInfo( archiveFilePath ).suffix().toLower();
    if ( extension == "gz" || extension == "tgz" ) {
        return Archive::Gz;
    }
    else if ( extension == "bz2" || extension == "tbz" || extension == "tbz2" ) {
        return Archive::Bz2;
    }
    else if ( extension == "xz" || extension == "lzma" || extension == "txz" ) {
        return Archive::Xz;
    }
    else if ( extension == "zst" || extension == "zstd" || extension == "tzst" ) {
        return Archive::Zstd;
    }
    else if ( extension == "lz4" ) {
        return Archive::Lz4;
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

    const auto mimeArchiveType = compressionLayer( archiveFilePath );
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

/// The decompressed stream of a file wrapped in `layer`: KCompressionDevice
/// for gz/bz2/xz, the custom devices for zstd/lz4, the file itself for None.
std::unique_ptr<QIODevice> makeStreamDevice( Archive layer, const QString& archiveFilePath )
{
    switch ( layer ) {
    case Archive::Gz:
        return std::make_unique<KCompressionDevice>( archiveFilePath, KCompressionDevice::GZip );
    case Archive::Bz2:
        return std::make_unique<KCompressionDevice>( archiveFilePath, KCompressionDevice::BZip2 );
    case Archive::Xz:
        return std::make_unique<KCompressionDevice>( archiveFilePath, KCompressionDevice::Xz );
    case Archive::Zstd:
        return std::make_unique<ZstdDevice>( archiveFilePath );
    case Archive::Lz4:
        return std::make_unique<Lz4Device>( archiveFilePath );
    default:
        return std::make_unique<QFile>( archiveFilePath );
    }
}

// An archive opened for reading, and the budgeted stream a tar is read
// through, which says whether the archive expanded past the budget.
struct ArchiveReader {
    std::shared_ptr<KArchive> archive;
    std::shared_ptr<BudgetedDevice> stream;
};

ArchiveReader makeExtractor( Archive archiveType, const QString& archiveFilePath,
                             qint64 budgetBytes )
{
    switch ( archiveType ) {
    case Archive::Zip:
        return { std::make_shared<KZip>( archiveFilePath ), {} };
    case Archive::Zip7:
        return { std::make_shared<K7Zip>( archiveFilePath ), {} };
    case Archive::Tar: {
        // KTar given a path copies the whole decompressed tarball into a
        // temporary file of its own before it reads a header, and nothing
        // bounds that copy. Given a device it reads the stream as it goes,
        // so the stream is handed over budgeted: the tar reader parses and
        // extracts no further than the budget (#816).
        auto stream = std::make_shared<BudgetedDevice>(
            makeStreamDevice( compressionLayer( archiveFilePath ), archiveFilePath ), budgetBytes );
        // KTar does not own the device; the deleter keeps it alive until the
        // KTar is gone.
        std::shared_ptr<KArchive> tar( new KTar( stream.get() ),
                                       [ stream ]( KArchive* archive ) { delete archive; } );
        return { std::move( tar ), std::move( stream ) };
    }
    default:
        return {};
    }
}

DecompressionFailure doExtract( const ArchiveReader& reader, const QString& archiveFilePath,
                                const QString& destination, qint64 budgetBytes,
                                AtomicFlag& interrupt )
{
    const auto streamExceeded
        = [ &reader ] { return reader.stream && reader.stream->budgetExceeded(); };

    if ( !reader.archive->open( QIODevice::ReadOnly ) ) {
        LOG_WARNING << "Cannot open " << archiveFilePath << ": " << reader.archive->errorString();
        return streamExceeded() ? DecompressionFailure::BudgetExceeded
                                : DecompressionFailure::Error;
    }

    const KArchiveDirectory* root = reader.archive->directory();

    if ( !root ) {
        LOG_WARNING << "Cannot open root directory" << archiveFilePath;
        reader.archive->close();
        return streamExceeded() ? DecompressionFailure::BudgetExceeded
                                : DecompressionFailure::Error;
    }

    auto result = DecompressionFailure::Error;
    try {
        // Never KArchive's own copy: it creates links and writes through
        // them (#815).
        result = extractArchiveDirectory( *root, destination, budgetBytes, interrupt );
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Exception during extract: " << e.what();
    }

    if ( interrupt ) {
        result = DecompressionFailure::Interrupted;
        LOG_INFO << "Interrupted extract of " << archiveFilePath;
    }
    else if ( result == DecompressionFailure::Error && streamExceeded() ) {
        // The tar stream ran out at the budget under an entry; the entry
        // then reads short.
        result = DecompressionFailure::BudgetExceeded;
    }
    if ( result == DecompressionFailure::BudgetExceeded ) {
        LOG_WARNING << "Extracting " << archiveFilePath
                    << " stopped at the decompression budget of " << budgetBytes << " bytes (#816)";
    }

    reader.archive->close();
    return result;
}

DecompressionFailure doDecompress( std::shared_ptr<QIODevice> input, const QString& archiveFilePath,
                                   QFile* outputFile, qint64 budgetBytes, AtomicFlag& interrupt )
{
    if ( !input->open( QIODevice::ReadOnly ) ) {
        LOG_WARNING << "Cannot open " << archiveFilePath;
        return DecompressionFailure::Error;
    }

    auto result = DecompressionFailure::None;
    try {
        // One output buffer for the whole Log File instead of a new one per chunk.
        std::vector<char> data( 4 * 1024 * 1024 );
        qint64 written = 0;

        while ( !input->atEnd() ) {
            if ( interrupt ) {
                result = DecompressionFailure::Interrupted;
                LOG_INFO << "Interrupted decompress of " << archiveFilePath;
                break;
            }

            const auto readBytes = input->read( data.data(), static_cast<qint64>( data.size() ) );
            if ( readBytes < 0 ) {
                // A corrupt or truncated archive, or an I/O error.
                LOG_ERROR << "Error decompressing " << archiveFilePath << ": "
                          << input->errorString();
                result = DecompressionFailure::Error;
                break;
            }
            if ( readBytes == 0 ) {
                // No progress while not at end — device is in a bad state.
                // Break to avoid an infinite loop.
                if ( !input->atEnd() ) {
                    LOG_ERROR << "Decompressor stalled (read returned 0 but not atEnd) for "
                              << archiveFilePath;
                    result = DecompressionFailure::Error;
                }
                break;
            }

            // Checked before the write: not a byte past the budget reaches
            // the disk, however far the input expands (#816).
            if ( written + readBytes > budgetBytes ) {
                LOG_WARNING << "Decompressing " << archiveFilePath
                            << " stopped at the decompression budget of " << budgetBytes
                            << " bytes (#816)";
                result = DecompressionFailure::BudgetExceeded;
                break;
            }

            // Detect short writes (e.g. disk full, quota exceeded) — these
            // would otherwise produce a silently truncated output file.
            const auto writtenBytes = outputFile->write( data.data(), readBytes );
            if ( writtenBytes < 0 ) {
                LOG_ERROR << "Error decompressing " << archiveFilePath << ": "
                          << outputFile->errorString();
                result = DecompressionFailure::Error;
                break;
            }
            if ( writtenBytes != readBytes ) {
                LOG_ERROR << "Short write while decompressing " << archiveFilePath << ": wrote "
                          << writtenBytes << " of " << readBytes << " bytes ("
                          << outputFile->errorString() << ")";
                result = DecompressionFailure::Error;
                break;
            }
            written += writtenBytes;
        }
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Exception during decompress: " << e.what();
        result = DecompressionFailure::Error;
    }

    input->close();
    outputFile->close();

    return result;
}

} // namespace

Decompressor::Decompressor( qint64 budgetBytes, QObject* parent )
    : QObject( parent )
    , budgetBytes_( budgetBytes )
{
    connect( &watcher_, &QFutureWatcher<DecompressionFailure>::finished, [ this ]() {
        const auto succeeded = watcher_.result() == DecompressionFailure::None;
        LOG_INFO << "Decompressor finished " << succeeded;
        Q_EMIT finished( succeeded );
    } );
}

bool Decompressor::waitForResult()
{
    return failure() == DecompressionFailure::None;
}

DecompressionFailure Decompressor::failure()
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
    const auto layer = archiveType( archiveFilePath );
    if ( layer == Archive::None || layer == Archive::Zip || layer == Archive::Zip7
         || layer == Archive::Tar ) {
        LOG_WARNING << "Unsupported archive " << archiveFilePath.constData();
        return false;
    }
    std::shared_ptr<QIODevice> decompressor = makeStreamDevice( layer, archiveFilePath );

    future_ = QtConcurrent::run( [ input = std::move( decompressor ), archiveFilePath, outputFile,
                                   budget = budgetBytes_, &interrupt ] {
        return doDecompress( input, archiveFilePath, outputFile, budget, interrupt );
    } );
    watcher_.setFuture( future_ );

    return true;
}

bool Decompressor::extract( const QString& archiveFilePath, const QString& destination,
                            AtomicFlag& interrupt )
{
    auto reader = makeExtractor( archiveType( archiveFilePath ), archiveFilePath, budgetBytes_ );
    if ( !reader.archive ) {
        LOG_WARNING << "Unsupported archive " << archiveFilePath;
        return false;
    }

    // Open the archive

    future_ = QtConcurrent::run( [ reader = std::move( reader ), archiveFilePath, destination,
                                   budget = budgetBytes_, &interrupt ] {
        return doExtract( reader, archiveFilePath, destination, budget, interrupt );
    } );
    watcher_.setFuture( future_ );

    return true;
}
