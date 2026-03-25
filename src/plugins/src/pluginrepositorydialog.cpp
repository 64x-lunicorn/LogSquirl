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

#include "log.h"

#include <QDialogButtonBox>
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
            statusLabel_->setText( tr( "Downloading %1..." ).arg( entry.name ) );
            button->setEnabled( false );

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
    statusLabel_->setText( tr( "Downloaded to %1 — restart to activate." ).arg( archivePath ) );

    QMessageBox::information( this, tr( "Plugin Downloaded" ),
                              tr( "Plugin archive saved to:\n%1\n\n"
                                  "Extract the archive to the plugin directory and "
                                  "restart LogSquirl to activate it." )
                                  .arg( archivePath ) );
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

    for ( int row = 0; row < static_cast<int>( entries.size() ); ++row ) {
        const auto& e = entries[ static_cast<size_t>( row ) ];

        table_->setItem( row, 0, new QTableWidgetItem( e.name ) );
        table_->setItem( row, 1, new QTableWidgetItem( e.version ) );
        table_->setItem( row, 2, new QTableWidgetItem( e.author ) );
        table_->setItem( row, 3, new QTableWidgetItem( e.description ) );

        // Install/update button
        const bool installed = manager_.isLoaded( e.id );
        auto* button = new QPushButton( installed ? tr( "Update" ) : tr( "Install" ), this );
        button->setProperty( "pluginId", e.id );
        connect( button, &QPushButton::clicked, this,
                 &PluginRepositoryDialog::onInstallClicked );
        table_->setCellWidget( row, 4, button );
    }

    table_->resizeColumnsToContents();
}

} // namespace logsquirl::plugins
