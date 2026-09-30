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
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QSyntaxHighlighter>
#include <QTableWidget>
#include <QTextBlock>
#include <QTimer>
#include <QToolTip>
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

// Shades the matching lines of the result shown and marks its matches, at
// most bounds().maxMarks of them. A line is formatted as it is laid out: no
// extra selection is made per line or per match, which costs a long line
// dearly. Matches of a sub-pattern that follow on each other are marked as
// one.
class RegexLabMarks : public QSyntaxHighlighter {
public:
    // Set on each format of a matching line, and on each match's.
    static constexpr int MatchingLine = QTextFormat::UserProperty;
    static constexpr int Match = QTextFormat::UserProperty + 1;

    RegexLabMarks( const regexlab::Result& result, QTextDocument* document )
        : QSyntaxHighlighter( document )
        , result_( result )
    {
    }

protected:
    void highlightBlock( const QString& text ) override
    {
        const auto line = static_cast<std::size_t>( currentBlock().blockNumber() );
        if ( line >= result_.lines.size() ) {
            return;
        }
        const auto isMatch = result_.lines[ line ].isMatch;
        if ( isMatch ) {
            QTextCharFormat format;
            format.setBackground( lineShade_ );
            format.setProperty( MatchingLine, true );
            setFormat( 0, static_cast<int>( text.size() ), format );
        }

        std::optional<regexlab::MatchSpan> marked;
        const auto mark = [ & ]() {
            if ( !marked.has_value() || marked->start + marked->length > text.size() ) {
                return;
            }
            QTextCharFormat format;
            format.setBackground( markColors_.has_value() ? markColors_->background
                                                          : matchColor( marked->subPattern ) );
            format.setForeground( markColors_.has_value() ? markColors_->text
                                                          : QColor( Qt::black ) );
            format.setProperty( Match, true );
            format.setProperty( MatchingLine, isMatch );
            setFormat( static_cast<int>( marked->start ), static_cast<int>( marked->length ),
                       format );
        };
        for ( const auto& match : result_.lines[ line ].matches ) {
            if ( marked.has_value() && marked->subPattern == match.subPattern
                 && marked->start + marked->length == match.start ) {
                marked->length += match.length;
                continue;
            }
            mark();
            marked = match;
        }
        mark();
    }

public:
    // The shade of a matching line, from the window's palette.
    void setLineShade( QColor shade )
    {
        lineShade_ = shade;
    }

    // The colors every mark is shown in, instead of one per sub-pattern.
    void setMarkColors( std::optional<RegexLabWindow::MarkColors> colors )
    {
        markColors_ = colors;
    }

private:
    const regexlab::Result& result_;
    QColor lineShade_;
    std::optional<RegexLabWindow::MarkColors> markColors_;
};

// Beside each line of the sample, the sub-patterns of a logical combination
// that match it (#661): each in a column of its own, numbered as written and
// in the color its matches are marked in, so that a combined pattern is
// understood by which of its parts match a line -- the line shaded or not
// by the combination's verdict. Hidden for any other pattern.
class RegexLabSubPatternColumn : public QWidget {
public:
    RegexLabSubPatternColumn( const regexlab::Result& result, QPlainTextEdit& text,
                              std::function<QString( int line )> describe, QWidget* parent )
        : QWidget( parent )
        , result_( result )
        , text_( text )
        , describe_( std::move( describe ) )
    {
        setObjectName( QStringLiteral( "subPatternColumn" ) );
        setFont( text.font() );
        hide();
    }

    // Sized for the sub-patterns of the result shown.
    void showResult()
    {
        const auto count = result_.subPatterns.size();
        const auto numberWidth
            = fontMetrics().horizontalAdvance( QString::number( std::max<qsizetype>( count, 1 ) ) )
              + 6;
        slotWidth_ = numberWidth;
        setFixedWidth( static_cast<int>( count ) * slotWidth_ + 2 );
        setVisible( count > 0 );
        update();
    }

    // The sub-patterns shown beside a line, numbered from 1.
    QList<int> subPatternsShown( int line ) const
    {
        QList<int> shown;
        if ( line < 0 || static_cast<std::size_t>( line ) >= result_.lines.size() ) {
            return shown;
        }
        const auto& matches = result_.lines[ static_cast<std::size_t>( line ) ].subPatternMatches;
        for ( std::size_t index = 0; index < matches.size(); ++index ) {
            if ( matches[ index ] ) {
                shown.append( static_cast<int>( index ) + 1 );
            }
        }
        return shown;
    }

protected:
    // Hovering a line says in words which sub-patterns match it.
    bool event( QEvent* event ) override
    {
        if ( event->type() == QEvent::ToolTip ) {
            const auto* help = static_cast<QHelpEvent*>( event );
            const auto top = mapFromGlobal( text_.viewport()->mapToGlobal( QPoint( 0, 0 ) ) ).y();
            const auto block
                = text_.cursorForPosition( QPoint( 0, help->pos().y() - top ) ).block();
            QToolTip::showText( help->globalPos(), describe_( block.blockNumber() ), this );
            return true;
        }
        return QWidget::event( event );
    }

    void paintEvent( QPaintEvent* ) override
    {
        if ( result_.subPatterns.isEmpty() ) {
            return;
        }
        QPainter painter( this );
        // The lines are laid out in the sample's viewport, which starts
        // below this column's top by the sample's frame.
        const auto top = mapFromGlobal( text_.viewport()->mapToGlobal( QPoint( 0, 0 ) ) ).y();
        const auto bottom = top + text_.viewport()->height();
        painter.setClipRect( 0, top, width(), text_.viewport()->height() );

        for ( auto block = text_.cursorForPosition( QPoint( 0, 0 ) ).block(); block.isValid();
              block = block.next() ) {
            const auto line = text_.cursorRect( QTextCursor( block ) );
            const auto lineTop = top + line.top();
            if ( lineTop > bottom ) {
                break;
            }
            for ( const auto number : subPatternsShown( block.blockNumber() ) ) {
                const QRect slot( ( number - 1 ) * slotWidth_ + 1, lineTop, slotWidth_ - 1,
                                  line.height() );
                painter.fillRect( slot, matchColor( number - 1 ) );
                painter.setPen( Qt::black );
                painter.drawText( slot, Qt::AlignCenter, QString::number( number ) );
            }
        }
    }

private:
    const regexlab::Result& result_;
    QPlainTextEdit& text_;
    std::function<QString( int line )> describe_;
    int slotWidth_ = 0;
};

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

RegexLabWindow::~RegexLabWindow()
{
    // Destroyed while open -- along with its parent, say -- the Lab still
    // answers once. Along with its parent, this runs inside the parent's
    // destruction, before Qt disconnects the parent as a receiver: see the
    // class comment on how to listen safely.
    cancel();
}

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

    // Where in the pattern the error is: the pattern, or the part of it
    // around the error, with a caret under the character. The cursor in the
    // pattern stays where the user has it.
    errorMarker_ = new QLabel( this );
    errorMarker_->setObjectName( QStringLiteral( "errorMarker" ) );
    errorMarker_->setFont( fixedFont );
    errorMarker_->setTextFormat( Qt::PlainText );
    errorMarker_->setPalette( errorPalette );
    errorMarker_->hide();

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
    marks_ = new RegexLabMarks( result_, sampleText_->document() );

    auto* sampleArea = new QWidget( this );
    subPatternColumn_ = new RegexLabSubPatternColumn(
        result_, *sampleText_, [ this ]( int line ) { return subPatternsDescription( line ); },
        sampleArea );
    subPatternColumn_->setAccessibleName( tr( "The sub-patterns that match the line" ) );
    auto* sampleAreaLayout = new QHBoxLayout( sampleArea );
    sampleAreaLayout->setContentsMargins( 0, 0, 0, 0 );
    sampleAreaLayout->setSpacing( 0 );
    sampleAreaLayout->addWidget( subPatternColumn_ );
    sampleAreaLayout->addWidget( sampleText_, 1 );
    connect( sampleText_, &QPlainTextEdit::updateRequest, subPatternColumn_,
             [ column = subPatternColumn_ ]() { column->update(); } );

    // The sub-patterns of a logical combination, numbered and in the colors
    // their matches are marked in.
    subPatterns_ = new QLabel( this );
    subPatterns_->setObjectName( QStringLiteral( "subPatterns" ) );
    subPatterns_->setWordWrap( true );
    subPatterns_->setTextFormat( Qt::RichText );
    subPatterns_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    subPatterns_->hide();

    groups_ = new QTableWidget( 0, 3, this );
    groups_->setObjectName( QStringLiteral( "captureGroups" ) );
    groups_->setHorizontalHeaderLabels( { tr( "Group" ), tr( "Name" ), tr( "Text" ) } );
    groups_->horizontalHeader()->setStretchLastSection( true );
    groups_->verticalHeader()->hide();
    groups_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    groups_->setSelectionBehavior( QAbstractItemView::SelectRows );
    groups_->setAccessibleName( tr( "Capture groups of the line with the cursor" ) );

    auto* splitter = new QSplitter( Qt::Vertical, this );
    splitter->addWidget( sampleArea );
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
    layout->addWidget( errorMarker_ );
    layout->addLayout( sampleRow );
    layout->addWidget( subPatterns_ );
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
    connect( sampleChoice_, &QComboBox::currentIndexChanged, this, [ this ]() { sampleChosen(); } );
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

void RegexLabWindow::setOptionsKept( Options kept, Options shownFixed )
{
    const auto show = [ & ]( QCheckBox* box, Option option ) {
        box->setVisible( kept.testFlag( option ) || shownFixed.testFlag( option ) );
        box->setEnabled( kept.testFlag( option ) );
    };
    show( matchCase_, Option::MatchCase );
    show( useRegexp_, Option::UseRegexp );
    show( inverse_, Option::Inverse );
    show( logicalCombination_, Option::LogicalCombination );
}

void RegexLabWindow::setLineDecision( DecisionFor decisionFor, std::optional<MarkColors> colors )
{
    decisionFor_ = std::move( decisionFor );
    marks_->setMarkColors( colors );
    evaluate();
}

bool RegexLabWindow::hasLineDecision() const
{
    return static_cast<bool>( decisionFor_ );
}

void RegexLabWindow::setEngine( RegexpEngine engine )
{
    if ( engine != engine_ ) {
        engine_ = engine;
        evaluate();
    }
}

RegexpEngine RegexLabWindow::engine() const
{
    return engine_;
}

void RegexLabWindow::setSampleSource( RegexLabSampleSource source )
{
    disconnect( tabDestroyed_ );
    source_ = std::move( source );
    if ( !source_.tab.isNull() ) {
        // A tab closed while the Lab is open leaves it without a Log File.
        tabDestroyed_ = connect( source_.tab, &QObject::destroyed, this,
                                 [ this ]() { setSampleSource( {} ); } );
    }
    updateTitle();
    updateSampleChoices();

    if ( !source_.hasLogFile() ) {
        setSample( Sample::PastedText );
        return;
    }

    // The selection read once: it is the sample, unless there is none.
    const auto count = LinesCount( bounds().maxLines );
    auto selected = source_.selectedLines( count );
    const auto chosen = selected.count > 0 ? Sample::SelectedLines : Sample::LinesAroundCurrentLine;
    {
        const QSignalBlocker blocker( sampleChoice_ );
        sampleChoice_->setCurrentIndex( sampleChoice_->findData( static_cast<int>( chosen ) ) );
    }
    sampleChosen( selected.count > 0 ? std::move( selected )
                                     : source_.linesAroundCurrentLine( count ) );
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
    sampleReader_.cancel();
    runner_.cancel();
    cancel();
    QWidget::closeEvent( event );
}

void RegexLabWindow::cancel()
{
    if ( isApplyOffered_ && !isAnswered_ ) {
        isAnswered_ = true;
        Q_EMIT cancelled();
    }
}

void RegexLabWindow::updateTitle()
{
    setWindowTitle( source_.name.isEmpty() || !source_.hasLogFile()
                        ? tr( "Regex Lab" )
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

void RegexLabWindow::sampleChosen( std::optional<RegexLabSample> taken )
{
    const auto chosen = sample();
    if ( shownSample_ == Sample::PastedText && chosen != Sample::PastedText ) {
        pastedText_ = sampleText_->toPlainText();
    }
    const auto wasPasted = shownSample_ == Sample::PastedText;
    shownSample_ = chosen;

    const auto isPasted = chosen == Sample::PastedText;
    refreshSample_->setEnabled( !isPasted );
    sampleText_->setReadOnly( !isPasted );
    if ( !isPasted ) {
        takeSample( std::move( taken ) );
        return;
    }

    // A sample still read from the tab is not shown over the pasted text.
    sampleReader_.cancel();
    if ( !wasPasted || sampleText_->document()->isEmpty() ) {
        isShowingLogFileSample_ = true;
        sampleText_->setPlainText( pastedText_ );
        isShowingLogFileSample_ = false;
    }
    evaluate();
}

void RegexLabWindow::refreshSample()
{
    if ( sample() != Sample::PastedText ) {
        takeSample( std::nullopt );
    }
}

void RegexLabWindow::takeSample( std::optional<RegexLabSample> taken )
{
    const auto choice = sample();
    if ( choice == Sample::PastedText ) {
        return;
    }
    if ( !taken.has_value() && source_.hasLogFile() ) {
        const auto count = LinesCount( bounds().maxLines );
        taken = choice == Sample::SelectedLines ? source_.selectedLines( count )
                                                : source_.linesAroundCurrentLine( count );
    }

    struct Read {
        std::shared_ptr<const logsquirl::vector<QString>> lines;
        QString shown;
    };
    auto read = taken.has_value() ? std::move( taken->read )
                                  : std::function<logsquirl::vector<QString>()>{};
    sampleReader_.start<Read>(
        [ read = std::move( read ) ]( const std::atomic<bool>& cancelled ) {
            Read result;
            auto lines = read ? read() : logsquirl::vector<QString>{};
            // Only the start of a long line is shown.
            QStringList shown;
            shown.reserve( static_cast<qsizetype>( lines.size() ) );
            for ( const auto& line : lines ) {
                if ( cancelled.load() ) {
                    break;
                }
                shown.append( regexlab::cutLine( line, bounds().maxLineLength ) );
            }
            result.shown = shown.join( QChar::LineFeed );
            result.lines = std::make_shared<const logsquirl::vector<QString>>( std::move( lines ) );
            return result;
        },
        [ this ]( Read sampleRead ) {
            if ( sample() == Sample::PastedText ) {
                return;
            }
            logFileSample_ = std::move( sampleRead.lines );
            isShowingLogFileSample_ = true;
            sampleText_->setPlainText( sampleRead.shown );
            isShowingLogFileSample_ = false;
            Q_EMIT sampleTaken();
            evaluate();
        } );
}

void RegexLabWindow::pastedTextEdited()
{
    if ( isShowingLogFileSample_ || sample() != Sample::PastedText ) {
        return;
    }
    runner_.cancel();
    debounce_->start();
}

std::shared_ptr<const logsquirl::vector<QString>> RegexLabWindow::pastedLines()
{
    const auto text = sampleText_->toPlainText();
    const auto maxLines = bounds().maxLines;
    logsquirl::vector<QString> lines;
    hasMorePastedLines_ = false;

    qsizetype start = 0;
    while ( start < text.size() ) {
        if ( lines.size() == maxLines ) {
            hasMorePastedLines_ = true;
            break;
        }
        auto end = text.indexOf( QChar::LineFeed, start );
        if ( end < 0 ) {
            end = text.size();
        }
        lines.push_back( text.mid( start, end - start ) );
        start = end + 1;
    }
    return std::make_shared<const logsquirl::vector<QString>>( std::move( lines ) );
}

void RegexLabWindow::evaluate()
{
    debounce_->stop();
    const auto searched = pattern();
    hasPattern_ = !searched.pattern.isEmpty();
    if ( !hasPattern_ ) {
        runner_.cancel();
        evaluatedPattern_ = searched;
        showResult( {} );
        return;
    }

    auto lines = sample() == Sample::PastedText ? pastedLines() : logFileSample_;
    auto decision = decisionFor_ ? decisionFor_( searched ) : regexlab::LineDecision{};
    runner_.start<regexlab::Result>(
        [ searched, engine = engine_, decision = std::move( decision ),
          lines = std::move( lines ) ]( const std::atomic<bool>& cancelled ) {
            return regexlab::evaluate( searched, engine,
                                       lines ? *lines : logsquirl::vector<QString>{}, bounds(),
                                       cancelled, decision );
        },
        [ this, searched ]( regexlab::Result evaluated ) {
            evaluatedPattern_ = searched;
            showResult( std::move( evaluated ) );
        } );
}

void RegexLabWindow::showResult( regexlab::Result result )
{
    result_ = std::move( result );

    auto shade = palette().color( QPalette::Highlight );
    shade.setAlpha( 50 );
    marks_->setLineShade( shade );
    // Formatting the sample is no edit of it.
    isShowingLogFileSample_ = true;
    marks_->rehighlight();
    isShowingLogFileSample_ = false;

    showError();
    showStatus();
    showGroups();
    showSubPatterns();
    Q_EMIT evaluated();
}

void RegexLabWindow::showGroups()
{
    // The sub-patterns of the line with the cursor, for a screen reader.
    subPatternColumn_->setAccessibleDescription(
        subPatternsDescription( sampleText_->textCursor().blockNumber() ) );

    groups_->setRowCount( 0 );
    const auto line = static_cast<std::size_t>( sampleText_->textCursor().blockNumber() );
    if ( result_.error.has_value() || line >= result_.lines.size() ) {
        return;
    }

    const auto isCombination = evaluatedPattern_.isBoolean;
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

void RegexLabWindow::showSubPatterns()
{
    subPatternColumn_->showResult();
    if ( result_.subPatterns.isEmpty() ) {
        subPatterns_->clear();
        subPatterns_->hide();
        return;
    }

    QStringList shown;
    for ( qsizetype index = 0; index < result_.subPatterns.size(); ++index ) {
        const auto color = matchColor( static_cast<int>( index ) ).name();
        shown.append( QStringLiteral(
                          "<span style=\"background-color:%1; color:black\">&nbsp;%2&nbsp;</span> "
                          "<code>%3</code>" )
                          .arg( color )
                          .arg( index + 1 )
                          .arg( result_.subPatterns[ index ].toHtmlEscaped() ) );
    }
    subPatterns_->setText(
        tr( "Sub-patterns: %1" ).arg( shown.join( QStringLiteral( "&nbsp; " ) ) ) );
    subPatterns_->show();
}

QList<int> RegexLabWindow::subPatternsShown( int line ) const
{
    return subPatternColumn_->subPatternsShown( line );
}

QString RegexLabWindow::subPatternsDescription( int line ) const
{
    if ( result_.subPatterns.isEmpty() || line < 0
         || static_cast<std::size_t>( line ) >= result_.lines.size() ) {
        return {};
    }
    QStringList numbers;
    for ( const auto number : subPatternsShown( line ) ) {
        numbers.append( QString::number( number ) );
    }
    return numbers.isEmpty() ? tr( "Line %1: no sub-pattern matches" ).arg( line + 1 )
                             : tr( "Line %1: sub-patterns %2 match" )
                                   .arg( line + 1 )
                                   .arg( numbers.join( QStringLiteral( ", " ) ) );
}

void RegexLabWindow::showError()
{
    if ( !result_.error.has_value() ) {
        error_->clear();
        error_->hide();
        errorMarker_->clear();
        errorMarker_->hide();
        return;
    }

    const auto& error = *result_.error;
    if ( error.position < 0 ) {
        error_->setText( tr( "Error in the pattern: %1" ).arg( error.message ) );
        error_->show();
        errorMarker_->hide();
        return;
    }

    error_->setText( tr( "Error at character %1 of the pattern: %2" )
                         .arg( error.position + 1 )
                         .arg( error.message ) );
    error_->show();

    // The pattern around the error, with a caret under its character.
    constexpr qsizetype Around = 40;
    const auto& text = evaluatedPattern_.pattern;
    const auto position = std::min( error.position, text.size() );
    const auto first = std::max<qsizetype>( position - Around, 0 );
    const auto elided = first > 0 ? QStringLiteral( "..." ) : QString();
    auto shown = elided + text.mid( first, 2 * Around );
    shown.replace( QChar::Tabulation, QChar::Space );
    const auto caret = QString( elided.size() + position - first, QChar::Space ) + QChar( '^' );
    errorMarker_->setText( shown + QChar::LineFeed + caret );
    errorMarker_->show();
}

void RegexLabWindow::showStatus()
{
    QStringList status;
    if ( sample() != Sample::PastedText && logFileSample_ && logFileSample_->empty() ) {
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
            status.append( tr( "Lines longer than %1 characters are shown and marked only up to "
                               "there; whether they match is decided on the whole line." )
                               .arg( bounds().maxLineLength ) );
        }
        if ( result_.isMarkingCut ) {
            status.append( tr( "Not every match is marked: at most %1 in a line and %2 in all." )
                               .arg( bounds().maxMarksPerLine )
                               .arg( bounds().maxMarks ) );
        }
    }
    if ( sample() == Sample::PastedText && hasMorePastedLines_ ) {
        status.append(
            tr( "Only the first %1 lines of the sample are evaluated." ).arg( bounds().maxLines ) );
    }
    status_->setText( status.join( QChar::Space ) );

    QStringList warnings;
    if ( hasPattern_ && result_.isSlow ) {
        warnings.append( tr( "Evaluating the sample took %1 ms: the pattern may backtrack "
                             "excessively on some lines." )
                             .arg( result_.elapsed.count() ) );
    }
    if ( hasPattern_ && result_.slowLines > 0 ) {
        warnings.append( tr( "%1 lines took more than %2 ms each. On such a line the engine may "
                             "have given up and reported no match, as a Search would." )
                             .arg( result_.slowLines )
                             .arg( bounds().slowLine.count() ) );
    }
    warning_->setText( warnings.join( QChar::Space ) );
    warning_->setVisible( !warnings.isEmpty() );
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
