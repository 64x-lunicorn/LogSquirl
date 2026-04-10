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

#include "plugindialog.h"

#include "configuration.h"
#include "log.h"
#include "openfilehelper.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStandardPaths>

namespace logsquirl::plugins {

// ── PluginCard ────────────────────────────────────────────────────────

PluginDialog::PluginCard::PluginCard( const MergedPlugin& plugin, PluginDialog* parent )
    : QFrame( parent )
    , pluginId_( plugin.id )
{
    setFrameShape( QFrame::StyledPanel );
    setLineWidth( 1 );
    setFixedHeight( 135 );
    setObjectName( "PluginCard" );
    setStyleSheet(
        "#PluginCard { background: palette(base); border: 1px solid palette(mid); "
        "border-radius: 8px; }" );

    auto* mainLayout = new QHBoxLayout( this );
    mainLayout->setContentsMargins( 12, 10, 12, 10 );
    mainLayout->setSpacing( 12 );

    // Icon
    iconLabel = new QLabel( this );
    iconLabel->setFixedSize( 48, 48 );
    iconLabel->setAlignment( Qt::AlignCenter );
    iconLabel->setPixmap(
        QPixmap( ":/images/logsquirl.png" ).scaled( 48, 48, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation ) );
    mainLayout->addWidget( iconLabel, 0, Qt::AlignTop );

    // Center column: name, author, description
    auto* centerLayout = new QVBoxLayout();
    centerLayout->setSpacing( 2 );

    auto* topRow = new QHBoxLayout();
    nameLabel = new QLabel( plugin.name, this );
    nameLabel->setStyleSheet( "font-weight: bold; font-size: 13px;" );
    topRow->addWidget( nameLabel );

    versionLabel = new QLabel( this );
    versionLabel->setStyleSheet( "color: palette(dark); font-size: 11px;" );
    topRow->addWidget( versionLabel );

    statusBadge = new QLabel( this );
    statusBadge->setAlignment( Qt::AlignCenter );
    statusBadge->setFixedHeight( 20 );
    topRow->addWidget( statusBadge );

    topRow->addStretch();
    centerLayout->addLayout( topRow );

    authorLabel = new QLabel( this );
    authorLabel->setStyleSheet( "color: palette(dark); font-size: 11px;" );
    centerLayout->addWidget( authorLabel );

    licenseLabel = new QLabel( this );
    licenseLabel->setStyleSheet( "color: palette(dark); font-size: 11px;" );
    centerLayout->addWidget( licenseLabel );

    descriptionLabel = new QLabel( plugin.description, this );
    descriptionLabel->setWordWrap( true );
    descriptionLabel->setStyleSheet( "font-size: 12px;" );
    descriptionLabel->setMaximumHeight( 40 );
    centerLayout->addWidget( descriptionLabel );

    centerLayout->addStretch();
    mainLayout->addLayout( centerLayout, 1 );

    // Right column: action buttons
    auto* rightLayout = new QVBoxLayout();
    rightLayout->setSpacing( 6 );

    actionButton = new QPushButton( this );
    actionButton->setFixedWidth( 90 );
    rightLayout->addWidget( actionButton );

    toggleButton = new QPushButton( this );
    toggleButton->setFixedWidth( 90 );
    rightLayout->addWidget( toggleButton );

    rightLayout->addStretch();
    mainLayout->addLayout( rightLayout );

    updateState( plugin );
}

void PluginDialog::PluginCard::setIcon( const QPixmap& icon )
{
    if ( !icon.isNull() ) {
        iconLabel->setPixmap(
            icon.scaled( 48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
    }
}

void PluginDialog::PluginCard::updateState( const MergedPlugin& plugin )
{
    // Version label
    if ( !plugin.installedVersion.isEmpty() ) {
        versionLabel->setText( QString( "v%1" ).arg( plugin.installedVersion ) );
    }
    else if ( !plugin.latestVersion.isEmpty() ) {
        versionLabel->setText( QString( "v%1" ).arg( plugin.latestVersion ) );
    }
    else {
        versionLabel->clear();
    }

    // Author
    authorLabel->setText( plugin.author.isEmpty() ? QString() : plugin.author );

    // License
    licenseLabel->setText( plugin.license.isEmpty() ? QString()
                                                    : tr( "License: %1" ).arg( plugin.license ) );
    licenseLabel->setVisible( !plugin.license.isEmpty() );

    // Status badge
    statusBadge->setText( badgeText( plugin.state ) );
    statusBadge->setStyleSheet( badgeStyleSheet( plugin.state ) );
    statusBadge->setVisible( true );

    // Action button
    switch ( plugin.state ) {
    case PluginState::NotInstalled:
        actionButton->setText( tr( "Install" ) );
        actionButton->setEnabled( true );
        actionButton->setVisible( true );
        toggleButton->setVisible( false );
        break;
    case PluginState::Installed:
        actionButton->setVisible( false );
        toggleButton->setText( tr( "Disable" ) );
        toggleButton->setEnabled( true );
        toggleButton->setVisible( true );
        break;
    case PluginState::Disabled:
        actionButton->setVisible( false );
        toggleButton->setText( tr( "Enable" ) );
        toggleButton->setEnabled( true );
        toggleButton->setVisible( true );
        break;
    case PluginState::UpdateReady:
        actionButton->setText( tr( "Update" ) );
        actionButton->setEnabled( true );
        actionButton->setVisible( true );
        toggleButton->setText( tr( "Disable" ) );
        toggleButton->setEnabled( true );
        toggleButton->setVisible( true );
        break;
    }
}

// ── PluginDialog ──────────────────────────────────────────────────────

PluginDialog::PluginDialog( PluginManager& manager, QWidget* parent )
    : QDialog( parent )
    , manager_( manager )
{
    setWindowTitle( tr( "Plugin Management" ) );
    resize( 750, 550 );

    auto* mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 16, 16, 16, 12 );
    mainLayout->setSpacing( 10 );

    // ── Search bar ───────────────────────────────────────────────────
    searchEdit_ = new QLineEdit( this );
    searchEdit_->setPlaceholderText( tr( "Search plugins..." ) );
    searchEdit_->setClearButtonEnabled( true );
    mainLayout->addWidget( searchEdit_ );

    // ── Tab bar ──────────────────────────────────────────────────────
    auto* tabLayout = new QHBoxLayout();
    tabLayout->setSpacing( 4 );

    tabAll_ = new QToolButton( this );
    tabAll_->setText( tr( "All" ) );
    tabAll_->setCheckable( true );
    tabAll_->setChecked( true );
    tabAll_->setAutoExclusive( true );
    tabAll_->setToolButtonStyle( Qt::ToolButtonTextOnly );
    tabLayout->addWidget( tabAll_ );

    tabInstalled_ = new QToolButton( this );
    tabInstalled_->setText( tr( "Installed" ) );
    tabInstalled_->setCheckable( true );
    tabInstalled_->setAutoExclusive( true );
    tabInstalled_->setToolButtonStyle( Qt::ToolButtonTextOnly );
    tabLayout->addWidget( tabInstalled_ );

    tabUpdates_ = new QToolButton( this );
    tabUpdates_->setText( tr( "Updates" ) );
    tabUpdates_->setCheckable( true );
    tabUpdates_->setAutoExclusive( true );
    tabUpdates_->setToolButtonStyle( Qt::ToolButtonTextOnly );
    tabLayout->addWidget( tabUpdates_ );

    tabLayout->addStretch();
    mainLayout->addLayout( tabLayout );

    // ── Scrollable card area ─────────────────────────────────────────
    scrollArea_ = new QScrollArea( this );
    scrollArea_->setWidgetResizable( true );
    scrollArea_->setFrameShape( QFrame::NoFrame );

    cardContainer_ = new QWidget();
    cardLayout_ = new QVBoxLayout( cardContainer_ );
    cardLayout_->setContentsMargins( 0, 0, 0, 0 );
    cardLayout_->setSpacing( 8 );
    cardLayout_->addStretch();

    scrollArea_->setWidget( cardContainer_ );
    mainLayout->addWidget( scrollArea_, 1 );

    // ── Progress bar (hidden by default) ─────────────────────────────
    progressBar_ = new QProgressBar( this );
    progressBar_->setVisible( false );
    mainLayout->addWidget( progressBar_ );

    // ── Status label ─────────────────────────────────────────────────
    statusLabel_ = new QLabel( tr( "Fetching plugin catalog..." ), this );
    statusLabel_->setStyleSheet( "color: palette(dark); font-size: 11px;" );
    mainLayout->addWidget( statusLabel_ );

    // ── Footer: auto-load + close ────────────────────────────────────
    auto* footerLayout = new QHBoxLayout();

    auto& config = Configuration::get();
    autoLoadCheck_ = new QCheckBox( tr( "Auto-load enabled plugins on startup" ), this );
    autoLoadCheck_->setChecked( config.pluginsAutoLoad() );
    connect( autoLoadCheck_, &QCheckBox::toggled, this, [ &config ]( bool checked ) {
        config.setPluginsAutoLoad( checked );
        config.save();
    } );
    footerLayout->addWidget( autoLoadCheck_ );

    footerLayout->addStretch();

    auto* pluginFolderButton = new QPushButton( tr( "Plugin Folder" ), this );
    pluginFolderButton->setToolTip(
        tr( "Open the user plugin directory in the file manager" ) );
    connect( pluginFolderButton, &QPushButton::clicked, this, []() {
        const auto dir = QStandardPaths::writableLocation(
                             QStandardPaths::AppDataLocation )
                         + "/plugins";
        QDir().mkpath( dir );
        showPathInFileExplorer( dir );
    } );
    footerLayout->addWidget( pluginFolderButton );

    auto* closeButton = new QPushButton( tr( "Close" ), this );
    connect( closeButton, &QPushButton::clicked, this, &QDialog::accept );
    footerLayout->addWidget( closeButton );

    mainLayout->addLayout( footerLayout );

    // ── Connect signals ──────────────────────────────────────────────
    connect( searchEdit_, &QLineEdit::textChanged, this, &PluginDialog::onFilterChanged );

    connect( tabAll_, &QToolButton::clicked, this, [ this ]() { onTabChanged( 0 ); } );
    connect( tabInstalled_, &QToolButton::clicked, this, [ this ]() { onTabChanged( 1 ); } );
    connect( tabUpdates_, &QToolButton::clicked, this, [ this ]() { onTabChanged( 2 ); } );

    connect( &repository_, &PluginRepository::catalogReady, this,
             &PluginDialog::onCatalogReady );
    connect( &repository_, &PluginRepository::fetchError, this,
             &PluginDialog::onFetchError );
    connect( &repository_, &PluginRepository::iconReady, this,
             &PluginDialog::onIconReady );
    connect( &repository_, &PluginRepository::downloadProgress, this,
             [ this ]( qint64 recv, qint64 total ) {
                 progressBar_->setVisible( true );
                 if ( total > 0 ) {
                     progressBar_->setRange( 0, static_cast<int>( total / 1024 ) );
                     progressBar_->setValue( static_cast<int>( recv / 1024 ) );
                 }
             } );
    connect( &repository_, &PluginRepository::downloadFinished, this,
             &PluginDialog::onDownloadFinished );
    connect( &repository_, &PluginRepository::downloadError, this,
             &PluginDialog::onDownloadError );

    // Build initial cards from locally discovered plugins
    rebuildMergedList();
    rebuildCards();

    // Start fetching the remote catalog
    repository_.fetchCatalog();
}

// ── Slots ─────────────────────────────────────────────────────────────

void PluginDialog::onCatalogReady()
{
    statusLabel_->setText(
        tr( "%1 plugins available" ).arg( repository_.catalog().size() ) );
    rebuildMergedList();
    rebuildCards();
}

void PluginDialog::onIconReady( const QString& pluginId, const QPixmap& icon )
{
    auto it = cards_.find( pluginId );
    if ( it != cards_.end() ) {
        it->second->setIcon( icon );
    }
}

void PluginDialog::onFetchError( const QString& message )
{
    statusLabel_->setText( tr( "Error: %1" ).arg( message ) );
}

void PluginDialog::onFilterChanged( const QString& /* text */ )
{
    applyFilter();
}

void PluginDialog::onTabChanged( int index )
{
    activeTab_ = index;
    applyFilter();
}

void PluginDialog::onDownloadFinished( const QString& archivePath )
{
    progressBar_->setVisible( false );

    const auto pluginId = currentInstallId_;
    currentInstallId_.clear();

    if ( pluginId.isEmpty() ) {
        statusLabel_->setText( tr( "Downloaded to %1" ).arg( archivePath ) );
        return;
    }

    if ( !extractAndInstall( archivePath, pluginId ) ) {
        return;
    }

    // Find name for status message
    QString pluginName = pluginId;
    for ( const auto& mp : mergedPlugins_ ) {
        if ( mp.id == pluginId ) {
            pluginName = mp.name;
            break;
        }
    }

    statusLabel_->setText( tr( "%1 installed and activated." ).arg( pluginName ) );
    QMessageBox::information(
        this, tr( "Plugin Installed" ),
        tr( "%1 has been installed and activated.\n\n"
            "You can now use it from the Plugins menu." )
            .arg( pluginName ) );

    rebuildMergedList();
    rebuildCards();
}

void PluginDialog::onDownloadError( const QString& message )
{
    progressBar_->setVisible( false );
    statusLabel_->setText( tr( "Download failed: %1" ).arg( message ) );
    QMessageBox::warning( this, tr( "Download Error" ), message );
}

// ── Merge local + remote data ─────────────────────────────────────────

void PluginDialog::rebuildMergedList()
{
    const auto& config = Configuration::get();
    const auto enabledIds = config.enabledPlugins();

    // Start with discovered (local) plugins
    std::map<QString, MergedPlugin> byId;

    for ( const auto& meta : manager_.discoveredPlugins() ) {
        MergedPlugin mp;
        mp.id = meta.id();
        mp.name = meta.name();
        mp.author = meta.author();
        mp.description = meta.description();
        mp.license = meta.license();
        mp.installedVersion = meta.version();

        if ( enabledIds.contains( meta.id() ) ) {
            mp.state = PluginState::Installed;
        }
        else {
            mp.state = PluginState::Disabled;
        }

        byId[ mp.id ] = std::move( mp );
    }

    // Merge catalog entries from the remote repository
    for ( const auto& ce : repository_.catalog() ) {
        auto it = byId.find( ce.id );
        if ( it != byId.end() ) {
            // Plugin exists locally — enrich with remote data
            auto& mp = it->second;
            if ( mp.description.isEmpty() ) {
                mp.description = ce.description;
            }
            if ( mp.author.isEmpty() ) {
                mp.author = ce.author;
            }
            if ( mp.license.isEmpty() ) {
                mp.license = ce.license;
            }

            // Check for updates
            const auto* latest = repository_.latestRelease( ce.id );
            if ( latest ) {
                mp.latestVersion = latest->version;
                if ( !mp.installedVersion.isEmpty()
                     && mp.installedVersion != latest->version ) {
                    mp.state = PluginState::UpdateReady;
                }
            }
        }
        else {
            // Remote-only plugin
            MergedPlugin mp;
            mp.id = ce.id;
            mp.name = ce.name;
            mp.author = ce.author;
            mp.description = ce.description;
            mp.license = ce.license;
            mp.state = PluginState::NotInstalled;

            const auto* latest = repository_.latestRelease( ce.id );
            if ( latest ) {
                mp.latestVersion = latest->version;
            }

            byId[ mp.id ] = std::move( mp );
        }
    }

    mergedPlugins_.clear();
    mergedPlugins_.reserve( byId.size() );
    for ( auto& [ id, mp ] : byId ) {
        mergedPlugins_.push_back( std::move( mp ) );
    }

    // Sort: installed/enabled first, then alphabetically
    std::sort( mergedPlugins_.begin(), mergedPlugins_.end(),
               []( const MergedPlugin& a, const MergedPlugin& b ) {
                   const auto aInstalled = ( a.state != PluginState::NotInstalled );
                   const auto bInstalled = ( b.state != PluginState::NotInstalled );
                   if ( aInstalled != bInstalled ) {
                       return aInstalled > bInstalled;
                   }
                   return a.name.toLower() < b.name.toLower();
               } );
}

// ── Card management ───────────────────────────────────────────────────

void PluginDialog::rebuildCards()
{
    // Remove existing cards
    for ( auto& [ id, card ] : cards_ ) {
        cardLayout_->removeWidget( card );
        card->deleteLater();
    }
    cards_.clear();

    // Insert new cards before the stretch
    for ( const auto& mp : mergedPlugins_ ) {
        auto* card = new PluginCard( mp, this );

        // Wire action button
        connect( card->actionButton, &QPushButton::clicked, this,
                 [ this, id = mp.id ]() { installPlugin( id ); } );

        // Wire toggle button
        connect( card->toggleButton, &QPushButton::clicked, this,
                 [ this, id = mp.id ]() { togglePlugin( id ); } );

        // Set icon from cache
        const auto icon = repository_.pluginIcon( mp.id );
        if ( !icon.isNull() ) {
            card->setIcon( icon );
        }

        // Insert before the trailing stretch
        cardLayout_->insertWidget( cardLayout_->count() - 1, card );
        cards_[ mp.id ] = card;
    }

    updateTabBadges();
    applyFilter();
}

void PluginDialog::applyFilter()
{
    const auto searchText = searchEdit_->text().toLower();

    for ( auto& [ id, card ] : cards_ ) {
        // Find the merged plugin for this card
        const MergedPlugin* mp = nullptr;
        for ( const auto& m : mergedPlugins_ ) {
            if ( m.id == id ) {
                mp = &m;
                break;
            }
        }

        bool visible = true;

        // Tab filter
        if ( mp ) {
            switch ( activeTab_ ) {
            case 1: // Installed
                visible = ( mp->state != PluginState::NotInstalled );
                break;
            case 2: // Updates
                visible = ( mp->state == PluginState::UpdateReady );
                break;
            default: // All
                break;
            }
        }

        // Text search filter
        if ( visible && !searchText.isEmpty() ) {
            const auto name = mp ? mp->name.toLower() : QString();
            const auto desc = mp ? mp->description.toLower() : QString();
            const auto author = mp ? mp->author.toLower() : QString();
            visible = name.contains( searchText ) || desc.contains( searchText )
                      || author.contains( searchText ) || id.toLower().contains( searchText );
        }

        card->setVisible( visible );
    }
}

void PluginDialog::updateTabBadges()
{
    int installedCount = 0;
    int updateCount = 0;

    for ( const auto& mp : mergedPlugins_ ) {
        if ( mp.state != PluginState::NotInstalled ) {
            ++installedCount;
        }
        if ( mp.state == PluginState::UpdateReady ) {
            ++updateCount;
        }
    }

    tabAll_->setText( tr( "All (%1)" ).arg( mergedPlugins_.size() ) );
    tabInstalled_->setText( tr( "Installed (%1)" ).arg( installedCount ) );
    tabUpdates_->setText(
        updateCount > 0 ? tr( "Updates (%1)" ).arg( updateCount ) : tr( "Updates" ) );
}

// ── Install / toggle ──────────────────────────────────────────────────

void PluginDialog::installPlugin( const QString& pluginId )
{
    const auto* latest = repository_.latestRelease( pluginId );
    if ( !latest || latest->assets.empty() ) {
        QMessageBox::warning( this, tr( "Install Error" ),
                              tr( "No compatible download available for %1 on this platform." )
                                  .arg( pluginId ) );
        return;
    }

    currentInstallId_ = pluginId;

    // Find the plugin name
    QString pluginName = pluginId;
    for ( const auto& mp : mergedPlugins_ ) {
        if ( mp.id == pluginId ) {
            pluginName = mp.name;
            break;
        }
    }

    statusLabel_->setText( tr( "Downloading %1..." ).arg( pluginName ) );

    // Disable the action button while downloading
    auto it = cards_.find( pluginId );
    if ( it != cards_.end() ) {
        it->second->actionButton->setEnabled( false );
        it->second->actionButton->setText( tr( "Installing..." ) );
    }

    const auto dirs = PluginManager::defaultPluginDirectories();
    const auto destDir = dirs.isEmpty() ? QDir::tempPath() : dirs.first();
    repository_.downloadPlugin( latest->assets.front(), pluginId, destDir );
}

void PluginDialog::togglePlugin( const QString& pluginId )
{
    auto& config = Configuration::get();
    auto enabled = config.enabledPlugins();

    if ( enabled.contains( pluginId ) ) {
        // Disable
        enabled.removeAll( pluginId );
        config.setEnabledPlugins( enabled );
        config.save();

        if ( manager_.isLoaded( pluginId ) ) {
            manager_.unloadPlugin( pluginId );
        }
        statusLabel_->setText( tr( "Plugin disabled." ) );
    }
    else {
        // Enable
        enabled.append( pluginId );
        config.setEnabledPlugins( enabled );
        config.save();

        const auto error = manager_.loadPlugin( pluginId );
        if ( !error.isEmpty() ) {
            QMessageBox::warning( this, tr( "Plugin Error" ),
                                  tr( "Failed to load %1:\n%2" ).arg( pluginId, error ) );
        }
        else {
            statusLabel_->setText( tr( "Plugin enabled." ) );
        }
    }

    rebuildMergedList();
    rebuildCards();
}

bool PluginDialog::extractAndInstall( const QString& archivePath, const QString& pluginId )
{
    const auto dirs = PluginManager::defaultPluginDirectories();
    if ( dirs.isEmpty() ) {
        QMessageBox::warning( this, tr( "Install Error" ),
                              tr( "No plugin directory configured." ) );
        return false;
    }

    const auto pluginDir = QDir( dirs.first() ).filePath( pluginId );
    const auto backupDir = pluginDir + ".bak";

    // Unload existing plugin before overwriting
    if ( manager_.isLoaded( pluginId ) ) {
        LOG_INFO << "Unloading existing plugin " << pluginId << " for update";
        manager_.unloadPlugin( pluginId );
    }

    // Back up existing directory
    bool hasBackup = false;
    if ( QDir( pluginDir ).exists() ) {
        if ( QDir( backupDir ).exists() ) {
            QDir( backupDir ).removeRecursively();
        }
        hasBackup = QDir().rename( pluginDir, backupDir );
    }

    // Extract
    QString extractError;
    if ( !PluginRepository::extractPluginArchive( archivePath, pluginDir, &extractError ) ) {
        if ( hasBackup ) {
            QDir( pluginDir ).removeRecursively();
            QDir().rename( backupDir, pluginDir );
        }
        statusLabel_->setText( tr( "Installation failed." ) );
        QMessageBox::warning( this, tr( "Install Error" ),
                              tr( "Failed to extract plugin archive:\n%1" )
                                  .arg( extractError ) );
        return false;
    }

    // Clean up backup and archive
    if ( hasBackup ) {
        QDir( backupDir ).removeRecursively();
    }
    QFile::remove( archivePath );

    // Re-discover and load
    manager_.discoverPlugins();
    const auto loadError = manager_.loadPlugin( pluginId );
    if ( !loadError.isEmpty() ) {
        LOG_WARNING << "Plugin extracted but failed to load: " << loadError;
        statusLabel_->setText( tr( "Extracted but failed to load." ) );
        QMessageBox::warning( this, tr( "Load Error" ),
                              tr( "Plugin was extracted but could not be loaded:\n%1\n\n"
                                  "Restart LogSquirl to try again." )
                                  .arg( loadError ) );
        return false;
    }

    // Add to enabled list
    auto& config = Configuration::get();
    auto enabled = config.enabledPlugins();
    if ( !enabled.contains( pluginId ) ) {
        enabled.append( pluginId );
        config.setEnabledPlugins( enabled );
        config.save();
    }

    return true;
}

// ── Badge helpers ─────────────────────────────────────────────────────

QString PluginDialog::badgeStyleSheet( PluginState state )
{
    switch ( state ) {
    case PluginState::Installed:
        return "background-color: #2ea043; color: white; border-radius: 4px; "
               "padding: 2px 8px; font-size: 10px; font-weight: bold;";
    case PluginState::UpdateReady:
        return "background-color: #d29922; color: white; border-radius: 4px; "
               "padding: 2px 8px; font-size: 10px; font-weight: bold;";
    case PluginState::Disabled:
        return "background-color: #6e7681; color: white; border-radius: 4px; "
               "padding: 2px 8px; font-size: 10px; font-weight: bold;";
    case PluginState::NotInstalled:
        return "background-color: #388bfd; color: white; border-radius: 4px; "
               "padding: 2px 8px; font-size: 10px; font-weight: bold;";
    }
    return {};
}

QString PluginDialog::badgeText( PluginState state )
{
    switch ( state ) {
    case PluginState::Installed:
        return tr( "Active" );
    case PluginState::UpdateReady:
        return tr( "Update" );
    case PluginState::Disabled:
        return tr( "Disabled" );
    case PluginState::NotInstalled:
        return tr( "Available" );
    }
    return {};
}

} // namespace logsquirl::plugins
