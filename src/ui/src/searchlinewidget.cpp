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

#include "searchlinewidget.h"

#include <limits>

#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QShortcut>
#include <QStringListModel>
#include <QToolButton>

#include "iconloader.h"
#include "infoline.h"
#include "theme.h"

// The texts of the row are translated in the context of the Crawler Widget,
// which showed them before the Search Line was a widget of its own, so that
// the translations keep matching them (#638). Each is spelled out in its
// QCoreApplication::translate() call, where lupdate finds it.

namespace {

// The Search line keeps room for this many characters, whatever else is in
// its row (#261).
constexpr int SearchLineMinimumCharacters = 20;

QToolButton* flagButton( const QString& toolTip, const QString& accessibleName, QWidget* parent )
{
    auto* button = new QToolButton( parent );
    button->setToolTip( toolTip );
    button->setAccessibleName( accessibleName );
    button->setCheckable( true );
    button->setFocusPolicy( Qt::TabFocus );
    button->setContentsMargins( 2, 2, 2, 2 );
    return button;
}

} // namespace

SearchLineWidget::SearchLineWidget( const QuickFindPolicy& startingState,
                                    const QStringList& history, QWidget* parent )
    : QWidget( parent )
    , searchLine_( startingState )
{
    infoLine_ = new InfoLine();
    infoLine_->setParent( this );
    infoLine_->setFrameStyle( QFrame::StyledPanel );
    infoLine_->setFrameShadow( QFrame::Sunken );
    infoLine_->setLineWidth( 1 );
    // The match count gives way to the Search line when the row runs out of
    // width: it elides rather than keep the width of its whole text (#261).
    infoLine_->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Minimum );
    infoLine_->setElidesText( true );
    auto infoLineSizePolicy = infoLine_->sizePolicy();
    infoLineSizePolicy.setRetainSizeWhenHidden( false );
    infoLine_->setSizePolicy( infoLineSizePolicy );
    infoLine_->setContentsMargins( 2, 2, 2, 2 );
    infoLine_->hide();
    defaultPalette_ = palette();

    matchCaseButton_
        = flagButton( QCoreApplication::translate( "CrawlerWidget", "Match case" ),
                      QCoreApplication::translate( "CrawlerWidget", "Match case" ), this );
    useRegexpButton_
        = flagButton( QCoreApplication::translate( "CrawlerWidget", "Use regex" ),
                      QCoreApplication::translate( "CrawlerWidget", "Use regex" ), this );
    inverseButton_
        = flagButton( QCoreApplication::translate( "CrawlerWidget", "Inverse match" ),
                      QCoreApplication::translate( "CrawlerWidget", "Inverse match" ), this );
    booleanButton_
        = flagButton( QCoreApplication::translate( "CrawlerWidget",
                                                   "Enable regular expression logical combining" ),
                      QCoreApplication::translate( "CrawlerWidget", "Boolean combining" ), this );
    autoRefreshButton_
        = flagButton( QCoreApplication::translate( "CrawlerWidget", "Auto-refresh" ),
                      QCoreApplication::translate( "CrawlerWidget", "Auto-refresh" ), this );

    // The pattern, with the search history offered while typing.
    completer_ = new QCompleter( history, this );
    patternEdit_ = new QComboBox( this );
    patternEdit_->setEditable( true );
    patternEdit_->setCompleter( completer_ );
    patternEdit_->addItems( history );
    patternEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
    patternEdit_->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    // Whatever else is in the row, the Search line keeps room for about 20
    // characters of the font it shows (#261).
    patternEdit_->setMinimumContentsLength( SearchLineMinimumCharacters );
    patternEdit_->lineEdit()->setMaxLength( std::numeric_limits<int>::max() / 1024 );
    patternEdit_->setContentsMargins( 2, 2, 2, 2 );
    patternEdit_->setAccessibleName(
        QCoreApplication::translate( "CrawlerWidget", "Search pattern" ) );
    patternEdit_->setContextMenuPolicy( Qt::CustomContextMenu );

    // Keyboard tab order for the search bar and filter buttons
    setTabOrder( patternEdit_, matchCaseButton_ );
    setTabOrder( matchCaseButton_, useRegexpButton_ );
    setTabOrder( useRegexpButton_, inverseButton_ );
    setTabOrder( inverseButton_, booleanButton_ );
    setTabOrder( booleanButton_, autoRefreshButton_ );

    setFocusProxy( patternEdit_ );

    clearButton_ = new QToolButton( this );
    clearButton_->setText( QCoreApplication::translate( "CrawlerWidget", "Clear search text" ) );
    clearButton_->setAutoRaise( true );
    clearButton_->setContentsMargins( 2, 2, 2, 2 );

    searchButton_ = new QToolButton( this );
    searchButton_->setText( QCoreApplication::translate( "CrawlerWidget", "Search" ) );
    searchButton_->setAutoRaise( true );
    searchButton_->setContentsMargins( 2, 2, 2, 2 );

    keepResultsButton_ = new QToolButton( this );
    keepResultsButton_->setText( QCoreApplication::translate( "CrawlerWidget", "Keep Results" ) );
    keepResultsButton_->setToolTip( QCoreApplication::translate(
        "CrawlerWidget", "Keep these results and show subsequent results in a new window" ) );
    keepResultsButton_->setCheckable( true );
    keepResultsButton_->setContentsMargins( 2, 2, 2, 2 );

    stopButton_ = new QToolButton( this );
    stopButton_->setAutoRaise( true );
    stopButton_->setEnabled( false );
    stopButton_->setVisible( false );
    stopButton_->setContentsMargins( 2, 2, 2, 2 );

    auto* layout = new QHBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->addWidget( matchCaseButton_ );
    layout->addWidget( useRegexpButton_ );
    layout->addWidget( inverseButton_ );
    layout->addWidget( booleanButton_ );
    layout->addWidget( autoRefreshButton_ );
    layout->addWidget( patternEdit_ );
    layout->addWidget( clearButton_ );
    layout->addWidget( searchButton_ );
    layout->addWidget( keepResultsButton_ );
    layout->addWidget( stopButton_ );
    layout->addWidget( infoLine_ );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );

    // The buttons start as the QuickFind Policy says, which the Search Line
    // was built with; nothing changed, nothing to tell.
    showFlags( searchLine_.flags() );
    // The line starts with the latest Search of the history in it.
    searchLine_.setPattern( patternEdit_->currentText() );

    // The palette is read off the Theme's, and read again with every Theme
    // (ADR 0004).
    Theme::whenApplied( this, [ this ] {
        defaultPalette_ = palette();
        if ( !searchLine_.display().gauge ) {
            infoLine_->setPalette( searchLine_.display().isError ? errorPalette()
                                                                 : defaultPalette_ );
        }
    } );

    // A button the user sets reaches the Search Line; one set to the Search
    // Line's flags does not come back to it.
    for ( auto* button : { matchCaseButton_, useRegexpButton_, inverseButton_, booleanButton_,
                           autoRefreshButton_ } ) {
        connect( button, &QToolButton::toggled, this, [ this ] {
            if ( !showingFlags_ ) {
                takeFlagsFromButtons();
            }
        } );
    }

    // Whatever changes the text -- typing, the Clear button, the history --
    // changes the Search Line's pattern.
    connect( patternEdit_, &QComboBox::editTextChanged, this,
             [ this ]( const QString& text ) { searchLine_.setPattern( text ); } );
    connect( patternEdit_->lineEdit(), &QLineEdit::textEdited, this,
             &SearchLineWidget::patternEdited );
    connect( patternEdit_->lineEdit(), &QLineEdit::returnPressed, this,
             &SearchLineWidget::requestSearch );
    patternEdit_->installEventFilter( this );
    connect( patternEdit_, &QWidget::customContextMenuRequested, this,
             &SearchLineWidget::contextMenuRequested );

    connect( searchButton_, &QToolButton::clicked, this, &SearchLineWidget::requestSearch );
    connect( stopButton_, &QToolButton::clicked, this, &SearchLineWidget::stopRequested );
    connect( clearButton_, &QToolButton::clicked, patternEdit_, &QComboBox::clearEditText );
}

void SearchLineWidget::setQuickFindPolicy( const QuickFindPolicy& policy )
{
    searchLine_.setQuickFindPolicy( policy );
}

SearchLine::Flags SearchLineWidget::flags() const
{
    return searchLine_.flags();
}

void SearchLineWidget::setFlags( const SearchLine::Flags& flags )
{
    const auto previous = searchLine_.flags();
    searchLine_.setFlags( flags );
    showFlags( previous );
}

QString SearchLineWidget::pattern() const
{
    return searchLine_.pattern();
}

RegularExpressionPattern SearchLineWidget::request() const
{
    return searchLine_.request();
}

bool SearchLineWidget::add( const QString& word )
{
    const auto previous = searchLine_.flags();
    return showEditedPattern( previous, searchLine_.add( word ) );
}

bool SearchLineWidget::exclude( const QString& word )
{
    const auto previous = searchLine_.flags();
    return showEditedPattern( previous, searchLine_.exclude( word ) );
}

bool SearchLineWidget::replace( const QString& word )
{
    const auto previous = searchLine_.flags();
    return showEditedPattern( previous, searchLine_.replace( word ) );
}

bool SearchLineWidget::useFilters( const QList<PredefinedFilter>& filters )
{
    const auto previous = searchLine_.flags();
    return showEditedPattern( previous, searchLine_.useFilters( filters ) );
}

void SearchLineWidget::requested( const SearchSession::State& state )
{
    searchLine_.requested( state );
    showDisplay();
}

void SearchLineWidget::progressed( const SearchSession::State& state,
                                   SearchAutoRefresh::State autoRefresh )
{
    searchLine_.progressed( state, autoRefresh );
    showDisplay();
}

void SearchLineWidget::stopped( SearchAutoRefresh::State autoRefresh, LinesCount matchCount )
{
    searchLine_.stopped( autoRefresh, matchCount );
    showDisplay();
}

void SearchLineWidget::cleared()
{
    searchLine_.cleared();
    showDisplay();
}

void SearchLineWidget::settled( SearchAutoRefresh::State autoRefresh, LinesCount matchCount )
{
    searchLine_.settled( autoRefresh, matchCount );
    showDisplay();
}

SearchLine::Display SearchLineWidget::display() const
{
    return searchLine_.display();
}

void SearchLineWidget::setHistory( const QStringList& history )
{
    const QString text = patternEdit_->lineEdit()->text();
    patternEdit_->clear();
    patternEdit_->addItems( history );
    // In case we had something that wasn't added to the list (blank...):
    patternEdit_->lineEdit()->setText( text );

    completer_->setModel( new QStringListModel( history, completer_ ) );
}

void SearchLineWidget::clearHistory()
{
    patternEdit_->clear();
    completer_->setModel( new QStringListModel( {}, completer_ ) );
}

void SearchLineWidget::registerShortcuts(
    const ShortcutAction::ConfiguredShortcuts& configuredShortcuts, QWidget* scope )
{
    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }
    shortcuts_.clear();

    const auto toggles = {
        std::pair{ ShortcutAction::CrawlerEnableCaseMatching, matchCaseButton_ },
        std::pair{ ShortcutAction::CrawlerEnableRegex, useRegexpButton_ },
        std::pair{ ShortcutAction::CrawlerEnableInverseMatching, inverseButton_ },
        std::pair{ ShortcutAction::CrawlerEnableRegexCombining, booleanButton_ },
        std::pair{ ShortcutAction::CrawlerEnableAutoRefresh, autoRefreshButton_ },
        std::pair{ ShortcutAction::CrawlerKeepResults, keepResultsButton_ },
    };
    for ( const auto& [ action, button ] : toggles ) {
        ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, scope,
                                          Qt::WidgetWithChildrenShortcut, action,
                                          [ toggled = button ]() { toggled->toggle(); } );
    }
}

void SearchLineWidget::loadIcons( IconLoader& iconLoader )
{
    autoRefreshButton_->setIcon( iconLoader.loadCheckable( "icons8-search-refresh" ) );
    useRegexpButton_->setIcon( iconLoader.loadCheckable( "regex" ) );
    inverseButton_->setIcon( iconLoader.loadCheckable( "icons8-not-equal" ) );
    booleanButton_->setIcon( iconLoader.loadCheckable( "icons8-venn-diagram" ) );
    clearButton_->setIcon( iconLoader.load( "icons8-delete" ) );
    searchButton_->setIcon( iconLoader.load( "icons8-search" ) );
    keepResultsButton_->setIcon( iconLoader.loadCheckable( "icons8-lock" ) );
    matchCaseButton_->setIcon( iconLoader.loadCheckable( "icons8-font-size" ) );
    stopButton_->setIcon( iconLoader.load( "icons8-close-window" ) );
}

QMenu* SearchLineWidget::createStandardContextMenu() const
{
    return patternEdit_->lineEdit()->createStandardContextMenu();
}

bool SearchLineWidget::eventFilter( QObject* watched, QEvent* event )
{
    // The line edit emits returnPressed() and leaves the key to its parent;
    // the combo box would hand it back to the line edit, which would emit it
    // again and ask for a second Search (#648). The line edit is done with
    // it, so it stops here.
    if ( watched == patternEdit_ && event->type() == QEvent::KeyPress ) {
        const auto key = static_cast<QKeyEvent*>( event )->key();
        if ( key == Qt::Key_Return || key == Qt::Key_Enter ) {
            return true;
        }
    }
    return QWidget::eventFilter( watched, event );
}

void SearchLineWidget::requestSearch()
{
    const bool keepResults = keepResultsButton_->isChecked();
    if ( keepResults ) {
        keepResultsButton_->setChecked( false );
    }
    Q_EMIT searchRequested( keepResults );
}

void SearchLineWidget::takeFlagsFromButtons()
{
    const auto previous = searchLine_.flags();
    const SearchLine::Flags flags{ .matchCase = matchCaseButton_->isChecked(),
                                   .useRegexp = useRegexpButton_->isChecked(),
                                   .inverse = inverseButton_->isChecked(),
                                   .booleanCombination = booleanButton_->isChecked(),
                                   .autoRefresh = autoRefreshButton_->isChecked() };
    searchLine_.setFlags( flags );
    showFlags( previous );
}

void SearchLineWidget::showFlags( const SearchLine::Flags& previous )
{
    // A button set here fires its toggle, which is not the user's: the
    // Search Line already holds the flags.
    const auto flags = searchLine_.flags();
    showingFlags_ = true;
    matchCaseButton_->setChecked( flags.matchCase );
    useRegexpButton_->setChecked( flags.useRegexp );
    inverseButton_->setChecked( flags.inverse );
    booleanButton_->setChecked( flags.booleanCombination );
    autoRefreshButton_->setChecked( flags.autoRefresh );
    showingFlags_ = false;

    completer_->setCaseSensitivity( flags.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive );

    if ( flags != previous ) {
        Q_EMIT flagsChanged( flags, previous );
    }
}

bool SearchLineWidget::showEditedPattern( const SearchLine::Flags& previous, bool runNow )
{
    // Excluding a word, and adding one to a plain Search, switch the logical
    // combination on: its button is set, as the user would, before the
    // pattern is shown.
    showFlags( previous );
    patternEdit_->setEditText( searchLine_.pattern() );
    // Set the focus to lineEdit so that the user can press 'Return' immediately
    patternEdit_->lineEdit()->setFocus();

    return runNow;
}

void SearchLineWidget::showDisplay()
{
    const auto display = searchLine_.display();

    const bool running = display.buttons == SearchLine::Buttons::Stop;
    stopButton_->setEnabled( running );
    stopButton_->setVisible( running );
    searchButton_->setVisible( !running );
    clearButton_->setVisible( !running );

    infoLine_->setText( display.text );
    // The gauge is drawn in a palette of its own; without it, the line takes
    // the default palette or the Theme's error colors.
    if ( display.gauge ) {
        infoLine_->displayGauge( *display.gauge );
    }
    else {
        infoLine_->hideGauge();
        infoLine_->setPalette( display.isError ? errorPalette() : defaultPalette_ );
    }
    infoLine_->setVisible( display.visible );
}

QPalette SearchLineWidget::errorPalette() const
{
    const Theme& theme = Theme::active();
    auto errors = defaultPalette_;
    errors.setColor( QPalette::Window, theme.color( ColorToken::ErrorBackground ) );
    errors.setColor( QPalette::WindowText, theme.color( ColorToken::ErrorText ) );
    return errors;
}
