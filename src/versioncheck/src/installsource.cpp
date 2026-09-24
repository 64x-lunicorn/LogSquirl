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

#include "installsource.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace logsquirl::versioncheck {

namespace {

constexpr auto RepositoryHost = "packages.lunicorn-lab.de";
constexpr auto PackageName = "logsquirl";

QString below( const InstallEnvironment& environment, const QString& path )
{
    return environment.root + path;
}

QString canonical( const QString& path )
{
    return QFileInfo( path ).canonicalFilePath();
}

// The bundle around an executable of a macOS app: <name>.app/Contents/MacOS/<binary>.
QString bundleOf( const QString& executablePath )
{
    const auto marker = QStringLiteral( ".app/Contents/MacOS/" );
    const auto at = executablePath.indexOf( marker );
    return at < 0 ? QString() : executablePath.left( at + 4 );
}

bool homebrewCaskOwnsBundle( const InstallEnvironment& environment )
{
    const auto bundle = bundleOf( environment.executablePath );
    if ( bundle.isEmpty() ) {
        return false;
    }
    const auto running = canonical( below( environment, bundle ) );
    if ( running.isEmpty() ) {
        return false;
    }
    // Apple Silicon and Intel Homebrew prefixes.
    for ( const auto* prefix : { "/opt/homebrew", "/usr/local" } ) {
        const QDir cask( below( environment, QString( prefix ) + "/Caskroom/" + PackageName ) );
        for ( const auto& version : cask.entryList( QDir::Dirs | QDir::NoDotAndDotDot ) ) {
            const QDir staged( cask.filePath( version ) );
            for ( const auto& app :
                  staged.entryList( { "*.app" }, QDir::AllEntries | QDir::NoDotAndDotDot ) ) {
                if ( canonical( staged.filePath( app ) ) == running ) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Whether any of the files names the LogSquirl repository's host.
bool repositoryConfigured( const InstallEnvironment& environment, const QString& directory,
                           const QStringList& filters )
{
    const QDir configured( below( environment, directory ) );
    for ( const auto& name : configured.entryList( filters, QDir::Files ) ) {
        QFile file( configured.filePath( name ) );
        if ( file.open( QIODevice::ReadOnly ) && file.readAll().contains( RepositoryHost ) ) {
            return true;
        }
    }
    return false;
}

bool debOwnsBinary( const InstallEnvironment& environment )
{
    QFile list(
        below( environment, QStringLiteral( "/var/lib/dpkg/info/" ) + PackageName + ".list" ) );
    if ( !list.open( QIODevice::ReadOnly ) ) {
        return false;
    }
    const auto running = canonical( below( environment, environment.executablePath ) );
    if ( running.isEmpty() ) {
        return false;
    }
    while ( !list.atEnd() ) {
        const auto line = QString::fromUtf8( list.readLine() ).trimmed();
        // Directories and every other file the deb lists are not the binary.
        if ( line.startsWith( '/' ) && canonical( below( environment, line ) ) == running ) {
            return true;
        }
    }
    return false;
}

} // namespace

InstallSource detectInstallSource( const InstallEnvironment& environment )
{
    if ( homebrewCaskOwnsBundle( environment ) ) {
        return InstallSource::Homebrew;
    }
    if ( repositoryConfigured( environment, "/etc/apt/sources.list.d", { "*.sources", "*.list" } )
         && debOwnsBinary( environment ) ) {
        return InstallSource::Apt;
    }
    if ( repositoryConfigured( environment, "/etc/yum.repos.d", { "*.repo" } )
         && environment.rpmOwnerOf
         && environment.rpmOwnerOf( environment.executablePath ) == PackageName ) {
        return InstallSource::Dnf;
    }
    return InstallSource::Unknown;
}

InstallEnvironment runningInstallEnvironment()
{
    return InstallEnvironment{
        QString(), QCoreApplication::applicationFilePath(),
        []( const QString& path ) {
            QProcess rpm;
            rpm.start( "rpm", { "-qf", "--qf", "%{NAME}", path } );
            constexpr int RpmTimeoutMs = 3000;
            if ( !rpm.waitForFinished( RpmTimeoutMs ) || rpm.exitStatus() != QProcess::NormalExit
                 || rpm.exitCode() != 0 ) {
                return QString();
            }
            return QString::fromUtf8( rpm.readAllStandardOutput() ).trimmed();
        }
    };
}

QString updateCommand( InstallSource source )
{
    switch ( source ) {
    case InstallSource::Homebrew:
        return QStringLiteral( "brew upgrade --cask logsquirl" );
    case InstallSource::Apt:
        return QStringLiteral( "sudo apt upgrade" );
    case InstallSource::Dnf:
        return QStringLiteral( "sudo dnf upgrade" );
    case InstallSource::Unknown:
        break;
    }
    return {};
}

QString updateNoticeHtml( const QString& version, const QString& url, const QStringList& changes,
                          InstallSource source )
{
    const auto command = updateCommand( source );
    QString message;
    if ( command.isEmpty() ) {
        message = QCoreApplication::translate(
                      "UpdateNotice",
                      "<p> A new version of logsquirl (%1) is available for download </p>"
                      "<a href=\"%2\">%2</a>" )
                      .arg( version, url );
    }
    else {
        message = QCoreApplication::translate(
                      "UpdateNotice",
                      "<p> A new version of logsquirl (%1) is available. Update it with your "
                      "package manager: </p><p><code>%2</code></p>" )
                      .arg( version, command );
    }

    if ( !changes.empty() ) {
        message.append(
            QCoreApplication::translate( "UpdateNotice", "<p>Important changes:</p><ul>" ) );
        for ( const auto& change : changes ) {
            message.append( QString( "<li>%1</li>" ).arg( change ) );
        }
        message.append( "</ul>" );
    }
    return message;
}

} // namespace logsquirl::versioncheck
