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

#include "chartwizarddialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QUuid>
#include <QVBoxLayout>

#include "charttemplategenerator.h"
#include "logformatdefinition.h"

ChartWizardDialog::ChartWizardDialog( const LogFormatDefinition* format,
                                      QWidget* parent )
    : QDialog( parent )
    , format_( format )
{
    setWindowTitle( tr( "Chart Wizard — %1" ).arg( format->title() ) );
    setMinimumWidth( 450 );

    auto* layout = new QVBoxLayout( this );

    auto* introLabel = new QLabel(
        tr( "Build a chart from the detected log format fields.\n"
            "Select what to measure (Y-Axis), what to plot against (X-Axis),\n"
            "and optionally add a filter to narrow down matching lines." ) );
    introLabel->setWordWrap( true );
    layout->addWidget( introLabel );

    layout->addSpacing( 8 );

    auto* form = new QFormLayout;

    // Name
    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText( tr( "e.g. Error Rate, Response Time" ) );
    form->addRow( tr( "Name:" ), nameEdit_ );

    // Y-Axis field selection
    yFieldCombo_ = new QComboBox;
    yFieldCombo_->setToolTip(
        tr( "Select the field to chart.\n"
            "\"Count occurrences\" counts how often lines match.\n"
            "Numeric fields extract their value." ) );
    connect( yFieldCombo_, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, &ChartWizardDialog::onYFieldChanged );
    form->addRow( tr( "Y-Axis (measure):" ), yFieldCombo_ );

    // X-Axis field selection
    xFieldCombo_ = new QComboBox;
    xFieldCombo_->setToolTip(
        tr( "Select the X-Axis.\n"
            "\"Timestamp\" uses the log's time field.\n"
            "\"Line Number\" uses sequential ordering." ) );
    form->addRow( tr( "X-Axis (over):" ), xFieldCombo_ );

    // Bucket size
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
    bucketSizeCombo_->setCurrentIndex(
        bucketSizeCombo_->findData( 1000 ) );
    bucketSizeCombo_->setToolTip(
        tr( "Group data points into time buckets and sum Y values.\n"
            "Only applies when X-Axis is Timestamp." ) );
    form->addRow( tr( "Bucket size:" ), bucketSizeCombo_ );

    // Filter regex (optional)
    filterEdit_ = new QLineEdit;
    filterEdit_->setPlaceholderText(
        tr( "Optional: e.g. error|warning (leave empty for all lines)" ) );
    filterEdit_->setToolTip(
        tr( "Additional regex filter applied before extraction.\n"
            "Only lines matching BOTH the field pattern AND this filter are counted." ) );
    form->addRow( tr( "Filter (optional):" ), filterEdit_ );

    // Color
    colorButton_ = new QPushButton;
    colorButton_->setFixedSize( 60, 24 );
    colorButton_->setStyleSheet(
        QString( "background-color: %1; border: 1px solid gray;" )
            .arg( selectedColor_.name() ) );
    connect( colorButton_, &QPushButton::clicked, this,
             &ChartWizardDialog::chooseColor );
    form->addRow( tr( "Color:" ), colorButton_ );

    layout->addLayout( form );

    // Preview label showing the generated pattern
    layout->addSpacing( 8 );
    previewLabel_ = new QLabel;
    previewLabel_->setWordWrap( true );
    auto previewFont = previewLabel_->font();
    previewFont.setPointSize( previewFont.pointSize() - 1 );
    previewLabel_->setFont( previewFont );
    layout->addWidget( previewLabel_ );

    // Buttons
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
    connect( buttons, &QDialogButtonBox::accepted, this,
             &ChartWizardDialog::validateAndAccept );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    layout->addWidget( buttons );

    populateFieldCombos();
}

void ChartWizardDialog::populateFieldCombos()
{
    // --- Y-Axis options ---
    // Always offer "Count occurrences" as first option
    yFieldCombo_->addItem( tr( "Count occurrences (all matching lines)" ),
                           QStringLiteral( "__count__" ) );

    // Add numeric fields (integer/float) as extractable values
    const auto& valueDefs = format_->valueDefinitions();
    const auto& fieldOrder = format_->valueFieldOrder();
    for ( const auto& fieldName : fieldOrder ) {
        const auto& def = valueDefs.value( fieldName );
        if ( def.hidden ) {
            continue;
        }
        if ( def.kind == "integer" || def.kind == "float" ) {
            yFieldCombo_->addItem(
                tr( "%1 (numeric value)" ).arg( fieldName ), fieldName );
        }
    }

    // Add non-numeric fields as "count occurrences of field X"
    for ( const auto& fieldName : fieldOrder ) {
        const auto& def = valueDefs.value( fieldName );
        if ( def.hidden ) {
            continue;
        }
        if ( fieldName == format_->timestampField()
             || fieldName == format_->bodyField() ) {
            continue;
        }
        if ( def.kind != "integer" && def.kind != "float" ) {
            yFieldCombo_->addItem(
                tr( "Count by field: %1" ).arg( fieldName ), fieldName );
        }
    }

    // --- X-Axis options ---
    xFieldCombo_->addItem( tr( "Timestamp (%1)" ).arg( format_->timestampField() ),
                           QStringLiteral( "__timestamp__" ) );
    xFieldCombo_->addItem( tr( "Line Number" ),
                           QStringLiteral( "__linenumber__" ) );

    // Trigger initial state
    onYFieldChanged( 0 );
}

void ChartWizardDialog::onYFieldChanged( int index )
{
    Q_UNUSED( index );

    const auto yField = yFieldCombo_->currentData().toString();
    const auto& valueDefs = format_->valueDefinitions();
    const auto& fieldDef = valueDefs.value( yField );
    const bool isNumeric = ( fieldDef.kind == "integer" || fieldDef.kind == "float" );

    // Show helpful preview
    if ( yField == "__count__" ) {
        if ( filterEdit_->text().trimmed().isEmpty() ) {
            previewLabel_->setText(
                tr( "Will count all lines matching the log format pattern." ) );
        }
        else {
            previewLabel_->setText(
                tr( "Will count lines matching: %1" )
                    .arg( filterEdit_->text() ) );
        }
    }
    else {
        if ( isNumeric ) {
            previewLabel_->setText(
                tr( "Will extract numeric value from field \"%1\"." ).arg( yField ) );
        }
        else {
            previewLabel_->setText(
                tr( "Will count occurrences where field \"%1\" is present." )
                    .arg( yField ) );
        }
    }

    // Auto-set name if empty
    if ( nameEdit_->text().trimmed().isEmpty() ) {
        if ( yField == "__count__" ) {
            nameEdit_->setText( tr( "Line Count" ) );
        }
        else {
            nameEdit_->setText( yField );
        }
    }
}

ChartSeriesDefinition ChartWizardDialog::series() const
{
    ChartSeriesDefinition def;
    def.id = QUuid::createUuid().toString();
    def.name = nameEdit_->text().trimmed();
    def.color = selectedColor_;
    def.visible = true;

    const auto yField = yFieldCombo_->currentData().toString();
    const auto xField = xFieldCombo_->currentData().toString();
    const auto filter = filterEdit_->text().trimmed();

    // Determine the base Y-pattern from the format.
    // We use the first regex pattern of the format that contains the relevant group.
    QString basePattern;
    int captureGroup = 0;

    if ( yField == "__count__" ) {
        // Count mode — match the format's first pattern (or filter if specified)
        if ( !filter.isEmpty() ) {
            // Use filter as the match pattern directly
            basePattern = filter;
            captureGroup = 0;
        }
        else {
            // Use the first format regex pattern to match all formatted lines
            const auto& patterns = format_->regexPatterns();
            if ( !patterns.isEmpty() ) {
                basePattern = patterns.constBegin().value();
            }
            captureGroup = 0;
        }
    }
    else {
        // Field-based extraction
        const auto& valueDefs = format_->valueDefinitions();
        const auto& fieldDef = valueDefs.value( yField );

        const auto pattern
            = ChartTemplateGenerator::patternContainingGroup( *format_, yField );
        if ( pattern.isEmpty() ) {
            // Fallback: use filter or first format pattern
            basePattern = filter.isEmpty() ? format_->regexPatterns().constBegin().value()
                                           : filter;
            captureGroup = 0;
        }
        else {
            if ( fieldDef.kind == "integer" || fieldDef.kind == "float" ) {
                // Numeric extraction: capture the named group's index
                basePattern = pattern;
                captureGroup
                    = ChartTemplateGenerator::namedGroupIndex( pattern, yField );
                if ( captureGroup < 1 ) {
                    captureGroup = 0; // fallback to count mode
                }
            }
            else {
                // Non-numeric field: count occurrences of the pattern
                basePattern = pattern;
                captureGroup = 0;
            }
        }

        // If a filter is specified AND we're using the format pattern,
        // wrap the base pattern with a lookahead filter.
        if ( !filter.isEmpty() && captureGroup > 0 ) {
            basePattern = QString( "(?=.*%1)%2" ).arg( filter, basePattern );
            // Recalculate capture group index since we prepended a lookahead
            // (lookaheads don't consume groups in PCRE2 when non-capturing)
            // Actually lookahead (?=...) doesn't add a capture group, so index is unchanged.
        }
        else if ( !filter.isEmpty() && captureGroup == 0 ) {
            // Count mode with filter: combine format pattern and filter
            basePattern = QString( "(?=.*%1)%2" ).arg( filter, basePattern );
        }
    }

    def.pattern = basePattern;
    def.captureGroup = captureGroup;

    // Configure X-axis
    if ( xField == "__timestamp__" ) {
        // Use the format's timestamp field for X-axis.
        const auto& tsField = format_->timestampField();
        const auto xPattern
            = ChartTemplateGenerator::patternContainingGroup( *format_, tsField );
        if ( !xPattern.isEmpty() ) {
            const int tsGroupIdx
                = ChartTemplateGenerator::namedGroupIndex( xPattern, tsField );
            if ( tsGroupIdx >= 1 ) {
                def.xPattern = xPattern;
                def.xCaptureGroup = tsGroupIdx;

                // Find Qt timestamp format
                for ( const auto& fmt : format_->timestampFormats() ) {
                    const auto qtFmt
                        = ChartTemplateGenerator::strftimeToQtFormat( fmt );
                    if ( !qtFmt.isEmpty() ) {
                        def.xTimestampFormat = qtFmt;
                        break;
                    }
                }

                def.bucketSizeMs
                    = bucketSizeCombo_->currentData().toLongLong();
            }
        }
    }
    // else: xField == "__linenumber__" → no custom X-axis (default behavior)

    def.compilePattern();
    return def;
}

void ChartWizardDialog::chooseColor()
{
    const QColor c
        = QColorDialog::getColor( selectedColor_, this, tr( "Series Color" ) );
    if ( c.isValid() ) {
        selectedColor_ = c;
        colorButton_->setStyleSheet(
            QString( "background-color: %1; border: 1px solid gray;" )
                .arg( c.name() ) );
    }
}

void ChartWizardDialog::validateAndAccept()
{
    if ( nameEdit_->text().trimmed().isEmpty() ) {
        QMessageBox::warning( this, tr( "Validation" ),
                              tr( "Please enter a series name." ) );
        return;
    }

    // Validate filter regex if provided
    const auto filter = filterEdit_->text().trimmed();
    if ( !filter.isEmpty() ) {
        const QRegularExpression re( filter );
        if ( !re.isValid() ) {
            QMessageBox::warning(
                this, tr( "Validation" ),
                tr( "Invalid filter regex:\n%1" ).arg( re.errorString() ) );
            return;
        }
    }

    // Build and validate the final pattern
    const auto def = series();
    if ( def.pattern.isEmpty() ) {
        QMessageBox::warning( this, tr( "Validation" ),
                              tr( "Could not build a valid pattern from the selected fields." ) );
        return;
    }

    const QRegularExpression re( def.pattern );
    if ( !re.isValid() ) {
        QMessageBox::warning(
            this, tr( "Validation" ),
            tr( "The generated regex pattern is invalid:\n%1\n\nPattern: %2" )
                .arg( re.errorString(), def.pattern ) );
        return;
    }

    accept();
}
