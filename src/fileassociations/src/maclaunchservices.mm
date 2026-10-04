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

// The LaunchServices of macOS (#721), through NSWorkspace. Only this file
// talks to the system; MacFileAssociations decides what to ask.

#include "macfileassociations.h"

#include <QCoreApplication>
#include <QMetaObject>

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace {

QString toQString( NSString* string )
{
    return string == nil ? QString{} : QString::fromNSString( string );
}

std::optional<MacFileAssociations::Application> applicationAt( NSURL* url )
{
    if ( url == nil ) {
        return std::nullopt;
    }
    NSBundle* bundle = [NSBundle bundleWithURL:url];
    return MacFileAssociations::Application{ toQString( url.path ),
                                             bundle == nil ? QString{}
                                                           : toQString( bundle.bundleIdentifier ) };
}

class WorkspaceLaunchServices : public MacFileAssociations::LaunchServices {
public:
    WorkspaceLaunchServices()
        : alive_( std::make_shared<bool>( true ) )
    {
    }

    std::optional<MacFileAssociations::Application> thisApplication() const override
    {
        NSBundle* bundle = NSBundle.mainBundle;
        // A run of the executable alone, outside LogSquirl.app, has no bundle
        // LaunchServices could open files with.
        if ( bundle.bundleIdentifier == nil || ![bundle.bundlePath hasSuffix:@".app"] ) {
            return std::nullopt;
        }
        return MacFileAssociations::Application{ toQString( bundle.bundlePath ),
                                                 toQString( bundle.bundleIdentifier ) };
    }

    std::optional<MacFileAssociations::Application>
    defaultApplication( const QString& contentType ) const override
    {
        UTType* type = [UTType typeWithIdentifier:contentType.toNSString()];
        if ( type == nil ) {
            return std::nullopt;
        }
        return applicationAt(
            [NSWorkspace.sharedWorkspace URLForApplicationToOpenContentType:type] );
    }

    std::vector<MacFileAssociations::Application>
    applications( const QString& contentType ) const override
    {
        std::vector<MacFileAssociations::Application> found;
        UTType* type = [UTType typeWithIdentifier:contentType.toNSString()];
        if ( type == nil ) {
            return found;
        }
        for ( NSURL* url in
              [NSWorkspace.sharedWorkspace URLsForApplicationsToOpenContentType:type] ) {
            if ( const auto application = applicationAt( url ) ) {
                found.push_back( *application );
            }
        }
        return found;
    }

    bool isApplication( const QString& path ) const override
    {
        return [NSBundle bundleWithPath:path.toNSString()] != nil;
    }

    void setDefaultApplication( const QString& applicationPath, const QString& contentType,
                                Done done ) override
    {
        UTType* type = [UTType typeWithIdentifier:contentType.toNSString()];
        if ( type == nil ) {
            done( QStringLiteral( "macOS does not know the type %1." ).arg( contentType ) );
            return;
        }
        // macOS asks the user to confirm and calls back once answered, on a
        // queue of its own: the answer goes to the main thread, and only while
        // this is still there.
        std::weak_ptr<bool> alive = alive_;
        auto* answered = new Done( std::move( done ) );
        [NSWorkspace.sharedWorkspace
            setDefaultApplicationAtURL:[NSURL fileURLWithPath:applicationPath.toNSString()]
                     toOpenContentType:type
                     completionHandler:^( NSError* error ) {
                       const auto message
                           = error == nil ? QString{} : toQString( error.localizedDescription );
                       QMetaObject::invokeMethod(
                           QCoreApplication::instance(),
                           [ alive, answered, message ] {
                               if ( alive.lock() ) {
                                   ( *answered )( message );
                               }
                               delete answered;
                           },
                           Qt::QueuedConnection );
                     }];
    }

private:
    std::shared_ptr<bool> alive_;
};

} // namespace

std::unique_ptr<MacFileAssociations::LaunchServices> MacFileAssociations::systemLaunchServices()
{
    return std::make_unique<WorkspaceLaunchServices>();
}
