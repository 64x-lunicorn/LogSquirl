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

#include "systemprogram.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

QString systemProgramPath( const QString& relativePath, const QString& systemRoot )
{
    if ( systemRoot.isEmpty() ) {
        return QStandardPaths::findExecutable( QFileInfo( relativePath ).fileName() );
    }
    return QDir::toNativeSeparators( QDir( systemRoot ).filePath( relativePath ) );
}

QString systemProgramPath( const QString& relativePath )
{
    return systemProgramPath( relativePath, qEnvironmentVariable( "SystemRoot" ) );
}

namespace {

bool isCommandInterpreter( const QString& name )
{
    return name.compare( QLatin1String( "cmd.exe" ), Qt::CaseInsensitive ) == 0
           || name.compare( QLatin1String( "cmd" ), Qt::CaseInsensitive ) == 0;
}

} // namespace

QString resolvedProgramPath( const QString& program, ProgramPlatform platform,
                             const QString& systemRoot )
{
    if ( program.isEmpty() ) {
        return {};
    }
    if ( QDir::isAbsolutePath( program ) ) {
        return QFileInfo::exists( program ) ? program : QString{};
    }
    if ( program.contains( QLatin1Char( '/' ) ) || program.contains( QLatin1Char( '\\' ) ) ) {
        return {};
    }
    if ( platform == ProgramPlatform::Windows && isCommandInterpreter( program ) ) {
        return systemProgramPath( QStringLiteral( "System32/cmd.exe" ), systemRoot );
    }
    // PATH only: a relative entry there is the user's own doing.
    return QStandardPaths::findExecutable( program );
}

QString resolvedProgramPath( const QString& program )
{
#ifdef Q_OS_WIN
    constexpr auto platform = ProgramPlatform::Windows;
#else
    constexpr auto platform = ProgramPlatform::Other;
#endif
    return resolvedProgramPath( program, platform, qEnvironmentVariable( "SystemRoot" ) );
}
