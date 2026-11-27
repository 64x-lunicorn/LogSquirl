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
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "groupedits.h"
#include "highlightercoloring.h"
#include "highlighterset.h"

namespace {

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

    // How it colors: the rows the Highlighter editor's presets and pickers
    // make, as Add as Predefined Filter's Also highlight it has them (#986).
    coloring_ = new HighlighterColoring( this );
    form->addRow( tr( "Colors:" ), coloring_->colorsField() );
    form->addRow( tr( "Highlight:" ), coloring_->highlightField() );

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
    connect( coloring_, &HighlighterColoring::changed, this, &HighlightThisDialog::updatePreview );
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
    const auto chosen = std::find_if( sets_.cbegin(), sets_.cend(),
                                      [ &setId ]( const auto& set ) { return set.id() == setId; } );
    coloring_->followSet( chosen != sets_.cend() ? chosen->highlighters().size() : 0 );
    updatePreview();
}

Highlighter HighlightThisDialog::highlighter() const
{
    return Highlighter( pattern_->text(), false, coloring_->onlyMatch(), coloring_->foreColor(),
                        coloring_->backColor() );
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
                    .arg( colorStyle( coloring_->foreColor(), coloring_->backColor() ),
                          logLine_.toHtmlEscaped() );
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
