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
#include "plaintext.h"
#include "plugincatalog.h"
#include "pluginhost.h"
#include "recentfiles.h"
#include "releasepages.h"
#include "starrequest.h"
#include "theme.h"

#include <QAction>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStringList>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

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

/// The targets of the footer's links that the main window acts on: they are
/// handled in code, never opened as URLs.
const QString kReportIssueLink = QStringLiteral( "logsquirl:report-issue" );
const QString kDocumentationLink = QStringLiteral( "logsquirl:documentation" );

/// The image a Theme's CloseIcon Token names: the path in its "url(...)".
QString closeIconPath( const Theme& theme )
{
    auto value = theme.value( StyleToken::CloseIcon ).trimmed();
    if ( value.startsWith( QStringLiteral( "url(" ) ) && value.endsWith( u')' ) ) {
        value = value.mid( 4, value.size() - 5 ).trimmed();
    }
    return value;
}

/// A rich-text link drawn in color, not in the palette's link color.
QString coloredLink( const QString& target, const QString& color, const QString& text )
{
    return QStringLiteral( "<a href=\"%1\" style=\"color:%2\">%3</a>" )
        .arg( target, color, text.toHtmlEscaped() );
}

/// The name of the command of a menu entry with this text: without its
/// mnemonic, also one written after it as a Chinese entry does ("(&O)"), and
/// without the
/// dots of an entry that opens a dialog.
QString commandName( QString text )
{
    static const QRegularExpression mnemonicAfter( QStringLiteral( "\\s*\\(&[^&]\\)" ) );
    text.remove( mnemonicAfter );
    // A doubled '&' is the character itself.
    text.replace( QStringLiteral( "&&" ), QStringLiteral( "\x01" ) );
    text.remove( u'&' );
    text.replace( u'\x01', u'&' );
    text = text.trimmed();
    if ( text.endsWith( QStringLiteral( "..." ) ) ) {
        text.chop( 3 );
    }
    else if ( text.endsWith( u'…' ) ) {
        text.chop( 1 );
    }
    return text.trimmed();
}

/// Gives label the Theme's secondary text color.
void markAsSecondaryText( QLabel* label )
{
    label->setProperty( "secondaryText", true );
}

/// A card: a titled frame in the Theme's surface color, styled by the
/// Theme's stylesheet (QFrame#dashboardCard). Its entries go into the
/// returned layout, below the title; the title label, still without text,
/// is added to titles.
QVBoxLayout* addCard( QWidget* parent, QVBoxLayout* column, QList<QLabel*>& titles )
{
    auto* card = new QFrame( parent );
    card->setObjectName( QStringLiteral( "dashboardCard" ) );
    card->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Maximum );
    auto* cardLayout = new QVBoxLayout( card );
    cardLayout->setContentsMargins( 16, 12, 16, 12 );
    cardLayout->setSpacing( 6 );

    auto* heading = new QLabel( card );
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
    btn->setToolTip( logsquirl::plaintext::toolTip( fullPath ) );
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
    // The plugin status colors, the footer's link color and the star
    // request's link color and close icon come from Tokens.
    Theme::whenApplied( this, [ this ] {
        refresh();
        updateFooterLinks();
        updateStarRequest();
    } );
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

    openFileButton_ = new QPushButton( columnWidget );
    openFileButton_->setMinimumWidth( 110 );
    // Drawn in the accent color by the Theme's stylesheet.
    openFileButton_->setProperty( "primaryAction", true );
    connect( openFileButton_, &QPushButton::clicked, this,
             &WelcomeDashboard::openFileDialogRequested );
    actionsLayout->addWidget( openFileButton_ );

    loadSessionButton_ = new QPushButton( columnWidget );
    loadSessionButton_->setMinimumWidth( 110 );
    connect( loadSessionButton_, &QPushButton::clicked, this,
             &WelcomeDashboard::loadSessionRequested );
    actionsLayout->addWidget( loadSessionButton_ );

    column->addLayout( actionsLayout );
    column->addSpacing( 6 );

    // ---- Cards: Recent Files, Favorites, Plugins ----
    // Their titles are written by retranslate().
    recentFilesLayout_ = addCard( columnWidget, column, titleLabels_ );
    recentFilesTitle_ = titleLabels_.back();
    favoritesLayout_ = addCard( columnWidget, column, titleLabels_ );
    favoritesTitle_ = titleLabels_.back();
    pluginStatusLayout_ = addCard( columnWidget, column, titleLabels_ );
    pluginsTitle_ = titleLabels_.back();
    pluginStatusLayout_->setSpacing( 2 );

    column->addSpacing( 6 );

    // ---- Keyboard shortcut hints: written from the window's actions ----
    shortcutHints_ = new QLabel( columnWidget );
    shortcutHints_->setObjectName( QStringLiteral( "dashboardShortcutHints" ) );
    markAsSecondaryText( shortcutHints_ );
    shortcutHints_->setTextFormat( Qt::PlainText );
    shortcutHints_->setAlignment( Qt::AlignCenter );
    shortcutHints_->setWordWrap( true );
    shortcutHints_->hide();
    column->addWidget( shortcutHints_ );

    // ---- Drop hint ----
    dropHint_ = new QLabel( columnWidget );
    markAsSecondaryText( dropHint_ );
    auto hintFont = dropHint_->font();
    hintFont.setItalic( true );
    dropHint_->setFont( hintFont );
    dropHint_->setAlignment( Qt::AlignCenter );
    column->addWidget( dropHint_ );

    column->addStretch();

    // ---- Footer: at the bottom edge, below the scrolled content ----
    auto* footer = new QWidget( this );
    footer->setObjectName( QStringLiteral( "dashboardFooter" ) );
    footerLayout_ = new QVBoxLayout( footer );
    footerLayout_->setContentsMargins( 24, 6, 24, 10 );
    footerLayout_->setSpacing( 4 );

    footerLinks_ = new QLabel( footer );
    footerLinks_->setObjectName( QStringLiteral( "dashboardFooterLinks" ) );
    markAsSecondaryText( footerLinks_ );
    footerLinks_->setAlignment( Qt::AlignCenter );
    footerLinks_->setTextFormat( Qt::RichText );
    footerLinks_->setTextInteractionFlags( Qt::LinksAccessibleByMouse
                                           | Qt::LinksAccessibleByKeyboard );
    // Handled here, so Report an Issue and Documentation reach the main window.
    connect( footerLinks_, &QLabel::linkActivated, this, &WelcomeDashboard::activateFooterLink );
    footerLayout_->addWidget( footerLinks_ );
    outerLayout->addWidget( footer );
    buildStarRequest();

    retranslate();
    updateTitleFonts();
}

void WelcomeDashboard::retranslate()
{
    openFileButton_->setText( tr( "Open File" ) );
    loadSessionButton_->setText( tr( "Load Session" ) );
    recentFilesTitle_->setText( tr( "Recent Files" ) );
    favoritesTitle_->setText( tr( "Favorites" ) );
    pluginsTitle_->setText( tr( "Plugins" ) );
    dropHint_->setText( tr( "Drop log files here to open them" ) );
    // The window retranslates the actions, which rewrites the line; the
    // names of keys are written in the new language here too.
    updateShortcutHints();
    // The rich-text lines are written anew, in the Theme's colors.
    updateFooterLinks();
    updateStarRequest();
}

void WelcomeDashboard::updateFooterLinks()
{
    // A link is drawn in the link color of the palette unless it names its
    // own, so each names the secondary text color of the rest of the line.
    const auto color = Theme::active().color( ColorToken::SecondaryText ).name( QColor::HexRgb );
    const auto link = [ &color ]( const QString& target, const QString& text ) {
        return coloredLink( target, color, text );
    };
    const QStringList parts = {
        tr( "LogSquirl is open source" ).toHtmlEscaped(),
        link( QString( logsquirl::versioncheck::ProjectPageUrl ), QStringLiteral( "GitHub" ) ),
        link( kReportIssueLink, tr( "Report an Issue" ) ),
        link( kDocumentationLink, tr( "Documentation" ) ),
    };
    footerLinks_->setText( parts.join( QStringLiteral( " \u00B7 " ) ) );
}

void WelcomeDashboard::activateFooterLink( const QString& target )
{
    if ( target == kReportIssueLink ) {
        Q_EMIT reportIssueRequested();
    }
    else if ( target == kDocumentationLink ) {
        Q_EMIT documentationRequested();
    }
    else if ( target == QString( logsquirl::versioncheck::ProjectPageUrl ) ) {
        QDesktopServices::openUrl( QUrl( target ) );
    }
}

void WelcomeDashboard::buildStarRequest()
{
    auto* footer = footerLayout_->parentWidget();
    starRequest_ = new QWidget( footer );
    starRequest_->setObjectName( QStringLiteral( "dashboardStarRequest" ) );
    auto* line = new QHBoxLayout( starRequest_ );
    line->setContentsMargins( 0, 0, 0, 0 );
    line->setSpacing( 6 );
    line->addStretch();

    starRequestText_ = new QLabel( starRequest_ );
    starRequestText_->setObjectName( QStringLiteral( "dashboardStarRequestText" ) );
    markAsSecondaryText( starRequestText_ );
    starRequestText_->setAlignment( Qt::AlignCenter );
    starRequestText_->setTextFormat( Qt::RichText );
    starRequestText_->setTextInteractionFlags( Qt::LinksAccessibleByMouse
                                               | Qt::LinksAccessibleByKeyboard );
    connect( starRequestText_, &QLabel::linkActivated, this, [ this ]( const QString& target ) {
        if ( target == QString( logsquirl::versioncheck::ProjectPageUrl ) ) {
            QDesktopServices::openUrl( QUrl( target ) );
        }
        dismissStarRequest();
    } );
    line->addWidget( starRequestText_ );

    starRequestClose_ = new QToolButton( starRequest_ );
    starRequestClose_->setObjectName( QStringLiteral( "dashboardStarRequestClose" ) );
    starRequestClose_->setAutoRaise( true );
    starRequestClose_->setCursor( Qt::PointingHandCursor );
    starRequestClose_->setIconSize( QSize( 10, 10 ) );
    connect( starRequestClose_, &QToolButton::clicked, this,
             &WelcomeDashboard::dismissStarRequest );
    line->addWidget( starRequestClose_ );
    line->addStretch();

    starRequest_->hide();
    footerLayout_->insertWidget( 0, starRequest_ );
}

void WelcomeDashboard::updateStarRequest()
{
    const auto& theme = Theme::active();
    const auto color = theme.color( ColorToken::SecondaryText ).name( QColor::HexRgb );
    starRequestText_->setText(
        coloredLink( QString( logsquirl::versioncheck::ProjectPageUrl ), color,
                     tr( "Find LogSquirl useful? A star on GitHub helps others find it." ) ) );
    starRequestClose_->setIcon( QIcon( closeIconPath( theme ) ) );
    const auto dismiss = tr( "Dismiss" );
    starRequestClose_->setToolTip( dismiss );
    starRequestClose_->setAccessibleName( dismiss );
}

void WelcomeDashboard::refreshStarRequest()
{
    starRequest_->setVisible( StarRequest::dueOn( today_() ) );
}

void WelcomeDashboard::dismissStarRequest()
{
    StarRequest::dismissForGood();
    starRequest_->hide();
}

void WelcomeDashboard::setToday( std::function<QDate()> today )
{
    today_ = std::move( today );
}

void WelcomeDashboard::setShortcutHints( const QList<QAction*>& actions )
{
    for ( const auto& action : std::as_const( shortcutHintActions_ ) ) {
        if ( action ) {
            disconnect( action, nullptr, this, nullptr );
        }
    }
    shortcutHintActions_.clear();
    for ( auto* action : actions ) {
        shortcutHintActions_.append( action );
        // Emitted when its keys or its text change, by Preferences or by a
        // change of language.
        connect( action, &QAction::changed, this, &WelcomeDashboard::updateShortcutHints );
    }
    updateShortcutHints();
}

void WelcomeDashboard::updateShortcutHints()
{
    QStringList hints;
    for ( const auto& action : std::as_const( shortcutHintActions_ ) ) {
        if ( !action || action->shortcut().isEmpty() ) {
            continue;
        }
        hints.append( action->shortcut().toString( QKeySequence::NativeText ) + u' '
                      + commandName( action->text() ) );
    }
    shortcutHints_->setText( hints.join( QStringLiteral( "  |  " ) ) );
    shortcutHints_->setVisible( !hints.isEmpty() );
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
    // Every text follows a changed language (#935); the lists' empty texts
    // are in the entries written anew.
    else if ( event->type() == QEvent::LanguageChange ) {
        retranslate();
        refreshRecentFiles();
        refreshFavorites();
        refreshPluginStatus();
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
    refreshStarRequest();
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
