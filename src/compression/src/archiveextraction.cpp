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

#include "archiveextraction.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>

#include <karchive.h>
#include <karchivedirectory.h>
#include <karchiveentry.h>
#include <karchivefile.h>

#include "atomicflag.h"
#include "log.h"

namespace {

// Where the file system folds letter case, so does the containment check:
// `dest` and `DEST` are one folder there (#815).
#if defined( Q_OS_WIN ) || defined( Q_OS_MACOS )
constexpr auto PathCase = Qt::CaseInsensitive;
#else
constexpr auto PathCase = Qt::CaseSensitive;
#endif

// Whether `path`, canonical, is `folder`, canonical too, or below it. Whole
// components only: `/tmp/destX` is not below `/tmp/dest`.
bool isInside( const QString& path, const QString& folder )
{
    const auto prefix = folder.endsWith( '/' ) ? folder : folder + '/';
    return path.compare( folder, PathCase ) == 0 || path.startsWith( prefix, PathCase );
}

// Whether `name` is one path component, so that an entry of that name lands
// in its directory and nowhere else.
bool isPlainName( const QString& name )
{
    return !name.isEmpty() && name != QLatin1String( "." ) && name != QLatin1String( ".." )
           && !name.contains( '/' ) && !name.contains( '\\' );
}

QFileDevice::Permissions withExecutableBits( QFileDevice::Permissions permissions,
                                             mode_t entryPermissions )
{
    if ( entryPermissions & 01 ) {
        permissions |= QFileDevice::ExeOther;
    }
    if ( entryPermissions & 010 ) {
        permissions |= QFileDevice::ExeGroup;
    }
    if ( entryPermissions & 0100 ) {
        permissions |= QFileDevice::ExeOwner;
    }
    return permissions;
}

// Writes `file` to `path`, in chunks, taking what it writes from
// `budgetLeft`. Why it stopped when it could not be written whole, the
// budget ran out before it or `interrupt` was set.
DecompressionFailure writeFileEntry( const KArchiveFile& file, const QString& path,
                                     qint64& budgetLeft, AtomicFlag& interrupt )
{
    QFile out{ path };
    if ( !out.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        LOG_WARNING << "Cannot write the archive entry " << path << ": " << out.errorString();
        return DecompressionFailure::Error;
    }

    const std::unique_ptr<QIODevice> in{ file.createDevice() };
    if ( !in ) {
        LOG_WARNING << "Cannot read the archive entry " << file.name();
        out.remove();
        return DecompressionFailure::Error;
    }

    std::vector<char> buffer( 1024 * 1024 );
    auto remaining = file.size();
    while ( remaining > 0 ) {
        if ( interrupt ) {
            return DecompressionFailure::Interrupted;
        }
        const auto chunk = std::min( remaining, static_cast<qint64>( buffer.size() ) );
        const auto read = in->read( buffer.data(), chunk );
        if ( read <= 0 ) {
            LOG_WARNING << "The archive entry " << file.name() << " ends " << remaining
                        << " bytes short";
            return DecompressionFailure::Error;
        }
        // Checked before the write: not a byte past the budget reaches the
        // disk (#816).
        if ( read > budgetLeft ) {
            LOG_WARNING << "The archive entry " << file.name()
                        << " passes the decompression budget (#816)";
            return DecompressionFailure::BudgetExceeded;
        }
        if ( out.write( buffer.data(), read ) != read ) {
            LOG_WARNING << "Short write of the archive entry " << path << ": " << out.errorString();
            return DecompressionFailure::Error;
        }
        budgetLeft -= read;
        remaining -= read;
    }

    out.setPermissions( withExecutableBits( out.permissions(), file.permissions() ) );
    return DecompressionFailure::None;
}

} // namespace

DecompressionFailure extractArchiveDirectory( const KArchiveDirectory& root, const QString& folder,
                                              qint64 budgetBytes, AtomicFlag& interrupt )
{
    if ( !QDir().mkpath( folder ) ) {
        LOG_WARNING << "Cannot create the extraction folder " << folder;
        return DecompressionFailure::Error;
    }
    const auto canonicalFolder = QFileInfo{ folder }.canonicalFilePath();
    if ( canonicalFolder.isEmpty() ) {
        LOG_WARNING << "Cannot resolve the extraction folder " << folder;
        return DecompressionFailure::Error;
    }

    struct PendingDirectory {
        const KArchiveDirectory* directory;
        // Canonical: resolved once, so no link is followed later.
        QString path;
    };
    struct PendingFile {
        const KArchiveFile* file;
        QString path;
    };

    // The directories first, the files after them sorted by their position
    // in the archive: a compressed tar reads forward only, and going back
    // means decompressing it from the start again.
    std::vector<PendingDirectory> directories{ { &root, canonicalFolder } };
    std::vector<PendingFile> files;
    while ( !directories.empty() ) {
        if ( interrupt ) {
            return DecompressionFailure::Interrupted;
        }
        const auto current = directories.back();
        directories.pop_back();

        const auto names = current.directory->entries();
        for ( const auto& name : names ) {
            const auto* entry = current.directory->entry( name );
            if ( !entry ) {
                continue;
            }
            if ( !isPlainName( name ) ) {
                LOG_WARNING << "Skipped the archive entry '" << name
                            << "': its name is not one path component (#815)";
                continue;
            }
            if ( !entry->symLinkTarget().isEmpty() ) {
                LOG_WARNING << "Skipped the archive entry '" << name << "': a link to '"
                            << entry->symLinkTarget() << "' is never extracted (#815)";
                continue;
            }

            const auto path = current.path + '/' + name;
            if ( entry->isDirectory() ) {
                const auto* directory = dynamic_cast<const KArchiveDirectory*>( entry );
                if ( !directory ) {
                    continue;
                }
                if ( QFileInfo{ path }.isSymLink() ) {
                    LOG_WARNING << "Skipped the archive directory '" << name
                                << "': a link is in its place (#815)";
                    continue;
                }
                if ( !QDir().mkpath( path ) ) {
                    LOG_WARNING << "Cannot create the archive directory " << path;
                    return DecompressionFailure::Error;
                }
                const auto canonical = QFileInfo{ path }.canonicalFilePath();
                if ( !isInside( canonical, canonicalFolder ) ) {
                    LOG_WARNING << "Skipped the archive directory '" << name
                                << "': it resolves outside the extraction folder (#815)";
                    continue;
                }
                directories.push_back( { directory, canonical } );
            }
            else if ( entry->isFile() ) {
                if ( const auto* file = dynamic_cast<const KArchiveFile*>( entry ) ) {
                    files.push_back( { file, path } );
                }
            }
        }
    }

    std::sort( files.begin(), files.end(), []( const PendingFile& a, const PendingFile& b ) {
        return a.file->position() < b.file->position();
    } );
    // One budget for the archive as a whole (#816).
    auto budgetLeft = budgetBytes;
    for ( const auto& pending : files ) {
        if ( interrupt ) {
            return DecompressionFailure::Interrupted;
        }
        // Never through a link that was in the folder before (#815).
        if ( QFileInfo{ pending.path }.isSymLink() ) {
            LOG_WARNING << "Skipped the archive entry '" << pending.file->name()
                        << "': a link is in its place (#815)";
            continue;
        }
        const auto failure = writeFileEntry( *pending.file, pending.path, budgetLeft, interrupt );
        if ( failure != DecompressionFailure::None ) {
            return failure;
        }
    }

    return interrupt ? DecompressionFailure::Interrupted : DecompressionFailure::None;
}

QString extractedMemberPath( const QString& folder, const QString& member )
{
    if ( member.isEmpty() || QDir::isAbsolutePath( member ) ) {
        return {};
    }
    const auto canonicalFolder = QFileInfo{ folder }.canonicalFilePath();
    if ( canonicalFolder.isEmpty() ) {
        return {};
    }

    // A member names a file inside the archive, never one beside it.
    auto path = QDir::cleanPath( QDir{ folder }.filePath( member ) );
    if ( !path.startsWith( QDir::cleanPath( folder ) + '/' ) ) {
        return {};
    }
    const QFileInfo info{ path };
    if ( info.isSymLink() || !info.isFile() ) {
        return {};
    }
    // Resolved whole: a link in any component above it leads out as well.
    const auto canonical = info.canonicalFilePath();
    if ( canonical.compare( canonicalFolder, PathCase ) == 0
         || !isInside( canonical, canonicalFolder ) ) {
        return {};
    }
    return path;
}
