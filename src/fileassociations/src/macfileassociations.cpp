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

#include "macfileassociations.h"

#include <QPointer>

#include "log.h"

namespace {

// The group of the settings that keeps, per content type, the application
// that opened it before LogSquirl.
constexpr auto PreviousDefaults = QLatin1String( "PreviousDefaults/" );

QString shownAs( const std::vector<FileType>& types )
{
    QStringList shown;
    for ( const auto& type : types ) {
        shown << type.shownAs;
    }
    return shown.join( QStringLiteral( ", " ) );
}

} // namespace

MacFileAssociations::LaunchServices::~LaunchServices() = default;

#ifndef Q_OS_MACOS
std::unique_ptr<MacFileAssociations::LaunchServices> MacFileAssociations::systemLaunchServices()
{
    return nullptr;
}
#endif

std::unique_ptr<QSettings> MacFileAssociations::userSettings()
{
    return std::make_unique<QSettings>( QSettings::NativeFormat, QSettings::UserScope,
                                        QStringLiteral( "logsquirl" ),
                                        QStringLiteral( "fileassociations" ) );
}

MacFileAssociations::MacFileAssociations( std::unique_ptr<LaunchServices> launchServices,
                                          std::unique_ptr<QSettings> settings )
    : launchServices_( std::move( launchServices ) )
    , settings_( std::move( settings ) )
{
}

MacFileAssociations::~MacFileAssociations() = default;

bool MacFileAssociations::isAvailable() const
{
    return launchServices_ && launchServices_->thisApplication().has_value();
}

QString MacFileAssociations::unavailableReason() const
{
    if ( isAvailable() ) {
        return {};
    }
    return tr( "LogSquirl runs outside its app bundle, so macOS cannot open files with it. Start "
               "LogSquirl from LogSquirl.app to choose them." );
}

QString MacFileAssociations::applyNote() const
{
    return tr( "macOS asks you to confirm each change. The states show what you answered." );
}

FileAssociationState MacFileAssociations::state( const FileType& type ) const
{
    const auto self = launchServices_ ? launchServices_->thisApplication() : std::nullopt;
    if ( !self ) {
        return FileAssociationState::NotRegistered;
    }
    // Any copy of LogSquirl is LogSquirl.
    if ( const auto current = launchServices_->defaultApplication( type.uti );
         current && current->identifier == self->identifier ) {
        return FileAssociationState::Default;
    }
    for ( const auto& application : launchServices_->applications( type.uti ) ) {
        if ( application.identifier == self->identifier ) {
            return FileAssociationState::Registered;
        }
    }
    return FileAssociationState::NotRegistered;
}

QString MacFileAssociations::previousDefault( const QString& contentType ) const
{
    return settings_->value( PreviousDefaults + contentType ).toString();
}

void MacFileAssociations::setDefault( const QString& applicationPath, const QString& contentType,
                                      std::function<void()> succeeded )
{
    ++pending_;
    launchServices_->setDefaultApplication(
        applicationPath, contentType,
        [ this, applicationPath, contentType,
          succeeded = std::move( succeeded ) ]( const QString& error ) {
            if ( error.isEmpty() ) {
                succeeded();
            }
            else {
                LOG_WARNING << "macOS did not make " << applicationPath.toStdString()
                            << " the default for " << contentType.toStdString() << ": "
                            << error.toStdString();
            }
            // Once every confirmation is answered, the states are what the
            // user chose.
            if ( --pending_ == 0 ) {
                Q_EMIT statesChanged();
            }
        } );
}

FileAssociationResult MacFileAssociations::apply( const std::vector<FileType>& makeDefault,
                                                  const std::vector<FileType>& release )
{
    FileAssociationResult result;
    if ( !isAvailable() ) {
        for ( const auto* types : { &makeDefault, &release } ) {
            for ( const auto& type : *types ) {
                result.failed << type.id;
            }
        }
        result.error = unavailableReason();
        return result;
    }

    const auto self = *launchServices_->thisApplication();
    for ( const auto& type : makeDefault ) {
        const auto before = launchServices_->defaultApplication( type.uti );
        const auto contentType = type.uti;
        const auto previous
            = before && before->identifier != self.identifier ? before->path : QString{};
        setDefault( self.path, contentType, [ this, contentType, previous ] {
            if ( !previous.isEmpty() ) {
                settings_->setValue( PreviousDefaults + contentType, previous );
            }
        } );
    }

    std::vector<FileType> notReleased;
    for ( const auto& type : release ) {
        // macOS cannot unset a default, only choose another application.
        const auto previous = previousDefault( type.uti );
        if ( previous.isEmpty() || !launchServices_->isApplication( previous ) ) {
            result.failed << type.id;
            notReleased.push_back( type );
            continue;
        }
        const auto contentType = type.uti;
        setDefault( previous, contentType, [ this, contentType ] {
            settings_->remove( PreviousDefaults + contentType );
        } );
    }
    if ( !notReleased.empty() ) {
        result.error = tr( "macOS cannot unset the application that opens %1, and LogSquirl does "
                           "not know which one opened them before. Choose one in the Finder: Get "
                           "Info on such a file, then Open with and Change All." )
                           .arg( shownAs( notReleased ) );
    }
    return result;
}
