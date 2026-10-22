/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov
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

#include "openfilehelper.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include "log.h"
#include "systemprogram.h"

namespace {

FileExplorerAction openFolderAction( const QString& filePath )
{
    FileExplorerAction action;
    action.url = QUrl::fromLocalFile( QFileInfo( filePath ).path() );
    return action;
}

FileExplorerPlatform currentFileExplorerPlatform()
{
#if defined( Q_OS_WIN )
    return FileExplorerPlatform::Windows;
#elif defined( Q_OS_MAC )
    return FileExplorerPlatform::MacOS;
#else
    return FileExplorerPlatform::Other;
#endif
}

} // namespace

FileExplorerAction fileExplorerAction( const QString& filePath, FileExplorerPlatform platform,
                                       const QString& systemRoot )
{
    switch ( platform ) {
    case FileExplorerPlatform::MacOS: {
        // open -R reveals the file in Finder and brings Finder to the front;
        // the path is an argument, so no AppleScript is composed from it (#822).
        FileExplorerAction action;
        action.program = QStringLiteral( "/usr/bin/open" );
        action.arguments = { QStringLiteral( "-R" ), filePath };
        return action;
    }
    case FileExplorerPlatform::Windows: {
        // The Explorer by its absolute path below the system root, never by
        // the bare name CreateProcess would look up next to the application
        // first (#822). Without one, the folder opens instead.
        FileExplorerAction action;
        action.program = systemProgramPath( QStringLiteral( "explorer.exe" ), systemRoot );
        if ( action.program.isEmpty() ) {
            return openFolderAction( filePath );
        }
        action.arguments = { QStringLiteral( "/select," ) + QDir::toNativeSeparators( filePath ) };
        return action;
    }
    case FileExplorerPlatform::Other:
        break;
    }
    return openFolderAction( filePath );
}

void showPathInFileExplorer( const QString& filePath )
{
    LOG_INFO << "Show path in explorer: " << filePath;

    const QFileInfo fileInfo( filePath );
    auto path = fileInfo.canonicalFilePath();
    if ( path.isEmpty() ) {
        path = fileInfo.absoluteFilePath();
    }

    const auto action = fileExplorerAction( path, currentFileExplorerPlatform(),
                                            qEnvironmentVariable( "SystemRoot" ) );
    if ( action.program.isEmpty() ) {
        QDesktopServices::openUrl( action.url );
    }
    else {
        QProcess::startDetached( action.program, action.arguments );
    }
}

void openFileInDefaultApplication( const QString& filePath )
{
    LOG_INFO << "Open file in default app: " << filePath;

    const QFileInfo fileInfo( filePath );
    QDesktopServices::openUrl( QUrl::fromLocalFile( fileInfo.canonicalFilePath() ) );
}
