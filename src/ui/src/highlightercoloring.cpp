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

#include "highlightercoloring.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "highlighteredit.h"
#include "highlighterpresets.h"

HighlighterColoring::HighlighterColoring( QWidget* parent )
    : QObject( parent )
{
    // The presets as swatches, one row of the soft and one of the strong, and
    // the buttons picking a color freely: those of the Highlighter editor.
    auto* presets = new QGridLayout;
    presets->setSpacing( 4 );
    const auto& colorPresets = highlighterColorPresets();
    for ( int i = 0; i < HighlighterColorPresetCount; ++i ) {
        const auto& preset = colorPresets[ static_cast<size_t>( i ) ];
        auto* button = new QToolButton( parent );
        button->setObjectName( QStringLiteral( "colorPresetButton%1" ).arg( i ) );
        button->setIcon( colorPresetIcon( preset, parent->devicePixelRatioF() ) );
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

    foreColorButton_ = new QPushButton( tr( "Text..." ), parent );
    foreColorButton_->setObjectName( QStringLiteral( "foreColorButton" ) );
    backColorButton_ = new QPushButton( tr( "Background..." ), parent );
    backColorButton_->setObjectName( QStringLiteral( "backColorButton" ) );
    auto* pickers = new QHBoxLayout;
    pickers->addWidget( foreColorButton_ );
    pickers->addWidget( backColorButton_ );
    pickers->addStretch( 1 );
    auto* colors = new QVBoxLayout;
    colors->addLayout( presets );
    colors->addLayout( pickers );
    colors_ = colors;

    wholeLine_ = new QRadioButton( tr( "The whole Log Line" ), parent );
    wholeLine_->setObjectName( QStringLiteral( "colorWholeLine" ) );
    onlyMatch_ = new QRadioButton( tr( "Only the matched text" ), parent );
    onlyMatch_->setObjectName( QStringLiteral( "colorOnlyMatch" ) );
    wholeLine_->setChecked( true );
    auto* highlight = new QHBoxLayout;
    highlight->addWidget( wholeLine_ );
    highlight->addWidget( onlyMatch_ );
    highlight->addStretch( 1 );
    highlight_ = highlight;

    connect( wholeLine_, &QRadioButton::toggled, this, &HighlighterColoring::changed );
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

    followSet( 0 );
}

bool HighlighterColoring::onlyMatch() const
{
    return onlyMatch_->isChecked();
}

void HighlighterColoring::followSet( qsizetype existingHighlighters )
{
    if ( colorsPicked_ ) {
        return;
    }
    // The soft preset a new Highlighter of a set takes, as one added in the
    // Highlighters dialog does: the one after the preset of the one before.
    const auto& preset = highlighterColorPresets()[ static_cast<size_t>(
        existingHighlighters % SoftHighlighterColorPresetCount ) ];
    setColors( preset.foreColor, preset.backColor );
}

void HighlighterColoring::setColors( const QColor& foreColor, const QColor& backColor )
{
    foreColor_ = foreColor;
    backColor_ = backColor;
    HighlighterEdit::updateIcon( foreColorButton_, foreColor_ );
    HighlighterEdit::updateIcon( backColorButton_, backColor_ );
    Q_EMIT changed();
}
