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

#include "pluginrepositorydialog.h"

#include "configuration.h"
#include "log.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QMessageBox>

namespace logsquirl::plugins {

PluginRepositoryDialog::PluginRepositoryDialog( PluginManager& manager, QWidget* parent )
    : QDialog( parent )
    , manager_( manager )
{
    setWindowTitle( tr( "Plugin Repository" ) );
    resize( 700, 450 );

    auto* layout = new QVBoxLayout( this );

    // Filter field
    filterEdit_ = new QLineEdit( this );
    filterEdit_->setPlaceholderText( tr( "Search plugins..." ) );
    layout->addWidget( filterEdit_ );

    // Plugin table
    table_ = new QTableWidget( this );
    table_->setColumnCount( 5 );
    table_->setHorizontalHeaderLabels(
        { tr( "Name" ), tr( "Version" ), tr( "Author" ), tr( "Description" ), tr( "Action" ) } );
    table_->horizontalHeader()->setStretchLastSection( true );
    table_->setSelectionBehavior( QAbstractItemView::SelectRows );
    table_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table_->verticalHeader()->hide();
    layout->addWidget( table_ );

    // Progress bar (hidden by default)
    progressBar_ = new QProgressBar( this );
    progressBar_->setVisible( false );
    layout->addWidget( progressBar_ );

    // Status label
    statusLabel_ = new QLabel( tr( "Fetching plugin index..." ), this );
    layout->addWidget( statusLabel_ );

    // Close button
    auto* buttonBox = new QDialogButtonBox( QDialogButtonBox::Close, this );
    connect( buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject );
    layout->addWidget( buttonBox );

    // Connect signals
    connect( filterEdit_, &QLineEdit::textChanged, this,
             &PluginRepositoryDialog::onFilterChanged );
    connect( &repository_, &PluginRepository::indexReady, this,
             &PluginRepositoryDialog::onIndexReady );
    connect( &repository_, &PluginRepository::fetchError, this,
             &PluginRepositoryDialog::onFetchError );
    connect( &repository_, &PluginRepository::downloadProgress, this,
             [ this ]( qint64 recv, qint64 total ) {
                 progressBar_->setVisible( true );
                 if ( total > 0 ) {
                     progressBar_->setRange( 0, static_cast<int>( total / 1024 ) );
                     progressBar_->setValue( static_cast<int>( recv / 1024 ) );
                 }
             } );
    connect( &repository_, &PluginRepository::downloadFinished, this,
             &PluginRepositoryDialog::onDownloadFinished );
    connect( &repository_, &PluginRepository::downloadError, this,
             &PluginRepositoryDialog::onDownloadError );

    // Start fetching
    repository_.fetchIndex();
}

void PluginRepositoryDialog::onIndexReady()
{
    statusLabel_->setText(
        tr( "%1 plugins available" ).arg( repository_.entries().size() ) );
    populateTable();
}

void PluginRepositoryDialog::onFetchError( const QString& message )
{
    statusLabel_->setText( tr( "Error: %1" ).arg( message ) );
}

void PluginRepositoryDialog::onFilterChanged( const QString& text )
{
    const auto filter = text.toLower();
    for ( int row = 0; row < table_->rowCount(); ++row ) {
        const auto name = table_->item( row, 0 )->text().toLower();
        const auto desc = table_->item( row, 3 )->text().toLower();
        const bool visible = name.contains( filter ) || desc.contains( filter );
        table_->setRowHidden( row, !visible );
    }
}

void PluginRepositoryDialog::onInstallClicked()
{
    auto* button = qobject_cast<QPushButton*>( sender() );
    if ( !button ) {
        return;
    }

    const auto pluginId = button->property( "pluginId" ).toString();

    // Find the entry
    for ( const auto& entry : repository_.entries() ) {
        if ( entry.id == pluginId ) {
            currentInstallId_ = pluginId;
            statusLabel_->setText( tr( "Downloading %1..." ).arg( entry.name ) );
            button->setEnabled( false );
            button->setText( tr( "Installing..." ) );

            // Download to the first default plugin directory
            const auto dirs = PluginManager::defaultPluginDirectories();
            const auto destDir = dirs.isEmpty() ? QDir::tempPath() : dirs.first();
            repository_.downloadPlugin( entry, destDir );
            return;
        }
    }
}

void PluginRepositoryDialog::onDownloadFinished( const QString& archivePath )
{
    progressBar_->setVisible( false );

    const auto pluginId = currentInstallId_;
    currentInstallId_.clear();

    if ( pluginId.isEmpty() ) {
        // Fallback: cannot determine plugin ID, show manual instructions
        statusLabel_->setText( tr( "Downloaded to %1" ).arg( archivePath ) );
        QMessageBox::information(
            this, tr( "Plugin Downloaded" ),
            tr( "Plugin archive saved to:\n%1\n\n"
                "Extract the archive to the plugin directory and "
                "restart LogSquirl to activate it." )
                .arg( archivePath ) );
        return;
    }

    if ( !extractAndInstall( archivePath, pluginId ) ) {
        return; // extractAndInstall already showed the error
    }

    // Find the human-readable name for the status message
    QString pluginName = pluginId;
    for ( const auto& entry : repository_.entries() ) {
        if ( entry.id == pluginId ) {
            pluginName = entry.name;
            break;
        }
    }

    statusLabel_->setText( tr( "%1 installed and activated." ).arg( pluginName ) );
    QMessageBox::information(
        this, tr( "Plugin Installed" ),
        tr( "%1 has been installed and activated.\n\n"
            "You can now use it from the Plugins menu." )
            .arg( pluginName ) );

    // Refresh the table to reflect the new installed state
    populateTable();
}

bool PluginRepositoryDialog::extractAndInstall( const QString& archivePath,
                                                const QString& pluginId )
{
    // Determine the plugin target directory
    const auto dirs = PluginManager::defaultPluginDirectories();
    if ( dirs.isEmpty() ) {
        QMessageBox::warning( this, tr( "Install Error" ),
                              tr( "No plugin directory configured." ) );
        return false;
    }
    const auto pluginDir = QDir( dirs.first() ).filePath( pluginId );
    const auto backupDir = pluginDir + ".bak";

    // Update case: unload the current version before overwriting on disk
    if ( manager_.isLoaded( pluginId ) ) {
        LOG_INFO << "Unloading existing plugin " << pluginId << " for update";
        manager_.unloadPlugin( pluginId );
    }

    // Back up the existing plugin directory for rollback
    bool hasBackup = false;
    if ( QDir( pluginDir ).exists() ) {
        // Remove any stale backup from a previous failed attempt
        if ( QDir( backupDir ).exists() ) {
            QDir( backupDir ).removeRecursively();
        }
        hasBackup = QDir().rename( pluginDir, backupDir );
        if ( !hasBackup ) {
            LOG_WARNING << "Could not create backup of " << pluginDir;
        }
    }

    // Extract the archive
    QString extractError;
    if ( !PluginRepository::extractPluginArchive( archivePath, pluginDir, &extractError ) ) {
        // Rollback: restore the backed-up directory
        if ( hasBackup ) {
            QDir( pluginDir ).removeRecursively();
            QDir().rename( backupDir, pluginDir );
            LOG_INFO << "Rolled back plugin " << pluginId << " to previous version";
        }

        statusLabel_->setText( tr( "Installation failed." ) );
        QMessageBox::warning( this, tr( "Install Error" ),
                              tr( "Failed to extract plugin archive:\n%1" )
                                  .arg( extractError ) );
        return false;
    }

    // Clean up: remove backup and archive
    if ( hasBackup ) {
        QDir( backupDir ).removeRecursively();
    }
    QFile::remove( archivePath );

    // Re-discover so PluginManager sees the new plugin on disk
    manager_.discoverPlugins();

    // Load the plugin
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

    // Add the plugin to the enabled list and persist
    auto& config = Configuration::get();
    auto enabled = config.enabledPlugins();
    if ( !enabled.contains( pluginId ) ) {
        enabled.append( pluginId );
        config.setEnabledPlugins( enabled );
        config.save();
    }

    return true;
}

void PluginRepositoryDialog::onDownloadError( const QString& message )
{
    progressBar_->setVisible( false );
    statusLabel_->setText( tr( "Download failed: %1" ).arg( message ) );
    QMessageBox::warning( this, tr( "Download Error" ), message );
}

void PluginRepositoryDialog::populateTable()
{
    const auto& entries = repository_.entries();
    table_->setRowCount( static_cast<int>( entries.size() ) );

    // Build a set of discovered plugin IDs for installed-state detection
    QSet<QString> discoveredIds;
    for ( const auto& meta : manager_.discoveredPlugins() ) {
        discoveredIds.insert( meta.id() );
    }

    for ( int row = 0; row < static_cast<int>( entries.size() ); ++row ) {
        const auto& e = entries[ static_cast<size_t>( row ) ];

        table_->setItem( row, 0, new QTableWidgetItem( e.name ) );
        table_->setItem( row, 1, new QTableWidgetItem( e.version ) );
        table_->setItem( row, 2, new QTableWidgetItem( e.author ) );
        table_->setItem( row, 3, new QTableWidgetItem( e.description ) );

        // Show "Update" if the plugin exists on disk, "Install" otherwise
        const bool installed = discoveredIds.contains( e.id );
        auto* button = new QPushButton( installed ? tr( "Update" ) : tr( "Install" ), this );
        button->setProperty( "pluginId", e.id );
        connect( button, &QPushButton::clicked, this,
                 &PluginRepositoryDialog::onInstallClicked );
        table_->setCellWidget( row, 4, button );
    }

    table_->resizeColumnsToContents();
}

} // namespace logsquirl::plugins
