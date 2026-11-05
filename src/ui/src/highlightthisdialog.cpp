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

#include "highlightthisdialog.h"

#include <algorithm>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QVBoxLayout>

#include "groupedits.h"
#include "highlighteredit.h"
#include "highlighterpresets.h"
#include "highlighterset.h"

namespace {

// The soft preset a new Highlighter of a set takes, as one added in the
// Highlighters dialog does: the one after the preset of the one before.
const HighlighterColorPreset& presetForNewHighlighter( qsizetype existingHighlighters )
{
    return highlighterColorPresets()[ static_cast<size_t>( existingHighlighters
                                                           % SoftHighlighterColorPresetCount ) ];
}

// A name for a new set none of the sets has, made unique as every group name
// is.
QString freeSetName( const QString& name, const QList<HighlighterSet>& sets )
{
    QStringList taken;
    for ( const auto& set : sets ) {
        taken.append( set.name() );
    }
    return logsquirl::valuenames::freeName( name, taken );
}

QString colorStyle( const QColor& foreColor, const QColor& backColor )
{
    QString style;
    if ( foreColor.isValid() ) {
        style += QStringLiteral( "color:%1;" ).arg( foreColor.name() );
    }
    if ( backColor.isValid() ) {
        style += QStringLiteral( "background-color:%1;" ).arg( backColor.name() );
    }
    return style;
}

} // namespace

HighlightThisDialog::HighlightThisDialog( const QString& selectedText, const QString& logLine,
                                          QWidget* parent )
    : QDialog( parent )
    , logLine_( logLine )
    , sets_( HighlighterSetCollection::getSynced().highlighterSets() )
{
    setWindowTitle( tr( "Highlight This" ) );

    auto* form = new QFormLayout;

    pattern_ = new QLineEdit( QRegularExpression::escape( selectedText ), this );
    pattern_->setObjectName( QStringLiteral( "highlighterPattern" ) );
    pattern_->setToolTip( tr( "The regex of the Highlighter; proposed from the selection, it "
                              "matches the selected text as it is" ) );
    form->addRow( tr( "Pattern:" ), pattern_ );

    // The presets as swatches, one row of the soft and one of the strong, and
    // the buttons picking a color freely: those of the Highlighter editor.
    auto* presets = new QGridLayout;
    presets->setSpacing( 4 );
    const auto& colorPresets = highlighterColorPresets();
    for ( int i = 0; i < HighlighterColorPresetCount; ++i ) {
        const auto& preset = colorPresets[ static_cast<size_t>( i ) ];
        auto* button = new QToolButton( this );
        button->setObjectName( QStringLiteral( "colorPresetButton%1" ).arg( i ) );
        button->setIcon( colorPresetIcon( preset, devicePixelRatioF() ) );
        button->setIconSize( QSize( 30, 18 ) );
        button->setToolTip( preset.displayName() );
        button->setAccessibleName( preset.displayName() );
        connect( button, &QToolButton::clicked, this, [ this, &preset ] {
            colorsPicked_ = true;
            setColors( preset.foreColor, preset.backColor );
        } );
        const bool soft = i < SoftHighlighterColorPresetCount;
        presets->addWidget( button, soft ? 0 : 1, soft ? i : i - SoftHighlighterColorPresetCount );
    }
    presets->setColumnStretch( SoftHighlighterColorPresetCount, 1 );

    foreColorButton_ = new QPushButton( tr( "Text..." ), this );
    foreColorButton_->setObjectName( QStringLiteral( "foreColorButton" ) );
    backColorButton_ = new QPushButton( tr( "Background..." ), this );
    backColorButton_->setObjectName( QStringLiteral( "backColorButton" ) );
    auto* pickers = new QHBoxLayout;
    pickers->addWidget( foreColorButton_ );
    pickers->addWidget( backColorButton_ );
    pickers->addStretch( 1 );
    auto* colors = new QVBoxLayout;
    colors->addLayout( presets );
    colors->addLayout( pickers );
    form->addRow( tr( "Colors:" ), colors );

    wholeLine_ = new QRadioButton( tr( "The whole Log Line" ), this );
    wholeLine_->setObjectName( QStringLiteral( "colorWholeLine" ) );
    onlyMatch_ = new QRadioButton( tr( "Only the matched text" ), this );
    onlyMatch_->setObjectName( QStringLiteral( "colorOnlyMatch" ) );
    wholeLine_->setChecked( true );
    auto* colored = new QHBoxLayout;
    colored->addWidget( wholeLine_ );
    colored->addWidget( onlyMatch_ );
    colored->addStretch( 1 );
    form->addRow( tr( "Highlight:" ), colored );

    // The set: one of the user's own, the first active one if there is one,
    // else a new one named here. The Team's and the plugins' sets are not the
    // user's to add to.
    set_ = new QComboBox( this );
    set_->setObjectName( QStringLiteral( "highlighterSet" ) );
    const auto activeSetIds = HighlighterSetCollection::get().activeSetIds();
    int chosen = -1;
    for ( int row = 0; row < sets_.size(); ++row ) {
        set_->addItem( sets_[ row ].name(), sets_[ row ].id() );
        if ( chosen < 0 && activeSetIds.contains( sets_[ row ].id() ) ) {
            chosen = row;
        }
    }
    set_->addItem( tr( "New Highlighter Set" ), QString{} );
    newSetName_ = new QLineEdit( freeSetName( tr( "Highlighter Set" ), sets_ ), this );
    newSetName_->setObjectName( QStringLiteral( "newSetName" ) );
    auto* setLine = new QHBoxLayout;
    setLine->addWidget( set_, 1 );
    setLine->addWidget( newSetName_, 1 );
    form->addRow( tr( "Highlighter Set:" ), setLine );

    preview_ = new QLabel( this );
    preview_->setObjectName( QStringLiteral( "preview" ) );
    preview_->setTextFormat( Qt::RichText );
    preview_->setWordWrap( true );
    preview_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    warning_ = new QLabel( this );
    warning_->setObjectName( QStringLiteral( "previewWarning" ) );
    warning_->setWordWrap( true );
    auto* previewColumn = new QVBoxLayout;
    previewColumn->addWidget( preview_ );
    previewColumn->addWidget( warning_ );
    form->addRow( tr( "Preview:" ), previewColumn );

    buttons_ = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    auto* layout = new QVBoxLayout( this );
    layout->addLayout( form );
    layout->addWidget( buttons_ );

    set_->setCurrentIndex( chosen < 0 ? set_->count() - 1 : chosen );
    setChosen();

    connect( set_, &QComboBox::currentIndexChanged, this, &HighlightThisDialog::setChosen );
    connect( pattern_, &QLineEdit::textChanged, this, &HighlightThisDialog::updatePreview );
    connect( wholeLine_, &QRadioButton::toggled, this, &HighlightThisDialog::updatePreview );
    connect( foreColorButton_, &QPushButton::clicked, this, [ this ] {
        QColor color;
        if ( HighlighterEdit::showColorPicker( foreColor_, color ) ) {
            colorsPicked_ = true;
            setColors( color, backColor_ );
        }
    } );
    connect( backColorButton_, &QPushButton::clicked, this, [ this ] {
        QColor color;
        if ( HighlighterEdit::showColorPicker( backColor_, color ) ) {
            colorsPicked_ = true;
            setColors( foreColor_, color );
        }
    } );
    connect( buttons_, &QDialogButtonBox::accepted, this, &HighlightThisDialog::accept );
    connect( buttons_, &QDialogButtonBox::rejected, this, &HighlightThisDialog::reject );

    pattern_->setFocus();
    resize( 640, sizeHint().height() );
}

QString HighlightThisDialog::chosenSetId() const
{
    return set_->currentData().toString();
}

void HighlightThisDialog::setChosen()
{
    const auto setId = chosenSetId();
    newSetName_->setEnabled( setId.isEmpty() );
    if ( !colorsPicked_ ) {
        const auto chosen
            = std::find_if( sets_.cbegin(), sets_.cend(),
                            [ &setId ]( const auto& set ) { return set.id() == setId; } );
        const auto& preset
            = presetForNewHighlighter( chosen != sets_.cend() ? chosen->highlighters().size() : 0 );
        setColors( preset.foreColor, preset.backColor );
    }
}

void HighlightThisDialog::setColors( const QColor& foreColor, const QColor& backColor )
{
    foreColor_ = foreColor;
    backColor_ = backColor;
    HighlighterEdit::updateIcon( foreColorButton_, foreColor_ );
    HighlighterEdit::updateIcon( backColorButton_, backColor_ );
    updatePreview();
}

Highlighter HighlightThisDialog::highlighter() const
{
    return Highlighter( pattern_->text(), false, onlyMatch_->isChecked(), foreColor_, backColor_ );
}

QString HighlightThisDialog::previewHtml() const
{
    return preview_->text();
}

QString HighlightThisDialog::previewWarning() const
{
    return warning_->text();
}

void HighlightThisDialog::updatePreview()
{
    const QRegularExpression regex( pattern_->text() );
    QString warning;
    if ( pattern_->text().isEmpty() ) {
        warning = tr( "The pattern is empty." );
    }
    else if ( !regex.isValid() ) {
        warning = tr( "The regex is not valid: %1" ).arg( regex.errorString() );
    }
    else if ( !regex.match( logLine_ ).hasMatch() ) {
        warning = tr( "The regex does not match this Log Line, so it colors nothing here." );
    }
    warning_->setText( warning );
    warning_->setVisible( !warning.isEmpty() );
    buttons_->button( QDialogButtonBox::Ok )
        ->setEnabled( !pattern_->text().isEmpty() && regex.isValid() );

    // The Log Line as the Highlighter colors it, as the views do.
    logsquirl::vector<HighlightedMatch> matches;
    const auto matchType = regex.isValid() && !pattern_->text().isEmpty()
                               ? highlighter().colorLine( logLine_, matches )
                               : HighlighterMatchType::NoMatch;
    QString html = QStringLiteral( "<span style=\"white-space: pre-wrap\">" );
    if ( matchType == HighlighterMatchType::LineMatch ) {
        html += QStringLiteral( "<span style=\"%1\">%2</span>" )
                    .arg( colorStyle( foreColor_, backColor_ ), logLine_.toHtmlEscaped() );
    }
    else {
        std::sort( matches.begin(), matches.end(), []( const auto& a, const auto& b ) {
            return a.startColumn() < b.startColumn();
        } );
        qsizetype position = 0;
        for ( const auto& match : matches ) {
            const auto start = static_cast<qsizetype>( match.startColumn().get() );
            const auto length = static_cast<qsizetype>( match.size().get() );
            if ( start < position || length <= 0 ) {
                continue;
            }
            html += logLine_.mid( position, start - position ).toHtmlEscaped();
            html += QStringLiteral( "<span style=\"%1\">%2</span>" )
                        .arg( colorStyle( match.foreColor(), match.backColor() ),
                              logLine_.mid( start, length ).toHtmlEscaped() );
            position = start + length;
        }
        html += logLine_.mid( position ).toHtmlEscaped();
    }
    html += QStringLiteral( "</span>" );
    preview_->setText( html );
}

void HighlightThisDialog::accept()
{
    auto& collection = HighlighterSetCollection::get();
    auto sets = collection.highlighterSets();

    auto setId = chosenSetId();
    const auto chosen = std::find_if( sets.begin(), sets.end(),
                                      [ &setId ]( const auto& set ) { return set.id() == setId; } );
    if ( chosen != sets.end() ) {
        chosen->addHighlighter( highlighter() );
    }
    else {
        // A set the user chose may have gone meanwhile: the Highlighter is
        // not lost, it goes into a new one.
        const auto name = newSetName_->text().trimmed();
        auto set = HighlighterSet::createNewSet(
            freeSetName( name.isEmpty() ? tr( "Highlighter Set" ) : name, sets ) );
        set.addHighlighter( highlighter() );
        setId = set.id();
        sets.append( set );
    }

    collection.setHighlighterSets( sets );
    // What is added shows at once: the set is active.
    if ( !collection.activeSetIds().contains( setId ) ) {
        collection.activateSet( setId );
    }
    collection.save();
    QDialog::accept();
}
