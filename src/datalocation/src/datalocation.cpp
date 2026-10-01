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

#include "datalocation.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <whereami.h>

#include <array>
#include <optional>
#include <vector>

namespace {

constexpr const char PortableSettingsFile[] = "logsquirl.conf";
constexpr const char TakeOverMarkerFile[] = "logsquirl_taken_over.txt";

constexpr auto EveryEntry = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;

// A folder of the data the portable package kept in the old locations before
// #602 (#613).
struct OldFolder {
    // Whether it was in the AppConfigLocation rather than the AppDataLocation.
    bool inConfigLocation;
    const char* name;
    // Whether this folder holding anything beside the executable counts as
    // data already being there. The portable package ships `plugins`, so that
    // one does not; each plugin folder in it is kept rather than replaced.
    bool countsAsData;
};

constexpr std::array OldFolders = {
    OldFolder{ false, "formats", true },       OldFolder{ false, "plugins", false },
    OldFolder{ false, "plugin_config", true }, OldFolder{ false, "teamfolder", true },
    OldFolder{ true, "themes", true },
};

bool holdsAnything( const QString& directory )
{
    const QDir dir( directory );
    return dir.exists() && !dir.isEmpty( EveryEntry );
}

QString copyDescription( const QString& from, const QString& to )
{
    return QStringLiteral( "%1 -> %2" )
        .arg( QDir::toNativeSeparators( from ), QDir::toNativeSeparators( to ) );
}

// Copies a file, a symbolic link or a whole folder to `to`, never over
// something that is already there, and adds what fails to `failed`.
void copyEntry( const QFileInfo& from, const QString& to, QStringList& failed )
{
    if ( from.isSymLink() ) {
        if ( !QFile::link( from.readSymLink(), to ) ) {
            failed << copyDescription( from.filePath(), to );
        }
    }
    else if ( from.isDir() ) {
        if ( !QDir().mkpath( to ) ) {
            failed << copyDescription( from.filePath(), to );
            return;
        }
        const auto entries = QDir( from.filePath() ).entryInfoList( EveryEntry );
        for ( const auto& entry : entries ) {
            copyEntry( entry, QDir( to ).filePath( entry.fileName() ), failed );
        }
    }
    else if ( !QFile::copy( from.filePath(), to ) ) {
        failed << copyDescription( from.filePath(), to );
    }
}

// Read with whereami rather than from QCoreApplication: the settings are
// opened through this, and a test binary relaunches itself before there is an
// application object (tests/helpers/isolated_settings.h).
QString runningExecutableDirectory()
{
    int dirnameLength = 0;
    const auto executablePathLength = wai_getExecutablePath( nullptr, 0, &dirnameLength );
    if ( executablePathLength <= 0 ) {
        return {};
    }
    auto path = std::vector<char>( static_cast<size_t>( executablePathLength ), '\0' );
    wai_getExecutablePath( path.data(), executablePathLength, &dirnameLength );
    return QString::fromUtf8( path.data(), dirnameLength );
}

// The directory isolateCurrentIn() was given, and whether current() has
// decided the location already. Both only touched from the main thread, before
// and while the location is first asked for.
std::optional<QString>& isolatedCurrentDirectory()
{
    static std::optional<QString> directory;
    return directory;
}

bool& currentDecided()
{
    static bool decided = false;
    return decided;
}

} // namespace

const DataLocation& DataLocation::current()
{
    static const DataLocation location = [] {
        currentDecided() = true;
        if ( const auto& isolated = isolatedCurrentDirectory() ) {
            return isolatedIn( *isolated );
        }
        return DataLocation{ ForcePortable, runningExecutableDirectory() };
    }();
    return location;
}

DataLocation DataLocation::isolatedIn( const QString& directory )
{
    DataLocation location{ true, directory };
    location.isolated_ = true;
    return location;
}

bool DataLocation::isolateCurrentIn( const QString& directory )
{
    if ( currentDecided() ) {
        return false;
    }
    isolatedCurrentDirectory() = directory;
    return true;
}

DataLocation::DataLocation( bool forcePortable, const QString& executableDirectory )
    : executableDirectory_( QDir::cleanPath( executableDirectory ) )
{
    portable_ = forcePortable || QFileInfo::exists( portableSettingsPath() );
}

bool DataLocation::isPortable() const
{
    return portable_;
}

const QString& DataLocation::executableDirectory() const
{
    return executableDirectory_;
}

QString DataLocation::portableSettingsPath() const
{
    return QDir( executableDirectory_ ).filePath( PortableSettingsFile );
}

QString DataLocation::dataDirectory() const
{
    // Asked every time, not kept: the installed locations follow the
    // application name, which is set after the location is first asked for.
    return portable_ ? executableDirectory_
                     : QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
}

QString DataLocation::configDirectory() const
{
    return portable_ ? executableDirectory_
                     : QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation );
}

QString DataLocation::takeOverMarkerPath() const
{
    return QDir( executableDirectory_ ).filePath( TakeOverMarkerFile );
}

DataLocation::TakeOver DataLocation::takeOverOldPortableData() const
{
    return takeOverOldPortableData(
        QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ),
        QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) );
}

DataLocation::TakeOver
DataLocation::takeOverOldPortableData( const QString& oldDataDirectory,
                                       const QString& oldConfigDirectory ) const
{
    TakeOver takeOver;
    // A Benchmark Run reads nothing of the user's (#666).
    if ( !portable_ || isolated_ || QFileInfo::exists( takeOverMarkerPath() ) ) {
        return takeOver;
    }

    struct Found {
        QString from;
        QString to;
        bool countsAsData;
    };
    std::vector<Found> found;
    found.reserve( OldFolders.size() );
    auto dataBeside = false;
    const QDir beside( executableDirectory_ );
    for ( const auto& folder : OldFolders ) {
        const auto to = beside.filePath( folder.name );
        dataBeside = dataBeside || ( folder.countsAsData && holdsAnything( to ) );

        const auto& oldDirectory = folder.inConfigLocation ? oldConfigDirectory : oldDataDirectory;
        if ( oldDirectory.isEmpty() || QDir::cleanPath( oldDirectory ) == executableDirectory_ ) {
            continue;
        }
        const auto from = QDir( oldDirectory ).filePath( folder.name );
        if ( holdsAnything( from ) ) {
            found.push_back( { from, to, folder.countsAsData } );
        }
    }

    // Nothing to take over, and no marker: the next start looks again, which
    // costs little.
    if ( found.empty() ) {
        return takeOver;
    }

    for ( const auto& folder : found ) {
        if ( dataBeside ) {
            takeOver.skipped << QStringLiteral( "%1: data is already beside the executable" )
                                    .arg( QDir::toNativeSeparators( folder.from ) );
        }
        else if ( folder.countsAsData ) {
            copyEntry( QFileInfo( folder.from ), folder.to, takeOver.failed );
            takeOver.copied << copyDescription( folder.from, folder.to );
        }
        else if ( !QDir().mkpath( folder.to ) ) {
            takeOver.failed << copyDescription( folder.from, folder.to );
        }
        else {
            // One plugin folder at a time: one the package ships stays as it is.
            const auto entries = QDir( folder.from ).entryInfoList( EveryEntry );
            for ( const auto& entry : entries ) {
                const auto to = QDir( folder.to ).filePath( entry.fileName() );
                if ( QFileInfo::exists( to ) || QFileInfo( to ).isSymLink() ) {
                    takeOver.skipped << QStringLiteral( "%1: %2 is already there" )
                                            .arg( QDir::toNativeSeparators( entry.filePath() ),
                                                  QDir::toNativeSeparators( to ) );
                    continue;
                }
                copyEntry( entry, to, takeOver.failed );
                takeOver.copied << copyDescription( entry.filePath(), to );
            }
        }
    }

    QFile marker( takeOverMarkerPath() );
    auto text = QStringLiteral( "LogSquirl looked for the data a portable run kept in %1 and %2 "
                                "before, on %3.\n" )
                    .arg( QDir::toNativeSeparators( oldDataDirectory ),
                          QDir::toNativeSeparators( oldConfigDirectory ),
                          QDateTime::currentDateTimeUtc().toString( Qt::ISODate ) );
    if ( !takeOver.copied.isEmpty() ) {
        text += QStringLiteral( "Copied:\n  %1\n" ).arg( takeOver.copied.join( "\n  " ) );
    }
    if ( !takeOver.skipped.isEmpty() ) {
        text += QStringLiteral( "Not copied:\n  %1\n" ).arg( takeOver.skipped.join( "\n  " ) );
    }
    text += QStringLiteral( "The old folders are left as they were. While this file is here, "
                            "LogSquirl does not look again.\n" );
    const auto written = marker.open( QIODevice::WriteOnly | QIODevice::Text )
                         && marker.write( text.toUtf8() ) >= 0 && marker.flush();
    if ( !written ) {
        takeOver.failed << QDir::toNativeSeparators( marker.fileName() );
    }

    return takeOver;
}
