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

#include "welcomedashboard.h"

#include "displayfilepath.h"
#include "favoritefiles.h"
#include "logsquirl_version.h"
#include "pluginmanager.h"
#include "recentfiles.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <QPixmap>

namespace {

/// Maximum number of recent/favorite entries shown on the dashboard.
constexpr int kMaxListEntries = 8;

/// Stylesheet for clickable file-link buttons (uses palette for theme awareness).
const QString kLinkButtonStyle = QStringLiteral(
    "QPushButton { color: palette(link); border: none; text-align: left;"
    " padding: 3px 0px; font-size: 12px; }"
    "QPushButton:hover { text-decoration: underline; }" );

/// Stylesheet for section headings.
const QString kSectionHeadingStyle = QStringLiteral(
    "QLabel { font-weight: bold; font-size: 13px; padding-top: 12px; }" );

/// Stylesheet for shortcut hint text (uses palette for theme awareness).
const QString kHintStyle = QStringLiteral(
    "QLabel { color: palette(dark); font-size: 11px; padding: 2px 0px; }" );

/// Create a clickable QPushButton styled as a link.
/// Clicking emits the dashboard's openFileRequested signal.
QPushButton* createFileLink( const QString& displayText, const QString& fullPath,
                             WelcomeDashboard* dashboard )
{
    auto* btn = new QPushButton( displayText, dashboard );
    btn->setStyleSheet( kLinkButtonStyle );
    btn->setCursor( Qt::PointingHandCursor );
    btn->setToolTip( fullPath );
    btn->setFlat( true );
    QObject::connect( btn, &QPushButton::clicked, dashboard,
                      [ dashboard, fullPath ] { Q_EMIT dashboard->openFileRequested( fullPath ); } );
    return btn;
}

/// Remove all child widgets and items from a layout.
void clearLayout( QLayout* layout )
{
    if ( !layout ) {
        return;
    }
    while ( auto* item = layout->takeAt( 0 ) ) {
        if ( auto* widget = item->widget() ) {
            widget->deleteLater();
        }
        delete item;
    }
}

} // namespace

WelcomeDashboard::WelcomeDashboard( QWidget* parent )
    : QWidget( parent )
{
    setAcceptDrops( true );
    buildUi();
}

void WelcomeDashboard::buildUi()
{
    // ---- Root scroll area so the dashboard works on small screens ----
    auto* outerLayout = new QVBoxLayout( this );
    outerLayout->setContentsMargins( 0, 0, 0, 0 );

    auto* scrollArea = new QScrollArea( this );
    scrollArea->setWidgetResizable( true );
    scrollArea->setFrameShape( QFrame::NoFrame );
    outerLayout->addWidget( scrollArea );

    auto* content = new QWidget( scrollArea );
    scrollArea->setWidget( content );

    auto* rootLayout = new QVBoxLayout( content );
    rootLayout->setAlignment( Qt::AlignHCenter | Qt::AlignTop );
    rootLayout->setContentsMargins( 40, 30, 40, 30 );
    rootLayout->setSpacing( 6 );

    // ---- Logo ----
    logoLabel_ = new QLabel( content );
    logoLabel_->setAlignment( Qt::AlignCenter );

    // Use the high-res app logo for the dashboard
    const QPixmap icon( ":/images/logsquirl-logo.png" );
    if ( !icon.isNull() ) {
        constexpr int kLogoSize = 128;
        logoLabel_->setPixmap(
            icon.scaled( kLogoSize, kLogoSize, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
    }
    else {
        logoLabel_->setText( QStringLiteral( "LogSquirl" ) );
        logoLabel_->setStyleSheet( QStringLiteral( "font-size: 28px; font-weight: bold;" ) );
    }
    rootLayout->addWidget( logoLabel_ );

    // ---- Version ----
    auto* versionLabel = new QLabel(
        QStringLiteral( "v%1" ).arg( logsquirlVersion() ), content );
    versionLabel->setAlignment( Qt::AlignCenter );
    versionLabel->setStyleSheet( QStringLiteral( "color: palette(dark); font-size: 12px;" ) );
    rootLayout->addWidget( versionLabel );

    rootLayout->addSpacing( 10 );

    // ---- Quick Actions ----
    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setAlignment( Qt::AlignCenter );
    actionsLayout->setSpacing( 12 );

    auto* openBtn = new QPushButton( tr( "Open File" ), content );
    openBtn->setMinimumWidth( 110 );
    connect( openBtn, &QPushButton::clicked, this,
             &WelcomeDashboard::openFileDialogRequested );
    actionsLayout->addWidget( openBtn );

    auto* sessionBtn = new QPushButton( tr( "Load Session" ), content );
    sessionBtn->setMinimumWidth( 110 );
    connect( sessionBtn, &QPushButton::clicked, this,
             &WelcomeDashboard::loadSessionRequested );
    actionsLayout->addWidget( sessionBtn );

    rootLayout->addLayout( actionsLayout );

    rootLayout->addSpacing( 6 );

    // ---- Two-column area: Recent Files | Favorites ----
    auto* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing( 40 );
    columnsLayout->setAlignment( Qt::AlignTop | Qt::AlignHCenter );

    // -- Recent Files column --
    auto* recentColumn = new QVBoxLayout();
    recentColumn->setSpacing( 2 );
    auto* recentHeading = new QLabel( tr( "Recent Files" ), content );
    recentHeading->setStyleSheet( kSectionHeadingStyle );
    recentColumn->addWidget( recentHeading );

    recentFilesLayout_ = new QVBoxLayout();
    recentFilesLayout_->setSpacing( 0 );
    recentColumn->addLayout( recentFilesLayout_ );
    recentColumn->addStretch();
    columnsLayout->addLayout( recentColumn );

    // -- Favorites column --
    auto* favColumn = new QVBoxLayout();
    favColumn->setSpacing( 2 );
    auto* favHeading = new QLabel( tr( "Favorites" ), content );
    favHeading->setStyleSheet( kSectionHeadingStyle );
    favColumn->addWidget( favHeading );

    favoritesLayout_ = new QVBoxLayout();
    favoritesLayout_->setSpacing( 0 );
    favColumn->addLayout( favoritesLayout_ );
    favColumn->addStretch();
    columnsLayout->addLayout( favColumn );

    rootLayout->addLayout( columnsLayout );

    rootLayout->addSpacing( 10 );

    // ---- Plugin Status ----
    auto* pluginHeading = new QLabel( tr( "Plugins" ), content );
    pluginHeading->setStyleSheet( kSectionHeadingStyle );
    pluginHeading->setAlignment( Qt::AlignCenter );
    rootLayout->addWidget( pluginHeading );

    pluginStatusLayout_ = new QVBoxLayout();
    pluginStatusLayout_->setSpacing( 2 );
    pluginStatusLayout_->setAlignment( Qt::AlignHCenter );
    rootLayout->addLayout( pluginStatusLayout_ );

    rootLayout->addSpacing( 16 );

    // ---- Keyboard shortcut hints ----
    auto* shortcutsLabel = new QLabel(
        QStringLiteral( "Ctrl+O Open File  |  Ctrl+W Close Tab  |  Ctrl+F Find  |  F5 Reload" ),
        content );
    shortcutsLabel->setStyleSheet( kHintStyle );
    shortcutsLabel->setAlignment( Qt::AlignCenter );
    rootLayout->addWidget( shortcutsLabel );

    // ---- Drop hint ----
    auto* dropHint = new QLabel( tr( "Drop log files here to open them" ), content );
    dropHint->setStyleSheet(
        QStringLiteral( "color: palette(dark); font-size: 11px; font-style: italic; padding-top: 8px;" ) );
    dropHint->setAlignment( Qt::AlignCenter );
    rootLayout->addWidget( dropHint );

    rootLayout->addStretch();
}

void WelcomeDashboard::setPluginManager( logsquirl::plugins::PluginManager* pm )
{
    pluginManager_ = pm;
}

void WelcomeDashboard::refresh()
{
    refreshRecentFiles();
    refreshFavorites();
    refreshPluginStatus();
}

void WelcomeDashboard::refreshRecentFiles()
{
    clearLayout( recentFilesLayout_ );

    const auto& recentFiles = RecentFiles::getSynced();
    const auto files = recentFiles.recentFiles();

    if ( files.isEmpty() ) {
        auto* empty = new QLabel( tr( "No recent files" ), this );
        empty->setStyleSheet( kHintStyle );
        recentFilesLayout_->addWidget( empty );
        return;
    }

    int count = 0;
    for ( const auto& filePath : files ) {
        if ( count >= kMaxListEntries ) {
            break;
        }
        const auto displayName = QFileInfo( filePath ).fileName();
        auto* link = createFileLink( displayName, filePath, this );
        recentFilesLayout_->addWidget( link );
        ++count;
    }
}

void WelcomeDashboard::refreshFavorites()
{
    clearLayout( favoritesLayout_ );

    const auto& favoriteFiles = FavoriteFiles::getSynced();
    const auto files = favoriteFiles.favorites();

    if ( files.empty() ) {
        auto* empty = new QLabel( tr( "No favorites" ), this );
        empty->setStyleSheet( kHintStyle );
        favoritesLayout_->addWidget( empty );
        return;
    }

    int count = 0;
    for ( const auto& fav : files ) {
        if ( count >= kMaxListEntries ) {
            break;
        }
        auto* link = createFileLink( fav.displayName(), fav.fullPath(), this );
        favoritesLayout_->addWidget( link );
        ++count;
    }
}

void WelcomeDashboard::refreshPluginStatus()
{
    clearLayout( pluginStatusLayout_ );

    if ( !pluginManager_ ) {
        auto* none = new QLabel( tr( "No plugin manager available" ), this );
        none->setStyleSheet( kHintStyle );
        none->setAlignment( Qt::AlignCenter );
        pluginStatusLayout_->addWidget( none );
        return;
    }

    const auto& discovered = pluginManager_->discoveredPlugins();
    const auto loaded = pluginManager_->loadedPluginIds();

    if ( discovered.empty() ) {
        auto* none = new QLabel( tr( "No plugins installed" ), this );
        none->setStyleSheet( kHintStyle );
        none->setAlignment( Qt::AlignCenter );
        pluginStatusLayout_->addWidget( none );
        return;
    }

    for ( const auto& plugin : discovered ) {
        const bool isLoaded = loaded.contains( plugin.id() );
        const QString statusDot = isLoaded ? QStringLiteral( "\u25CF " ) // ● filled circle
                                           : QStringLiteral( "\u25CB " ); // ○ empty circle
        const QString color = isLoaded ? QStringLiteral( "#4CAF50" )  // green
                                       : QStringLiteral( "#808080" ); // gray
        // The plugin name and version come from the plugin's plugin.json on
        // disk and could contain HTML special characters.  Escape them before
        // they end up in the rich-text label.
        auto* row = new QLabel(
            QStringLiteral( "<span style='color:%1'>%2</span>%3 <span style='color:#808080'>v%4</span>" )
                .arg( color, statusDot, plugin.name().toHtmlEscaped(),
                      plugin.version().toHtmlEscaped() ),
            this );
        row->setAlignment( Qt::AlignCenter );
        row->setTextFormat( Qt::RichText );
        row->setTextInteractionFlags( Qt::NoTextInteraction );
        pluginStatusLayout_->addWidget( row );
    }
}

void WelcomeDashboard::dragEnterEvent( QDragEnterEvent* event )
{
    if ( event->mimeData()->hasUrls() ) {
        event->acceptProposedAction();
    }
}

void WelcomeDashboard::dropEvent( QDropEvent* event )
{
    const auto urls = event->mimeData()->urls();
    for ( const auto& url : urls ) {
        if ( url.isLocalFile() ) {
            Q_EMIT openFileRequested( url.toLocalFile() );
        }
    }
}
