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

#include <map>
#include <set>
#include <utility>

#include "filetypes.h"
#include "windowsfileassociations.h"

namespace {

const FileType& typeWithId( const QString& id )
{
    const auto* type = FileTypes::find( id );
    REQUIRE( type != nullptr );
    return *type;
}

using Hive = WindowsFileAssociations::Hive;

// A registry that stands in for the machine: keys and value names are not
// case sensitive, as Windows' are not, and a key exists once anything was
// written under it.
struct Registry {
    // By hive and key in lower case: the values by name in lower case.
    std::map<std::pair<Hive, QString>, std::map<QString, QString>> keys;
    int associationsChanged = 0;
    QStringList openedDefaultApps;
    bool openingDefaultAppsFails = false;

    void set( Hive hive, const QString& key, const QString& name, const QString& value )
    {
        auto path = key.toLower();
        keys[ { hive, path } ][ name.toLower() ] = value;
        // Its parents exist too.
        for ( auto separator = path.lastIndexOf( '\\' ); separator > 0;
              separator = path.lastIndexOf( '\\' ) ) {
            path.truncate( separator );
            keys[ { hive, path } ];
        }
    }

    std::optional<QString> get( Hive hive, const QString& key, const QString& name = {} ) const
    {
        const auto it = keys.find( { hive, key.toLower() } );
        if ( it == keys.end() ) {
            return std::nullopt;
        }
        const auto value = it->second.find( name.toLower() );
        return value == it->second.end() ? std::nullopt : std::make_optional( value->second );
    }

    bool has( Hive hive, const QString& key ) const
    {
        return keys.count( { hive, key.toLower() } ) > 0;
    }

    void remove( Hive hive, const QString& key )
    {
        const auto path = key.toLower();
        for ( auto it = keys.begin(); it != keys.end(); ) {
            if ( it->first.first == hive
                 && ( it->first.second == path || it->first.second.startsWith( path + '\\' ) ) ) {
                it = keys.erase( it );
            }
            else {
                ++it;
            }
        }
    }

    // What the installer of #719 registers for the machine, for a type it
    // makes the default.
    void install( const FileType& type, const QString& executable, bool makeDefault = true )
    {
        const auto classes = QString( "Software\\Classes\\" );
        set( Hive::LocalMachine, classes + type.progId, {}, type.name );
        set( Hive::LocalMachine, classes + type.progId + "\\DefaultIcon", {}, executable + ",1" );
        set( Hive::LocalMachine, classes + type.progId + "\\shell\\open\\command", {},
             '"' + executable + "\" \"%1\"" );
        for ( const auto& extension : type.extensions ) {
            set( Hive::LocalMachine, classes + "." + extension + "\\OpenWithProgids", type.progId,
                 {} );
            if ( makeDefault ) {
                set( Hive::LocalMachine, classes + "." + extension, {}, type.progId );
            }
        }
    }

    // The user chooses the ProgID for the extension in Windows.
    void choose( const QString& extension, const QString& progId )
    {
        set( Hive::CurrentUser,
             "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\." + extension
                 + "\\UserChoice",
             "ProgId", progId );
    }
};

class FakeSystem : public WindowsFileAssociations::System {
public:
    explicit FakeSystem( Registry& registry )
        : registry_( registry )
    {
    }

    std::optional<QString> value( Hive hive, const QString& key,
                                  const QString& name ) const override
    {
        return registry_.get( hive, key, name );
    }

    bool hasKey( Hive hive, const QString& key ) const override
    {
        return registry_.has( hive, key );
    }

    QStringList valueNames( Hive hive, const QString& key ) const override
    {
        QStringList names;
        const auto it = registry_.keys.find( { hive, key.toLower() } );
        if ( it != registry_.keys.end() ) {
            for ( const auto& value : it->second ) {
                names << value.first;
            }
        }
        return names;
    }

    QStringList subkeys( Hive hive, const QString& key ) const override
    {
        QStringList names;
        const auto prefix = key.toLower() + '\\';
        for ( const auto& entry : registry_.keys ) {
            if ( entry.first.first == hive && entry.first.second.startsWith( prefix )
                 && !entry.first.second.mid( prefix.size() ).contains( '\\' ) ) {
                names << entry.first.second.mid( prefix.size() );
            }
        }
        return names;
    }

    bool setValue( const QString& key, const QString& name, const QString& value ) override
    {
        registry_.set( Hive::CurrentUser, key, name, value );
        return true;
    }

    bool removeValue( const QString& key, const QString& name ) override
    {
        const auto it = registry_.keys.find( { Hive::CurrentUser, key.toLower() } );
        if ( it != registry_.keys.end() ) {
            it->second.erase( name.toLower() );
        }
        return true;
    }

    bool removeKey( const QString& key ) override
    {
        registry_.remove( Hive::CurrentUser, key );
        return true;
    }

    void associationsChanged() override
    {
        ++registry_.associationsChanged;
    }

    bool openDefaultApps( const QString& registeredApplication ) override
    {
        registry_.openedDefaultApps << registeredApplication;
        return !registry_.openingDefaultAppsFails;
    }

private:
    Registry& registry_;
};

const QString Installed = "C:\\Program Files\\logsquirl\\logsquirl.exe";
const QString Portable = "D:\\Tools\\LogSquirl\\logsquirl_portable.exe";

struct Windows {
    Registry registry;
    WindowsFileAssociations::Environment environment{ Installed, false };

    std::unique_ptr<WindowsFileAssociations> associations()
    {
        return std::make_unique<WindowsFileAssociations>(
            environment, std::make_unique<FakeSystem>( registry ) );
    }

    std::optional<QString> user( const QString& key, const QString& name = {} ) const
    {
        return registry.get( Hive::CurrentUser, key, name );
    }
};

const QString Classes = "Software\\Classes\\";

} // namespace

TEST_CASE( "On Windows, the page says the user confirms in the settings, and warns a portable run",
           "[fileassociations][windows]" )
{
    Windows windows;

    SECTION( "installed" )
    {
        const auto associations = windows.associations();
        CHECK( associations->isAvailable() );
        CHECK( associations->unavailableReason().isEmpty() );
        CHECK( associations->applyNote().contains( "Default apps" ) );
        CHECK_FALSE( associations->applyNote().contains( "move" ) );
    }

    SECTION( "portable" )
    {
        windows.environment = { Portable, true };
        const auto associations = windows.associations();
        CHECK( associations->isAvailable() );
        CHECK( associations->applyNote().contains( "Default apps" ) );
        CHECK( associations->applyNote().contains(
            "If you move LogSquirl, these associations stop working." ) );
    }
}

TEST_CASE( "On Windows, the states are the user's actual choice", "[fileassociations][windows]" )
{
    Windows windows;

    SECTION( "nothing registered" )
    {
        CHECK( windows.associations()->state( typeWithId( "trace" ) )
               == FileAssociationState::NotRegistered );
    }

    SECTION( "the installer's default for the machine, with no choice of the user" )
    {
        windows.registry.install( typeWithId( "log" ), Installed );
        windows.registry.install( typeWithId( "text" ), Installed, false );
        const auto associations = windows.associations();
        CHECK( associations->openingProgId( "log" ) == "LogSquirl.log" );
        CHECK( associations->state( typeWithId( "log" ) ) == FileAssociationState::Default );
        CHECK( associations->state( typeWithId( "text" ) ) == FileAssociationState::Registered );
    }

    SECTION( "the user's choice wins over the installer's default" )
    {
        windows.registry.install( typeWithId( "log" ), Installed );
        windows.registry.choose( "log", "txtfile" );
        CHECK( windows.associations()->state( typeWithId( "log" ) )
               == FileAssociationState::Registered );

        // ProgIDs are not case sensitive.
        windows.registry.choose( "log", "logsquirl.LOG" );
        CHECK( windows.associations()->state( typeWithId( "log" ) )
               == FileAssociationState::Default );
    }

    SECTION( "the newer UserChoiceLatest wins over UserChoice" )
    {
        windows.registry.install( typeWithId( "log" ), Installed, false );
        windows.registry.choose( "log", "txtfile" );
        windows.registry.set( Hive::CurrentUser,
                              "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\."
                              "log\\UserChoiceLatest",
                              "ProgId", "LogSquirl.log" );
        CHECK( windows.associations()->state( typeWithId( "log" ) )
               == FileAssociationState::Default );
    }

    SECTION( "a type is the default only where every extension of it is" )
    {
        windows.registry.install( typeWithId( "logcat" ), Installed );
        windows.registry.choose( "adb", "AdaFile" );
        CHECK( windows.associations()->state( typeWithId( "logcat" ) )
               == FileAssociationState::Registered );
    }

    SECTION( "a choice of a ProgID that is gone opens nothing of LogSquirl's" )
    {
        windows.registry.choose( "trace", "LogSquirl.trace" );
        CHECK( windows.associations()->state( typeWithId( "trace" ) )
               == FileAssociationState::NotRegistered );
    }
}

TEST_CASE( "On Windows, applying registers for the current user and opens Default apps",
           "[fileassociations][windows]" )
{
    Windows windows;

    SECTION( "the installed build registers its ProgIDs for the user, then the user confirms" )
    {
        windows.registry.install( typeWithId( "log" ), Installed );
        const auto keysOfTheMachine = windows.registry.keys;
        auto associations = windows.associations();
        QSignalSpy changed( associations.get(), &FileAssociations::statesChanged );

        const auto result = associations->apply( { typeWithId( "trace" ) }, {} );
        CHECK( result.succeeded() );

        CHECK( windows.user( Classes + "LogSquirl.trace" ) == "Trace file" );
        CHECK( windows.user( Classes + "LogSquirl.trace\\DefaultIcon" ) == Installed + ",1" );
        CHECK( windows.user( Classes + "LogSquirl.trace\\shell\\open\\command" )
               == "\"" + Installed + "\" \"%1\"" );
        CHECK( windows.user( Classes + ".trace\\OpenWithProgids", "LogSquirl.trace" ) == "" );
        // Windows does not let an application set the default: the user does.
        CHECK_FALSE( windows.user( Classes + ".trace" ).has_value() );
        CHECK( windows.user( "Software\\RegisteredApplications", "LogSquirl" )
               == "Software\\LogSquirl\\Capabilities" );
        CHECK( windows.user( "Software\\LogSquirl\\Capabilities", "ApplicationName" )
               == "LogSquirl" );
        CHECK( windows.user( "Software\\LogSquirl\\Capabilities\\FileAssociations", ".trace" )
               == "LogSquirl.trace" );
        CHECK( windows.registry.associationsChanged == 1 );
        CHECK( windows.registry.openedDefaultApps == QStringList{ "LogSquirl" } );

        // Nothing of the machine was touched.
        for ( const auto& [ key, values ] : keysOfTheMachine ) {
            CHECK( windows.registry.keys[ key ] == values );
        }

        // Registered until the user confirms.
        CHECK( associations->state( typeWithId( "trace" ) ) == FileAssociationState::Registered );
        CHECK( associations->isWatching() );
        associations->checkForChanges();
        CHECK( changed.isEmpty() );

        windows.registry.choose( "trace", "LogSquirl.trace" );
        associations->checkForChanges();
        CHECK( changed.size() == 1 );
        CHECK( associations->state( typeWithId( "trace" ) ) == FileAssociationState::Default );
        CHECK_FALSE( associations->isWatching() );
    }

    SECTION( "the portable build registers its own executable" )
    {
        windows.environment = { Portable, true };
        windows.associations()->apply( { typeWithId( "logcat" ) }, {} );
        CHECK( windows.user( Classes + "LogSquirl.logcat\\shell\\open\\command" )
               == "\"" + Portable + "\" \"%1\"" );
        CHECK( windows.user( Classes + "LogSquirl.logcat\\DefaultIcon" ) == Portable + ",1" );
        for ( const auto& extension : typeWithId( "logcat" ).extensions ) {
            INFO( extension.toStdString() );
            CHECK(
                windows.user( Classes + "." + extension + "\\OpenWithProgids", "LogSquirl.logcat" )
                    .has_value() );
            CHECK( windows.user( "Software\\LogSquirl\\Capabilities\\FileAssociations",
                                 "." + extension )
                   == "LogSquirl.logcat" );
        }
    }

    SECTION( "a type that is the default already does not open Default apps" )
    {
        windows.registry.install( typeWithId( "log" ), Installed );
        windows.registry.choose( "trace", "LogSquirl.trace" );
        windows.associations()->apply( { typeWithId( "trace" ) }, {} );
        CHECK( windows.registry.openedDefaultApps.isEmpty() );
    }

    SECTION( "settings that do not open say so" )
    {
        windows.registry.openingDefaultAppsFails = true;
        const auto result = windows.associations()->apply( { typeWithId( "trace" ) }, {} );
        CHECK_FALSE( result.succeeded() );
        CHECK( result.error.contains( "Default apps" ) );
    }

    SECTION( "the watch stops after a while" )
    {
        auto associations = windows.associations();
        associations->apply( { typeWithId( "trace" ) }, {} );
        REQUIRE( associations->isWatching() );
        for ( int tick = 0; tick < 1000 && associations->isWatching(); ++tick ) {
            associations->checkForChanges();
        }
        CHECK_FALSE( associations->isWatching() );
    }
}

TEST_CASE( "On Windows, giving a type back removes the current user's registration only",
           "[fileassociations][windows]" )
{
    Windows windows;
    // Another application is offered for .trace too.
    windows.registry.set( Hive::CurrentUser, Classes + ".trace\\OpenWithProgids", "Editor.trace",
                          {} );
    auto associations = windows.associations();
    associations->apply( { typeWithId( "trace" ), typeWithId( "output" ) }, {} );
    windows.registry.choose( "trace", "LogSquirl.trace" );
    REQUIRE( associations->state( typeWithId( "trace" ) ) == FileAssociationState::Default );

    SECTION( "the user's registration goes, other applications' entries stay" )
    {
        const auto result = associations->apply( {}, { typeWithId( "trace" ) } );
        CHECK( result.succeeded() );
        CHECK_FALSE( windows.registry.has( Hive::CurrentUser, Classes + "LogSquirl.trace" ) );
        CHECK_FALSE(
            windows.user( Classes + ".trace\\OpenWithProgids", "LogSquirl.trace" ).has_value() );
        CHECK( windows.user( Classes + ".trace\\OpenWithProgids", "Editor.trace" ).has_value() );
        CHECK_FALSE( windows.user( "Software\\LogSquirl\\Capabilities\\FileAssociations", ".trace" )
                         .has_value() );
        // Still registered for the other type.
        CHECK( windows.user( "Software\\RegisteredApplications", "LogSquirl" ).has_value() );
        CHECK( associations->state( typeWithId( "trace" ) )
               == FileAssociationState::NotRegistered );
        CHECK( windows.registry.associationsChanged == 2 );
    }

    SECTION( "the last type takes the capabilities and the empty keys with it" )
    {
        associations->apply( {}, { typeWithId( "trace" ), typeWithId( "output" ) } );
        CHECK_FALSE(
            windows.registry.has( Hive::CurrentUser, "Software\\LogSquirl\\Capabilities" ) );
        CHECK_FALSE( windows.user( "Software\\RegisteredApplications", "LogSquirl" ).has_value() );
        CHECK_FALSE( windows.registry.has( Hive::CurrentUser, Classes + ".out" ) );
        CHECK_FALSE( windows.registry.has( Hive::CurrentUser, Classes + ".err" ) );
        CHECK( windows.registry.has( Hive::CurrentUser, Classes + ".trace\\OpenWithProgids" ) );
    }

    SECTION( "the installer's registration for the machine stays, and the page says so" )
    {
        windows.registry.install( typeWithId( "trace" ), Installed );
        const auto result = associations->apply( {}, { typeWithId( "trace" ) } );
        CHECK_FALSE( windows.registry.has( Hive::CurrentUser, Classes + "LogSquirl.trace" ) );
        CHECK( windows.registry.has( Hive::LocalMachine, Classes + "LogSquirl.trace" ) );
        // The user's choice still names it, and the machine still has it.
        CHECK( associations->state( typeWithId( "trace" ) ) == FileAssociationState::Default );
        CHECK( result.failed == QStringList{ "trace" } );
        CHECK( result.error.contains( "Default apps" ) );
    }
}
