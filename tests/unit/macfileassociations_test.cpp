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

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QTemporaryDir>

#include <algorithm>
#include <map>

#include "filetypes.h"
#include "macfileassociations.h"

namespace {

const FileType& typeWithId( const QString& id )
{
    const auto* type = FileTypes::find( id );
    REQUIRE( type != nullptr );
    return *type;
}

using Application = MacFileAssociations::Application;

const Application LogSquirl{ "/Applications/LogSquirl.app", "io.github.logsquirl" };
const Application Console{ "/System/Applications/Utilities/Console.app", "com.apple.Console" };
const Application TextEdit{ "/System/Applications/TextEdit.app", "com.apple.TextEdit" };

// LaunchServices that stand in for the machine: the defaults and the
// applications are what the test sets, and every change of a default waits
// for the test to answer it, the way macOS waits for the user to confirm.
struct LaunchServicesState {
    std::optional<Application> self = LogSquirl;
    std::map<QString, Application> defaults;
    std::map<QString, std::vector<Application>> handlers;
    std::vector<Application> installed{ LogSquirl, Console, TextEdit };

    struct Request {
        QString applicationPath;
        QString contentType;
        MacFileAssociations::LaunchServices::Done done;
    };
    std::vector<Request> requests;

    // The user confirms the request, or declines it with an error.
    void answer( size_t index, const QString& error = {} )
    {
        REQUIRE( index < requests.size() );
        const auto request = requests[ index ];
        if ( error.isEmpty() ) {
            for ( const auto& application : installed ) {
                if ( application.path == request.applicationPath ) {
                    defaults[ request.contentType ] = application;
                }
            }
        }
        request.done( error );
    }
};

class FakeLaunchServices : public MacFileAssociations::LaunchServices {
public:
    explicit FakeLaunchServices( LaunchServicesState& state )
        : state_( state )
    {
    }

    std::optional<Application> thisApplication() const override
    {
        return state_.self;
    }

    std::optional<Application> defaultApplication( const QString& contentType ) const override
    {
        const auto it = state_.defaults.find( contentType );
        return it == state_.defaults.end() ? std::nullopt : std::make_optional( it->second );
    }

    std::vector<Application> applications( const QString& contentType ) const override
    {
        const auto it = state_.handlers.find( contentType );
        return it == state_.handlers.end() ? std::vector<Application>{} : it->second;
    }

    bool isApplication( const QString& path ) const override
    {
        return std::any_of( state_.installed.begin(), state_.installed.end(),
                            [ &path ]( const Application& app ) { return app.path == path; } );
    }

    void setDefaultApplication( const QString& applicationPath, const QString& contentType,
                                Done done ) override
    {
        state_.requests.push_back( { applicationPath, contentType, std::move( done ) } );
    }

private:
    LaunchServicesState& state_;
};

struct Mac {
    QTemporaryDir root;
    LaunchServicesState launchServices;

    Mac()
    {
        REQUIRE( root.isValid() );
        launchServices.handlers[ "com.apple.log" ] = { Console, LogSquirl };
        launchServices.handlers[ "io.github.logsquirl.logcat" ] = { LogSquirl };
        launchServices.handlers[ "public.plain-text" ] = { TextEdit, LogSquirl };
        launchServices.defaults[ "com.apple.log" ] = Console;
        launchServices.defaults[ "public.plain-text" ] = TextEdit;
    }

    std::unique_ptr<QSettings> settings() const
    {
        return std::make_unique<QSettings>( root.filePath( "fileassociations.ini" ),
                                            QSettings::IniFormat );
    }

    std::unique_ptr<MacFileAssociations> associations()
    {
        return std::make_unique<MacFileAssociations>(
            std::make_unique<FakeLaunchServices>( launchServices ), settings() );
    }
};

} // namespace

TEST_CASE( "On macOS, the states are LaunchServices' defaults of each type's content type",
           "[fileassociations][macos]" )
{
    Mac mac;

    SECTION( "a run from the app bundle can associate, and says macOS asks to confirm" )
    {
        const auto associations = mac.associations();
        CHECK( associations->isAvailable() );
        CHECK( associations->unavailableReason().isEmpty() );
        CHECK( associations->applyNote().contains( "confirm" ) );
    }

    SECTION( "a run outside the app bundle cannot, and says why" )
    {
        mac.launchServices.self.reset();
        const auto associations = mac.associations();
        CHECK_FALSE( associations->isAvailable() );
        CHECK( associations->unavailableReason().contains( "app bundle" ) );
    }

    SECTION( "the default, an application offered, or neither" )
    {
        mac.launchServices.defaults[ "io.github.logsquirl.logcat" ] = LogSquirl;
        const auto associations = mac.associations();
        CHECK( associations->state( typeWithId( "logcat" ) ) == FileAssociationState::Default );
        CHECK( associations->state( typeWithId( "log" ) ) == FileAssociationState::Registered );
        CHECK( associations->state( typeWithId( "text" ) ) == FileAssociationState::Registered );
        CHECK( associations->state( typeWithId( "trace" ) )
               == FileAssociationState::NotRegistered );
    }

    SECTION( "another copy of LogSquirl is LogSquirl" )
    {
        mac.launchServices.defaults[ "com.apple.log" ]
            = Application{ "/Users/u/Downloads/LogSquirl.app", LogSquirl.identifier };
        const auto associations = mac.associations();
        CHECK( associations->state( typeWithId( "log" ) ) == FileAssociationState::Default );
    }
}

TEST_CASE( "On macOS, applying asks LaunchServices and the states follow the confirmations",
           "[fileassociations][macos]" )
{
    Mac mac;
    auto associations = mac.associations();
    QSignalSpy changed( associations.get(), &FileAssociations::statesChanged );

    SECTION( "making LogSquirl the default waits for the user to confirm" )
    {
        const auto result
            = associations->apply( { typeWithId( "log" ), typeWithId( "logcat" ) }, {} );
        CHECK( result.succeeded() );
        REQUIRE( mac.launchServices.requests.size() == 2 );
        CHECK( mac.launchServices.requests[ 0 ].applicationPath == LogSquirl.path );
        CHECK( mac.launchServices.requests[ 0 ].contentType == "com.apple.log" );
        CHECK( mac.launchServices.requests[ 1 ].contentType == "io.github.logsquirl.logcat" );

        // Not answered yet: as it was.
        CHECK( associations->state( typeWithId( "log" ) ) == FileAssociationState::Registered );

        mac.launchServices.answer( 0 );
        CHECK( changed.isEmpty() );
        mac.launchServices.answer( 1 );
        CHECK( changed.size() == 1 );
        CHECK( associations->state( typeWithId( "log" ) ) == FileAssociationState::Default );
        CHECK( associations->state( typeWithId( "logcat" ) ) == FileAssociationState::Default );
        // What opened .log before is kept, to give it back to.
        CHECK( associations->previousDefault( "com.apple.log" ) == Console.path );
        CHECK( associations->previousDefault( "io.github.logsquirl.logcat" ).isEmpty() );
    }

    SECTION( "a declined confirmation leaves the type as it was" )
    {
        associations->apply( { typeWithId( "log" ) }, {} );
        REQUIRE( mac.launchServices.requests.size() == 1 );
        mac.launchServices.answer( 0, "The user declined." );
        CHECK( changed.size() == 1 );
        CHECK( associations->state( typeWithId( "log" ) ) == FileAssociationState::Registered );
        CHECK( associations->previousDefault( "com.apple.log" ).isEmpty() );
    }

    SECTION( "giving a type back makes the application before LogSquirl its default again" )
    {
        associations->apply( { typeWithId( "text" ) }, {} );
        mac.launchServices.answer( 0 );
        REQUIRE( associations->state( typeWithId( "text" ) ) == FileAssociationState::Default );

        // Remembered across runs.
        associations = mac.associations();
        QSignalSpy changedAgain( associations.get(), &FileAssociations::statesChanged );
        const auto result = associations->apply( {}, { typeWithId( "text" ) } );
        CHECK( result.succeeded() );
        REQUIRE( mac.launchServices.requests.size() == 2 );
        CHECK( mac.launchServices.requests[ 1 ].applicationPath == TextEdit.path );
        CHECK( mac.launchServices.requests[ 1 ].contentType == "public.plain-text" );
        mac.launchServices.answer( 1 );
        CHECK( changedAgain.size() == 1 );
        CHECK( associations->state( typeWithId( "text" ) ) == FileAssociationState::Registered );
        CHECK( associations->previousDefault( "public.plain-text" ).isEmpty() );
    }

    SECTION( "a type nothing opened before LogSquirl cannot be given back, and it says so" )
    {
        mac.launchServices.defaults[ "io.github.logsquirl.logcat" ] = LogSquirl;
        const auto result = associations->apply( {}, { typeWithId( "logcat" ) } );
        CHECK_FALSE( result.succeeded() );
        CHECK( result.failed == QStringList{ "logcat" } );
        CHECK( result.error.contains( "Get Info" ) );
        CHECK( mac.launchServices.requests.empty() );
    }

    SECTION( "an application removed since cannot have it back either" )
    {
        associations->apply( { typeWithId( "log" ) }, {} );
        mac.launchServices.answer( 0 );
        mac.launchServices.installed = { LogSquirl };
        const auto result = associations->apply( {}, { typeWithId( "log" ) } );
        CHECK( result.failed == QStringList{ "log" } );
        CHECK( mac.launchServices.requests.size() == 1 );
    }

    SECTION( "a run that cannot associate changes nothing" )
    {
        mac.launchServices.self.reset();
        associations = mac.associations();
        const auto result = associations->apply( { typeWithId( "log" ) }, {} );
        CHECK( result.failed == QStringList{ "log" } );
        CHECK( mac.launchServices.requests.empty() );
    }
}
