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

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "fileassociations.h"
#include "filetypes.h"
#include "xdgfileassociations.h"

namespace {

QStringList idsOf( const std::vector<FileType>& types )
{
    QStringList ids;
    for ( const auto& type : types ) {
        ids << type.id;
    }
    return ids;
}

const FileType& typeWithId( const QString& id )
{
    const auto* type = FileTypes::find( id );
    REQUIRE( type != nullptr );
    return *type;
}

void writeFile( const QString& path, const QString& content )
{
    REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    file.write( content.toUtf8() );
}

QString readFile( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return {};
    }
    return QString::fromUtf8( file.readAll() );
}

const QString DesktopEntry = QStringLiteral( "[Desktop Entry]\n"
                                             "Name=LogSquirl\n"
                                             "Exec=logsquirl %F\n"
                                             "MimeType=text/x-log;application/x-logcat;\n"
                                             "Actions=Session;\n"
                                             "\n"
                                             "[Desktop Action Session]\n"
                                             "MimeType=text/x-trace;\n"
                                             "Exec=logsquirl --load-session %F\n" );

// A system with LogSquirl's desktop entry installed in a data directory and an
// xdg-mime that keeps its defaults in the user's mimeapps.list, the way the
// generic one does.
struct XdgSystem {
    QTemporaryDir root;
    XdgFileAssociations::Environment environment;
    QStringList calls;
    bool xdgMimeFails = false;

    XdgSystem()
    {
        REQUIRE( root.isValid() );
        environment.configHome = root.filePath( "config" );
        environment.dataHome = root.filePath( "data" );
        environment.dataDirs = { root.filePath( "usr/local/share" ), root.filePath( "usr/share" ) };
        environment.currentDesktops = { "gnome" };
        environment.hasXdgMime = true;
        writeFile( root.filePath( "usr/share/applications/logsquirl.desktop" ), DesktopEntry );
    }

    QString mimeAppsList() const
    {
        return QDir( environment.configHome ).filePath( "mimeapps.list" );
    }

    std::optional<QString> xdgMime( const QStringList& arguments )
    {
        calls << arguments.join( ' ' );
        if ( xdgMimeFails ) {
            return std::nullopt;
        }
        if ( arguments.size() == 3 && arguments[ 0 ] == "query" && arguments[ 1 ] == "default" ) {
            const auto defaults
                = XdgFiles::defaultApplications( readFile( mimeAppsList() ), arguments[ 2 ] );
            return defaults.isEmpty() ? QString{} : defaults.first() + "\n";
        }
        if ( arguments.size() >= 3 && arguments[ 0 ] == "default" ) {
            auto text = readFile( mimeAppsList() );
            if ( !text.contains( "[Default Applications]" ) ) {
                text += "[Default Applications]\n";
            }
            for ( const auto& mimeType : arguments.mid( 2 ) ) {
                text = XdgFiles::withoutDefaultApplication( text, mimeType, arguments[ 1 ] );
                text.replace(
                    "[Default Applications]\n",
                    QString( "[Default Applications]\n%1=%2;\n" ).arg( mimeType, arguments[ 1 ] ) );
            }
            writeFile( mimeAppsList(), text );
            return QString{};
        }
        return std::nullopt;
    }

    XdgFileAssociations associations()
    {
        return XdgFileAssociations( environment, [ this ]( const QStringList& arguments ) {
            return xdgMime( arguments );
        } );
    }
};

} // namespace

TEST_CASE( "The file types are the ones cmake/FileTypes.cmake declares", "[fileassociations]" )
{
    CHECK( idsOf( FileTypes::choices() )
           == QStringList{ "log", "logcat", "output", "trace", "text" } );
    CHECK( idsOf( FileTypes::openWithOnly() ) == QStringList{ "gz", "zip" } );

    const auto& logcat = typeWithId( "logcat" );
    CHECK( logcat.group == FileType::Group::Logs );
    CHECK( logcat.checkedByDefault );
    CHECK( logcat.label == "Android Logcat traces" );
    CHECK( logcat.shownAs == ".adb, .adb0-.adb9" );
    CHECK( logcat.name == "Android Logcat trace" );
    CHECK( logcat.extensions
           == QStringList{ "adb", "adb0", "adb1", "adb2", "adb3", "adb4", "adb5", "adb6", "adb7",
                           "adb8", "adb9" } );
    CHECK( logcat.mimeType == "application/x-logcat" );
    CHECK( logcat.uti == "io.github.logsquirl.logcat" );
    CHECK( logcat.progId == "LogSquirl.logcat" );

    const auto& log = typeWithId( "log" );
    CHECK( log.group == FileType::Group::Logs );
    CHECK( log.checkedByDefault );
    CHECK( log.mimeType == "text/x-log" );

    for ( const auto* id : { "output", "trace", "text" } ) {
        INFO( id );
        CHECK( typeWithId( id ).group == FileType::Group::Optional );
        CHECK_FALSE( typeWithId( id ).checkedByDefault );
    }
    CHECK( typeWithId( "output" ).extensions == QStringList{ "out", "err" } );
    CHECK( typeWithId( "text" ).mimeType == "text/plain" );

    CHECK( typeWithId( "gz" ).group == FileType::Group::OpenWith );
    CHECK( FileTypes::find( "ada" ) == nullptr );

    CHECK( FileTypes::groupTitle( FileType::Group::Logs ) == "Log files" );
    CHECK( FileTypes::groupTitle( FileType::Group::Optional ) == "More (optional)" );
    CHECK( FileTypes::label( logcat ) == "Android Logcat traces" );
    CHECK( FileTypes::shownAs( { log, typeWithId( "output" ) } ) == ".log, .out, .err" );
}

TEST_CASE( "Every label of a file type is there for lupdate to translate", "[fileassociations]" )
{
    QStringList translatable;
    for ( const auto* label : FileTypes::translatableLabels() ) {
        translatable << QString::fromUtf8( label );
    }
    for ( const auto& type : FileTypes::choices() ) {
        INFO( type.id.toStdString() );
        CHECK( translatable.contains( type.label ) );
    }
}

TEST_CASE( "Applying a choice changes only the types whose check differs from their state",
           "[fileassociations]" )
{
    const FileAssociationStates states = {
        { "log", FileAssociationState::Default },
        { "logcat", FileAssociationState::Registered },
        { "output", FileAssociationState::NotRegistered },
        { "trace", FileAssociationState::Default },
        { "text", FileAssociationState::Registered },
    };

    SECTION( "checked types become the default, unchecked defaults are given back" )
    {
        const auto plan = FileAssociationPlan::of( states, { "log", "logcat", "output" } );
        CHECK( idsOf( plan.makeDefault ) == QStringList{ "logcat", "output" } );
        CHECK( idsOf( plan.release ) == QStringList{ "trace" } );
        CHECK_FALSE( plan.isEmpty() );
    }

    SECTION( "the checks as the states are change nothing" )
    {
        CHECK( FileAssociationPlan::of( states, { "log", "trace" } ).isEmpty() );
    }

    SECTION( "a type another application opens is not touched when unchecked" )
    {
        const auto plan = FileAssociationPlan::of( states, { "log", "trace" } );
        CHECK_FALSE( idsOf( plan.release ).contains( "text" ) );
        CHECK_FALSE( idsOf( plan.release ).contains( "logcat" ) );
    }

    SECTION( "an id that is no choice is ignored" )
    {
        CHECK( FileAssociationPlan::of( states, { "log", "trace", "gz", "nothing" } ).isEmpty() );
    }
}

TEST_CASE( "mimeapps.list defaults are read and LogSquirl's alone is removed",
           "[fileassociations][xdg]" )
{
    const QString list = "# chosen by the user\n"
                         "[Added Associations]\n"
                         "text/x-log=logsquirl.desktop;\n"
                         "\n"
                         "[Default Applications]\n"
                         "text/x-log=logsquirl.desktop;org.gnome.TextEditor.desktop;\n"
                         "application/x-logcat = logsquirl.desktop\n"
                         "text/plain=org.gnome.TextEditor.desktop;\n"
                         "\n"
                         "[Removed Associations]\n"
                         "application/x-logcat=logsquirl.desktop;\n";

    CHECK( XdgFiles::defaultApplications( list, "text/x-log" )
           == QStringList{ "logsquirl.desktop", "org.gnome.TextEditor.desktop" } );
    CHECK( XdgFiles::defaultApplications( list, "application/x-logcat" )
           == QStringList{ "logsquirl.desktop" } );
    CHECK( XdgFiles::defaultApplications( list, "text/x-trace" ).isEmpty() );
    CHECK( XdgFiles::defaultApplications( "", "text/x-log" ).isEmpty() );

    SECTION( "another application stays the default after LogSquirl" )
    {
        CHECK( XdgFiles::withoutDefaultApplication( list, "text/x-log", "logsquirl.desktop" )
               == QString( list ).replace(
                   "text/x-log=logsquirl.desktop;org.gnome.TextEditor.desktop;\n",
                   "text/x-log=org.gnome.TextEditor.desktop;\n" ) );
    }

    SECTION( "a type LogSquirl alone was the default for loses its line" )
    {
        CHECK(
            XdgFiles::withoutDefaultApplication( list, "application/x-logcat", "logsquirl.desktop" )
            == QString( list ).remove( "application/x-logcat = logsquirl.desktop\n" ) );
    }

    SECTION( "a type LogSquirl is not the default for is left alone" )
    {
        CHECK( XdgFiles::withoutDefaultApplication( list, "text/plain", "logsquirl.desktop" )
               == list );
        CHECK( XdgFiles::withoutDefaultApplication( list, "text/x-trace", "logsquirl.desktop" )
               == list );
    }
}

TEST_CASE( "A desktop entry's MIME types are those of its [Desktop Entry] group",
           "[fileassociations][xdg]" )
{
    CHECK( XdgFiles::desktopEntryMimeTypes( DesktopEntry )
           == QStringList{ "text/x-log", "application/x-logcat" } );
    CHECK( XdgFiles::desktopEntryMimeTypes( "[Desktop Entry]\nName=x\n" ).isEmpty() );
}

TEST_CASE( "On Linux, xdg-mime makes LogSquirl the default and the user's mimeapps.list gives a "
           "type back",
           "[fileassociations][xdg]" )
{
    XdgSystem system;

    SECTION( "an installed LogSquirl with xdg-mime can associate" )
    {
        auto associations = system.associations();
        CHECK( associations.isAvailable() );
        CHECK( associations.unavailableReason().isEmpty() );
        CHECK( associations.desktopEntryPath()
               == system.root.filePath( "usr/share/applications/logsquirl.desktop" ) );
    }

    SECTION( "an AppImage run cannot, and says why" )
    {
        system.environment.appImage = true;
        auto associations = system.associations();
        CHECK_FALSE( associations.isAvailable() );
        CHECK( associations.unavailableReason().contains( "AppImage" ) );
    }

    SECTION( "a run without the desktop entry cannot, and says why" )
    {
        QFile::remove( system.root.filePath( "usr/share/applications/logsquirl.desktop" ) );
        auto associations = system.associations();
        CHECK_FALSE( associations.isAvailable() );
        CHECK( associations.unavailableReason().contains( "desktop entry" ) );
    }

    SECTION( "a desktop without xdg-mime cannot, and says why" )
    {
        system.environment.hasXdgMime = false;
        auto associations = system.associations();
        CHECK_FALSE( associations.isAvailable() );
        CHECK( associations.unavailableReason().contains( "xdg-mime" ) );
    }

    SECTION( "the user's own desktop entry comes first" )
    {
        writeFile( QDir( system.environment.dataHome ).filePath( "applications/logsquirl.desktop" ),
                   "[Desktop Entry]\nMimeType=text/plain;\n" );
        auto associations = system.associations();
        CHECK( associations.state( typeWithId( "text" ) ) == FileAssociationState::Registered );
        CHECK( associations.state( typeWithId( "log" ) ) == FileAssociationState::NotRegistered );
    }

    SECTION( "the states follow xdg-mime and the desktop entry" )
    {
        writeFile( system.mimeAppsList(), "[Default Applications]\n"
                                          "text/x-log=logsquirl.desktop;\n"
                                          "application/x-logcat=ada-editor.desktop;\n" );
        auto associations = system.associations();
        CHECK( associations.state( typeWithId( "log" ) ) == FileAssociationState::Default );
        CHECK( associations.state( typeWithId( "logcat" ) ) == FileAssociationState::Registered );
        // Not in the desktop entry.
        CHECK( associations.state( typeWithId( "trace" ) ) == FileAssociationState::NotRegistered );
        CHECK( system.calls.contains( "query default text/x-log" ) );
    }

    SECTION( "the states ask xdg-mime once per type and read the desktop entry" )
    {
        writeFile( system.mimeAppsList(), "[Default Applications]\n"
                                          "text/x-log=logsquirl.desktop;\n" );
        const auto states = system.associations().states();
        CHECK( states.at( "log" ) == FileAssociationState::Default );
        CHECK( states.at( "logcat" ) == FileAssociationState::Registered );
        CHECK( states.at( "trace" ) == FileAssociationState::NotRegistered );
        CHECK( system.calls.size() == static_cast<qsizetype>( FileTypes::choices().size() ) );
    }

    SECTION( "an xdg-mime that does not answer is not asked again for the other types" )
    {
        system.xdgMimeFails = true;
        const auto states = system.associations().states();
        CHECK( system.calls == QStringList{ "query default text/x-log" } );
        // What the desktop entry says is all that is known.
        CHECK( states.at( "log" ) == FileAssociationState::Registered );
        CHECK( states.at( "logcat" ) == FileAssociationState::Registered );
        CHECK( states.at( "trace" ) == FileAssociationState::NotRegistered );
    }

    SECTION( "applying makes the checked types LogSquirl's with one xdg-mime default" )
    {
        auto associations = system.associations();
        const auto result
            = associations.apply( { typeWithId( "log" ), typeWithId( "logcat" ) }, {} );
        CHECK( result.succeeded() );
        CHECK(
            system.calls.contains( "default logsquirl.desktop text/x-log application/x-logcat" ) );
        CHECK( associations.state( typeWithId( "logcat" ) ) == FileAssociationState::Default );
        CHECK( associations.state( typeWithId( "log" ) ) == FileAssociationState::Default );
    }

    SECTION( "giving a type back removes LogSquirl from every mimeapps.list of the user" )
    {
        const auto desktopList
            = QDir( system.environment.configHome ).filePath( "gnome-mimeapps.list" );
        const auto oldList
            = QDir( system.environment.dataHome ).filePath( "applications/mimeapps.list" );
        writeFile( system.mimeAppsList(),
                   "[Default Applications]\n"
                   "application/x-logcat=logsquirl.desktop;ada-editor.desktop;\n"
                   "text/x-log=logsquirl.desktop;\n" );
        writeFile( desktopList,
                   "[Default Applications]\napplication/x-logcat=logsquirl.desktop;\n" );
        writeFile( oldList, "[Default Applications]\napplication/x-logcat=logsquirl.desktop;\n" );

        auto associations = system.associations();
        REQUIRE( associations.state( typeWithId( "logcat" ) ) == FileAssociationState::Default );
        const auto result = associations.apply( {}, { typeWithId( "logcat" ) } );

        CHECK( result.succeeded() );
        CHECK( readFile( system.mimeAppsList() )
               == "[Default Applications]\n"
                  "application/x-logcat=ada-editor.desktop;\n"
                  "text/x-log=logsquirl.desktop;\n" );
        CHECK( readFile( desktopList ) == "[Default Applications]\n" );
        CHECK( readFile( oldList ) == "[Default Applications]\n" );
        CHECK( associations.state( typeWithId( "logcat" ) ) == FileAssociationState::Registered );
        CHECK( associations.state( typeWithId( "log" ) ) == FileAssociationState::Default );
        // No mimeapps.list is created where there was none.
        CHECK_FALSE( QFile::exists(
            QDir( system.environment.configHome ).filePath( "kde-mimeapps.list" ) ) );
    }

    SECTION( "an xdg-mime that fails names the types it could not change" )
    {
        auto associations = system.associations();
        system.xdgMimeFails = true;
        const auto result = associations.apply( { typeWithId( "trace" ) }, {} );
        CHECK_FALSE( result.succeeded() );
        CHECK( result.failed == QStringList{ "trace" } );
        CHECK_FALSE( result.error.isEmpty() );
    }
}

TEST_CASE( "The user's mimeapps.list files are the desktops' and the general one",
           "[fileassociations][xdg]" )
{
    XdgFileAssociations::Environment environment;
    environment.configHome = "/home/u/.config";
    environment.dataHome = "/home/u/.local/share";
    environment.currentDesktops = { "ubuntu", "gnome" };
    const XdgFileAssociations associations(
        environment, []( const QStringList& ) { return std::optional<QString>{}; } );
    CHECK( associations.userMimeAppsLists()
           == QStringList{ "/home/u/.config/ubuntu-mimeapps.list",
                           "/home/u/.config/gnome-mimeapps.list", "/home/u/.config/mimeapps.list",
                           "/home/u/.local/share/applications/mimeapps.list" } );
}
