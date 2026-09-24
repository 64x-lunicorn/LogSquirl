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
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include "configuration.h"
#include "configurationfixture.h"
#include "logformatcatalog.h"
#include "optionsdialog.h"
#include "recentfiles.h"
#include "savedsearches.h"

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
    "view.contextLinesCount",
    "view.fastScrollEnabled",
    "view.fastScrollMultiplier",
    "view.hideAnsiColorSequences",
    "view.language",
    "view.minimizeToTray",
    "view.qtHiDpi",
    "view.scaleFactorRounding",
    "view.showDashboard",
    "view.showSplashScreen",
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
