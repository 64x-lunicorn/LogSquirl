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

#ifndef LOGSQUIRL_TESTS_CONFIGURATIONFIXTURE_H
#define LOGSQUIRL_TESTS_CONFIGURATIONFIXTURE_H

// Helpers shared by the tests that check which settings are stored (#108).

#include <memory>

#include <QFile>
#include <QFontInfo>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>

#include "configuration.h"
#include "fontutils.h"
#include "painting_test_font.h"
#include "shortcuts.h"
#include "theme.h"

namespace configuration_fixture {

// A settings file in a temporary directory that lives as long as this object.
//
// QSettings keeps what it wrote to a file in a per-process cache, with the
// values' original types. Every access therefore opens a fresh copy of the
// file, so values are always parsed from disk as on the next start.
class SettingsFile {
public:
    SettingsFile() = default;

    static std::unique_ptr<SettingsFile> copyOf( const QString& sourcePath )
    {
        auto file = std::make_unique<SettingsFile>();
        const auto path = file->nextPath();
        QFile::copy( sourcePath, path );
        QFile::setPermissions( path, QFile::ReadOwner | QFile::WriteOwner );
        return file;
    }

    void write( const Configuration& config )
    {
        QSettings settings( nextPath(), QSettings::IniFormat );
        config.saveToStorage( settings );
    }

    void setValue( const QString& key, const QVariant& value )
    {
        QSettings settings( nextPath(), QSettings::IniFormat );
        settings.setValue( key, value );
    }

    Configuration load()
    {
        Configuration config;
        QSettings settings( nextPath(), QSettings::IniFormat );
        config.retrieveFromStorage( settings );
        return config;
    }

    // Every stored key with its value, as parsed from disk.
    QVariantMap values()
    {
        QSettings settings( nextPath(), QSettings::IniFormat );
        QVariantMap values;
        for ( const auto& key : settings.allKeys() ) {
            values.insert( key, settings.value( key ) );
        }
        return values;
    }

private:
    // A path no QSettings has opened yet, holding the current file content.
    QString nextPath()
    {
        const auto path = dir_.filePath( QStringLiteral( "settings-%1.ini" ).arg( ++version_ ) );
        if ( !path_.isEmpty() ) {
            QFile::copy( path_, path );
        }
        path_ = path;
        return path_;
    }

    QTemporaryDir dir_;
    QString path_;
    int version_ = 0;
};

inline QVariantMap storedSettings( const Configuration& config )
{
    SettingsFile file;
    file.write( config );
    return file.values();
}

// The setting a stored key belongs to: groups and arrays ("dark/Window",
// "shortcuts/1/keys") are one setting each.
inline QString settingName( const QString& key )
{
    return key.section( '/', 0, 0 );
}

inline QStringList settingNames( const QVariantMap& values )
{
    QStringList names;
    for ( const auto& key : values.keys() ) {
        const auto name = settingName( key );
        if ( !names.contains( name ) ) {
            names << name;
        }
    }
    names.sort();
    return names;
}

// A fixed-pitch family the Options Dialog offers that does not resolve to
// the same font as the default one, so a stored font is not the default.
// The tests' own fixed-pitch fonts are registered first: the Linux CI
// containers have one fixed-pitch family and Windows' offscreen platform none,
// so the default could otherwise be the only family on offer. With two
// registered families, at least one differs from whatever the default resolves
// to (data/painting/make_test_font.py generates both).
inline QString nonDefaultFontFamily()
{
    paintingtestfont::loadPaintingTestFont();
    static const auto settingsFontId
        = QFontDatabase::addApplicationFont( QStringLiteral( LOGSQUIRL_CONFIGURATION_TEST_DATA_DIR )
                                             + QStringLiteral( "/logsquirl-settings-test.ttf" ) );
    Q_UNUSED( settingsFontId );
    const auto defaultFamily = QFontInfo( Configuration{}.mainFont() ).family();
    for ( const auto& family : FontUtils::availableFonts() ) {
        const auto resolved = QFontInfo( QFont( family, 14 ) ).family();
        if ( resolved == family && resolved != defaultFamily ) {
            return family;
        }
    }
    return {};
}

inline constexpr auto FixtureShortcutAction = ShortcutAction::CrawlerEnableRegex;
inline const QStringList FixtureShortcutKeys = { "F7", "F8" };

// A Configuration where every setting that has a setter holds a value other
// than its default. The values also fit the Options Dialog's widgets, so the
// dialog can show every one of them.
inline Configuration nonDefaultConfiguration()
{
    const Configuration defaults;
    Configuration config;

    config.setMainFont( QFont( nonDefaultFontFamily(), 14 ) );
    config.setForceFontAntialiasing( true );
    config.setUseBoldFont( true );
    config.setLanguage( "de" );
    config.setEnableQtHighDpi( false );
    config.setScaleFactorRounding( 3 );

    config.setMainRegexpType( SearchRegexpType::FixedString );
    config.setQuickfindRegexpType( SearchRegexpType::ExtendedRegexp );
    config.setRegexpEngine( RegexpEngine::QRegularExpression );
    config.setQuickfindIncremental( false );
    config.setEnableMainSearchHighlight( true );
    config.setVariateMainSearchHighlight( true );
    config.setMainSearchBackColor( QColor( 0x12, 0x34, 0x56, 0x80 ) );
    config.setQfBackColor( QColor( 0xab, 0xcd, 0xef ) );
    config.setQfIgnoreCase( true );
    config.setAutoRunSearchOnPatternChange( true );

    config.setNativeFileWatchEnabled( false );
    config.setPollingEnabled( !defaults.pollingEnabled() );
    config.setPollIntervalMs( 3000 );
    config.setFastModificationDetection( true );
    config.setAllowFollowOnScroll( false );
    config.setFastScrollEnabled( false );
    config.setFastScrollMultiplier( 7 );

    config.setLoadLastSession( false );
    config.setAllowMultipleWindows( true );
    config.setFollowFileOnLoad( true );
    config.setConfirmTabClose( false );

    config.setEnableLogging( true );
    config.setLoggingLevel( 2 );

    config.setVersionCheckingEnabled( false );
    config.setBetaVersionCheckingEnabled( true );

    config.setExtractArchives( false );
    config.setExtractArchivesAlways( true );

    config.setUseParallelSearch( false );
    config.setUseSearchResultsCache( false );
    config.setSearchResultsCacheLines( 500000 );
    config.setIndexReadBufferSizeMb( 32 );
    config.setSearchReadBufferSizeLines( 20000 );
    config.setSearchThreadPoolSize( 4 );
    config.setKeepFileClosed( true );
    config.setOptimizeForNotLatinEncodings( true );
    config.setUseCompressedIndex( false );
    config.setUseIndexCache( true );
    config.setIndexCacheMaxSizeMb( 1000 );

    config.setVerifySslPeers( false );

    config.setOverviewVisible( false );
    config.setMainLineNumbersVisible( true );
    config.setFilteredLineNumbersVisible( false );
    config.setMinimizeToTray( true );
    config.setContextLinesCount( 10 );
    config.setHideAnsiColorSequences( true );
    config.setUseTextWrap( true );
    config.setStyle( Theme::DarkKey );

    config.setSearchAutoRefreshDefault( true );
    config.setSearchIgnoreCaseDefault( true );
    config.setSearchLogicalCombiningDefault( true );
    config.setDefaultEncodingMib( 106 );
    config.setSplitterSizes( { 300, 200 } );
    config.setSearchWindowMinutes( 15 );

    config.setShortcuts( { { FixtureShortcutAction, FixtureShortcutKeys } } );

    config.setShowSplashScreen( true );
    config.setShowDashboard( false );
    config.setToolbarIconSize( 32 );

    config.setAutoDetectLogFormats( true );
    config.setAutoShowTableView( true );

    config.setPluginsAutoLoad( false );
    config.setEnabledPlugins( { "com.example.a", "com.example.b" } );

    config.setChartPreset( "Latency", R"({"series": [{"field": "duration", "unit": "ms"}]})" );
    config.setChartPreset( "Errors", "[]" );

    // Dark Token overrides have no setter; the settings file test covers them.
    return config;
}

} // namespace configuration_fixture

#endif
