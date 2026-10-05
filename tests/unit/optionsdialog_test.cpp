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
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "configuration.h"
#include "configurationfixture.h"
#include "fake_file_associations.h"
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
        // What to do, under the heading.
        CHECK( dialog.teamFolderHintLabel->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( dialog.teamFolderHintLabel->text()
               == TeamFolder::hintOf( logsquirl::teamfolder::FailureHint::GitMissing ) );
        auto* const statusLayout = dialog.teamFolderStatusGroup->layout();
        CHECK( statusLayout->indexOf( dialog.teamFolderHintLabel )
               == statusLayout->indexOf( dialog.teamFolderStatusLineLayout ) + 1 );
        CHECK_FALSE( dialog.teamFolderStatusIconLabel->pixmap().isNull() );
        dialog.teamFolderCheckBox->setChecked( true );
        dialog.teamFolderUrlEdit->setText( missingServer );
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
        CHECK_FALSE( dialog.teamFolderHintLabel->isVisibleTo( dialog.teamFolderTab ) );
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
        CHECK( dialog.teamFolderHintLabel->text()
               == TeamFolder::hintOf( logsquirl::teamfolder::FailureHint::RepositoryNotFound ) );
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
        CHECK_FALSE( dialog.teamFolderHintLabel->isVisibleTo( dialog.teamFolderTab ) );
        CHECK_FALSE( detailsButton->isVisibleTo( dialog.teamFolderTab ) );
        CHECK_FALSE( details->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( dialog.teamFolderRemarksLabel->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( dialog.teamFolderRemarksLabel->text().startsWith( "Skipped broken_filter.conf: " ) );
    }
}

namespace {

// A bare repository with one commit, as a team's Git server holds it.
QString serverWithACommit( const QTemporaryDir& root, const QString& name )
{
    const auto server = root.filePath( name + ".git" );
    const auto work = root.filePath( name + "-work" );
    const logsquirl::teamfolder::Git git( QStringLiteral( "git" ) );
    REQUIRE( git.run( { "init", "--quiet", "--bare", server } ).succeeded );
    REQUIRE( git.run( { "clone", "--quiet", server, work } ).succeeded );
    {
        QFile readme( QDir( work ).filePath( "README" ) );
        REQUIRE( readme.open( QIODevice::WriteOnly ) );
        readme.write( "Team groups\n" );
    }
    REQUIRE( git.run( { "add", "--", "README" }, work ).succeeded );
    REQUIRE( git.run( { "commit", "--quiet", "-m", "Start" }, work ).succeeded );
    REQUIRE( git.run( { "push", "--quiet", "origin", "HEAD" }, work ).succeeded );
    return QUrl::fromLocalFile( server ).toString();
}

void applyTeamFolderSettings( const TeamFolderPolicy& policy )
{
    auto& config = Configuration::get();
    config.setTeamFolderEnabled( policy.enabled );
    config.setTeamFolderUrl( policy.repositoryUrl );
    config.setTeamFolderSubfolder( policy.subfolder );
}

} // namespace

TEST_CASE( "Sync Now syncs the Team Folder the fields show", "[optionsdialog][teamfolder]" )
{
    using namespace teamfolder_testing;
    if ( !gitInstalled() ) {
        return;
    }
    const IsolatedGitEnvironment environment;
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;
    Configuration::get() = Configuration{};

    const QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto missingServer = QUrl::fromLocalFile( root.filePath( "missing.git" ) ).toString();
    const auto server = serverWithACommit( root, "server" );
    const auto contextLines = Configuration::get().contextLinesCount();

    TeamFolder folder( root.filePath( "clone" ) );

    SECTION( "unchanged fields sync the applied Team Folder, applying nothing" )
    {
        applyTeamFolderSettings( policyFor( server ) );
        folder.setUp( policyFor( server ) );
        REQUIRE( settled( folder ) );
        LogFormatCatalog catalog;
        OptionsDialog dialog( catalog );
        dialog.showTeamFolder( folder );
        const QSignalSpy applied( &dialog, &OptionsDialog::optionsChanged );
        const QSignalSpy synced( &folder, &TeamFolder::syncFinished );

        REQUIRE( dialog.teamFolderSyncButton->isEnabled() );
        dialog.teamFolderSyncButton->click();
        REQUIRE( settled( folder ) );

        CHECK( synced.count() == 1 );
        CHECK( applied.isEmpty() );
        CHECK( folder.state() == TeamFolder::State::Synced );
    }

    SECTION( "a changed URL applies only the Team Folder settings, then syncs that repository" )
    {
        applyTeamFolderSettings( policyFor( missingServer ) );
        folder.setUp( policyFor( missingServer ) );
        REQUIRE( settled( folder ) );
        REQUIRE( folder.state() == TeamFolder::State::Error );
        LogFormatCatalog catalog;
        OptionsDialog dialog( catalog );
        dialog.showTeamFolder( folder );
        const QSignalSpy applied( &dialog, &OptionsDialog::optionsChanged );

        // A change on another tab that is still pending.
        dialog.contextLinesSpinBox->setValue( contextLines + 3 );
        dialog.teamFolderUrlEdit->setText( server );
        dialog.teamFolderSubfolderEdit->setText( " " );

        dialog.teamFolderSyncButton->click();
        REQUIRE( settled( folder ) );

        CHECK( folder.state() == TeamFolder::State::Synced );
        CHECK( dialog.teamFolderStatusHeadingLabel->text() == "Synced" );
        CHECK( Configuration::get().teamFolderEnabled() );
        CHECK( Configuration::get().teamFolderUrl() == server );
        CHECK( Configuration::get().teamFolderSubfolder().isEmpty() );
        CHECK( applied.count() == 1 );

        // The other tab's change is neither applied nor discarded.
        CHECK( Configuration::get().contextLinesCount() == contextLines );
        CHECK( dialog.contextLinesSpinBox->value() == contextLines + 3 );

        // Cancel does not undo what Sync Now applied.
        dialog.buttonBox->button( QDialogButtonBox::Cancel )->click();
        CHECK( Configuration::get().teamFolderUrl() == server );
        CHECK( Configuration::get().contextLinesCount() == contextLines );
    }

    SECTION( "Sync Now is there as soon as the check box is on and a URL entered" )
    {
        LogFormatCatalog catalog;
        OptionsDialog dialog( catalog );
        dialog.showTeamFolder( folder );
        REQUIRE( folder.state() == TeamFolder::State::Off );

        CHECK_FALSE( dialog.teamFolderSyncButton->isEnabled() );
        dialog.teamFolderCheckBox->setChecked( true );
        CHECK_FALSE( dialog.teamFolderSyncButton->isEnabled() );
        dialog.teamFolderUrlEdit->setText( "  " );
        CHECK_FALSE( dialog.teamFolderSyncButton->isEnabled() );
        dialog.teamFolderUrlEdit->setText( server );
        REQUIRE( dialog.teamFolderSyncButton->isEnabled() );

        dialog.teamFolderSyncButton->click();
        REQUIRE( settled( folder ) );

        CHECK( folder.state() == TeamFolder::State::Synced );
        CHECK( Configuration::get().teamFolderEnabled() );
        CHECK( Configuration::get().teamFolderUrl() == server );

        dialog.teamFolderCheckBox->setChecked( false );
        CHECK_FALSE( dialog.teamFolderSyncButton->isEnabled() );
    }
}

namespace {

// Takes the file URLs the dialog asks to open, instead of a file manager.
class FileUrlRecorder : public QObject {
    Q_OBJECT
public:
    FileUrlRecorder()
    {
        QDesktopServices::setUrlHandler( "file", this, "open" );
    }
    ~FileUrlRecorder() override
    {
        QDesktopServices::unsetUrlHandler( "file" );
    }

    FileUrlRecorder( const FileUrlRecorder& ) = delete;
    FileUrlRecorder& operator=( const FileUrlRecorder& ) = delete;

    QList<QUrl> opened;

public Q_SLOTS:
    void open( const QUrl& url )
    {
        opened.append( url );
    }
};

} // namespace

TEST_CASE( "The Team Folder tab shows when it last synced, and opens its folder",
           "[optionsdialog][teamfolder][lastsynced]" )
{
    using namespace teamfolder_testing;
    if ( !gitInstalled() ) {
        return;
    }
    const IsolatedGitEnvironment environment;
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;
    Configuration::get() = Configuration{};
    LogFormatCatalog catalog;
    OptionsDialog dialog( catalog );
    const FileUrlRecorder recorder;

    const QTemporaryDir root;
    REQUIRE( root.isValid() );
    auto* const lastSynced = dialog.teamFolderLastSyncedLabel;
    auto* const openFolder = dialog.teamFolderOpenFolderButton;
    CHECK( dialog.teamFolderStatusGroup->isAncestorOf( openFolder ) );

    SECTION( "before a sync reached the repository: never, and no folder to open" )
    {
        TeamFolder folder( root.filePath( "clone" ) );
        folder.setUp(
            policyFor( QUrl::fromLocalFile( root.filePath( "missing.git" ) ).toString() ) );
        REQUIRE( settled( folder ) );
        dialog.showTeamFolder( folder );

        CHECK( lastSynced->isVisibleTo( dialog.teamFolderTab ) );
        CHECK( lastSynced->text() == "Last synced: never" );
        CHECK_FALSE( openFolder->isEnabled() );
    }

    SECTION( "after a sync: its date and time, and the clone opens" )
    {
        const auto server = serverWithACommit( root, "server" );
        TeamFolder folder( root.filePath( "clone" ) );
        folder.setUp( policyFor( server ) );
        REQUIRE( settled( folder ) );
        REQUIRE( folder.state() == TeamFolder::State::Synced );
        dialog.showTeamFolder( folder );

        CHECK( lastSynced->text()
               == "Last synced: "
                      + QLocale{}.toString( folder.lastSynced().toLocalTime(),
                                            QLocale::ShortFormat ) );
        REQUIRE( openFolder->isEnabled() );
        openFolder->click();
        REQUIRE( recorder.opened.size() == 1 );
        CHECK( QDir( recorder.opened.front().toLocalFile() ) == QDir( root.filePath( "clone" ) ) );
    }

    SECTION( "a Team Folder that is off shows no time" )
    {
        TeamFolder folder( root.filePath( "clone" ) );
        dialog.showTeamFolder( folder );
        CHECK_FALSE( lastSynced->isVisibleTo( dialog.teamFolderTab ) );
        CHECK_FALSE( openFolder->isEnabled() );
    }
}

namespace {

// The row of a file type on the File Associations page.
QTreeWidgetItem* fileTypeRow( const OptionsDialog& dialog, const QString& id )
{
    auto* tree = dialog.fileAssociationsTree;
    for ( int group = 0; group < tree->topLevelItemCount(); ++group ) {
        auto* groupItem = tree->topLevelItem( group );
        for ( int row = 0; row < groupItem->childCount(); ++row ) {
            if ( groupItem->child( row )->data( 0, Qt::UserRole ).toString() == id ) {
                return groupItem->child( row );
            }
        }
    }
    return nullptr;
}

bool isFileAssociationsTabVisible( const OptionsDialog& dialog )
{
    return dialog.tabWidget->isTabVisible(
        dialog.tabWidget->indexOf( dialog.fileAssociationsTab ) );
}

constexpr int StateColumn = 2;

} // namespace

TEST_CASE( "The File Associations page lists the file types grouped, each with its state",
           "[optionsdialog][fileassociations]" )
{
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;
    Configuration::get() = Configuration{};
    LogFormatCatalog catalog;
    OptionsDialog dialog( catalog );

    // Without file associations to show, there is no page.
    CHECK_FALSE( isFileAssociationsTabVisible( dialog ) );

    FakeFileAssociations associations;
    associations.current = {
        { "log", FileAssociationState::Default },
        { "logcat", FileAssociationState::Registered },
    };
    dialog.showFileAssociations( associations );

    CHECK( isFileAssociationsTabVisible( dialog ) );
    CHECK( dialog.tabWidget->tabText( dialog.tabWidget->indexOf( dialog.fileAssociationsTab ) )
           == "File Associations" );
    CHECK( dialog.fileAssociationsTree->isEnabled() );
    CHECK_FALSE( dialog.fileAssociationsUnavailable->isVisibleTo( dialog.fileAssociationsTab ) );
    CHECK_FALSE( dialog.fileAssociationsNoteLabel->isVisibleTo( dialog.fileAssociationsTab ) );

    auto* tree = dialog.fileAssociationsTree;
    REQUIRE( tree->topLevelItemCount() == 2 );
    const auto* logs = tree->topLevelItem( 0 );
    const auto* optional = tree->topLevelItem( 1 );
    CHECK( logs->text( 0 ) == "Log files" );
    CHECK( optional->text( 0 ) == "More (optional)" );
    REQUIRE( logs->childCount() == 2 );
    REQUIRE( optional->childCount() == 3 );
    CHECK( logs->child( 1 )->text( 0 ) == ".adb, .adb0-.adb9" );
    CHECK( logs->child( 1 )->text( 1 ) == "Android Logcat traces" );
    CHECK( optional->child( 0 )->text( 0 ) == ".out, .err" );
    CHECK( optional->child( 2 )->text( 1 ) == "Text files" );
    CHECK_FALSE( logs->flags().testFlag( Qt::ItemIsUserCheckable ) );

    // Checked where LogSquirl is the default.
    CHECK( fileTypeRow( dialog, "log" )->checkState( 0 ) == Qt::Checked );
    CHECK( fileTypeRow( dialog, "logcat" )->checkState( 0 ) == Qt::Unchecked );
    CHECK( fileTypeRow( dialog, "trace" )->checkState( 0 ) == Qt::Unchecked );

    CHECK( fileTypeRow( dialog, "log" )->text( StateColumn ) == "Default" );
    CHECK( fileTypeRow( dialog, "logcat" )->text( StateColumn ) == "Registered" );
    CHECK( fileTypeRow( dialog, "trace" )->text( StateColumn ) == "Not registered" );
    for ( const auto* id : { "log", "logcat", "trace" } ) {
        INFO( id );
        CHECK_FALSE( fileTypeRow( dialog, id )->icon( StateColumn ).isNull() );
        CHECK_FALSE( fileTypeRow( dialog, id )->toolTip( StateColumn ).isEmpty() );
    }
}

TEST_CASE( "Applying the File Associations page makes the checked types LogSquirl's and gives "
           "the unchecked back",
           "[optionsdialog][fileassociations]" )
{
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;
    Configuration::get() = Configuration{};
    LogFormatCatalog catalog;

    FakeFileAssociations associations;
    associations.current = {
        { "log", FileAssociationState::Default },
        { "logcat", FileAssociationState::Registered },
        { "text", FileAssociationState::Registered },
    };

    {
        OptionsDialog dialog( catalog );
        dialog.showFileAssociations( associations );

        SECTION( "an unchanged page applies nothing" )
        {
            dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
            CHECK( associations.applied.empty() );
            // Nothing chosen, so the first start still asks (#723).
            CHECK( Configuration::get().askForFileAssociations() );
            CHECK_FALSE( Configuration::get().chosenFileAssociations() );
        }

        SECTION( "a changed page applies exactly the changes" )
        {
            fileTypeRow( dialog, "logcat" )->setCheckState( 0, Qt::Checked );
            fileTypeRow( dialog, "log" )->setCheckState( 0, Qt::Unchecked );
            dialog.buttonBox->button( QDialogButtonBox::Apply )->click();

            REQUIRE( associations.applied.size() == 1 );
            CHECK( associations.applied[ 0 ].first == QStringList{ "logcat" } );
            CHECK( associations.applied[ 0 ].second == QStringList{ "log" } );

            // The choice is kept, and the first start asks no more (#723).
            CHECK_FALSE( Configuration::get().askForFileAssociations() );
            CHECK( Configuration::get().chosenFileAssociations() == QStringList{ "logcat" } );

            // The states after Apply, as the system tells them.
            CHECK( fileTypeRow( dialog, "logcat" )->text( StateColumn ) == "Default" );
            CHECK( fileTypeRow( dialog, "log" )->text( StateColumn ) == "Registered" );
            CHECK( fileTypeRow( dialog, "text" )->text( StateColumn ) == "Registered" );

            // Applying again changes nothing more.
            dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
            CHECK( associations.applied.size() == 1 );

            // Reopened, the page shows the same.
            OptionsDialog reopened( catalog );
            reopened.showFileAssociations( associations );
            CHECK( fileTypeRow( reopened, "logcat" )->checkState( 0 ) == Qt::Checked );
            CHECK( fileTypeRow( reopened, "log" )->checkState( 0 ) == Qt::Unchecked );
            CHECK( fileTypeRow( reopened, "logcat" )->text( StateColumn ) == "Default" );
        }

        SECTION( "OK applies them too" )
        {
            fileTypeRow( dialog, "trace" )->setCheckState( 0, Qt::Checked );
            dialog.buttonBox->button( QDialogButtonBox::Ok )->click();
            REQUIRE( associations.applied.size() == 1 );
            CHECK( associations.applied[ 0 ].first == QStringList{ "trace" } );
        }

        SECTION( "Cancel applies nothing" )
        {
            fileTypeRow( dialog, "trace" )->setCheckState( 0, Qt::Checked );
            dialog.buttonBox->button( QDialogButtonBox::Cancel )->click();
            CHECK( associations.applied.empty() );
        }

        SECTION( "a type that could not be changed is reported and shown as it is" )
        {
            associations.failing = { "trace" };
            fileTypeRow( dialog, "trace" )->setCheckState( 0, Qt::Checked );
            fileTypeRow( dialog, "output" )->setCheckState( 0, Qt::Checked );

            QString reported;
            QTimer messageBoxCloser;
            QObject::connect( &messageBoxCloser, &QTimer::timeout, [ &reported ] {
                if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
                    reported = box->text();
                    box->accept();
                }
            } );
            messageBoxCloser.start( 10 );
            dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
            messageBoxCloser.stop();

            CHECK( reported.contains( associations.failure ) );
            CHECK( fileTypeRow( dialog, "output" )->text( StateColumn ) == "Default" );
            CHECK( fileTypeRow( dialog, "trace" )->text( StateColumn ) == "Not registered" );
            CHECK( fileTypeRow( dialog, "trace" )->checkState( 0 ) == Qt::Unchecked );
        }

        SECTION( "a change outside the page shows at once" )
        {
            associations.change( "text", FileAssociationState::Default );
            CHECK( fileTypeRow( dialog, "text" )->text( StateColumn ) == "Default" );
            CHECK( fileTypeRow( dialog, "text" )->checkState( 0 ) == Qt::Checked );
        }

        SECTION( "a check the user changed stays while the states change, until Apply" )
        {
            fileTypeRow( dialog, "trace" )->setCheckState( 0, Qt::Checked );
            fileTypeRow( dialog, "log" )->setCheckState( 0, Qt::Unchecked );
            associations.change( "text", FileAssociationState::Default );
            associations.change( "log", FileAssociationState::Default );

            CHECK( fileTypeRow( dialog, "text" )->checkState( 0 ) == Qt::Checked );
            CHECK( fileTypeRow( dialog, "trace" )->checkState( 0 ) == Qt::Checked );
            CHECK( fileTypeRow( dialog, "log" )->checkState( 0 ) == Qt::Unchecked );
            // The state column always shows what the system says.
            CHECK( fileTypeRow( dialog, "text" )->text( StateColumn ) == "Default" );

            dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
            REQUIRE( associations.applied.size() == 1 );
            CHECK( associations.applied[ 0 ].first == QStringList{ "trace" } );
            CHECK( associations.applied[ 0 ].second == QStringList{ "log" } );

            // After Apply, the checks follow the states again.
            associations.change( "log", FileAssociationState::Default );
            CHECK( fileTypeRow( dialog, "log" )->checkState( 0 ) == Qt::Checked );
        }

        SECTION( "a type the user is still to confirm is checked, and unchecking gives it back" )
        {
            associations.change( "output", FileAssociationState::Unconfirmed );
            auto* output = fileTypeRow( dialog, "output" );
            CHECK( output->checkState( 0 ) == Qt::Checked );
            CHECK( output->text( StateColumn ) == "Not confirmed" );
            CHECK( output->toolTip( StateColumn ).contains( "Default apps" ) );
            CHECK_FALSE( output->icon( StateColumn ).isNull() );

            output->setCheckState( 0, Qt::Unchecked );
            dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
            REQUIRE( associations.applied.size() == 1 );
            CHECK( associations.applied[ 0 ].first.isEmpty() );
            CHECK( associations.applied[ 0 ].second == QStringList{ "output" } );
            CHECK( output->checkState( 0 ) == Qt::Unchecked );
        }
    }
}

TEST_CASE( "The File Associations page says what applying leads to, and why it is disabled",
           "[optionsdialog][fileassociations]" )
{
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;
    Configuration::get() = Configuration{};
    LogFormatCatalog catalog;
    OptionsDialog dialog( catalog );
    FakeFileAssociations associations;

    SECTION( "a platform where the user confirms elsewhere" )
    {
        associations.note = "Windows asks you to confirm.";
        dialog.showFileAssociations( associations );
        CHECK( dialog.fileAssociationsNoteLabel->isVisibleTo( dialog.fileAssociationsTab ) );
        CHECK( dialog.fileAssociationsNoteLabel->text() == "Windows asks you to confirm." );
    }

    SECTION( "a run that cannot associate, such as an AppImage" )
    {
        associations.available = false;
        associations.reason = "An AppImage cannot register the file types it opens.";
        associations.current = { { "log", FileAssociationState::Default } };
        dialog.showFileAssociations( associations );

        CHECK( isFileAssociationsTabVisible( dialog ) );
        CHECK_FALSE( dialog.fileAssociationsTree->isEnabled() );
        CHECK( dialog.fileAssociationsUnavailable->isVisibleTo( dialog.fileAssociationsTab ) );
        CHECK( dialog.fileAssociationsUnavailableLabel->text() == associations.reason );
        CHECK_FALSE( dialog.fileAssociationsUnavailableIconLabel->pixmap().isNull() );
        // Nothing is read from the system, nothing applied.
        CHECK( fileTypeRow( dialog, "log" )->text( StateColumn ).isEmpty() );
        dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
        CHECK( associations.applied.empty() );
    }
}

TEST_CASE( "The File Associations page adds Open with LogSquirl to every file's context menu",
           "[optionsdialog][fileassociations]" )
{
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    ConfigurationRestorer restorer;
    Configuration::get() = Configuration{};
    LogFormatCatalog catalog;
    OptionsDialog dialog( catalog );
    FakeFileAssociations associations;
    auto* checkBox = dialog.fileAssociationsContextMenuCheckBox;

    SECTION( "a platform without such an entry does not show the check box" )
    {
        dialog.showFileAssociations( associations );
        CHECK_FALSE( checkBox->isVisibleTo( dialog.fileAssociationsTab ) );
        dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
        CHECK( associations.entrySet.empty() );
    }

    SECTION( "Windows shows it checked where the entry is there" )
    {
        associations.offersEntry = true;
        associations.entry = true;
        dialog.showFileAssociations( associations );
        CHECK( checkBox->isVisibleTo( dialog.fileAssociationsTab ) );
        CHECK( checkBox->isChecked() );
        CHECK( checkBox->text().contains( "Open with LogSquirl" ) );

        // Unchanged, nothing is applied.
        dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
        CHECK( associations.entrySet.empty() );

        checkBox->setChecked( false );
        dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
        CHECK( associations.entrySet == std::vector<bool>{ false } );
        CHECK_FALSE( checkBox->isChecked() );

        checkBox->setChecked( true );
        dialog.buttonBox->button( QDialogButtonBox::Ok )->click();
        CHECK( associations.entrySet == std::vector<bool>{ false, true } );
        // The file types were not touched.
        CHECK( associations.applied.empty() );
    }

    SECTION( "the check box the user changed stays while the states change" )
    {
        associations.offersEntry = true;
        associations.entry = true;
        dialog.showFileAssociations( associations );
        checkBox->setChecked( false );
        associations.change( "log", FileAssociationState::Default );
        CHECK_FALSE( checkBox->isChecked() );
        dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
        CHECK( associations.entrySet == std::vector<bool>{ false } );
    }

    SECTION( "Cancel adds nothing" )
    {
        associations.offersEntry = true;
        dialog.showFileAssociations( associations );
        checkBox->setChecked( true );
        dialog.buttonBox->button( QDialogButtonBox::Cancel )->click();
        CHECK( associations.entrySet.empty() );
    }

    SECTION( "a run that cannot associate does not show it" )
    {
        associations.offersEntry = true;
        associations.available = false;
        dialog.showFileAssociations( associations );
        CHECK_FALSE( checkBox->isVisibleTo( dialog.fileAssociationsTab ) );
    }
}

#include "optionsdialog_test.moc"
