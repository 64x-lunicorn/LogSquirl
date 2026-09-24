/*
 * Copyright (C) 2009, 2010, 2013, 2015 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <map>
#include <type_traits>

#include <QCoreApplication>
#include <QFontInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QVariant>

#include "configuration.h"
#include "log.h"
#include "shortcuts.h"
#include "theme.h"

namespace {
#ifdef Q_OS_WIN
constexpr bool PollingEnabledByDefault = true;
#else
constexpr bool PollingEnabledByDefault = false;
#endif

using Shortcuts = std::map<std::string, QStringList>;
using ChartPresets = QMap<QString, QString>;
using DarkPalette = std::map<QString, QString>;

// The key a setting is stored under, and the key an older release stored it
// under, if there was one.
struct SettingKey {
    constexpr SettingKey( const char* keyName, const char* retiredKeyName = nullptr )
        : name( keyName )
        , retiredName( retiredKeyName )
    {
    }

    const char* name;
    const char* retiredName;
};

// Corrects a value just read from storage.
template <typename T>
using Correction = T ( * )( T );

// The stored value of a setting, or its default when nothing is stored. A
// value stored under the retired key counts when the current key has none;
// the retired key is removed.
QVariant storedValue( QSettings& settings, const SettingKey& key, const QVariant& defaultValue )
{
    auto fallback = defaultValue;
    if ( key.retiredName != nullptr ) {
        fallback = settings.value( key.retiredName, defaultValue );
        settings.remove( key.retiredName );
    }
    return settings.value( key.name, fallback );
}

// How a value of each type is read from and written to storage.
template <typename T>
struct Codec;

template <typename T>
struct VariantCodec {
    static void write( QSettings& settings, const SettingKey& key, const T& value )
    {
        settings.setValue( key.name, QVariant::fromValue( value ) );
    }
};

template <>
struct Codec<bool> : VariantCodec<bool> {
    static bool read( QSettings& settings, const SettingKey& key, bool defaultValue )
    {
        return storedValue( settings, key, defaultValue ).toBool();
    }
};

template <>
struct Codec<int> : VariantCodec<int> {
    static int read( QSettings& settings, const SettingKey& key, int defaultValue )
    {
        return storedValue( settings, key, defaultValue ).toInt();
    }
};

template <>
struct Codec<unsigned> : VariantCodec<unsigned> {
    static unsigned read( QSettings& settings, const SettingKey& key, unsigned defaultValue )
    {
        return storedValue( settings, key, defaultValue ).toUInt();
    }
};

template <>
struct Codec<QString> : VariantCodec<QString> {
    static QString read( QSettings& settings, const SettingKey& key, const QString& defaultValue )
    {
        return storedValue( settings, key, defaultValue ).toString();
    }
};

template <>
struct Codec<QStringList> : VariantCodec<QStringList> {
    static QStringList read( QSettings& settings, const SettingKey& key,
                             const QStringList& defaultValue )
    {
        return storedValue( settings, key, defaultValue ).toStringList();
    }
};

// Enumerations are stored as their numeric value.
template <typename Enum>
    requires std::is_enum_v<Enum>
struct Codec<Enum> {
    static Enum read( QSettings& settings, const SettingKey& key, Enum defaultValue )
    {
        return static_cast<Enum>(
            storedValue( settings, key, static_cast<int>( defaultValue ) ).toInt() );
    }

    static void write( QSettings& settings, const SettingKey& key, Enum value )
    {
        settings.setValue( key.name, static_cast<int>( value ) );
    }
};

// Colors are stored as #AARRGGBB.
template <>
struct Codec<QColor> {
    static QColor read( QSettings& settings, const SettingKey& key, const QColor& defaultValue )
    {
        return QColor::fromString(
            storedValue( settings, key, defaultValue.name( QColor::HexArgb ) ).toString() );
    }

    static void write( QSettings& settings, const SettingKey& key, const QColor& value )
    {
        settings.setValue( key.name, value.name( QColor::HexArgb ) );
    }
};

// A font is stored as <key>.family and <key>.size; the family stored is the
// one the platform resolves.
template <>
struct Codec<QFont> {
    static QFont read( QSettings& settings, const SettingKey& key, const QFont& defaultValue )
    {
        const auto family = settings.value( familyKey( key ), defaultValue.family() ).toString();
        const auto size = settings.value( sizeKey( key ), defaultValue.pointSize() ).toInt();

        return family.isNull() ? defaultValue : QFont( family, size );
    }

    static void write( QSettings& settings, const SettingKey& key, const QFont& value )
    {
        const QFontInfo fontInfo( value );
        settings.setValue( familyKey( key ), fontInfo.family() );
        settings.setValue( sizeKey( key ), fontInfo.pointSize() );
    }

private:
    static QString familyKey( const SettingKey& key )
    {
        return QString( key.name ) + QStringLiteral( ".family" );
    }

    static QString sizeKey( const SettingKey& key )
    {
        return QString( key.name ) + QStringLiteral( ".size" );
    }
};

template <>
struct Codec<QList<int>> {
    static QList<int> read( QSettings& settings, const SettingKey& key,
                            const QList<int>& defaultValue )
    {
        if ( !settings.contains( key.name ) ) {
            return defaultValue;
        }

        QList<int> values;
        const auto variants = settings.value( key.name ).toList();
        for ( const auto& variant : variants ) {
            values << variant.toInt();
        }
        return values;
    }

    static void write( QSettings& settings, const SettingKey& key, const QList<int>& values )
    {
        QVariantList variants;
        for ( const auto value : values ) {
            variants << value;
        }
        settings.setValue( key.name, variants );
    }
};

// Shortcuts are stored as an array of actions with their keys. The retired
// key held them as one map from action to keys.
template <>
struct Codec<Shortcuts> {
    static Shortcuts read( QSettings& settings, const SettingKey& key,
                           const Shortcuts& defaultValue )
    {
        const auto currentActionName = []( const QString& action ) {
            return action == ShortcutAction::LogViewJumpToButtom
                       ? std::string{ ShortcutAction::LogViewJumpToBottom }
                       : action.toStdString();
        };

        auto shortcuts = defaultValue;

        if ( key.retiredName != nullptr && settings.contains( key.retiredName ) ) {
            const auto mapping = settings.value( key.retiredName ).toMap();
            for ( auto keys = mapping.begin(); keys != mapping.end(); ++keys ) {
                shortcuts.emplace( currentActionName( keys.key() ), keys.value().toStringList() );
            }
            settings.remove( key.retiredName );
        }

        const auto count = settings.beginReadArray( key.name );
        for ( auto index = 0; index < count; ++index ) {
            settings.setArrayIndex( index );
            const auto action = settings.value( ActionKey, "" ).toString();
            if ( !action.isEmpty() ) {
                shortcuts.emplace( currentActionName( action ),
                                   settings.value( KeysKey, QStringList() ).toStringList() );
            }
        }
        settings.endArray();

        return shortcuts;
    }

    static void write( QSettings& settings, const SettingKey& key, const Shortcuts& shortcuts )
    {
        settings.beginWriteArray( key.name );
        auto index = 0;
        for ( const auto& [ action, keys ] : shortcuts ) {
            settings.setArrayIndex( index++ );
            settings.setValue( ActionKey, QString::fromStdString( action ) );
            settings.setValue( KeysKey, keys );
        }
        settings.endArray();
    }

private:
    static constexpr auto ActionKey = "action";
    static constexpr auto KeysKey = "keys";
};

template <>
struct Codec<ChartPresets> {
    static ChartPresets read( QSettings& settings, const SettingKey& key,
                              const ChartPresets& defaultValue )
    {
        auto presets = defaultValue;

        const auto count = settings.beginReadArray( key.name );
        for ( auto index = 0; index < count; ++index ) {
            settings.setArrayIndex( index );
            const auto name = settings.value( NameKey ).toString();
            if ( !name.isEmpty() ) {
                presets[ name ] = settings.value( DefinitionsKey ).toString();
            }
        }
        settings.endArray();

        return presets;
    }

    static void write( QSettings& settings, const SettingKey& key, const ChartPresets& presets )
    {
        settings.beginWriteArray( key.name );
        auto index = 0;
        for ( auto preset = presets.cbegin(); preset != presets.cend(); ++preset ) {
            settings.setArrayIndex( index++ );
            settings.setValue( NameKey, preset.key() );
            settings.setValue( DefinitionsKey, preset.value() );
        }
        settings.endArray();
    }

private:
    static constexpr auto NameKey = "name";
    static constexpr auto DefinitionsKey = "definitions";
};

// Overrides of Dark Tokens are stored as a group of color names by Token
// name; exactly the stored entries are overrides (see Theme::fromName()).
template <>
struct Codec<DarkPalette> {
    static DarkPalette read( QSettings& settings, const SettingKey& key,
                             const DarkPalette& defaultValue )
    {
        auto palette = defaultValue;

        settings.beginGroup( key.name );
        const auto tokens = settings.childKeys();
        for ( const auto& token : tokens ) {
            palette[ token ] = settings.value( token ).toString();
        }
        settings.endGroup();

        return palette;
    }

    static void write( QSettings& settings, const SettingKey& key, const DarkPalette& palette )
    {
        settings.beginGroup( key.name );
        for ( const auto& [ token, color ] : palette ) {
            settings.setValue( token, color );
        }
        settings.endGroup();
    }
};

class ApplyDefault {
public:
    template <typename T>
    void operator()( const SettingKey&, T& member, const std::type_identity_t<T>& defaultValue,
                     Correction<std::type_identity_t<T>> = nullptr ) const
    {
        member = defaultValue;
    }
};

class ReadSetting {
public:
    explicit ReadSetting( QSettings& settings )
        : settings_( settings )
    {
    }

    template <typename T>
    void operator()( const SettingKey& key, T& member, const std::type_identity_t<T>& defaultValue,
                     Correction<std::type_identity_t<T>> correction = nullptr ) const
    {
        member = Codec<T>::read( settings_, key, defaultValue );
        if ( correction != nullptr ) {
            member = correction( member );
        }
    }

private:
    QSettings& settings_;
};

class WriteSetting {
public:
    explicit WriteSetting( QSettings& settings )
        : settings_( settings )
    {
    }

    template <typename T>
    void operator()( const SettingKey& key, const T& member, const std::type_identity_t<T>&,
                     Correction<std::type_identity_t<T>> = nullptr ) const
    {
        Codec<T>::write( settings_, key, member );
    }

private:
    QSettings& settings_;
};

// Keeps the size in megabytes small enough to count in bytes in a qint64.
int withinIndexCacheSizeLimits( int sizeMb )
{
    return std::clamp( sizeMb, 0, 8'000'000 ); // ~8 TB, a generous upper bound
}

// The main font is resolved as a fixed-pitch outline font, whichever family
// it names. Every main font a Configuration holds is resolved: its default, the
// one read from storage and the one set on it (#229).
QFont resolvedMainFont( QFont font )
{
    font.setStyleHint( QFont::Courier, QFont::PreferOutline );
    return font;
}

QString availableStyle( QString style )
{
    const auto styles = Theme::availableThemes();
    if ( !styles.contains( style ) ) {
        style = Theme::defaultTheme();
    }
    if ( !styles.contains( style ) ) {
        style = styles.front();
    }
    return style;
}

} // namespace

// Every stored setting, declared once: the key it is stored under, the member
// that holds it, and its default. The declarations drive the defaults, the
// read path and the write path alike.
template <typename Self, typename Visit>
void Configuration::forEachSetting( Self& config, Visit&& visit )
{
    // Stored as mainFont.family and mainFont.size.
    visit( "mainFont", config.mainFont_, resolvedMainFont( QFont{ "DejaVu Sans Mono", 10 } ),
           resolvedMainFont );
    visit( "mainFont.antialiasing", config.forceFontAntialiasing_, false );
    visit( "mainFont.bold", config.useBoldFont_, false );
    visit( "view.language", config.language_, "en" );
    visit( "view.qtHiDpi", config.enableQtHighDpi_, true );
    visit( "view.scaleFactorRounding", config.scaleFactorRounding_, 1 );

    visit( "regexpType.main", config.mainRegexpType_, SearchRegexpType::ExtendedRegexp );
    visit( "regexpType.quickfind", config.quickfindRegexpType_, SearchRegexpType::FixedString );
    visit( "regexpType.engine", config.regexpEngine_, RegexpEngine::Vectorscan );
    visit( "quickfind.incremental", config.quickfindIncremental_, true );
    visit( "regexpType.mainHighlight", config.enableMainSearchHighlight_, false );
    visit( "regexpType.mainHighlightVariate", config.enableMainSearchHighlightVariance_, false );
    visit( "regexpType.mainBackColor", config.mainSearchBackColor_, QColor{ Qt::lightGray } );
    visit( "regexpType.quickfindBackColor", config.qfBackColor_, QColor{ Qt::yellow } );
    visit( "quickfind.ignore_case", config.qfIgnoreCase_, false );
    visit( "regexpType.autoRunSearch", config.autoRunSearchOnPatternChange_, false );

    visit( { "filewatch.useNative", "nativeFileWatch.enabled" }, config.nativeFileWatchEnabled_,
           true );
    visit( { "filewatch.usePolling", "polling.enabled" }, config.pollingEnabled_,
           PollingEnabledByDefault );
    visit( { "filewatch.pollingIntervalMs", "polling.intervalMs" }, config.pollIntervalMs_, 2000 );
    visit( "filewatch.fastModificationDetection", config.fastModificationDetection_, false );
    visit( "filewatch.allowFollowOnScroll", config.allowFollowOnScroll_, true );
    visit( "view.fastScrollEnabled", config.fastScrollEnabled_, true );
    visit( "view.fastScrollMultiplier", config.fastScrollMultiplier_, 5 );

    visit( "session.loadLast", config.loadLastSession_, true );
    visit( "session.multipleWindows", config.allowMultipleWindows_, false );
    visit( "session.followOnLoad", config.followFileOnLoad_, false );
    visit( "session.confirmTabClose", config.confirmTabClose_, true );

    visit( "logging.enableLogging", config.enableLogging_, false );
    visit( "logging.verbosity", config.loggingLevel_, 4 );
    visit( "versionchecker.enabled", config.enableVersionChecking_, true );
    visit( "versionchecker.betaEnabled", config.enableBetaVersionChecking_, false );
    visit( "archives.extract", config.extractArchives_, true );
    visit( "archives.extractAlways", config.extractArchivesAlways_, false );

    visit( "perf.useParallelSearch", config.useParallelSearch_, true );
    visit( "perf.useSearchResultsCache", config.useSearchResultsCache_, true );
    visit( "perf.searchResultsCacheLines", config.searchResultsCacheLines_, 1'000'000u );
    visit( "perf.indexReadBufferSizeMb", config.indexReadBufferSizeMb_, 16 );
    visit( "perf.searchReadBufferSizeLines", config.searchReadBufferSizeLines_, 10'000 );
    visit( "perf.searchThreadPoolSize", config.searchThreadPoolSize_, 0 );
    visit( "perf.keepFileClosed", config.keepFileClosed_, false );
    visit( "perf.optimizeForNotLatinEncodings", config.optimizeForNotLatinEncodings_, false );
    visit( "perf.useCompressedIndex", config.useCompressedIndex_, true );
    visit( "perf.useIndexCache", config.useIndexCache_, false );
    visit( "perf.indexCacheMaxSizeMb", config.indexCacheMaxSizeMb_, 500,
           withinIndexCacheSizeLimits );
    visit( "net.verifySslPeers", config.verifySslPeers_, true );

    visit( "teamFolder.enabled", config.teamFolderEnabled_, false );
    visit( "teamFolder.url", config.teamFolderUrl_, QString{} );
    visit( "teamFolder.subfolder", config.teamFolderSubfolder_, QString{} );

    visit( "view.overviewVisible", config.overviewVisible_, true );
    visit( "view.lineNumbersVisibleInMain", config.lineNumbersVisibleInMain_, false );
    visit( "view.lineNumbersVisibleInFiltered", config.lineNumbersVisibleInFiltered_, true );
    visit( "view.minimizeToTray", config.minimizeToTray_, false );
    visit( "view.contextLinesCount", config.contextLinesCount_, 5 );
    visit( "view.hideAnsiColorSequences", config.hideAnsiColorSequences_, false );
    visit( "view.textWrap", config.useTextWrap_, false );
    visit( "view.style", config.style_, QString{}, availableStyle );
    visit( "view.showSplashScreen", config.showSplashScreen_, false );
    visit( "view.showDashboard", config.showDashboard_, true );
    visit( "view.toolbarIconSize", config.toolbarIconSize_, 24 );

    visit( "defaultView.searchAutoRefresh", config.searchAutoRefresh_, false );
    visit( "defaultView.searchIgnoreCase", config.searchIgnoreCase_, false );
    visit( "defaultView.searchLogicalCombining", config.searchLogicalCombining_, false );
    visit( "defaultView.encodingMib", config.defaultEncodingMib_, -1 );
    visit( "defaultView.splitterSizes", config.splitterSizes_, QList<int>{ 400, 100 } );

    visit( { "shortcuts", "shortcuts.mapping" }, config.shortcuts_, Shortcuts{} );

    visit( "logformat.autoDetect", config.autoDetectLogFormats_, false );
    visit( "logformat.autoShowTable", config.autoShowTableView_, false );

    visit( "plugins.autoLoad", config.pluginsAutoLoad_, true );
    visit( "plugins.enabledPlugins", config.enabledPlugins_, QStringList{} );

    visit( "chartPresets", config.chartPresets_, ChartPresets{} );

    // Only overrides are stored; the Dark Theme holds the Tokens themselves.
    visit( "dark", config.darkPalette_, DarkPalette{} );
}

Configuration::Configuration()
{
    forEachSetting( *this, ApplyDefault{} );
}

QString Configuration::indexCacheDirectory() const
{
    return QStandardPaths::writableLocation( QStandardPaths::CacheLocation )
           + QStringLiteral( "/index" );
}

// Accessor functions
QFont Configuration::mainFont() const
{
    return mainFont_;
}

void Configuration::setMainFont( QFont newFont )
{
    LOG_DEBUG << "Configuration::setMainFont";

    mainFont_ = resolvedMainFont( std::move( newFont ) );
}

QString Configuration::mainFontInWords( const QFont& font )
{
    // QFontInfo answers which font the request resolves to, but it resolves
    // through the font database, and a QCoreApplication has none: asking for
    // one there aborts the process. Without a font database there is no
    // resolution to report, so the font asked for is all there is to say.
    if ( qobject_cast<QGuiApplication*>( QCoreApplication::instance() ) == nullptr ) {
        return QStringLiteral( "%1: %2, as requested (no font database)" )
            .arg( font.family() )
            .arg( font.pointSize() );
    }

    const QFontInfo resolved( font );
    return QStringLiteral( "%1: %2" ).arg( resolved.family() ).arg( resolved.pointSize() );
}

void Configuration::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "Configuration::retrieveFromStorage";

    forEachSetting( *this, ReadSetting{ settings } );

    LOG_INFO << "Main font is " << mainFontInWords( mainFont_ );
}

void Configuration::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "Configuration::saveToStorage";

    forEachSetting( *this, WriteSetting{ settings } );
}
