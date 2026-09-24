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

#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace logsquirl::versioncheck {

// How the running LogSquirl was installed, as far as the update notice cares
// (#382): through a package manager that can update it, or not known.
enum class InstallSource { Unknown, Homebrew, Apt, Dnf };

// What the detection reads. Everything is looked up below `root`, which is
// empty on a real machine and a temporary directory in a test, so the tests
// never depend on the machine they run on.
struct InstallEnvironment {
    QString root;
    // Where the running binary lives, as the machine sees it (no root).
    QString executablePath;
    // Name of the RPM package that owns the file, or empty. Only asked on a
    // machine that has the LogSquirl DNF repository configured.
    std::function<QString( const QString& path )> rpmOwnerOf;
};

// The environment of the running application: the real machine, and `rpm -qf`.
InstallEnvironment runningInstallEnvironment();

// Decides at RUN time, from what is on the machine, and never at build time:
// the DMG a cask installs and the DMG a user drags to Applications are the
// same bytes, and so are the .deb and .rpm from the repository and from the
// release page. A source is reported only on positive evidence:
//  - Homebrew: the Caskroom of the `logsquirl` cask links to the running app
//    bundle (a dragged DMG has no such entry, another copy is not the cask's);
//  - apt: the LogSquirl APT repository is configured and the installed
//    `logsquirl` deb lists the running binary;
//  - dnf: the LogSquirl DNF repository is configured and the installed
//    `logsquirl` RPM owns the running binary.
// The AppImage, the Windows builds, and a deb or RPM installed by hand without
// the repository leave it Unknown, and the notice keeps the release page link.
InstallSource detectInstallSource( const InstallEnvironment& environment );

// The command that updates a package manager install; empty for Unknown.
QString updateCommand( InstallSource source );

// The update notice's HTML: the package manager's command for a known source,
// the link to the release page otherwise, and the changes the user skips.
QString updateNoticeHtml( const QString& version, const QString& url, const QStringList& changes,
                          InstallSource source );

} // namespace logsquirl::versioncheck
