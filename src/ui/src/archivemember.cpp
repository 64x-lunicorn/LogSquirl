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

#include "archivemember.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "atomicflag.h"
#include "decompressor.h"
#include "log.h"

namespace {

// Decompresses one level: the one file of a compressed single file, or the
// member of an archive. Empty when it cannot.
QString decompressLevel( const QString& fileName, const QString& member, const QString& directory )
{
    Decompressor decompressor;
    AtomicFlag interrupt;
    const auto action = Decompressor::action( fileName );

    if ( action == DecompressAction::Decompress && member.isEmpty() ) {
        // Named as when the user opened it; the directory takes it away.
        QTemporaryFile output{ QDir{ directory }.filePath( QFileInfo( fileName ).fileName() ) };
        output.setAutoRemove( false );
        if ( !output.open() || !decompressor.decompress( fileName, &output, interrupt )
             || !decompressor.waitForResult() ) {
            output.remove();
            return {};
        }
        return output.fileName();
    }

    if ( action == DecompressAction::Extract && !member.isEmpty() ) {
        QTemporaryDir extracted{ QDir{ directory }.filePath( QFileInfo( fileName ).fileName() ) };
        extracted.setAutoRemove( false );
        if ( !extracted.isValid() || !decompressor.extract( fileName, extracted.path(), interrupt )
             || !decompressor.waitForResult() ) {
            extracted.remove();
            return {};
        }

        // A member names a file inside the archive, never one beside it.
        const auto path = QDir::cleanPath( extracted.filePath( member ) );
        if ( !path.startsWith( QDir::cleanPath( extracted.path() ) + '/' )
             || !QFileInfo{ path }.isFile() ) {
            LOG_WARNING << "No member " << member << " in " << fileName;
            return {};
        }
        return path;
    }

    LOG_WARNING << "Cannot take member '" << member << "' from " << fileName;
    return {};
}

} // namespace

QString decompressArchiveMember( const ArchiveMember& member, const QString& directory )
{
    if ( member.isEmpty() || member.members.isEmpty() ) {
        return {};
    }

    // Gone like any missing Log File: nothing to report (#596).
    if ( !QFileInfo{ member.archive }.isFile() ) {
        LOG_INFO << "The archive " << member.archive << " is gone";
        return {};
    }

    auto current = member.archive;
    for ( const auto& level : member.members ) {
        current = decompressLevel( current, level, directory );
        if ( current.isEmpty() ) {
            return {};
        }
    }
    return current;
}
