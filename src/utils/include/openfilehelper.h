/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov
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
 *
 * showPathInFileExplorer code is derived from Qt Creator sources
 */

/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#ifndef LOGSQUIRL_OPEN_FILE_HELPER_H
#define LOGSQUIRL_OPEN_FILE_HELPER_H

#include <QString>
#include <QStringList>
#include <QUrl>

// The platform whose file manager reveals a file, a parameter so every branch
// is tested everywhere (#822).
enum class FileExplorerPlatform { Windows, MacOS, Other };

// What reveals a file in the file manager: a program run with its arguments,
// or, when the program is empty, the file's folder opened through the desktop
// services. The path only ever travels as one argument; no script or command
// text is composed from it (#822).
struct FileExplorerAction {
    QString program;
    QStringList arguments;
    QUrl url;
};

// The action for a file, on a platform, with the SystemRoot the environment
// names (Windows only; empty when unset). The path is used as given.
FileExplorerAction fileExplorerAction( const QString& filePath, FileExplorerPlatform platform,
                                       const QString& systemRoot );

void showPathInFileExplorer( const QString& filePath );

void openFileInDefaultApplication( const QString& filePath );

#endif
