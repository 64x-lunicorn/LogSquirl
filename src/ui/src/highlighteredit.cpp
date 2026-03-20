/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "highlighteredit.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <qcolor.h>

HighlighterEdit::HighlighterEdit( Highlighter defaultHighlighter, QWidget* parent )
    : QWidget( parent )
    , defaultHighlighter_( std::move( defaultHighlighter ) )
{
    setupUi( this );

    QStringList regexpTypes;
    regexpTypes << tr( "Extended Regexp" ) << tr( "Fixed Strings" );
    patternTypeComboBox->addItems( regexpTypes );

    reset();

    connect( patternEdit, &QLineEdit::textEdited, this, &HighlighterEdit::setPattern );
    connect( ignoreCaseCheckBox, &QCheckBox::toggled, this, &HighlighterEdit::setIgnoreCase );
    connect( onlyMatchCheckBox, &QCheckBox::toggled, this,
             &HighlighterEdit::setHighlightOnlyMatch );
    connect( variateColorsCheckBox, &QCheckBox::toggled, this, &HighlighterEdit::setVariateColors );
    connect( variationSpinBox, QOverload<int>::of( &QSpinBox::valueChanged ), this,
             &HighlighterEdit::setColorVariance );

    connect( foreColorButton, &QPushButton::clicked, this, &HighlighterEdit::changeForeColor );
    connect( backColorButton, &QPushButton::clicked, this, &HighlighterEdit::changeBackColor );
    connect( patternTypeComboBox, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &HighlighterEdit::setPatternType );
}

void HighlighterEdit::reset()
{
    patternEdit->clear();
    patternEdit->setEnabled( false );
    patternTypeComboBox->setEnabled(false);

    ignoreCaseCheckBox->setEnabled( false );
    onlyMatchCheckBox->setEnabled( false );
    foreColorButton->setEnabled( false );
    backColorButton->setEnabled( false );

    ignoreCaseCheckBox->setChecked( defaultHighlighter_.ignoreCase() );
    onlyMatchCheckBox->setChecked( defaultHighlighter_.highlightOnlyMatch() );
    variateColorsCheckBox->setChecked( defaultHighlighter_.variateColors() );
    variationSpinBox->setValue( defaultHighlighter_.colorVariance() );

    updateIcon( foreColorButton, defaultHighlighter_.foreColor() );
    updateIcon( backColorButton, defaultHighlighter_.backColor() );

    if ( defaultHighlighter_.useRegex() ) {
        patternTypeComboBox->setCurrentIndex( 0 );
    }
}

void HighlighterEdit::setHighlighter( Highlighter highlighter )
{
    highlighter_ = std::move( highlighter );

    patternEdit->setText( highlighter_.pattern() );
    ignoreCaseCheckBox->setChecked( highlighter_.ignoreCase() );
    onlyMatchCheckBox->setChecked( highlighter_.highlightOnlyMatch() );

    variateColorsCheckBox->setChecked( highlighter_.variateColors() );
    variationSpinBox->setValue( highlighter_.colorVariance() );

    updateIcon( foreColorButton, highlighter_.foreColor() );
    updateIcon( backColorButton, highlighter_.backColor() );

    patternEdit->setEnabled( true );
    patternTypeComboBox->setEnabled( true );
    ignoreCaseCheckBox->setEnabled( true );
    onlyMatchCheckBox->setEnabled( true );
    foreColorButton->setEnabled( true );
    backColorButton->setEnabled( true );

    variateColorsCheckBox->setEnabled( highlighter_.highlightOnlyMatch() );
    variationSpinBox->setEnabled( highlighter_.highlightOnlyMatch() );

    if ( highlighter.useRegex() ) {
        patternTypeComboBox->setCurrentIndex( 0 );
    }
    else {
        patternTypeComboBox->setCurrentIndex( 1 );
    }
}

Highlighter HighlighterEdit::highlighter() const
{
    return highlighter_;
}

void HighlighterEdit::setPattern( const QString& pattern )
{
    highlighter_.setPattern( pattern );
    Q_EMIT changed();
}

void HighlighterEdit::setIgnoreCase( bool ignoreCase )
{
    highlighter_.setIgnoreCase( ignoreCase );
    Q_EMIT changed();
}

void HighlighterEdit::setHighlightOnlyMatch( bool onlyMatch )
{
    highlighter_.setHighlightOnlyMatch( onlyMatch );
    variateColorsCheckBox->setEnabled( onlyMatch );
    variationSpinBox->setEnabled( onlyMatch );
    Q_EMIT changed();
}

void HighlighterEdit::setVariateColors( bool variateColors )
{
    highlighter_.setVariateColors( variateColors );
    highlighter_.setColorVariance( variationSpinBox->value() );
    Q_EMIT changed();
}

void HighlighterEdit::setColorVariance( int colorVariance )
{
    highlighter_.setColorVariance( colorVariance );
    Q_EMIT changed();
}

void HighlighterEdit::changeForeColor()
{
    QColor new_color;
    if ( showColorPicker( highlighter_.foreColor(), new_color ) ) {
        highlighter_.setForeColor( new_color );
        updateIcon( foreColorButton, highlighter_.foreColor() );
        Q_EMIT changed();
    }
}

void HighlighterEdit::changeBackColor()
{
    QColor new_color;
    if ( showColorPicker( highlighter_.backColor(), new_color ) ) {
        highlighter_.setBackColor( new_color );
        updateIcon( backColorButton, highlighter_.backColor() );
        Q_EMIT changed();
    }
}

void HighlighterEdit::updateIcon( QPushButton* button, const QColor& color )
{
    QPixmap pixmap( 20, 10 );
    pixmap.fill( color );
    button->setIcon( QIcon( pixmap ) );
}

bool HighlighterEdit::showColorPicker( const QColor& in, QColor& out )
{
    QColorDialog dialog;

    // non native dialog ensures they will have a default
    // set of colors to pick from in a pallette. For example,
    // on some linux desktops, the basic palette is missing
    dialog.setOption( QColorDialog::DontUseNativeDialog, true );
    dialog.setOption( QColorDialog::ShowAlphaChannel, true );

    dialog.setModal( true );
    dialog.setCurrentColor( in );
    dialog.exec();
    out = dialog.currentColor();

    return ( dialog.result() == QDialog::Accepted );
}

void HighlighterEdit::setPatternType( int index )
{
    highlighter_.setUseRegex( index == 0 );
    Q_EMIT changed();
}
