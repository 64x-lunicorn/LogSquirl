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

#include "installsource.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace logsquirl::versioncheck;

namespace {

// A directory standing in for the machine's file system: the detection reads
// everything below it, never the real machine (#382).
class FakeMachine {
public:
    FakeMachine()
    {
        REQUIRE( root_.isValid() );
    }

    QString root() const
    {
        return root_.path();
    }

    QString path( const QString& inside ) const
    {
        return root_.filePath( inside.mid( 1 ) );
    }

    void write( const QString& inside, const QByteArray& content = {} )
    {
        const auto file = path( inside );
        QDir().mkpath( QFileInfo( file ).absolutePath() );
        QFile out( file );
        REQUIRE( out.open( QIODevice::WriteOnly ) );
        out.write( content );
    }

    void link( const QString& inside, const QString& target )
    {
        QDir().mkpath( QFileInfo( path( inside ) ).absolutePath() );
        REQUIRE( QFile::link( path( target ), path( inside ) ) );
    }

private:
    QTemporaryDir root_;
};

InstallEnvironment environment( const FakeMachine& machine, const QString& executable,
                                QString rpmOwner = {} )
{
    return InstallEnvironment{ machine.root(), executable,
                               [ rpmOwner ]( const QString& ) { return rpmOwner; } };
}

const auto AptSource = QByteArrayLiteral(
    "Types: deb\nURIs: https://packages.lunicorn-lab.de/apt\nSuites: noble\n" );
const auto DnfRepo = QByteArrayLiteral(
    "[logsquirl]\nbaseurl=https://packages.lunicorn-lab.de/dnf/fedora\nrepo_gpgcheck=1\n" );

} // namespace

SCENARIO( "A Homebrew cask install is recognised by the Caskroom", "[versioncheck][installsource]" )
{
    GIVEN( "An app in Applications that the cask's Caskroom entry points to" )
    {
        FakeMachine machine;
        const auto bundle = QStringLiteral( "/Applications/logsquirl.app" );
        const auto executable = bundle + "/Contents/MacOS/logsquirl";
        machine.write( executable );

        WHEN( "The Caskroom of Apple Silicon Homebrew links to that bundle" )
        {
            machine.link( "/opt/homebrew/Caskroom/logsquirl/26.7.0/logsquirl.app", bundle );

            THEN( "The install source is Homebrew" )
            {
                CHECK( detectInstallSource( environment( machine, executable ) )
                       == InstallSource::Homebrew );
            }
        }

        WHEN( "The Caskroom of Intel Homebrew links to that bundle" )
        {
            machine.link( "/usr/local/Caskroom/logsquirl/26.7.0/logsquirl.app", bundle );

            THEN( "The install source is Homebrew" )
            {
                CHECK( detectInstallSource( environment( machine, executable ) )
                       == InstallSource::Homebrew );
            }
        }

        WHEN( "Homebrew has no cask of that name" )
        {
            machine.link( "/opt/homebrew/Caskroom/other/1.0/logsquirl.app", bundle );

            THEN( "It is a dragged DMG: nothing is known" )
            {
                CHECK( detectInstallSource( environment( machine, executable ) )
                       == InstallSource::Unknown );
            }
        }

        WHEN( "The cask's link points to another copy of the app" )
        {
            machine.write( "/Users/me/Downloads/logsquirl.app/Contents/MacOS/logsquirl" );
            machine.link( "/opt/homebrew/Caskroom/logsquirl/26.7.0/logsquirl.app",
                          "/Users/me/Downloads/logsquirl.app" );

            THEN( "The running bundle is not the cask's: nothing is known" )
            {
                CHECK( detectInstallSource( environment( machine, executable ) )
                       == InstallSource::Unknown );
            }
        }
    }
}

SCENARIO( "A deb from the LogSquirl APT repository is recognised", "[versioncheck][installsource]" )
{
    GIVEN( "A binary that the installed deb lists" )
    {
        FakeMachine machine;
        const auto executable = QStringLiteral( "/usr/bin/logsquirl" );
        machine.write( executable );
        machine.write( "/var/lib/dpkg/info/logsquirl.list",
                       ( "/usr\n" + executable + "\n" ).toUtf8() );

        WHEN( "The repository's source file is configured" )
        {
            machine.write( "/etc/apt/sources.list.d/logsquirl.sources", AptSource );

            THEN( "The install source is apt" )
            {
                CHECK( detectInstallSource( environment( machine, executable ) )
                       == InstallSource::Apt );
            }
        }

        WHEN( "The repository is configured in an old-style list file" )
        {
            machine.write( "/etc/apt/sources.list.d/logsquirl.list",
                           "deb [signed-by=/etc/apt/keyrings/logsquirl.asc] "
                           "https://packages.lunicorn-lab.de/apt noble main\n" );

            THEN( "The install source is apt" )
            {
                CHECK( detectInstallSource( environment( machine, executable ) )
                       == InstallSource::Apt );
            }
        }

        WHEN( "Only another repository is configured: the deb was installed by hand" )
        {
            machine.write( "/etc/apt/sources.list.d/other.sources",
                           "Types: deb\nURIs: https://example.org/apt\n" );

            THEN( "The link to the release page stays" )
            {
                CHECK( detectInstallSource( environment( machine, executable ) )
                       == InstallSource::Unknown );
            }
        }
    }

    GIVEN( "The repository configured, but a binary the deb does not list" )
    {
        FakeMachine machine;
        machine.write( "/etc/apt/sources.list.d/logsquirl.sources", AptSource );
        machine.write( "/var/lib/dpkg/info/logsquirl.list", "/usr/bin/logsquirl\n" );
        machine.write( "/home/me/logsquirl" );

        THEN( "An AppImage or a self-built binary is not an apt install" )
        {
            CHECK( detectInstallSource( environment( machine, "/home/me/logsquirl" ) )
                   == InstallSource::Unknown );
        }
    }
}

SCENARIO( "An RPM from the LogSquirl DNF repository is recognised",
          "[versioncheck][installsource]" )
{
    GIVEN( "A binary that the installed logsquirl RPM owns" )
    {
        FakeMachine machine;
        const auto executable = QStringLiteral( "/usr/bin/logsquirl" );
        machine.write( executable );

        WHEN( "The repository's .repo file is configured" )
        {
            machine.write( "/etc/yum.repos.d/logsquirl.repo", DnfRepo );

            THEN( "The install source is dnf" )
            {
                CHECK( detectInstallSource( environment( machine, executable, "logsquirl" ) )
                       == InstallSource::Dnf );
            }

            THEN( "A binary that belongs to no package is not a dnf install" )
            {
                CHECK( detectInstallSource( environment( machine, executable, "" ) )
                       == InstallSource::Unknown );
            }

            THEN( "A binary owned by another package is not a dnf install" )
            {
                CHECK( detectInstallSource( environment( machine, executable, "other" ) )
                       == InstallSource::Unknown );
            }
        }

        WHEN( "No LogSquirl repository is configured: the RPM was installed by hand" )
        {
            THEN( "The link to the release page stays" )
            {
                CHECK( detectInstallSource( environment( machine, executable, "logsquirl" ) )
                       == InstallSource::Unknown );
            }
        }
    }
}

SCENARIO( "Nothing on the machine means nothing is known", "[versioncheck][installsource]" )
{
    GIVEN( "An empty machine" )
    {
        FakeMachine machine;

        THEN( "A Windows build, an AppImage or a dragged DMG keeps the link" )
        {
            CHECK( detectInstallSource( environment( machine, "/anywhere/logsquirl" ) )
                   == InstallSource::Unknown );
        }
    }
}

SCENARIO( "The update notice names the package manager only for a known install source",
          "[versioncheck][installsource]" )
{
    const auto url
        = QStringLiteral( "https://github.com/64x-lunicorn/LogSquirl/releases/tag/v26.10.0" );
    const QStringList changes{ "26.10.0: Notes" };

    GIVEN( "An unknown install source" )
    {
        const auto html = updateNoticeHtml( "26.10.0", url, changes, InstallSource::Unknown );

        THEN( "The notice links to the release page and lists the changes" )
        {
            CHECK( html.contains( "href=\"" + url + "\"" ) );
            CHECK( html.contains( "<li>26.10.0: Notes</li>" ) );
            CHECK_FALSE( html.contains( "upgrade" ) );
        }
    }

    GIVEN( "A package manager install" )
    {
        struct Case {
            InstallSource source;
            const char* command;
        };
        for ( const auto& [ source, command ] :
              { Case{ InstallSource::Homebrew, "brew upgrade --cask logsquirl" },
                Case{ InstallSource::Apt, "sudo apt upgrade" },
                Case{ InstallSource::Dnf, "sudo dnf upgrade" } } ) {
            const auto html = updateNoticeHtml( "26.10.0", url, changes, source );

            THEN( QString( "The notice tells to run %1 instead of linking the release page" )
                      .arg( command )
                      .toStdString() )
            {
                CHECK( html.contains( command ) );
                CHECK_FALSE( html.contains( "href=" ) );
                CHECK( html.contains( "26.10.0" ) );
                CHECK( html.contains( "<li>26.10.0: Notes</li>" ) );
            }
        }
    }
}
