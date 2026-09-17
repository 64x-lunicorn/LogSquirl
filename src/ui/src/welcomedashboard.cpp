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
#include "plugincatalog.h"
#include "pluginhost.h"
#include "recentfiles.h"
#include "theme.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
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

/// Width of the column the logo, actions and cards are laid out in.
constexpr int kColumnWidth = 560;

/// How much larger than the application font a card's title is.
constexpr double kTitleScale = 1.25;

/// Stylesheet for clickable file-link buttons (uses palette for theme awareness).
/// The text keeps the application font.
const QString kLinkButtonStyle
    = QStringLiteral( "QPushButton { color: palette(link); background: transparent; border: none;"
                      " text-align: left; padding: 3px 0px; }"
                      "QPushButton:hover { text-decoration: underline; }" );

/// Gives label the Theme's secondary text color.
void markAsSecondaryText( QLabel* label )
{
    label->setProperty( "secondaryText", true );
}

/// A card: a titled frame in the Theme's surface color, styled by the
/// Theme's stylesheet (QFrame#dashboardCard). Its entries go into the
/// returned layout, below the title; the title label is added to titles.
QVBoxLayout* addCard( const QString& title, QWidget* parent, QVBoxLayout* column,
                      QList<QLabel*>& titles )
{
    auto* card = new QFrame( parent );
    card->setObjectName( QStringLiteral( "dashboardCard" ) );
    card->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Maximum );
    auto* cardLayout = new QVBoxLayout( card );
    cardLayout->setContentsMargins( 16, 12, 16, 12 );
    cardLayout->setSpacing( 6 );

    auto* heading = new QLabel( title, card );
    heading->setObjectName( QStringLiteral( "dashboardCardTitle" ) );
    cardLayout->addWidget( heading );
    titles.push_back( heading );

    auto* entries = new QVBoxLayout();
    entries->setSpacing( 0 );
    cardLayout->addLayout( entries );

    column->addWidget( card );
    return entries;
}

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
    QObject::connect( btn, &QPushButton::clicked, dashboard, [ dashboard, fullPath ] {
        Q_EMIT dashboard->openFileRequested( fullPath );
    } );
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
    // The plugin status colors come from Tokens.
    Theme::whenApplied( this, [ this ] { refresh(); } );
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

    // One centered column: everything is laid out in its width.
    auto* rootLayout = new QHBoxLayout( content );
    rootLayout->setContentsMargins( 24, 30, 24, 30 );
    rootLayout->addStretch();
    auto* columnWidget = new QWidget( content );
    columnWidget->setMaximumWidth( kColumnWidth );
    columnWidget->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
    rootLayout->addWidget( columnWidget, 1000 );
    rootLayout->addStretch();

    auto* column = new QVBoxLayout( columnWidget );
    column->setContentsMargins( 0, 0, 0, 0 );
    column->setSpacing( 12 );

    // ---- Logo ----
    logoLabel_ = new QLabel( columnWidget );
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
        titleLabels_.push_back( logoLabel_ );
    }
    column->addWidget( logoLabel_ );

    // ---- Version ----
    auto* versionLabel
        = new QLabel( QStringLiteral( "v%1" ).arg( logsquirlVersion() ), columnWidget );
    versionLabel->setAlignment( Qt::AlignCenter );
    markAsSecondaryText( versionLabel );
    column->addWidget( versionLabel );

    // ---- Quick Actions ----
    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setAlignment( Qt::AlignCenter );
    actionsLayout->setSpacing( 12 );

    auto* openBtn = new QPushButton( tr( "Open File" ), columnWidget );
    openBtn->setMinimumWidth( 110 );
    // Drawn in the accent color by the Theme's stylesheet.
    openBtn->setProperty( "primaryAction", true );
    connect( openBtn, &QPushButton::clicked, this, &WelcomeDashboard::openFileDialogRequested );
    actionsLayout->addWidget( openBtn );

    auto* sessionBtn = new QPushButton( tr( "Load Session" ), columnWidget );
    sessionBtn->setMinimumWidth( 110 );
    connect( sessionBtn, &QPushButton::clicked, this, &WelcomeDashboard::loadSessionRequested );
    actionsLayout->addWidget( sessionBtn );

    column->addLayout( actionsLayout );
    column->addSpacing( 6 );

    // ---- Cards: Recent Files, Favorites, Plugins ----
    recentFilesLayout_ = addCard( tr( "Recent Files" ), columnWidget, column, titleLabels_ );
    favoritesLayout_ = addCard( tr( "Favorites" ), columnWidget, column, titleLabels_ );
    pluginStatusLayout_ = addCard( tr( "Plugins" ), columnWidget, column, titleLabels_ );
    pluginStatusLayout_->setSpacing( 2 );

    column->addSpacing( 6 );

    // ---- Keyboard shortcut hints ----
    auto* shortcutsLabel = new QLabel(
        QStringLiteral( "Ctrl+O Open File  |  Ctrl+W Close Tab  |  Ctrl+F Find  |  F5 Reload" ),
        columnWidget );
    markAsSecondaryText( shortcutsLabel );
    shortcutsLabel->setAlignment( Qt::AlignCenter );
    shortcutsLabel->setWordWrap( true );
    column->addWidget( shortcutsLabel );

    // ---- Drop hint ----
    auto* dropHint = new QLabel( tr( "Drop log files here to open them" ), columnWidget );
    markAsSecondaryText( dropHint );
    auto hintFont = dropHint->font();
    hintFont.setItalic( true );
    dropHint->setFont( hintFont );
    dropHint->setAlignment( Qt::AlignCenter );
    column->addWidget( dropHint );

    column->addStretch();

    updateTitleFonts();
}

void WelcomeDashboard::updateTitleFonts()
{
    auto titleFont = font();
    titleFont.setBold( true );
    if ( titleFont.pointSizeF() > 0 ) {
        titleFont.setPointSizeF( titleFont.pointSizeF() * kTitleScale );
    }
    else {
        titleFont.setPixelSize( qRound( titleFont.pixelSize() * kTitleScale ) );
    }
    for ( auto* title : titleLabels_ ) {
        title->setFont( titleFont );
    }
}

void WelcomeDashboard::changeEvent( QEvent* event )
{
    QWidget::changeEvent( event );
    // The titles follow a changed application font.
    if ( event->type() == QEvent::FontChange ) {
        updateTitleFonts();
    }
}

void WelcomeDashboard::setPlugins( const logsquirl::plugins::PluginCatalog* catalog,
                                   const logsquirl::plugins::PluginHost* host )
{
    pluginCatalog_ = catalog;
    pluginHost_ = host;
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
        markAsSecondaryText( empty );
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
        markAsSecondaryText( empty );
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

    if ( !pluginCatalog_ || !pluginHost_ ) {
        auto* none = new QLabel( tr( "No plugins available" ), this );
        markAsSecondaryText( none );
        pluginStatusLayout_->addWidget( none );
        return;
    }

    const auto& discovered = pluginCatalog_->discoveredPlugins();
    const auto loaded = pluginHost_->loadedPluginIds();

    if ( discovered.empty() ) {
        auto* none = new QLabel( tr( "No plugins installed" ), this );
        markAsSecondaryText( none );
        pluginStatusLayout_->addWidget( none );
        return;
    }

    const auto& theme = Theme::active();
    const auto versionColor = theme.color( ColorToken::SecondaryText ).name( QColor::HexRgb );
    for ( const auto& plugin : discovered ) {
        const bool isLoaded = loaded.contains( plugin.id() );
        const QString statusDot = isLoaded ? QStringLiteral( "\u25CF " )  // ● filled circle
                                           : QStringLiteral( "\u25CB " ); // ○ empty circle
        const QString color
            = theme.color( isLoaded ? ColorToken::StatusOk : ColorToken::StatusInactive )
                  .name( QColor::HexRgb );
        // The plugin name and version come from the plugin's plugin.json on
        // disk and could contain HTML special characters.  Escape them before
        // they end up in the rich-text label.
        auto* row = new QLabel(
            QStringLiteral( "<span style='color:%1'>%2</span>%3 <span style='color:%4'>v%5</span>" )
                .arg( color, statusDot, plugin.name().toHtmlEscaped(), versionColor,
                      plugin.version().toHtmlEscaped() ),
            this );
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
