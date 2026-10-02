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

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "configuration.h"
#include "configurationfixture.h"
#include "logformatcatalog.h"
#include "optionsdialog.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "teamfolder.h"
#include "teamfoldergit.h"
#include "teamfoldertesting.h"

using namespace configuration_fixture;

namespace {

// The stored settings the Options Dialog shows. Every other stored setting
// must pass through the dialog untouched.
const QStringList DialogSettingNames = {
    "archives.extract",
    "archives.extractAlways",
    "defaultView.encodingMib",
    "defaultView.searchAutoRefresh",
    "defaultView.searchIgnoreCase",
    "defaultView.searchLogicalCombining",
    "filewatch.allowFollowOnScroll",
    "filewatch.fastModificationDetection",
    "filewatch.pollingIntervalMs",
    "filewatch.useNative",
    "filewatch.usePolling",
    "logformat.autoDetect",
    "logformat.autoShowTable",
    "logging.enableLogging",
    "logging.verbosity",
    "mainFont.antialiasing",
    "mainFont.bold",
    "mainFont.family",
    "mainFont.size",
    "net.verifySslPeers",
    "perf.indexCacheMaxSizeMb",
    "perf.indexReadBufferSizeMb",
    "perf.keepFileClosed",
    "perf.optimizeForNotLatinEncodings",
    "perf.searchReadBufferSizeLines",
    "perf.searchResultsCacheLines",
    "perf.useCompressedIndex",
    "perf.useIndexCache",
    "perf.useParallelSearch",
    "perf.useSearchResultsCache",
    "quickfind.incremental",
    "regexpType.autoRunSearch",
    "regexpType.engine",
    "regexpType.main",
    "regexpType.mainBackColor",
    "regexpType.mainHighlight",
    "regexpType.mainHighlightVariate",
    "regexpType.quickfind",
    "regexpType.quickfindBackColor",
    "session.confirmTabClose",
    "session.followOnLoad",
    "session.loadLast",
    "session.multipleWindows",
    "shortcuts",
    "teamFolder.enabled",
    "teamFolder.subfolder",
    "teamFolder.url",
    "versionchecker.betaEnabled",
    "versionchecker.enabled",
    "view.ansiColorSequences",
    "view.contextLinesCount",
    "view.fastScrollEnabled",
    "view.fastScrollMultiplier",
    "view.language",
    "view.minimizeToTray",
    "view.qtHiDpi",
    "view.scaleFactorRounding",
    "view.showDashboard",
    "view.showSplashScreen",
    "view.showValueNames",
    "view.style",
    "view.textWrap",
};

// The dialog lists every known shortcut, so the stored shortcuts are
// compared by action instead of by stored key.
const QString ShortcutsSettingName = QStringLiteral( "shortcuts" );

// The dialog saves what it writes to the application's settings file; this
// puts the Configuration back, in memory and in that file.
class ConfigurationRestorer {
public:
    ConfigurationRestorer()
        : saved_( Configuration::get() )
    {
    }

    ~ConfigurationRestorer()
    {
        Configuration::get() = saved_;
        Configuration::get().save();
    }

    ConfigurationRestorer( const ConfigurationRestorer& ) = delete;
    ConfigurationRestorer& operator=( const ConfigurationRestorer& ) = delete;

private:
    Configuration saved_;
};

QStringList allKeys( std::initializer_list<const QVariantMap*> maps )
{
    QStringList keys;
    for ( const auto* map : maps ) {
        for ( const auto& key : map->keys() ) {
            if ( !keys.contains( key ) ) {
                keys << key;
            }
        }
    }
    return keys;
}

} // namespace

SCENARIO( "The Options Dialog reads and writes the same settings",
          "[configuration][optionsdialog]" )
{
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;

    GIVEN( "An Options Dialog showing a Configuration where every setting is changed" )
    {
        const auto shown = nonDefaultConfiguration();
        Configuration::get() = shown;

        LogFormatCatalog catalog;
        OptionsDialog dialog( catalog );

        WHEN( "It is applied to a default Configuration" )
        {
            Configuration::get() = Configuration{};

            // A changed style or language makes the dialog ask for a restart.
            QTimer messageBoxCloser;
            QObject::connect( &messageBoxCloser, &QTimer::timeout, [] {
                if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
                    box->accept();
                }
            } );
            messageBoxCloser.start( 10 );

            auto* applyButton = dialog.buttonBox->button( QDialogButtonBox::Apply );
            REQUIRE( applyButton->isEnabled() );
            applyButton->click();
            messageBoxCloser.stop();

            const auto written = storedSettings( Configuration::get() );
            const auto expected = storedSettings( shown );
            const auto defaults = storedSettings( Configuration{} );

            THEN( "Every setting it shows is a stored setting" )
            {
                const auto storedNames = settingNames( expected );
                for ( const auto& name : DialogSettingNames ) {
                    INFO( name.toStdString() );
                    CHECK( storedNames.contains( name ) );
                }
            }

            THEN( "Every setting it shows is written back as it was read" )
            {
                for ( const auto& key : expected.keys() ) {
                    const auto name = settingName( key );
                    if ( name == ShortcutsSettingName || !DialogSettingNames.contains( name ) ) {
                        continue;
                    }
                    INFO( key.toStdString() );
                    CHECK( written.value( key ) == expected.value( key ) );
                }

                CHECK( Configuration::get().shortcuts().at( FixtureShortcutAction )
                       == FixtureShortcutKeys );
            }

            THEN( "Every setting it does not show is left alone" )
            {
                for ( const auto& key : allKeys( { &written, &expected, &defaults } ) ) {
                    if ( DialogSettingNames.contains( settingName( key ) ) ) {
                        continue;
                    }
                    INFO( key.toStdString() );
                    CHECK( written.value( key ) == defaults.value( key ) );
                }
            }
        }
    }
}

SCENARIO( "The Options Dialog offers three ways to show ANSI color sequences",
          "[configuration][optionsdialog][ansi]" )
{
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;

    GIVEN( "An Options Dialog showing a new installation's settings" )
    {
        Configuration::get() = Configuration{};
        LogFormatCatalog catalog;
        OptionsDialog dialog( catalog );
        const auto* comboBox = dialog.ansiColorSequencesComboBox;

        THEN( "it offers Show as text, Hide and Show colors, with Show as text chosen" )
        {
            REQUIRE( comboBox->count() == 3 );
            CHECK( comboBox->itemText( 0 ) == "Show as text" );
            CHECK( comboBox->itemText( 1 ) == "Hide" );
            CHECK( comboBox->itemText( 2 ) == "Show colors" );
            CHECK( comboBox->currentIndex() == 0 );
        }

        WHEN( "Show colors is chosen and applied" )
        {
            dialog.ansiColorSequencesComboBox->setCurrentIndex( 2 );
            dialog.buttonBox->button( QDialogButtonBox::Apply )->click();

            THEN( "the settings show the colors" )
            {
                CHECK( Configuration::get().ansiColorSequences()
                       == AnsiColorSequences::ShowColors );
            }
        }
    }
}

TEST_CASE( "The Team Folder tab groups the repository and the status, and the note comes last",
           "[optionsdialog][teamfolder]" )
{
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;
    Configuration::get() = Configuration{};
    LogFormatCatalog catalog;
    OptionsDialog dialog( catalog );

    CHECK( dialog.teamFolderRepositoryGroup->isAncestorOf( dialog.teamFolderUrlEdit ) );
    CHECK( dialog.teamFolderRepositoryGroup->isAncestorOf( dialog.teamFolderSubfolderEdit ) );
    CHECK( dialog.teamFolderStatusGroup->isAncestorOf( dialog.teamFolderSyncButton ) );

    const auto checkBoxText = dialog.teamFolderCheckBox->text();
    CHECK( checkBoxText.contains( "Filter Groups" ) );
    CHECK( checkBoxText.contains( "Highlighter Sets" ) );
    CHECK( checkBoxText.contains( "Naming Groups" ) );

    // The note is the last thing on the tab, smaller and subdued.
    auto* layout = dialog.teamFolderTab->layout();
    CHECK( layout->indexOf( dialog.teamFolderNoteLabel ) == layout->count() - 1 );
    CHECK( dialog.teamFolderNoteLabel->font().pointSizeF()
           < dialog.teamFolderCheckBox->font().pointSizeF() );
    CHECK( dialog.teamFolderNoteLabel->foregroundRole() == QPalette::PlaceholderText );

    // Without a Team Folder to show, there is no status.
    CHECK_FALSE( dialog.teamFolderStatusGroup->isVisibleTo( dialog.teamFolderTab ) );
}

TEST_CASE( "The Team Folder tab shows the failed step and Git's output in collapsed details",
           "[optionsdialog][teamfolder]" )
{
    using namespace teamfolder_testing;
    const IsolatedGitEnvironment environment;
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;
    Configuration::get() = Configuration{};
    LogFormatCatalog catalog;
    OptionsDialog dialog( catalog );

    const QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto missingServer = QUrl::fromLocalFile( root.filePath( "missing.git" ) ).toString();

    auto* const details = dialog.teamFolderDetailsEdit;
    auto* const detailsButton = dialog.teamFolderDetailsButton;

    SECTION( "Git that cannot be started" )
    {
        TeamFolder folder( root.filePath( "clone" ), root.filePath( "no-git-here" ) );
        folder.setUp( policyFor( missingServer ) );
        REQUIRE( settled( folder ) );
        dialog.showTeamFolder( folder );

        CHECK( dialog.teamFolderStatusGroup->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( dialog.teamFolderStatusHeadingLabel->text() == "Git could not be started" );
        CHECK_FALSE( dialog.teamFolderStatusIconLabel->pixmap().isNull() );
        CHECK( dialog.teamFolderSyncButton->isEnabled() );

        // Collapsed by default, with Git's output as it is.
        CHECK( detailsButton->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( detailsButton->text() == "Details from Git" );
        CHECK_FALSE( detailsButton->isChecked() );
        CHECK_FALSE( details->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( details->toPlainText() == folder.gitOutput() );
        CHECK( details->isReadOnly() );
        CHECK( details->textInteractionFlags().testFlag( Qt::TextSelectableByMouse ) );
        CHECK( details->lineWrapMode() == QPlainTextEdit::NoWrap );
        CHECK( details->font().family()
               == QFontDatabase::systemFont( QFontDatabase::FixedFont ).family() );

        detailsButton->click();
        CHECK( details->isVisibleTo( dialog.teamFolderTab ) );
        detailsButton->click();
        CHECK_FALSE( details->isVisibleTo( dialog.teamFolderTab ) );

        QGuiApplication::clipboard()->clear();
        dialog.teamFolderCopyDetailsButton->click();
        CHECK( QGuiApplication::clipboard()->text() == folder.gitOutput() );
    }

    SECTION( "a subfolder outside the repository, named under the heading" )
    {
        TeamFolder folder( root.filePath( "clone" ) );
        folder.setUp( policyFor( missingServer, "../elsewhere" ) );
        REQUIRE( settled( folder ) );
        dialog.showTeamFolder( folder );

        CHECK( dialog.teamFolderStatusHeadingLabel->text()
               == "The subfolder lies outside the repository" );
        CHECK( dialog.teamFolderRemarksLabel->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( dialog.teamFolderRemarksLabel->text()
               == "The subfolder ../elsewhere does not lie inside the repository." );
        // Nothing of Git's to show.
        CHECK_FALSE( detailsButton->isVisibleTo( dialog.teamFolderTab ) );
    }

    if ( !gitInstalled() ) {
        return;
    }

    SECTION( "a clone that failed, with Git's lines kept" )
    {
        TeamFolder folder( root.filePath( "clone" ) );
        folder.setUp( policyFor( missingServer ) );
        REQUIRE( settled( folder ) );
        dialog.showTeamFolder( folder );

        CHECK( dialog.teamFolderStatusHeadingLabel->text() == "Clone failed" );
        const auto output = folder.gitOutput();
        REQUIRE( output.contains( '\n' ) );
        CHECK( details->toPlainText() == output );
        CHECK( details->blockCount() == output.count( '\n' ) + 1 );
        CHECK_FALSE( dialog.teamFolderRemarksLabel->isVisibleTo( dialog.teamFolderTab ) );
    }

    SECTION( "a sync that worked shows no details, and the files it skipped" )
    {
        const auto server = root.filePath( "server.git" );
        const auto work = root.filePath( "work" );
        const logsquirl::teamfolder::Git git( QStringLiteral( "git" ) );
        REQUIRE( git.run( { "init", "--quiet", "--bare", server } ).succeeded );
        REQUIRE( git.run( { "clone", "--quiet", server, work } ).succeeded );
        {
            QFile broken( QDir( work ).filePath( "broken_filter.conf" ) );
            REQUIRE( broken.open( QIODevice::WriteOnly ) );
            broken.write( "this is not a group\n" );
        }
        REQUIRE( git.run( { "add", "--", "broken_filter.conf" }, work ).succeeded );
        REQUIRE( git.run( { "commit", "--quiet", "-m", "Add" }, work ).succeeded );
        REQUIRE( git.run( { "push", "--quiet", "origin", "HEAD" }, work ).succeeded );

        TeamFolder folder( root.filePath( "clone" ) );
        folder.setUp( policyFor( QUrl::fromLocalFile( server ).toString() ) );
        REQUIRE( settled( folder ) );
        REQUIRE( folder.state() == TeamFolder::State::Synced );
        dialog.showTeamFolder( folder );

        CHECK( dialog.teamFolderStatusHeadingLabel->text() == "Synced" );
        CHECK_FALSE( detailsButton->isVisibleTo( dialog.teamFolderTab ) );
        CHECK_FALSE( details->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( dialog.teamFolderRemarksLabel->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( dialog.teamFolderRemarksLabel->text().startsWith( "Skipped broken_filter.conf: " ) );
    }
}
