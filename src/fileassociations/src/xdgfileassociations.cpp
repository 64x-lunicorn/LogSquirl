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

#include "xdgfileassociations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include "log.h"

namespace {

constexpr auto DefaultApplicationsGroup = QLatin1String( "[Default Applications]" );
constexpr auto DesktopEntryGroup = QLatin1String( "[Desktop Entry]" );

bool isGroupHeader( const QString& trimmedLine )
{
    return trimmedLine.startsWith( QLatin1Char( '[' ) );
}

// The key and the value of a "key=value" line, trimmed; nothing for a comment,
// an empty line or a group header.
std::optional<std::pair<QString, QString>> keyAndValue( const QString& line )
{
    const auto trimmed = line.trimmed();
    if ( trimmed.isEmpty() || trimmed.startsWith( QLatin1Char( '#' ) )
         || isGroupHeader( trimmed ) ) {
        return std::nullopt;
    }
    const auto separator = trimmed.indexOf( QLatin1Char( '=' ) );
    if ( separator < 0 ) {
        return std::nullopt;
    }
    return std::make_pair( trimmed.left( separator ).trimmed(),
                           trimmed.mid( separator + 1 ).trimmed() );
}

QStringList listValue( const QString& value )
{
    QStringList items;
    for ( const auto& item : value.split( QLatin1Char( ';' ), Qt::SkipEmptyParts ) ) {
        if ( !item.trimmed().isEmpty() ) {
            items << item.trimmed();
        }
    }
    return items;
}

// The list value of a key in a group of a desktop-entry-like file.
QStringList listValueOf( const QString& text, QLatin1String group, const QString& key )
{
    bool inGroup = false;
    for ( const auto& line : text.split( QLatin1Char( '\n' ) ) ) {
        const auto trimmed = line.trimmed();
        if ( isGroupHeader( trimmed ) ) {
            inGroup = trimmed == group;
            continue;
        }
        if ( !inGroup ) {
            continue;
        }
        if ( const auto entry = keyAndValue( line ); entry && entry->first == key ) {
            return listValue( entry->second );
        }
    }
    return {};
}

std::optional<QString> readText( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return std::nullopt;
    }
    return QString::fromUtf8( file.readAll() );
}

bool writeText( const QString& path, const QString& text )
{
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        return false;
    }
    file.write( text.toUtf8() );
    return file.commit();
}

QString homeLocation( const char* variable, const QString& fallback )
{
    // The specification ignores a relative path.
    const auto value = qEnvironmentVariable( variable );
    return QDir::isAbsolutePath( value ) ? QDir::cleanPath( value )
                                         : QDir( QDir::homePath() ).filePath( fallback );
}

} // namespace

namespace XdgFiles {

QStringList defaultApplications( const QString& mimeAppsList, const QString& mimeType )
{
    return listValueOf( mimeAppsList, DefaultApplicationsGroup, mimeType );
}

QString withoutDefaultApplication( const QString& mimeAppsList, const QString& mimeType,
                                   const QString& desktopId )
{
    auto lines = mimeAppsList.split( QLatin1Char( '\n' ) );
    bool inGroup = false;
    for ( auto line = lines.begin(); line != lines.end(); ) {
        const auto trimmed = line->trimmed();
        if ( isGroupHeader( trimmed ) ) {
            inGroup = trimmed == DefaultApplicationsGroup;
        }
        else if ( const auto entry = keyAndValue( *line );
                  inGroup && entry && entry->first == mimeType ) {
            auto applications = listValue( entry->second );
            if ( applications.removeAll( desktopId ) > 0 ) {
                if ( applications.isEmpty() ) {
                    line = lines.erase( line );
                    continue;
                }
                *line = mimeType + QLatin1Char( '=' ) + applications.join( QLatin1Char( ';' ) )
                        + QLatin1Char( ';' );
            }
        }
        ++line;
    }
    return lines.join( QLatin1Char( '\n' ) );
}

QStringList desktopEntryMimeTypes( const QString& desktopEntry )
{
    return listValueOf( desktopEntry, DesktopEntryGroup, QStringLiteral( "MimeType" ) );
}

} // namespace XdgFiles

XdgFileAssociations::Environment XdgFileAssociations::Environment::ofThisRun()
{
    Environment environment;
    environment.configHome = homeLocation( "XDG_CONFIG_HOME", QStringLiteral( ".config" ) );
    environment.dataHome = homeLocation( "XDG_DATA_HOME", QStringLiteral( ".local/share" ) );
    environment.dataDirs
        = qEnvironmentVariable( "XDG_DATA_DIRS" ).split( QLatin1Char( ':' ), Qt::SkipEmptyParts );
    if ( environment.dataDirs.isEmpty() ) {
        environment.dataDirs
            = { QStringLiteral( "/usr/local/share" ), QStringLiteral( "/usr/share" ) };
    }
    for ( const auto& desktop : qEnvironmentVariable( "XDG_CURRENT_DESKTOP" )
                                    .split( QLatin1Char( ':' ), Qt::SkipEmptyParts ) ) {
        environment.currentDesktops << desktop.toLower();
    }
    // The AppImage is built without the file types (#717); the runtime of
    // any AppImage names it in $APPIMAGE.
    environment.appImage
        = !LOGSQUIRL_REGISTERS_FILE_TYPES || qEnvironmentVariableIsSet( "APPIMAGE" );
    environment.hasXdgMime
        = !QStandardPaths::findExecutable( QStringLiteral( "xdg-mime" ) ).isEmpty();
    return environment;
}

std::optional<QString> XdgFileAssociations::runXdgMime( const QStringList& arguments )
{
    QProcess xdgMime;
    xdgMime.setProcessChannelMode( QProcess::ForwardedErrorChannel );
    xdgMime.start( QStringLiteral( "xdg-mime" ), arguments, QIODevice::ReadOnly );
    // xdg-mime is a shell script that asks the desktop's own tool; it answers
    // at once or not at all. A query blocks the GUI while it waits.
    constexpr int QueryTimeoutMs = 3000;
    constexpr int ChangeTimeoutMs = 10000;
    const auto isQuery = !arguments.isEmpty() && arguments.first() == QLatin1String( "query" );
    if ( !xdgMime.waitForFinished( isQuery ? QueryTimeoutMs : ChangeTimeoutMs )
         || xdgMime.exitStatus() != QProcess::NormalExit || xdgMime.exitCode() != 0 ) {
        LOG_WARNING << "xdg-mime " << arguments.join( QLatin1Char( ' ' ) ).toStdString()
                    << " failed: " << xdgMime.errorString().toStdString();
        xdgMime.kill();
        xdgMime.waitForFinished();
        return std::nullopt;
    }
    return QString::fromLocal8Bit( xdgMime.readAllStandardOutput() );
}

XdgFileAssociations::XdgFileAssociations( Environment environment, XdgMime xdgMime )
    : environment_( std::move( environment ) )
    , xdgMime_( std::move( xdgMime ) )
{
}

bool XdgFileAssociations::isAvailable() const
{
    return unavailableReason().isEmpty();
}

QString XdgFileAssociations::unavailableReason() const
{
    if ( environment_.appImage ) {
        return tr( "An AppImage cannot register the file types it opens. Install the deb or rpm "
                   "package of LogSquirl to choose them." );
    }
    if ( desktopEntryPath().isEmpty() ) {
        return tr( "LogSquirl's desktop entry is not installed, so the desktop cannot open files "
                   "with it. Install the deb or rpm package of LogSquirl to choose them." );
    }
    if ( !environment_.hasXdgMime ) {
        return tr( "xdg-mime is not installed, which chooses the application a file type opens "
                   "with. Install xdg-utils to choose them." );
    }
    return {};
}

QString XdgFileAssociations::desktopEntryPath() const
{
    QStringList dataDirs{ environment_.dataHome };
    dataDirs << environment_.dataDirs;
    for ( const auto& dataDir : dataDirs ) {
        const auto path = QDir( dataDir ).filePath( QStringLiteral( "applications/" )
                                                    + environment_.desktopId );
        if ( QFileInfo::exists( path ) ) {
            return path;
        }
    }
    return {};
}

QStringList XdgFileAssociations::userMimeAppsLists() const
{
    const QDir configHome( environment_.configHome );
    QStringList lists;
    for ( const auto& desktop : environment_.currentDesktops ) {
        lists << configHome.filePath( desktop + QStringLiteral( "-mimeapps.list" ) );
    }
    lists << configHome.filePath( QStringLiteral( "mimeapps.list" ) );
    lists
        << QDir( environment_.dataHome ).filePath( QStringLiteral( "applications/mimeapps.list" ) );
    return lists;
}

QStringList XdgFileAssociations::desktopEntryMimeTypes() const
{
    const auto entry = desktopEntryPath();
    const auto entryText = entry.isEmpty() ? std::nullopt : readText( entry );
    return entryText ? XdgFiles::desktopEntryMimeTypes( *entryText ) : QStringList{};
}

std::optional<QString> XdgFileAssociations::queryDefault( const FileType& type ) const
{
    return xdgMime_( { QStringLiteral( "query" ), QStringLiteral( "default" ), type.mimeType } );
}

FileAssociationState XdgFileAssociations::stateFrom( const FileType& type,
                                                     const std::optional<QString>& opening,
                                                     const QStringList& offeredFor ) const
{
    if ( opening && opening->trimmed() == environment_.desktopId ) {
        return FileAssociationState::Default;
    }
    return offeredFor.contains( type.mimeType ) ? FileAssociationState::Registered
                                                : FileAssociationState::NotRegistered;
}

FileAssociationState XdgFileAssociations::state( const FileType& type ) const
{
    return stateFrom( type, queryDefault( type ), desktopEntryMimeTypes() );
}

FileAssociationStates XdgFileAssociations::states() const
{
    const auto offeredFor = desktopEntryMimeTypes();
    FileAssociationStates current;
    auto answers = true;
    for ( const auto& type : FileTypes::choices() ) {
        const auto opening = answers ? queryDefault( type ) : std::nullopt;
        if ( answers && !opening ) {
            LOG_WARNING << "xdg-mime did not answer; the other file types are not asked";
            answers = false;
        }
        current[ type.id ] = stateFrom( type, opening, offeredFor );
    }
    return current;
}

FileAssociationResult XdgFileAssociations::apply( const std::vector<FileType>& makeDefault,
                                                  const std::vector<FileType>& release )
{
    if ( !isAvailable() ) {
        return FileAssociationResult::failedFor( makeDefault, release, unavailableReason() );
    }

    FileAssociationResult result;
    QStringList errors;

    if ( !makeDefault.empty() ) {
        // One call for every type: xdg-mime takes them all.
        QStringList arguments{ QStringLiteral( "default" ), environment_.desktopId };
        for ( const auto& type : makeDefault ) {
            arguments << type.mimeType;
        }
        if ( !xdgMime_( arguments ) ) {
            for ( const auto& type : makeDefault ) {
                result.failed << type.id;
            }
            errors << tr( "xdg-mime could not make LogSquirl the default for %1." )
                          .arg( FileTypes::shownAs( makeDefault ) );
        }
    }

    std::vector<FileType> notReleased;
    for ( const auto& type : release ) {
        bool released = true;
        for ( const auto& list : userMimeAppsLists() ) {
            const auto text = readText( list );
            if ( !text ) {
                continue;
            }
            const auto changed = XdgFiles::withoutDefaultApplication( *text, type.mimeType,
                                                                      environment_.desktopId );
            if ( changed != *text && !writeText( list, changed ) ) {
                LOG_WARNING << "Could not write " << list.toStdString();
                released = false;
            }
        }
        if ( !released ) {
            result.failed << type.id;
            notReleased.push_back( type );
        }
    }
    if ( !notReleased.empty() ) {
        errors << tr( "LogSquirl could not give %1 back: its mimeapps.list cannot be written." )
                      .arg( FileTypes::shownAs( notReleased ) );
    }

    result.error = errors.join( QLatin1Char( ' ' ) );
    return result;
}
