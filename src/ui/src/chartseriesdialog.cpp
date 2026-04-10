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
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
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
    form->addRow( tr( "Regex pattern (Y):" ), patternEdit_ );

    auto* patternHint
        = new QLabel( tr( "Capture group 0 = count occurrences (Y=1 per match).\n"
                          "Otherwise the capture group should match a numeric value." ) );
    patternHint->setWordWrap( true );
    auto hintFont = patternHint->font();
    hintFont.setPointSize( hintFont.pointSize() - 1 );
    patternHint->setFont( hintFont );
    form->addRow( QString(), patternHint );

    captureGroupSpin_ = new QSpinBox;
    captureGroupSpin_->setMinimum( 0 );
    captureGroupSpin_->setMaximum( 20 );
    captureGroupSpin_->setValue( 1 );
    captureGroupSpin_->setToolTip(
        tr( "0 = count occurrences (Y=1 per match); 1+ = extract numeric value" ) );
    form->addRow( tr( "Capture group (Y):" ), captureGroupSpin_ );

    colorButton_ = new QPushButton;
    colorButton_->setFixedSize( 60, 24 );
    colorButton_->setStyleSheet(
        QString( "background-color: %1; border: 1px solid gray;" ).arg( selectedColor_.name() ) );
    connect( colorButton_, &QPushButton::clicked, this, &ChartSeriesDialog::chooseColor );
    form->addRow( tr( "Color:" ), colorButton_ );

    layout->addLayout( form );

    // X-axis configuration (optional, defaults to line number).
    xAxisGroup_ = new QGroupBox( tr( "X-Axis (custom)" ) );
    xAxisGroup_->setCheckable( true );
    xAxisGroup_->setChecked( false );
    xAxisGroup_->setToolTip(
        tr( "Extract X-axis values from log lines instead of using line numbers" ) );

    auto* xForm = new QFormLayout( xAxisGroup_ );

    xPatternEdit_ = new QLineEdit;
    xPatternEdit_->setPlaceholderText(
        tr( R"(e.g. (\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}))" ) );
    xForm->addRow( tr( "Pattern:" ), xPatternEdit_ );

    xCaptureGroupSpin_ = new QSpinBox;
    xCaptureGroupSpin_->setMinimum( 1 );
    xCaptureGroupSpin_->setMaximum( 20 );
    xCaptureGroupSpin_->setValue( 1 );
    xForm->addRow( tr( "Capture group:" ), xCaptureGroupSpin_ );

    xTimestampCheckbox_ = new QCheckBox( tr( "Parse as timestamp" ) );
    xForm->addRow( xTimestampCheckbox_ );

    xTimestampFormatEdit_ = new QLineEdit;
    xTimestampFormatEdit_->setPlaceholderText( tr( "MM-dd HH:mm:ss.zzz" ) );
    xTimestampFormatEdit_->setEnabled( false );
    xForm->addRow( tr( "Format:" ), xTimestampFormatEdit_ );

    auto* xHint
        = new QLabel( tr( "Qt date/time tokens: yyyy, MM, dd, HH, mm, ss, zzz" ) );
    xHint->setWordWrap( true );
    auto xHintFont = xHint->font();
    xHintFont.setPointSize( xHintFont.pointSize() - 1 );
    xHint->setFont( xHintFont );
    xForm->addRow( QString(), xHint );

    connect( xTimestampCheckbox_, &QCheckBox::toggled, xTimestampFormatEdit_,
             &QWidget::setEnabled );

    bucketSizeCombo_ = new QComboBox;
    bucketSizeCombo_->addItem( tr( "No aggregation" ), 0 );
    bucketSizeCombo_->addItem( tr( "100 ms" ), 100 );
    bucketSizeCombo_->addItem( tr( "500 ms" ), 500 );
    bucketSizeCombo_->addItem( tr( "1 second" ), 1000 );
    bucketSizeCombo_->addItem( tr( "5 seconds" ), 5000 );
    bucketSizeCombo_->addItem( tr( "10 seconds" ), 10000 );
    bucketSizeCombo_->addItem( tr( "30 seconds" ), 30000 );
    bucketSizeCombo_->addItem( tr( "1 minute" ), 60000 );
    bucketSizeCombo_->addItem( tr( "5 minutes" ), 300000 );
    bucketSizeCombo_->setEnabled( false );
    bucketSizeCombo_->setToolTip(
        tr( "Group data points into time buckets and sum Y values.\n"
            "Useful with count mode (Y capture group = 0) to see activity peaks." ) );
    xForm->addRow( tr( "Aggregate:" ), bucketSizeCombo_ );

    // Enable bucket combo only when timestamp parsing is active.
    connect( xTimestampCheckbox_, &QCheckBox::toggled, bucketSizeCombo_,
             &QWidget::setEnabled );

    layout->addWidget( xAxisGroup_ );

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

    if ( !def.xPattern.isEmpty() ) {
        xAxisGroup_->setChecked( true );
        xPatternEdit_->setText( def.xPattern );
        xCaptureGroupSpin_->setValue( def.xCaptureGroup );
        if ( !def.xTimestampFormat.isEmpty() ) {
            xTimestampCheckbox_->setChecked( true );
            xTimestampFormatEdit_->setText( def.xTimestampFormat );
        }
        if ( def.bucketSizeMs > 0 ) {
            const int idx
                = bucketSizeCombo_->findData( static_cast<int>( def.bucketSizeMs ) );
            if ( idx >= 0 ) {
                bucketSizeCombo_->setCurrentIndex( idx );
            }
        }
    }
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

    if ( xAxisGroup_->isChecked() && !xPatternEdit_->text().isEmpty() ) {
        def.xPattern = xPatternEdit_->text();
        def.xCaptureGroup = xCaptureGroupSpin_->value();
        if ( xTimestampCheckbox_->isChecked()
             && !xTimestampFormatEdit_->text().isEmpty() ) {
            def.xTimestampFormat = xTimestampFormatEdit_->text();
            def.bucketSizeMs
                = bucketSizeCombo_->currentData().toLongLong();
        }
    }

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

    if ( captureGroupSpin_->value() > 0 ) {
        if ( re.captureCount() < 1 ) {
            QMessageBox::warning(
                this, tr( "Validation" ),
                tr( "The pattern needs at least one capture group () for Y values.\n"
                    "Set capture group to 0 for count mode." ) );
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
    }

    if ( xAxisGroup_->isChecked() ) {
        if ( xPatternEdit_->text().isEmpty() ) {
            QMessageBox::warning( this, tr( "Validation" ),
                                  tr( "Please enter an X-axis regex pattern." ) );
            return;
        }

        const QRegularExpression xRe( xPatternEdit_->text() );
        if ( !xRe.isValid() ) {
            QMessageBox::warning(
                this, tr( "Validation" ),
                tr( "Invalid X-axis regex:\n%1" ).arg( xRe.errorString() ) );
            return;
        }

        if ( xRe.captureCount() < xCaptureGroupSpin_->value() ) {
            QMessageBox::warning(
                this, tr( "Validation" ),
                tr( "X-axis capture group %1 does not exist. Pattern has %2 group(s)." )
                    .arg( xCaptureGroupSpin_->value() )
                    .arg( xRe.captureCount() ) );
            return;
        }

        if ( xTimestampCheckbox_->isChecked()
             && xTimestampFormatEdit_->text().isEmpty() ) {
            QMessageBox::warning( this, tr( "Validation" ),
                                  tr( "Please enter a timestamp format." ) );
            return;
        }
    }

    accept();
}
