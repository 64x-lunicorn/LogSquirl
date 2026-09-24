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

#include "chartpanel.h"

#include <algorithm>

#include <QComboBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>

#include "chartseriesdialog.h"
#include "charttemplategenerator.h"
#include "chartwizarddialog.h"
#include "configuration.h"
#include "logdata.h"
#include "logformatdefinition.h"

ChartPanel::ChartPanel( QWidget* parent )
    : QWidget( parent )
{
    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    // Toolbar with series management actions.
    toolBar_ = new QToolBar;
    toolBar_->setIconSize( QSize( 16, 16 ) );

    addAction_ = toolBar_->addAction( tr( "+ Add Series" ) );
    addAction_->setToolTip( tr( "Add a new chart series" ) );
    connect( addAction_, &QAction::triggered, this, &ChartPanel::addSeries );

    wizardAction_ = toolBar_->addAction( tr( "Wizard" ) );
    wizardAction_->setToolTip(
        tr( "Guided chart builder — pick fields from the detected log format" ) );
    wizardAction_->setVisible( false );
    connect( wizardAction_, &QAction::triggered, this, &ChartPanel::addSeriesWizard );

    // Format-aware quick-add templates (hidden until a format is set).
    templatesMenu_ = new QMenu( this );
    templatesButton_ = new QToolButton;
    templatesButton_->setText( tr( "Templates" ) );
    templatesButton_->setToolTip(
        tr( "Add pre-configured chart series from the detected log format" ) );
    templatesButton_->setPopupMode( QToolButton::InstantPopup );
    templatesButton_->setMenu( templatesMenu_ );
    templatesButton_->setVisible( false );
    toolBar_->addWidget( templatesButton_ );

    seriesCombo_ = new QComboBox;
    seriesCombo_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    seriesCombo_->setToolTip( tr( "Select series to edit or remove" ) );
    connect( seriesCombo_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &ChartPanel::onSeriesComboChanged );
    toolBar_->addWidget( seriesCombo_ );

    editAction_ = toolBar_->addAction( tr( "Edit" ) );
    editAction_->setToolTip( tr( "Edit selected series" ) );
    editAction_->setEnabled( false );
    connect( editAction_, &QAction::triggered, this, &ChartPanel::editSeries );

    removeAction_ = toolBar_->addAction( tr( "Remove" ) );
    removeAction_->setToolTip( tr( "Remove selected series" ) );
    removeAction_->setEnabled( false );
    connect( removeAction_, &QAction::triggered, this, &ChartPanel::removeSeries );

    toolBar_->addSeparator();

    fitAction_ = toolBar_->addAction( tr( "Fit" ) );
    fitAction_->setToolTip( tr( "Fit chart to data bounds" ) );
    connect( fitAction_, &QAction::triggered, this, &ChartPanel::fitView );

    toolBar_->addSeparator();

    savePresetAction_ = toolBar_->addAction( tr( "Save Preset" ) );
    savePresetAction_->setToolTip( tr( "Save current series as a reusable preset" ) );
    connect( savePresetAction_, &QAction::triggered, this, &ChartPanel::savePreset );

    loadPresetAction_ = toolBar_->addAction( tr( "Load Preset" ) );
    loadPresetAction_->setToolTip( tr( "Load a saved preset" ) );
    connect( loadPresetAction_, &QAction::triggered, this, &ChartPanel::loadPreset );

    deletePresetAction_ = toolBar_->addAction( tr( "Delete Preset" ) );
    deletePresetAction_->setToolTip( tr( "Delete a saved preset" ) );
    connect( deletePresetAction_, &QAction::triggered, this, &ChartPanel::deletePreset );

    toolBar_->addSeparator();

    exportPresetAction_ = toolBar_->addAction( tr( "Export…" ) );
    exportPresetAction_->setToolTip( tr( "Export current series to a JSON file" ) );
    connect( exportPresetAction_, &QAction::triggered, this, &ChartPanel::exportPreset );

    importPresetAction_ = toolBar_->addAction( tr( "Import…" ) );
    importPresetAction_->setToolTip( tr( "Import series from a JSON file" ) );
    connect( importPresetAction_, &QAction::triggered, this, &ChartPanel::importPreset );

    layout->addWidget( toolBar_ );

    // Progress bar shown during async extraction.
    progressBar_ = new QProgressBar;
    progressBar_->setTextVisible( true );
    progressBar_->setFormat( tr( "Extracting chart data… %p%" ) );
    progressBar_->setMaximumHeight( 16 );
    progressBar_->setVisible( false );
    layout->addWidget( progressBar_ );

    // Chart rendering area.
    chartWidget_ = new ChartWidget;
    connect( chartWidget_, &ChartWidget::lineSelected, this, &ChartPanel::lineSelected );
    layout->addWidget( chartWidget_, 1 );

    connect( &extraction_, &ChartExtraction::started, this, &ChartPanel::onExtractionStarted );
    connect( &extraction_, &ChartExtraction::extracted, this, &ChartPanel::onExtracted );

    progressTimer_.setInterval( 100 );
    connect( &progressTimer_, &QTimer::timeout, this, &ChartPanel::showProgress );
}

ChartPanel::~ChartPanel() = default;

void ChartPanel::setLogData( const std::shared_ptr<LogData>& logData )
{
    logData_ = logData;
    extraction_.setLogData( logData );
}

void ChartPanel::setLogFormat( const LogFormatDefinition* format )
{
    format_ = format;
    wizardAction_->setVisible( format_ != nullptr );
    rebuildTemplatesMenu();
}

void ChartPanel::extractData()
{
    if ( !logData_ || series_.isEmpty() ) {
        return;
    }

    extraction_.update();
}

void ChartPanel::logFileTruncated()
{
    extraction_.restart();
}

void ChartPanel::setUpdateDelay( std::chrono::milliseconds delay )
{
    extraction_.setUpdateDelay( delay );
}

// ---------------------------------------------------------------------------
// Async extraction
// ---------------------------------------------------------------------------

void ChartPanel::seriesChanged()
{
    extraction_.setSeries( series_ );
    extractData();
    showProgress();
}

void ChartPanel::onExtractionStarted()
{
    // Appending a few Log Lines is extracted in no time: showing progress for
    // it would only flicker.
    constexpr uint64_t linesWorthProgress = 100'000;
    if ( extraction_.linesToExtract().get() >= linesWorthProgress ) {
        progressBar_->setRange( 0, 100 );
        progressBar_->setValue( 0 );
        progressBar_->setVisible( true );
        progressTimer_.start();
    }
}

void ChartPanel::showProgress()
{
    if ( !extraction_.isExtracting() ) {
        progressTimer_.stop();
        progressBar_->setVisible( false );
        return;
    }
    progressBar_->setValue( std::min( extraction_.progress(), 99 ) );
}

void ChartPanel::onExtracted( bool fromStart )
{
    progressTimer_.stop();
    progressBar_->setVisible( false );

    for ( qsizetype i = 0; i < series_.size(); ++i ) {
        series_[ i ].points = extraction_.points( i );
    }

    // Only an extraction from the first Log Line may have other series or
    // other points for the Log Lines extracted before; an incremental one only
    // appends, so it leaves a view the user zoomed or panned to alone.
    chartWidget_->setSeriesList( series_, fromStart ? ChartWidget::Change::Series
                                                    : ChartWidget::Change::AppendedPoints );
}

QVector<ChartSeriesDefinition> ChartPanel::seriesDefinitions() const
{
    return series_;
}

void ChartPanel::setSeriesDefinitions( const QVector<ChartSeriesDefinition>& defs )
{
    series_ = defs;
    for ( auto& s : series_ ) {
        s.compilePattern();
    }
    rebuildSeriesCombo();
    extraction_.setSeries( series_ );
}

void ChartPanel::addFilterFrequencySeries( const QStringList& patterns, bool matchCase )
{
    // Predefined palette for auto-assigned colors.
    static const QColor palette[] = {
        QColor( "#e6194b" ), QColor( "#3cb44b" ), QColor( "#4363d8" ), QColor( "#f58231" ),
        QColor( "#911eb4" ), QColor( "#42d4f4" ), QColor( "#f032e6" ), QColor( "#bfef45" ),
        QColor( "#fabebe" ), QColor( "#469990" ),
    };
    constexpr int paletteSize = sizeof( palette ) / sizeof( palette[ 0 ] );

    auto colorIdx = static_cast<int>( series_.size() );
    for ( const auto& pat : patterns ) {
        if ( pat.trimmed().isEmpty() ) {
            continue;
        }
        ChartSeriesDefinition def;
        def.id = QUuid::createUuid().toString( QUuid::WithoutBraces );
        def.name = tr( "Filter: %1" ).arg( pat );
        def.color = palette[ colorIdx % paletteSize ];
        def.pattern = pat;
        def.captureGroup = 0; // count mode: Y = 1 per match
        def.matchCase = matchCase;
        def.compilePattern();
        if ( def.compiledRegex.isValid() ) {
            series_.append( def );
            colorIdx++;
        }
    }
    rebuildSeriesCombo();
    seriesChanged();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ChartPanel::addSeries()
{
    ChartSeriesDialog dlg( this );
    dlg.setFormatDefaults( format_ );
    if ( dlg.exec() == QDialog::Accepted ) {
        series_.append( dlg.series() );
        rebuildSeriesCombo();
        seriesChanged();
    }
}

void ChartPanel::addSeriesWizard()
{
    if ( !format_ ) {
        return;
    }

    ChartWizardDialog dlg( format_, this );
    if ( dlg.exec() == QDialog::Accepted ) {
        series_.append( dlg.series() );
        rebuildSeriesCombo();
        seriesChanged();
    }
}

void ChartPanel::editSeries()
{
    const int idx = seriesCombo_->currentIndex();
    if ( idx < 0 || idx >= series_.size() ) {
        return;
    }

    ChartSeriesDialog dlg( this );
    dlg.setSeries( series_[ idx ] );
    if ( dlg.exec() == QDialog::Accepted ) {
        auto updated = dlg.series();
        // Preserve the original ID, and the Match case the dialog does not
        // show.
        updated.id = series_[ idx ].id;
        updated.matchCase = series_[ idx ].matchCase;
        updated.compilePattern();
        series_[ idx ] = std::move( updated );
        rebuildSeriesCombo();
        seriesChanged();
    }
}

void ChartPanel::removeSeries()
{
    const int idx = seriesCombo_->currentIndex();
    if ( idx < 0 || idx >= series_.size() ) {
        return;
    }
    series_.removeAt( idx );
    rebuildSeriesCombo();
    seriesChanged();
}

void ChartPanel::fitView()
{
    chartWidget_->fitView();
}

void ChartPanel::onSeriesComboChanged( int index )
{
    const bool valid = ( index >= 0 && index < series_.size() );
    editAction_->setEnabled( valid );
    removeAction_->setEnabled( valid );
}

void ChartPanel::rebuildSeriesCombo()
{
    seriesCombo_->blockSignals( true );
    seriesCombo_->clear();
    for ( const auto& s : series_ ) {
        seriesCombo_->addItem( s.name );
    }
    seriesCombo_->blockSignals( false );

    const bool hasSeries = !series_.isEmpty();
    editAction_->setEnabled( hasSeries );
    removeAction_->setEnabled( hasSeries );
    if ( hasSeries ) {
        seriesCombo_->setCurrentIndex( 0 );
    }
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

// Serialize current series definitions to a JSON string.
static QString seriesToJsonString( const QVector<ChartSeriesDefinition>& defs )
{
    QJsonArray arr;
    for ( const auto& d : defs ) {
        arr.append( d.toJson() );
    }
    return QString::fromUtf8( QJsonDocument( arr ).toJson( QJsonDocument::Indented ) );
}

// Deserialize series definitions from a JSON string.
static QVector<ChartSeriesDefinition> seriesFromJsonString( const QString& json )
{
    QVector<ChartSeriesDefinition> result;
    const auto doc = QJsonDocument::fromJson( json.toUtf8() );
    if ( !doc.isArray() ) {
        return result;
    }
    for ( const auto& val : doc.array() ) {
        auto def = ChartSeriesDefinition::fromJson( val.toObject() );
        def.compilePattern();
        result.append( def );
    }
    return result;
}

void ChartPanel::savePreset()
{
    if ( series_.isEmpty() ) {
        QMessageBox::information( this, tr( "Save Preset" ), tr( "No series defined to save." ) );
        return;
    }

    bool ok = false;
    const auto name = QInputDialog::getText( this, tr( "Save Chart Preset" ), tr( "Preset name:" ),
                                             QLineEdit::Normal, {}, &ok );
    if ( !ok || name.trimmed().isEmpty() ) {
        return;
    }

    auto& config = Configuration::get();
    config.setChartPreset( name.trimmed(), seriesToJsonString( series_ ) );
    config.save();
}

void ChartPanel::loadPreset()
{
    const auto& config = Configuration::get();
    const auto presets = config.chartPresets();
    if ( presets.isEmpty() ) {
        QMessageBox::information( this, tr( "Load Preset" ), tr( "No presets saved yet." ) );
        return;
    }

    bool ok = false;
    const auto name = QInputDialog::getItem(
        this, tr( "Load Chart Preset" ), tr( "Select preset:" ), presets.keys(), 0, false, &ok );
    if ( !ok || name.isEmpty() ) {
        return;
    }

    auto defs = seriesFromJsonString( presets.value( name ) );
    if ( defs.isEmpty() ) {
        return;
    }
    series_ = defs;
    rebuildSeriesCombo();
    seriesChanged();
}

void ChartPanel::deletePreset()
{
    auto& config = Configuration::get();
    const auto presets = config.chartPresets();
    if ( presets.isEmpty() ) {
        QMessageBox::information( this, tr( "Delete Preset" ), tr( "No presets saved yet." ) );
        return;
    }

    bool ok = false;
    const auto name
        = QInputDialog::getItem( this, tr( "Delete Chart Preset" ),
                                 tr( "Select preset to delete:" ), presets.keys(), 0, false, &ok );
    if ( !ok || name.isEmpty() ) {
        return;
    }

    config.removeChartPreset( name );
    config.save();
}

void ChartPanel::exportPreset()
{
    if ( series_.isEmpty() ) {
        QMessageBox::information( this, tr( "Export" ), tr( "No series defined to export." ) );
        return;
    }

    const auto path = QFileDialog::getSaveFileName( this, tr( "Export Chart Preset" ), {},
                                                    tr( "JSON files (*.json)" ) );
    if ( path.isEmpty() ) {
        return;
    }

    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
        QMessageBox::warning( this, tr( "Export" ), tr( "Cannot write to %1" ).arg( path ) );
        return;
    }
    // Surface I/O failures (disk full, permission errors) to the user so a corrupt or
    // truncated preset file is not silently produced.
    const auto jsonData = seriesToJsonString( series_ ).toUtf8();
    if ( file.write( jsonData ) != jsonData.size() ) {
        QMessageBox::warning( this, tr( "Export" ),
                              tr( "Failed to write all data to %1" ).arg( path ) );
    }
}

void ChartPanel::importPreset()
{
    const auto path = QFileDialog::getOpenFileName( this, tr( "Import Chart Preset" ), {},
                                                    tr( "JSON files (*.json)" ) );
    if ( path.isEmpty() ) {
        return;
    }

    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        QMessageBox::warning( this, tr( "Import" ), tr( "Cannot read %1" ).arg( path ) );
        return;
    }

    const auto json = QString::fromUtf8( file.readAll() );
    auto defs = seriesFromJsonString( json );
    if ( defs.isEmpty() ) {
        QMessageBox::warning( this, tr( "Import" ),
                              tr( "No valid series found in %1" ).arg( path ) );
        return;
    }

    series_.append( defs );
    rebuildSeriesCombo();
    seriesChanged();
}

// ---------------------------------------------------------------------------
// Format-aware templates
// ---------------------------------------------------------------------------

void ChartPanel::rebuildTemplatesMenu()
{
    templatesMenu_->clear();

    if ( !format_ ) {
        templatesButton_->setVisible( false );
        return;
    }

    templatesButton_->setVisible( true );
    templatesButton_->setText( tr( "Templates (%1)" ).arg( format_->title() ) );

    // --- Log Level Distribution ---
    const auto& levelMappings = format_->levelMappings();
    if ( !levelMappings.isEmpty() ) {
        auto* levelMenu = templatesMenu_->addMenu( tr( "Log Level Distribution" ) );

        levelMenu->addAction( tr( "All Levels (1 s buckets)" ), this, [ this ]() {
            addTemplateSeries( ChartTemplateGenerator::levelFrequencyTemplates( *format_, 1000 ) );
        } );
        levelMenu->addAction( tr( "All Levels (5 s buckets)" ), this, [ this ]() {
            addTemplateSeries( ChartTemplateGenerator::levelFrequencyTemplates( *format_, 5000 ) );
        } );
        levelMenu->addAction( tr( "All Levels (1 min buckets)" ), this, [ this ]() {
            addTemplateSeries( ChartTemplateGenerator::levelFrequencyTemplates( *format_, 60000 ) );
        } );

        levelMenu->addSeparator();

        // Individual levels
        static const QStringList kLevelOrder
            = { "fatal", "critical", "error", "warning", "notice", "info", "debug", "trace" };

        for ( const auto& level : kLevelOrder ) {
            if ( !levelMappings.contains( level ) ) {
                continue;
            }
            levelMenu->addAction( tr( "%1 only" ).arg( level ), this, [ this, level ]() {
                auto all = ChartTemplateGenerator::levelFrequencyTemplates( *format_, 1000 );
                QVector<ChartSeriesDefinition> filtered;
                const auto target = QObject::tr( "Level: %1" ).arg( level );
                for ( const auto& s : all ) {
                    if ( s.name == target ) {
                        filtered.append( s );
                    }
                }
                addTemplateSeries( filtered );
            } );
        }
    }

    // --- Message Rate ---
    {
        auto* rateMenu = templatesMenu_->addMenu( tr( "Message Rate" ) );
        rateMenu->addAction( tr( "per Second" ), this, [ this ]() {
            addTemplateSeries( ChartTemplateGenerator::messageRateTemplates( *format_, 1000 ) );
        } );
        rateMenu->addAction( tr( "per 5 Seconds" ), this, [ this ]() {
            addTemplateSeries( ChartTemplateGenerator::messageRateTemplates( *format_, 5000 ) );
        } );
        rateMenu->addAction( tr( "per 10 Seconds" ), this, [ this ]() {
            addTemplateSeries( ChartTemplateGenerator::messageRateTemplates( *format_, 10000 ) );
        } );
        rateMenu->addAction( tr( "per Minute" ), this, [ this ]() {
            addTemplateSeries( ChartTemplateGenerator::messageRateTemplates( *format_, 60000 ) );
        } );
    }

    // --- Numeric Fields ---
    {
        const auto numericDefs = ChartTemplateGenerator::numericFieldTemplates( *format_ );
        if ( !numericDefs.isEmpty() ) {
            auto* numMenu = templatesMenu_->addMenu( tr( "Numeric Fields" ) );
            for ( const auto& def : numericDefs ) {
                numMenu->addAction( def.name, this,
                                    [ this, def ]() { addTemplateSeries( { def } ); } );
            }
        }
    }

    // --- Field Occurrence ---
    {
        const auto fieldDefs = ChartTemplateGenerator::fieldOccurrenceTemplates( *format_, 1000 );
        if ( !fieldDefs.isEmpty() ) {
            auto* fieldMenu = templatesMenu_->addMenu( tr( "Field Occurrence" ) );
            for ( const auto& def : fieldDefs ) {
                fieldMenu->addAction( def.name, this,
                                      [ this, def ]() { addTemplateSeries( { def } ); } );
            }
        }
    }
}

void ChartPanel::addTemplateSeries( const QVector<ChartSeriesDefinition>& defs )
{
    for ( const auto& def : defs ) {
        if ( def.compiledRegex.isValid() ) {
            series_.append( def );
        }
    }
    rebuildSeriesCombo();
    seriesChanged();
}
