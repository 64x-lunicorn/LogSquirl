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

#include "regexlabwindow.h"

#include <algorithm>
#include <array>
#include <utility>

#include <QAbstractButton>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// The pause in typing after which the pattern is evaluated.
constexpr int DebounceMs = 250;

// The colors a match is marked in, one for each sub-pattern of a logical
// combination in turn; the text on them is black.
QColor matchColor( int subPattern )
{
    static const std::array<QColor, 4> Colors{ QColor( 255, 225, 80 ), QColor( 150, 225, 150 ),
                                               QColor( 150, 200, 255 ), QColor( 255, 175, 210 ) };
    return Colors[ static_cast<std::size_t>( subPattern ) % Colors.size() ];
}

// Where a Search Line option is named, the Lab names it the same, with the
// Search Line's translation.
QString searchLineText( const char* text )
{
    return QCoreApplication::translate( "CrawlerWidget", text );
}

} // namespace

RegexLabWindow::RegexLabWindow( RegexpEngine engine, QWidget* parent )
    : QWidget( parent, Qt::Window )
    , engine_( engine )
{
    setObjectName( QStringLiteral( "regexLab" ) );
    buildWidgets();
    updateTitle();
    updateSampleChoices();
    // A pattern starts out read as a regular expression, matching case.
    setPattern( RegularExpressionPattern{} );
    setSample( Sample::PastedText );
    resize( 800, 600 );
}

RegexLabWindow::~RegexLabWindow() = default;

regexlab::Bounds RegexLabWindow::bounds()
{
    return regexlab::Bounds{};
}

void RegexLabWindow::buildWidgets()
{
    const auto fixedFont = QFontDatabase::systemFont( QFontDatabase::FixedFont );

    patternEdit_ = new QLineEdit( this );
    patternEdit_->setObjectName( QStringLiteral( "pattern" ) );
    patternEdit_->setFont( fixedFont );
    patternEdit_->setClearButtonEnabled( true );
    patternEdit_->setAccessibleName( tr( "Pattern" ) );

    copyPattern_ = new QPushButton( tr( "Copy pattern" ), this );
    copyPattern_->setObjectName( QStringLiteral( "copyPattern" ) );

    auto* patternLabel = new QLabel( tr( "&Pattern:" ), this );
    patternLabel->setBuddy( patternEdit_ );
    auto* patternRow = new QHBoxLayout;
    patternRow->addWidget( patternLabel );
    patternRow->addWidget( patternEdit_, 1 );
    patternRow->addWidget( copyPattern_ );

    matchCase_ = new QCheckBox( searchLineText( "Match case" ), this );
    matchCase_->setObjectName( QStringLiteral( "matchCase" ) );
    useRegexp_ = new QCheckBox( searchLineText( "Use regex" ), this );
    useRegexp_->setObjectName( QStringLiteral( "useRegexp" ) );
    inverse_ = new QCheckBox( searchLineText( "Inverse match" ), this );
    inverse_->setObjectName( QStringLiteral( "inverse" ) );
    logicalCombination_ = new QCheckBox( searchLineText( "Boolean combining" ), this );
    logicalCombination_->setObjectName( QStringLiteral( "logicalCombination" ) );
    logicalCombination_->setToolTip(
        searchLineText( "Enable regular expression logical combining" ) );
    auto* optionsRow = new QHBoxLayout;
    optionsRow->addWidget( matchCase_ );
    optionsRow->addWidget( useRegexp_ );
    optionsRow->addWidget( inverse_ );
    optionsRow->addWidget( logicalCombination_ );
    optionsRow->addStretch();

    error_ = new QLabel( this );
    error_->setObjectName( QStringLiteral( "error" ) );
    error_->setWordWrap( true );
    error_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    auto errorPalette = error_->palette();
    errorPalette.setColor( QPalette::WindowText, QColor( 0xd3, 0x2f, 0x2f ) );
    error_->setPalette( errorPalette );
    error_->hide();

    sampleChoice_ = new QComboBox( this );
    sampleChoice_->setObjectName( QStringLiteral( "sampleChoice" ) );
    sampleChoice_->addItem( tr( "Selected Log Lines" ), static_cast<int>( Sample::SelectedLines ) );
    sampleChoice_->addItem( tr( "Lines around the current line" ),
                            static_cast<int>( Sample::LinesAroundCurrentLine ) );
    sampleChoice_->addItem( tr( "Pasted text" ), static_cast<int>( Sample::PastedText ) );
    refreshSample_ = new QPushButton( tr( "Refresh sample" ), this );
    refreshSample_->setObjectName( QStringLiteral( "refreshSample" ) );
    auto* sampleLabel = new QLabel( tr( "&Sample:" ), this );
    sampleLabel->setBuddy( sampleChoice_ );
    auto* sampleRow = new QHBoxLayout;
    sampleRow->addWidget( sampleLabel );
    sampleRow->addWidget( sampleChoice_ );
    sampleRow->addWidget( refreshSample_ );
    sampleRow->addStretch();

    sampleText_ = new QPlainTextEdit( this );
    sampleText_->setObjectName( QStringLiteral( "sampleText" ) );
    sampleText_->setFont( fixedFont );
    sampleText_->setLineWrapMode( QPlainTextEdit::NoWrap );
    sampleText_->setPlaceholderText( tr( "Paste or type sample lines here." ) );
    sampleText_->setAccessibleName( tr( "Sample lines" ) );

    groups_ = new QTableWidget( 0, 3, this );
    groups_->setObjectName( QStringLiteral( "captureGroups" ) );
    groups_->setHorizontalHeaderLabels( { tr( "Group" ), tr( "Name" ), tr( "Text" ) } );
    groups_->horizontalHeader()->setStretchLastSection( true );
    groups_->verticalHeader()->hide();
    groups_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    groups_->setSelectionBehavior( QAbstractItemView::SelectRows );
    groups_->setAccessibleName( tr( "Capture groups of the line with the cursor" ) );

    auto* splitter = new QSplitter( Qt::Vertical, this );
    splitter->addWidget( sampleText_ );
    splitter->addWidget( groups_ );
    splitter->setStretchFactor( 0, 3 );
    splitter->setStretchFactor( 1, 1 );

    status_ = new QLabel( this );
    status_->setObjectName( QStringLiteral( "status" ) );
    status_->setWordWrap( true );
    warning_ = new QLabel( this );
    warning_->setObjectName( QStringLiteral( "warning" ) );
    warning_->setWordWrap( true );
    warning_->hide();

    buttons_ = new QDialogButtonBox( QDialogButtonBox::Close, this );
    buttons_->setObjectName( QStringLiteral( "buttons" ) );

    auto* layout = new QVBoxLayout( this );
    layout->addLayout( patternRow );
    layout->addLayout( optionsRow );
    layout->addWidget( error_ );
    layout->addLayout( sampleRow );
    layout->addWidget( splitter, 1 );
    layout->addWidget( status_ );
    layout->addWidget( warning_ );
    layout->addWidget( buttons_ );

    debounce_ = new QTimer( this );
    debounce_->setSingleShot( true );
    debounce_->setInterval( DebounceMs );
    connect( debounce_, &QTimer::timeout, this, &RegexLabWindow::evaluate );

    connect( patternEdit_, &QLineEdit::textChanged, this, &RegexLabWindow::patternEdited );
    for ( auto* option : { matchCase_, useRegexp_, inverse_, logicalCombination_ } ) {
        connect( option, &QCheckBox::toggled, this, &RegexLabWindow::patternEdited );
    }
    connect( copyPattern_, &QPushButton::clicked, this, &RegexLabWindow::copyPattern );
    connect( sampleChoice_, &QComboBox::currentIndexChanged, this, &RegexLabWindow::sampleChosen );
    connect( refreshSample_, &QPushButton::clicked, this, &RegexLabWindow::refreshSample );
    connect( sampleText_, &QPlainTextEdit::textChanged, this, &RegexLabWindow::pastedTextEdited );
    connect( sampleText_, &QPlainTextEdit::cursorPositionChanged, this,
             &RegexLabWindow::showGroups );
    connect( buttons_, &QDialogButtonBox::rejected, this, &QWidget::close );
    connect( buttons_, &QDialogButtonBox::clicked, this, [ this ]( QAbstractButton* button ) {
        if ( buttons_->buttonRole( button ) == QDialogButtonBox::ApplyRole ) {
            apply();
        }
    } );

    patternEdit_->setFocus();
}

void RegexLabWindow::setPattern( const RegularExpressionPattern& pattern )
{
    {
        const QSignalBlocker patternBlocker( patternEdit_ );
        const QSignalBlocker caseBlocker( matchCase_ );
        const QSignalBlocker regexpBlocker( useRegexp_ );
        const QSignalBlocker inverseBlocker( inverse_ );
        const QSignalBlocker combinationBlocker( logicalCombination_ );
        patternEdit_->setText( pattern.pattern );
        matchCase_->setChecked( pattern.isCaseSensitive );
        useRegexp_->setChecked( !pattern.isPlainText );
        inverse_->setChecked( pattern.isExclude );
        logicalCombination_->setChecked( pattern.isBoolean );
    }
    evaluate();
}

RegularExpressionPattern RegexLabWindow::pattern() const
{
    return RegularExpressionPattern( patternEdit_->text(), matchCase_->isChecked(),
                                     inverse_->isChecked(), logicalCombination_->isChecked(),
                                     !useRegexp_->isChecked() );
}

void RegexLabWindow::setEngine( RegexpEngine engine )
{
    if ( engine != engine_ ) {
        engine_ = engine;
        evaluate();
    }
}

void RegexLabWindow::setSampleSource( RegexLabSampleSource source )
{
    source_ = std::move( source );
    updateTitle();
    updateSampleChoices();

    if ( !source_.hasLogFile() ) {
        setSample( Sample::PastedText );
        return;
    }
    const auto hasSelection = !source_.selectedLines( LinesCount( bounds().maxLines ) ).empty();
    setSample( hasSelection ? Sample::SelectedLines : Sample::LinesAroundCurrentLine );
}

RegexLabWindow::Sample RegexLabWindow::sample() const
{
    return static_cast<Sample>( sampleChoice_->currentData().toInt() );
}

void RegexLabWindow::setSample( Sample sample )
{
    if ( sample != Sample::PastedText && !source_.hasLogFile() ) {
        sample = Sample::PastedText;
    }
    {
        const QSignalBlocker blocker( sampleChoice_ );
        sampleChoice_->setCurrentIndex( sampleChoice_->findData( static_cast<int>( sample ) ) );
    }
    sampleChosen();
}

void RegexLabWindow::offerApply( bool offer )
{
    isApplyOffered_ = offer;
    buttons_->setStandardButtons( offer ? QDialogButtonBox::Apply | QDialogButtonBox::Cancel
                                        : QDialogButtonBox::Close );
}

const regexlab::Result& RegexLabWindow::result() const
{
    return result_;
}

void RegexLabWindow::closeEvent( QCloseEvent* event )
{
    debounce_->stop();
    runner_.cancel();
    if ( isApplyOffered_ && !isAnswered_ ) {
        isAnswered_ = true;
        Q_EMIT cancelled();
    }
    QWidget::closeEvent( event );
}

void RegexLabWindow::updateTitle()
{
    setWindowTitle( source_.name.isEmpty() ? tr( "Regex Lab" )
                                           : tr( "Regex Lab - %1" ).arg( source_.name ) );
}

void RegexLabWindow::updateSampleChoices()
{
    auto* model = qobject_cast<QStandardItemModel*>( sampleChoice_->model() );
    for ( const auto fromLogFile : { Sample::SelectedLines, Sample::LinesAroundCurrentLine } ) {
        const auto row = sampleChoice_->findData( static_cast<int>( fromLogFile ) );
        if ( model != nullptr && row >= 0 ) {
            model->item( row )->setEnabled( source_.hasLogFile() );
        }
    }
}

void RegexLabWindow::patternEdited()
{
    runner_.cancel();
    debounce_->start();
}

void RegexLabWindow::sampleChosen()
{
    const auto isPasted = sample() == Sample::PastedText;
    refreshSample_->setEnabled( !isPasted );
    sampleText_->setReadOnly( !isPasted );

    if ( !isPasted ) {
        refreshSample();
        return;
    }

    isShowingLogFileSample_ = true;
    sampleText_->setPlainText( pastedText_ );
    isShowingLogFileSample_ = false;
    evaluate();
}

void RegexLabWindow::refreshSample()
{
    const auto choice = sample();
    if ( choice == Sample::PastedText ) {
        return;
    }

    const auto count = LinesCount( bounds().maxLines );
    if ( !source_.hasLogFile() ) {
        logFileSample_.clear();
    }
    else if ( choice == Sample::SelectedLines ) {
        logFileSample_ = source_.selectedLines( count );
    }
    else {
        logFileSample_ = source_.linesAroundCurrentLine( count );
    }

    QStringList lines;
    lines.reserve( static_cast<qsizetype>( logFileSample_.size() ) );
    for ( const auto& line : logFileSample_ ) {
        lines.append( line );
    }
    isShowingLogFileSample_ = true;
    sampleText_->setPlainText( lines.join( QChar::LineFeed ) );
    isShowingLogFileSample_ = false;
    evaluate();
}

void RegexLabWindow::pastedTextEdited()
{
    if ( isShowingLogFileSample_ || sample() != Sample::PastedText ) {
        return;
    }
    pastedText_ = sampleText_->toPlainText();
    runner_.cancel();
    debounce_->start();
}

logsquirl::vector<QString> RegexLabWindow::sampleLines() const
{
    if ( sample() != Sample::PastedText ) {
        return logFileSample_;
    }
    logsquirl::vector<QString> lines;
    if ( pastedText_.isEmpty() ) {
        return lines;
    }
    const auto pasted = pastedText_.split( QChar::LineFeed );
    lines.reserve( std::min( static_cast<std::size_t>( pasted.size() ), bounds().maxLines ) );
    for ( const auto& line : pasted ) {
        if ( lines.size() == bounds().maxLines ) {
            break;
        }
        lines.push_back( line );
    }
    return lines;
}

void RegexLabWindow::evaluate()
{
    debounce_->stop();
    hasPattern_ = !patternEdit_->text().isEmpty();
    if ( !hasPattern_ ) {
        runner_.cancel();
        showResult( {} );
        return;
    }

    runner_.start<regexlab::Result>(
        [ searched = pattern(), engine = engine_,
          lines = sampleLines() ]( const std::atomic<bool>& cancelled ) {
            return regexlab::evaluate( searched, engine, lines, bounds(), cancelled );
        },
        [ this ]( regexlab::Result evaluated ) { showResult( std::move( evaluated ) ); } );
}

void RegexLabWindow::showResult( regexlab::Result result )
{
    result_ = std::move( result );

    QList<QTextEdit::ExtraSelection> selections;
    auto matchingLineColor = palette().color( QPalette::Highlight );
    matchingLineColor.setAlpha( 50 );

    const auto* document = sampleText_->document();
    for ( std::size_t index = 0; index < result_.lines.size(); ++index ) {
        const auto block = document->findBlockByNumber( static_cast<int>( index ) );
        if ( !block.isValid() ) {
            break;
        }
        const auto& line = result_.lines[ index ];
        if ( line.isMatch ) {
            QTextEdit::ExtraSelection selection;
            selection.cursor = QTextCursor( block );
            selection.format.setBackground( matchingLineColor );
            selection.format.setProperty( QTextFormat::FullWidthSelection, true );
            selections.append( selection );
        }
        for ( const auto& match : line.matches ) {
            if ( match.start + match.length >= block.length() ) {
                continue;
            }
            QTextEdit::ExtraSelection selection;
            selection.cursor = QTextCursor( block );
            selection.cursor.setPosition( block.position() + static_cast<int>( match.start ) );
            selection.cursor.setPosition( block.position()
                                              + static_cast<int>( match.start + match.length ),
                                          QTextCursor::KeepAnchor );
            selection.format.setBackground( matchColor( match.subPattern ) );
            selection.format.setForeground( QColor( Qt::black ) );
            selections.append( selection );
        }
    }
    sampleText_->setExtraSelections( selections );

    showStatus();
    showGroups();
    Q_EMIT evaluated();
}

void RegexLabWindow::showGroups()
{
    groups_->setRowCount( 0 );
    const auto line = static_cast<std::size_t>( sampleText_->textCursor().blockNumber() );
    if ( result_.error.has_value() || line >= result_.lines.size() ) {
        return;
    }

    const auto isCombination = logicalCombination_->isChecked();
    const auto& groups = result_.lines[ line ].groups;
    groups_->setRowCount( static_cast<int>( groups.size() ) );
    for ( std::size_t index = 0; index < groups.size(); ++index ) {
        const auto& group = groups[ index ];
        const auto row = static_cast<int>( index );
        const auto number
            = isCombination
                  ? QStringLiteral( "%1.%2" ).arg( group.subPattern + 1 ).arg( group.number )
                  : QString::number( group.number );
        groups_->setItem( row, 0, new QTableWidgetItem( number ) );
        groups_->setItem( row, 1, new QTableWidgetItem( group.name ) );
        auto* text = new QTableWidgetItem( group.start >= 0 ? group.text
                                                            : tr( "(no part in the match)" ) );
        if ( group.start < 0 ) {
            auto font = text->font();
            font.setItalic( true );
            text->setFont( font );
        }
        groups_->setItem( row, 2, text );
    }
}

void RegexLabWindow::showStatus()
{
    if ( result_.error.has_value() ) {
        const auto& error = *result_.error;
        error_->setText( error.position >= 0
                             ? tr( "Error at character %1 of the pattern: %2" )
                                   .arg( error.position + 1 )
                                   .arg( error.message )
                             : tr( "Error in the pattern: %1" ).arg( error.message ) );
        error_->show();
        if ( error.position >= 0 ) {
            patternEdit_->setCursorPosition( static_cast<int>( error.position ) );
        }
    }
    else {
        error_->clear();
        error_->hide();
    }

    QStringList status;
    const auto sampleSize = sampleLines().size();
    if ( sample() != Sample::PastedText && logFileSample_.empty() ) {
        status.append( sample() == Sample::SelectedLines
                           ? tr( "No Log Lines are selected in the tab." )
                           : tr( "The tab shows no Log Lines." ) );
    }
    if ( !hasPattern_ ) {
        status.append( tr( "Type a pattern to see what it matches in the sample." ) );
    }
    else if ( !result_.error.has_value() ) {
        status.append( tr( "%1 of %2 sample lines match." )
                           .arg( result_.matchingLines )
                           .arg( result_.lines.size() ) );
        if ( result_.stop == regexlab::Stop::TimeLimit ) {
            status.append( tr( "The evaluation stopped after %1 ms, at line %2 of %3." )
                               .arg( result_.elapsed.count() )
                               .arg( result_.lines.size() + 1 )
                               .arg( result_.sampleLines ) );
        }
        if ( std::ranges::any_of(
                 result_.lines, []( const regexlab::LineResult& line ) { return line.isCut; } ) ) {
            status.append( tr( "Lines longer than %1 characters are evaluated only up to there." )
                               .arg( bounds().maxLineLength ) );
        }
    }
    if ( sample() == Sample::PastedText
         && pastedText_.count( QChar::LineFeed ) >= static_cast<qsizetype>( sampleSize )
         && sampleSize == bounds().maxLines ) {
        status.append(
            tr( "Only the first %1 lines of the sample are evaluated." ).arg( bounds().maxLines ) );
    }
    status_->setText( status.join( QChar::Space ) );

    if ( hasPattern_ && result_.isSlow ) {
        warning_->setText( tr( "Evaluating the sample took %1 ms: the pattern may backtrack "
                               "excessively on some lines." )
                               .arg( result_.elapsed.count() ) );
        warning_->show();
    }
    else {
        warning_->clear();
        warning_->hide();
    }
}

void RegexLabWindow::copyPattern()
{
    QGuiApplication::clipboard()->setText( patternEdit_->text() );
}

void RegexLabWindow::apply()
{
    isAnswered_ = true;
    Q_EMIT applied( pattern() );
    close();
}
