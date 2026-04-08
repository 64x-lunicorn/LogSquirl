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

#include "chartseriesdialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>

ChartSeriesDialog::ChartSeriesDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Chart Series" ) );
    setMinimumWidth( 400 );

    auto* layout = new QVBoxLayout( this );

    auto* form = new QFormLayout;

    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText( tr( "e.g. Response Time" ) );
    form->addRow( tr( "Name:" ), nameEdit_ );

    patternEdit_ = new QLineEdit;
    patternEdit_->setPlaceholderText( tr( "e.g. duration=(\\d+\\.?\\d*)ms" ) );
    form->addRow( tr( "Regex pattern:" ), patternEdit_ );

    auto* patternHint
        = new QLabel( tr( "The pattern must contain at least one capture group ().\n"
                          "The selected capture group should match a numeric value." ) );
    patternHint->setWordWrap( true );
    auto hintFont = patternHint->font();
    hintFont.setPointSize( hintFont.pointSize() - 1 );
    patternHint->setFont( hintFont );
    form->addRow( QString(), patternHint );

    captureGroupSpin_ = new QSpinBox;
    captureGroupSpin_->setMinimum( 1 );
    captureGroupSpin_->setMaximum( 20 );
    captureGroupSpin_->setValue( 1 );
    captureGroupSpin_->setToolTip( tr( "Which capture group contains the numeric value" ) );
    form->addRow( tr( "Capture group:" ), captureGroupSpin_ );

    colorButton_ = new QPushButton;
    colorButton_->setFixedSize( 60, 24 );
    colorButton_->setStyleSheet(
        QString( "background-color: %1; border: 1px solid gray;" ).arg( selectedColor_.name() ) );
    connect( colorButton_, &QPushButton::clicked, this, &ChartSeriesDialog::chooseColor );
    form->addRow( tr( "Color:" ), colorButton_ );

    layout->addLayout( form );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
    connect( buttons, &QDialogButtonBox::accepted, this, &ChartSeriesDialog::validateAndAccept );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    layout->addWidget( buttons );
}

void ChartSeriesDialog::setSeries( const ChartSeriesDefinition& def )
{
    nameEdit_->setText( def.name );
    patternEdit_->setText( def.pattern );
    captureGroupSpin_->setValue( def.captureGroup );
    selectedColor_ = def.color;
    colorButton_->setStyleSheet(
        QString( "background-color: %1; border: 1px solid gray;" ).arg( selectedColor_.name() ) );
}

ChartSeriesDefinition ChartSeriesDialog::series() const
{
    ChartSeriesDefinition def;
    def.id = QUuid::createUuid().toString();
    def.name = nameEdit_->text().trimmed();
    def.pattern = patternEdit_->text();
    def.captureGroup = captureGroupSpin_->value();
    def.color = selectedColor_;
    def.visible = true;
    def.compilePattern();
    return def;
}

void ChartSeriesDialog::chooseColor()
{
    const QColor c = QColorDialog::getColor( selectedColor_, this, tr( "Series Color" ) );
    if ( c.isValid() ) {
        selectedColor_ = c;
        colorButton_->setStyleSheet(
            QString( "background-color: %1; border: 1px solid gray;" ).arg( c.name() ) );
    }
}

void ChartSeriesDialog::validateAndAccept()
{
    if ( nameEdit_->text().trimmed().isEmpty() ) {
        QMessageBox::warning( this, tr( "Validation" ), tr( "Please enter a series name." ) );
        return;
    }

    const QRegularExpression re( patternEdit_->text() );
    if ( !re.isValid() ) {
        QMessageBox::warning( this, tr( "Validation" ),
                              tr( "Invalid regex pattern:\n%1" ).arg( re.errorString() ) );
        return;
    }

    if ( re.captureCount() < 1 ) {
        QMessageBox::warning(
            this, tr( "Validation" ),
            tr( "The pattern must contain at least one capture group ()." ) );
        return;
    }

    if ( captureGroupSpin_->value() > re.captureCount() ) {
        QMessageBox::warning(
            this, tr( "Validation" ),
            tr( "Capture group %1 does not exist. The pattern has %2 group(s)." )
                .arg( captureGroupSpin_->value() )
                .arg( re.captureCount() ) );
        return;
    }

    accept();
}
