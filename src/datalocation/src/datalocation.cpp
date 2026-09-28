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

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <whereami.h>

#include <utility>
#include <vector>

namespace {

constexpr const char PortableSettingsFile[] = "logsquirl.conf";

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

} // namespace

const DataLocation& DataLocation::current()
{
    static const DataLocation location{ ForcePortable, runningExecutableDirectory() };
    return location;
}

DataLocation::DataLocation( bool forcePortable, QString executableDirectory )
    : executableDirectory_( QDir::cleanPath( std::move( executableDirectory ) ) )
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
