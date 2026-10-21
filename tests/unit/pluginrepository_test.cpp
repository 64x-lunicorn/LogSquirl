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

#include "pluginrepository.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <kzip.h>

using logsquirl::plugins::PluginRepository;
using logsquirl::plugins::ReleaseAsset;

namespace {

/// A network that records every request and serves the path of any URL it
/// is asked for from a folder, so a catalog of https URLs can be read and
/// downloaded without a server (#818).
class RecordingNetwork : public QNetworkAccessManager {
public:
    explicit RecordingNetwork( QString root )
        : root_( std::move( root ) )
    {
    }

    QList<QUrl> requested;

protected:
    QNetworkReply* createRequest( Operation op, const QNetworkRequest& request,
                                  QIODevice* outgoingData ) override
    {
        requested << request.url();
        QNetworkRequest local( request );
        local.setUrl(
            QUrl::fromLocalFile( QDir( root_ ).filePath( request.url().path().mid( 1 ) ) ) );
        return QNetworkAccessManager::createRequest( op, local, outgoingData );
    }

private:
    QString root_;
};

/// Writes content to name in root and returns its SHA-256 as the catalog spells it.
QString serveFile( const QString& root, const QString& name, const QByteArray& content )
{
    QFile f( QDir( root ).filePath( name ) );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( content );
    f.close();
    return QString::fromLatin1(
        QCryptographicHash::hash( content, QCryptographicHash::Sha256 ).toHex() );
}

/// A v1 catalog with one entry for every platform.
QByteArray catalogV1( const QString& downloadUrl, const QString& sha256 )
{
    return QStringLiteral( R"({
        "schema_version": 1,
        "plugins": [
            { "id": "io.test.plugin", "name": "Test", "version": "1.0.0",
              "download_url": "%1", "sha256": "%2", "platforms": ["all"] }
        ]
    })" )
        .arg( downloadUrl, sha256 )
        .toUtf8();
}

// Helper: create a minimal ZIP archive containing a plugin.json and a dummy library file.
// Returns the path to the created archive on success, empty string on failure.
QString createTestPluginZip( const QString& archivePath, const QString& pluginJsonContent,
                             const QString& libraryName )
{
    KZip zip( archivePath );
    if ( !zip.open( QIODevice::WriteOnly ) ) {
        return {};
    }

    const auto jsonBytes = pluginJsonContent.toUtf8();
    zip.writeFile( "plugin.json", jsonBytes );

    // Write a small dummy binary to simulate a shared library
    const QByteArray dummyLib( "FAKE_SHARED_LIBRARY_CONTENT" );
    zip.writeFile( libraryName, dummyLib );

    zip.close();
    return archivePath;
}

// Helper: build a minimal plugin.json string
QString makePluginJson( const QString& id, const QString& name )
{
    QJsonObject obj;
    obj[ "id" ] = id;
    obj[ "name" ] = name;
    obj[ "version" ] = "1.0.0";
    obj[ "type" ] = "ui";
    obj[ "library" ] = "logsquirl_test";
    obj[ "api_version" ] = 1;
    obj[ "description" ] = "Test plugin";
    obj[ "author" ] = "Tests";
    obj[ "license" ] = "MIT";
    return QString::fromUtf8( QJsonDocument( obj ).toJson( QJsonDocument::Compact ) );
}

} // namespace

SCENARIO( "extractPluginArchive extracts a valid ZIP to the destination",
          "[pluginrepository][plugins]" )
{
    GIVEN( "A temporary directory and a valid plugin ZIP archive" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto archivePath = tmpDir.filePath( "test-plugin.zip" );
        const auto json = makePluginJson( "io.test.plugin", "Test Plugin" );
        REQUIRE_FALSE(
            createTestPluginZip( archivePath, json, "liblogsquirl_test.dylib" ).isEmpty() );

        WHEN( "extractPluginArchive is called with a new destination directory" )
        {
            const auto destDir = tmpDir.filePath( "io.test.plugin" );
            QString errorMessage;
            const auto result
                = PluginRepository::extractPluginArchive( archivePath, destDir, &errorMessage );

            THEN( "it returns true and the files exist in the destination" )
            {
                REQUIRE( result );
                REQUIRE( errorMessage.isEmpty() );
                REQUIRE( QFile::exists( QDir( destDir ).filePath( "plugin.json" ) ) );
                REQUIRE( QFile::exists( QDir( destDir ).filePath( "liblogsquirl_test.dylib" ) ) );
            }

            THEN( "the extracted plugin.json has the expected content" )
            {
                QFile f( QDir( destDir ).filePath( "plugin.json" ) );
                REQUIRE( f.open( QIODevice::ReadOnly ) );
                const auto doc = QJsonDocument::fromJson( f.readAll() );
                REQUIRE( doc.isObject() );
                REQUIRE( doc.object().value( "id" ).toString() == QString( "io.test.plugin" ) );
            }
        }
    }
}

SCENARIO( "extractPluginArchive creates the destination directory if it does not exist",
          "[pluginrepository][plugins]" )
{
    GIVEN( "A valid plugin ZIP and a non-existent nested destination path" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto archivePath = tmpDir.filePath( "nested-test.zip" );
        const auto json = makePluginJson( "io.test.nested", "Nested" );
        REQUIRE_FALSE( createTestPluginZip( archivePath, json, "lib.so" ).isEmpty() );

        const auto destDir = tmpDir.filePath( "a/b/c/io.test.nested" );
        REQUIRE_FALSE( QDir( destDir ).exists() );

        WHEN( "extractPluginArchive is called" )
        {
            const auto result = PluginRepository::extractPluginArchive( archivePath, destDir );

            THEN( "the directory is created and files are extracted" )
            {
                REQUIRE( result );
                REQUIRE( QDir( destDir ).exists() );
                REQUIRE( QFile::exists( QDir( destDir ).filePath( "plugin.json" ) ) );
            }
        }
    }
}

SCENARIO( "extractPluginArchive fails gracefully on an invalid archive",
          "[pluginrepository][plugins]" )
{
    GIVEN( "A file that is not a valid ZIP" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto fakePath = tmpDir.filePath( "not-a-zip.zip" );
        {
            QFile f( fakePath );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( "This is not a ZIP file" );
        }

        WHEN( "extractPluginArchive is called" )
        {
            const auto destDir = tmpDir.filePath( "output" );
            QString errorMessage;
            const auto result
                = PluginRepository::extractPluginArchive( fakePath, destDir, &errorMessage );

            THEN( "it returns false and provides an error message" )
            {
                REQUIRE_FALSE( result );
                REQUIRE_FALSE( errorMessage.isEmpty() );
            }
        }
    }
}

SCENARIO( "extractPluginArchive fails gracefully for a non-existent file",
          "[pluginrepository][plugins]" )
{
    GIVEN( "A path to a file that does not exist" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto missingPath = tmpDir.filePath( "missing.zip" );

        WHEN( "extractPluginArchive is called" )
        {
            const auto destDir = tmpDir.filePath( "output" );
            QString errorMessage;
            const auto result
                = PluginRepository::extractPluginArchive( missingPath, destDir, &errorMessage );

            THEN( "it returns false with an error message" )
            {
                REQUIRE_FALSE( result );
                REQUIRE_FALSE( errorMessage.isEmpty() );
            }
        }
    }
}

SCENARIO( "extractPluginArchive overwrites existing files in the destination",
          "[pluginrepository][plugins]" )
{
    GIVEN( "An existing plugin directory with old content" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );

        const auto destDir = tmpDir.filePath( "io.test.overwrite" );
        QDir().mkpath( destDir );

        // Write an old plugin.json with version 0.1.0
        {
            QFile f( QDir( destDir ).filePath( "plugin.json" ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( makePluginJson( "io.test.overwrite", "Old" ).toUtf8() );
        }

        const auto archivePath = tmpDir.filePath( "update.zip" );
        QJsonObject obj;
        obj[ "id" ] = "io.test.overwrite";
        obj[ "name" ] = "Updated";
        obj[ "version" ] = "2.0.0";
        obj[ "type" ] = "ui";
        obj[ "library" ] = "logsquirl_test";
        obj[ "api_version" ] = 1;
        obj[ "description" ] = "Updated plugin";
        obj[ "author" ] = "Tests";
        obj[ "license" ] = "MIT";
        const auto json
            = QString::fromUtf8( QJsonDocument( obj ).toJson( QJsonDocument::Compact ) );
        REQUIRE_FALSE( createTestPluginZip( archivePath, json, "lib.so" ).isEmpty() );

        WHEN( "extractPluginArchive is called on the existing directory" )
        {
            const auto result = PluginRepository::extractPluginArchive( archivePath, destDir );

            THEN( "files are overwritten with the new content" )
            {
                REQUIRE( result );
                QFile f( QDir( destDir ).filePath( "plugin.json" ) );
                REQUIRE( f.open( QIODevice::ReadOnly ) );
                const auto doc = QJsonDocument::fromJson( f.readAll() );
                REQUIRE( doc.object().value( "version" ).toString() == QString( "2.0.0" ) );
                REQUIRE( doc.object().value( "name" ).toString() == QString( "Updated" ) );
            }
        }
    }
}

SCENARIO( "A catalog entry whose id is not a plain name is skipped", "[pluginrepository][plugins]" )
{
    GIVEN( "A schema v1 catalog with one plain id and ids that name a path" )
    {
        const QByteArray json = R"({
            "schema_version": 1,
            "plugins": [
                { "id": "io.test.good", "name": "Good", "version": "1.0.0",
                  "download_url": "https://example.test/good.zip", "sha256": "ab" },
                { "id": "/Users/me/.ssh", "name": "Absolute", "version": "1.0.0",
                  "download_url": "https://example.test/abs.zip", "sha256": "ab" },
                { "id": "../up", "name": "Parent", "version": "1.0.0",
                  "download_url": "https://example.test/up.zip", "sha256": "ab" },
                { "id": "a/b", "name": "Nested", "version": "1.0.0",
                  "download_url": "https://example.test/ab.zip", "sha256": "ab" },
                { "id": "a\\b", "name": "Backslash", "version": "1.0.0",
                  "download_url": "https://example.test/ab2.zip", "sha256": "ab" },
                { "id": ".hidden", "name": "Hidden", "version": "1.0.0",
                  "download_url": "https://example.test/hidden.zip", "sha256": "ab" },
                { "id": "", "name": "Empty", "version": "1.0.0",
                  "download_url": "https://example.test/empty.zip", "sha256": "ab" }
            ]
        })";

        PluginRepository repository;

        WHEN( "The catalog is read" )
        {
            repository.parseCatalog( json );

            THEN( "Only the plain id is listed, in the catalog and the legacy entries (#819)" )
            {
                REQUIRE( repository.catalog().size() == 1 );
                CHECK( repository.catalog().front().id == "io.test.good" );
                REQUIRE( repository.entries().size() == 1 );
                CHECK( repository.entries().front().id == "io.test.good" );
                CHECK( repository.releases( "io.test.good" ).size() == 1 );
                CHECK( repository.releases( "../up" ).empty() );
            }
        }
    }

    GIVEN( "A schema v2 catalog with one plain id and ids that name a path" )
    {
        const QByteArray json = R"({
            "schema_version": 2,
            "plugins": [
                { "id": "io.test.good", "name": "Good" },
                { "id": "/Users/me/.ssh", "name": "Absolute" },
                { "id": "../up", "name": "Parent" },
                { "id": "a/b", "name": "Nested" },
                { "id": "a\\b", "name": "Backslash" },
                { "id": ".hidden", "name": "Hidden" },
                { "id": "", "name": "Empty" }
            ]
        })";

        PluginRepository repository;

        WHEN( "The catalog is read" )
        {
            repository.parseCatalog( json );

            THEN( "Only the plain id is listed (#819)" )
            {
                REQUIRE( repository.catalog().size() == 1 );
                CHECK( repository.catalog().front().id == "io.test.good" );
            }
        }
    }
}

// A Plugin's archive is extracted by the rules of a Log File archive (#815).
SCENARIO( "extractPluginArchive writes nothing outside the destination",
          "[pluginrepository][plugins]" )
{
    GIVEN( "a plugin ZIP with a link, a directory of the same name but case, and an entry leading "
           "out" )
    {
        QTemporaryDir tmpDir;
        REQUIRE( tmpDir.isValid() );
        const auto outside = tmpDir.filePath( "outside" );
        REQUIRE( QDir().mkpath( outside ) );
        const auto destDir = tmpDir.filePath( "io.test.plugin" );
        const auto sibling = tmpDir.filePath( "io.test.pluginX" );

        const auto archivePath = tmpDir.filePath( "hostile-plugin.zip" );
        {
            KZip zip( archivePath );
            REQUIRE( zip.open( QIODevice::WriteOnly ) );
            REQUIRE( zip.writeFile( "plugin.json",
                                    makePluginJson( "io.test.plugin", "Hostile" ).toUtf8() ) );
            REQUIRE( zip.writeSymLink( "escape", outside ) );
            REQUIRE( zip.writeFile( "Escape/planted.txt", "planted" ) );
            REQUIRE( zip.writeFile( "../io.test.pluginX/planted.txt", "planted" ) );
            zip.close();
        }

        WHEN( "extractPluginArchive is called" )
        {
            QString errorMessage;
            const auto result
                = PluginRepository::extractPluginArchive( archivePath, destDir, &errorMessage );

            THEN( "the plugin is extracted, the link is not, and nothing lands outside" )
            {
                REQUIRE( result );
                REQUIRE( errorMessage.isEmpty() );
                REQUIRE( QFile::exists( QDir( destDir ).filePath( "plugin.json" ) ) );
                REQUIRE_FALSE( QFileInfo( QDir( destDir ).filePath( "escape" ) ).isSymLink() );
                REQUIRE_FALSE( QFileInfo( QDir( destDir ).filePath( "escape.lnk" ) ).exists() );
                REQUIRE( QDir( outside )
                             .entryList( QDir::AllEntries | QDir::NoDotAndDotDot )
                             .isEmpty() );
                REQUIRE_FALSE( QFileInfo::exists( sibling ) );
            }
        }
    }
}

// A download is verified against the catalog's SHA-256, so an entry without
// a usable one is not installable (#818).
SCENARIO( "A release asset without a well-formed SHA-256 is refused before any request",
          "[pluginrepository][plugins]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );
    RecordingNetwork network( tmpDir.path() );
    PluginRepository repository( network );
    QSignalSpy failed( &repository, &PluginRepository::downloadError );
    QSignalSpy finished( &repository, &PluginRepository::downloadFinished );

    const auto refusedAsset = [ & ]( const QString& sha256 ) {
        ReleaseAsset asset;
        asset.platform = PluginRepository::currentPlatform();
        asset.downloadUrl = QUrl( "https://files.test/plugin.zip" );
        asset.sha256 = sha256;
        return asset;
    };

    GIVEN( "an asset whose catalog entry carries no checksum" )
    {
        const auto asset = refusedAsset( "" );

        THEN( "it is not installable, and the reason says so" )
        {
            const auto reason = PluginRepository::downloadRefusal( asset );
            REQUIRE_FALSE( reason.isEmpty() );
            CHECK( reason.contains( "checksum" ) );
        }

        WHEN( "it is downloaded" )
        {
            repository.downloadPlugin( asset, "io.test.plugin", tmpDir.filePath( "dest" ) );

            THEN( "the download is refused at once and nothing is requested" )
            {
                REQUIRE( failed.size() == 1 );
                CHECK( failed.front().front().toString().contains( "checksum" ) );
                CHECK( network.requested.isEmpty() );
                CHECK( finished.isEmpty() );
            }
        }
    }

    GIVEN( "an asset whose checksum is not 64 hex digits" )
    {
        for ( const auto* malformed :
              { "abc", "sha256:e3b0c44298fc1c149afbf4c8996fb924",
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85g" } ) {
            const auto asset = refusedAsset( malformed );

            THEN( "it is not installable and nothing is requested: " << malformed )
            {
                REQUIRE_FALSE( PluginRepository::downloadRefusal( asset ).isEmpty() );
                repository.downloadPlugin( asset, "io.test.plugin", tmpDir.filePath( "dest" ) );
                REQUIRE( failed.size() == 1 );
                CHECK( network.requested.isEmpty() );
            }
        }
    }

    GIVEN( "an asset with a well-formed checksum in either case" )
    {
        THEN( "it is installable" )
        {
            CHECK(
                PluginRepository::downloadRefusal( refusedAsset( QString( 64, 'a' ) ) ).isEmpty() );
            CHECK( PluginRepository::downloadRefusal(
                       refusedAsset( QString( 32, 'A' ) + QString( 32, '0' ) ) )
                       .isEmpty() );
        }
    }
}

SCENARIO( "A release asset whose download URL is not https is refused before any request",
          "[pluginrepository][plugins]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );
    RecordingNetwork network( tmpDir.path() );
    PluginRepository repository( network );
    QSignalSpy failed( &repository, &PluginRepository::downloadError );

    GIVEN( "an asset with a checksum but an http, file or empty download URL" )
    {
        for ( const auto* url : { "http://files.test/plugin.zip", "ftp://files.test/plugin.zip",
                                  "file:///etc/passwd", "" } ) {
            ReleaseAsset asset;
            asset.platform = PluginRepository::currentPlatform();
            asset.downloadUrl = QUrl( url );
            asset.sha256 = QString( 64, 'a' );

            THEN( "it is refused with a reason and nothing is requested: " << url )
            {
                REQUIRE_FALSE( PluginRepository::downloadRefusal( asset ).isEmpty() );
                repository.downloadPlugin( asset, "io.test.plugin", tmpDir.filePath( "dest" ) );
                REQUIRE( failed.size() == 1 );
                CHECK( network.requested.isEmpty() );
            }
        }
    }
}

SCENARIO( "A catalog entry with a URL that is not https is skipped and nothing is requested",
          "[pluginrepository][plugins]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );
    RecordingNetwork network( tmpDir.path() );
    PluginRepository repository( network );

    GIVEN( "a v1 catalog whose entry downloads over http" )
    {
        WHEN( "the catalog is read" )
        {
            repository.parseCatalog(
                catalogV1( "http://files.test/plugin.zip", QString( 64, 'a' ) ) );

            THEN( "the entry is skipped" )
            {
                CHECK( repository.catalog().empty() );
                CHECK( repository.entries().empty() );
                CHECK( repository.releases( "io.test.plugin" ).empty() );
            }
        }
    }

    GIVEN( "a v2 catalog with entries whose releases, icon or repository URL is not https" )
    {
        const QByteArray json = R"({
            "schema_version": 2,
            "plugins": [
                { "id": "io.test.releases", "name": "Releases over http",
                  "releases_url": "http://files.test/releases.json" },
                { "id": "io.test.icon", "name": "Icon over http",
                  "releases_url": "https://files.test/releases.json",
                  "icon_url": "http://files.test/icon.png" },
                { "id": "io.test.repo", "name": "Repository over http",
                  "releases_url": "https://files.test/releases.json",
                  "repo_url": "http://example.test/repo" },
                { "id": "io.test.local", "name": "Releases from a file",
                  "releases_url": "file:///etc/releases.json" }
            ]
        })";

        WHEN( "the catalog is read" )
        {
            QSignalSpy ready( &repository, &PluginRepository::catalogReady );
            repository.parseCatalog( json );

            THEN( "every entry is skipped and no request is made" )
            {
                CHECK( repository.catalog().empty() );
                CHECK( network.requested.isEmpty() );
                CHECK( ready.size() == 1 );
            }
        }
    }

    GIVEN( "a v2 catalog whose release feed offers an asset over http and one over https" )
    {
        const auto good = serveFile( tmpDir.path(), "plugin.zip", "zip bytes" );
        serveFile( tmpDir.path(), "releases.json",
                   QStringLiteral( R"({
            "releases": [
                { "version": "1.0.0", "assets": [
                    { "platform": "all", "download_url": "http://files.test/plugin.zip",
                      "sha256": "%1" },
                    { "platform": "all", "download_url": "https://files.test/plugin.zip",
                      "sha256": "%1" }
                ] },
                { "version": "0.9.0", "assets": [
                    { "platform": "all", "download_url": "http://files.test/old.zip",
                      "sha256": "%1" }
                ] },
                { "version": "0.8.0", "assets": [
                    { "platform": "all", "download_url": "https://files.test/older.zip",
                      "sha256": "" }
                ] }
            ]
        })" )
                       .arg( good )
                       .toUtf8() );
        const QByteArray json = R"({
            "schema_version": 2,
            "plugins": [
                { "id": "io.test.plugin", "name": "Test",
                  "releases_url": "https://files.test/releases.json" }
            ]
        })";

        WHEN( "the catalog and the feed are read" )
        {
            QSignalSpy ready( &repository, &PluginRepository::catalogReady );
            repository.parseCatalog( json );
            if ( ready.isEmpty() ) {
                REQUIRE( ready.wait( 5000 ) );
            }

            THEN( "only the feed was requested, the http assets are gone, and the release "
                  "without a checksum stays but is not installable" )
            {
                REQUIRE( network.requested.size() == 1 );
                CHECK( network.requested.front().scheme() == "https" );
                const auto& releases = repository.releases( "io.test.plugin" );
                REQUIRE( releases.size() == 2 );
                CHECK( releases[ 0 ].version == "1.0.0" );
                REQUIRE( releases[ 0 ].assets.size() == 1 );
                CHECK( releases[ 0 ].assets.front().downloadUrl.scheme() == "https" );
                CHECK(
                    PluginRepository::downloadRefusal( releases[ 0 ].assets.front() ).isEmpty() );
                CHECK( releases[ 1 ].version == "0.8.0" );
                CHECK_FALSE(
                    PluginRepository::downloadRefusal( releases[ 1 ].assets.front() ).isEmpty() );
            }
        }
    }
}

SCENARIO( "A downloaded plugin archive is kept only when its SHA-256 matches the catalog's",
          "[pluginrepository][plugins]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );
    RecordingNetwork network( tmpDir.path() );
    PluginRepository repository( network );
    QSignalSpy failed( &repository, &PluginRepository::downloadError );
    QSignalSpy finished( &repository, &PluginRepository::downloadFinished );

    const QByteArray content( "the archive bytes" );
    const auto sha256 = serveFile( tmpDir.path(), "plugin.zip", content );
    const auto destDir = tmpDir.filePath( "dest" );
    const auto archivePath = QDir( destDir ).filePath( "plugin.zip" );

    GIVEN( "a v1 catalog entry whose checksum is the archive's" )
    {
        repository.parseCatalog( catalogV1( "https://files.test/plugin.zip", sha256 ) );
        REQUIRE( repository.entries().size() == 1 );

        WHEN( "it is downloaded" )
        {
            repository.downloadPlugin( repository.entries().front(), destDir );
            REQUIRE( finished.wait( 5000 ) );

            THEN( "the archive is requested over https, written and reported" )
            {
                REQUIRE( network.requested.size() == 1 );
                CHECK( network.requested.front().scheme() == "https" );
                CHECK( failed.isEmpty() );
                CHECK( finished.front().front().toString() == archivePath );
                QFile written( archivePath );
                REQUIRE( written.open( QIODevice::ReadOnly ) );
                CHECK( written.readAll() == content );
            }
        }
    }

    GIVEN( "a v1 catalog entry whose checksum is another archive's" )
    {
        repository.parseCatalog( catalogV1( "https://files.test/plugin.zip", QString( 64, 'a' ) ) );
        REQUIRE( repository.entries().size() == 1 );

        WHEN( "it is downloaded" )
        {
            repository.downloadPlugin( repository.entries().front(), destDir );
            REQUIRE( failed.wait( 5000 ) );

            THEN( "the archive is discarded and the mismatch reported" )
            {
                CHECK( failed.front().front().toString().contains( "mismatch" ) );
                CHECK( finished.isEmpty() );
                CHECK_FALSE( QFile::exists( archivePath ) );
            }
        }
    }
}
