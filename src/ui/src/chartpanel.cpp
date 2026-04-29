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

#include <cmath>

#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>

#include "chartseriesdialog.h"
#include "configuration.h"
#include "logdata.h"

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

    // Chart rendering area.
    chartWidget_ = new ChartWidget;
    connect( chartWidget_, &ChartWidget::lineSelected, this, &ChartPanel::lineSelected );
    layout->addWidget( chartWidget_, 1 );
}

void ChartPanel::setLogData( const std::shared_ptr<LogData>& logData )
{
    logData_ = logData;
}

void ChartPanel::extractData()
{
    if ( !logData_ ) {
        return;
    }

    const auto totalLines = logData_->getNbLine();

    // Process in batches to avoid creating huge temporary vectors.
    constexpr uint64_t batchSize = 5000;
    for ( auto& s : series_ ) {
        s.points.clear();
        if ( !s.compiledRegex.isValid() ) {
            continue;
        }
    }

    for ( uint64_t start = 0; start < totalLines.get(); start += batchSize ) {
        const auto count = std::min( batchSize, totalLines.get() - start );
        const auto lines
            = logData_->getExpandedLines( LineNumber( start ), LinesCount( count ) );

        for ( uint64_t i = 0; i < static_cast<uint64_t>( lines.size() ); ++i ) {
            const auto lineNum = LineNumber( start + i );
            const auto& lineText = lines[ static_cast<size_t>( i ) ];

            for ( auto& s : series_ ) {
                if ( !s.compiledRegex.isValid() ) {
                    continue;
                }
                const auto match = s.compiledRegex.match( lineText );
                if ( !match.hasMatch() ) {
                    continue;
                }

                // Extract Y value; captureGroup 0 = count mode (Y=1).
                double yVal = 1.0;
                if ( s.captureGroup > 0
                     && match.lastCapturedIndex() >= s.captureGroup ) {
                    bool ok = false;
                    yVal = match.captured( s.captureGroup ).toDouble( &ok );
                    if ( !ok ) {
                        yVal = 1.0;
                    }
                }

                // Extract X value (line number or custom regex).
                double xVal = static_cast<double>( lineNum.get() );
                QString xLabel;

                if ( s.hasCustomXAxis() && s.compiledXRegex.isValid() ) {
                    const auto xMatch = s.compiledXRegex.match( lineText );
                    if ( xMatch.hasMatch()
                         && xMatch.lastCapturedIndex() >= s.xCaptureGroup ) {
                        const auto captured
                            = xMatch.captured( s.xCaptureGroup );
                        if ( s.isTimestampXAxis() ) {
                            auto dt = QDateTime::fromString(
                                captured, s.xTimestampFormat );
                            if ( dt.isValid() ) {
                                if ( dt.date().year() < 1970 ) {
                                    dt.setDate( QDate(
                                        QDate::currentDate().year(),
                                        dt.date().month(),
                                        dt.date().day() ) );
                                }
                                xVal = static_cast<double>(
                                    dt.toMSecsSinceEpoch() );
                                xLabel = captured;
                            }
                        }
                        else {
                            bool ok = false;
                            const auto numVal
                                = captured.toDouble( &ok );
                            if ( ok ) {
                                xVal = numVal;
                            }
                        }
                    }
                }

                s.points.append( { lineNum, xVal, yVal, xLabel } );
            }
        }
    }

    // Aggregate into time buckets where configured.
    for ( auto& s : series_ ) {
        if ( !s.isBucketed() || s.points.isEmpty() ) {
            continue;
        }

        const auto bucket = static_cast<double>( s.bucketSizeMs );
        QVector<ChartPoint> bucketed;

        // Points are already in log-line order (i.e. ascending xValue).
        double bucketStart
            = std::floor( s.points.first().xValue / bucket ) * bucket;
        double bucketSum = 0.0;
        LineNumber bucketLine = s.points.first().line;

        for ( const auto& pt : s.points ) {
            const double ptBucket
                = std::floor( pt.xValue / bucket ) * bucket;
            if ( ptBucket != bucketStart ) {
                // Flush the previous bucket.
                const double mid = bucketStart + bucket / 2.0;
                const auto dt = QDateTime::fromMSecsSinceEpoch(
                    static_cast<qint64>( mid ) );
                bucketed.append( { bucketLine, mid, bucketSum,
                                   dt.toString( "HH:mm:ss" ) } );
                bucketStart = ptBucket;
                bucketSum = 0.0;
                bucketLine = pt.line;
            }
            bucketSum += pt.value;
        }
        // Flush last bucket.
        const double mid = bucketStart + bucket / 2.0;
        const auto dt
            = QDateTime::fromMSecsSinceEpoch( static_cast<qint64>( mid ) );
        bucketed.append(
            { bucketLine, mid, bucketSum, dt.toString( "HH:mm:ss" ) } );

        s.points = bucketed;
    }

    chartWidget_->setSeriesList( series_ );
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
}

void ChartPanel::addFilterFrequencySeries( const QStringList& patterns )
{
    // Predefined palette for auto-assigned colors.
    static const QColor palette[] = {
        QColor( "#e6194b" ), QColor( "#3cb44b" ), QColor( "#4363d8" ),
        QColor( "#f58231" ), QColor( "#911eb4" ), QColor( "#42d4f4" ),
        QColor( "#f032e6" ), QColor( "#bfef45" ), QColor( "#fabebe" ),
        QColor( "#469990" ),
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
        def.captureGroup = 0;  // count mode: Y = 1 per match
        def.compilePattern();
        if ( def.compiledRegex.isValid() ) {
            series_.append( def );
            colorIdx++;
        }
    }
    rebuildSeriesCombo();
    extractData();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ChartPanel::addSeries()
{
    ChartSeriesDialog dlg( this );
    if ( dlg.exec() == QDialog::Accepted ) {
        series_.append( dlg.series() );
        rebuildSeriesCombo();
        extractData();
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
        // Preserve the original ID.
        updated.id = series_[ idx ].id;
        series_[ idx ] = updated;
        rebuildSeriesCombo();
        extractData();
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
    extractData();
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
        QMessageBox::information( this, tr( "Save Preset" ),
                                  tr( "No series defined to save." ) );
        return;
    }

    bool ok = false;
    const auto name = QInputDialog::getText( this, tr( "Save Chart Preset" ),
                                             tr( "Preset name:" ), QLineEdit::Normal, {}, &ok );
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
        QMessageBox::information( this, tr( "Load Preset" ),
                                  tr( "No presets saved yet." ) );
        return;
    }

    bool ok = false;
    const auto name = QInputDialog::getItem( this, tr( "Load Chart Preset" ),
                                             tr( "Select preset:" ), presets.keys(), 0, false,
                                             &ok );
    if ( !ok || name.isEmpty() ) {
        return;
    }

    auto defs = seriesFromJsonString( presets.value( name ) );
    if ( defs.isEmpty() ) {
        return;
    }
    series_ = defs;
    rebuildSeriesCombo();
    extractData();
}

void ChartPanel::deletePreset()
{
    auto& config = Configuration::get();
    const auto presets = config.chartPresets();
    if ( presets.isEmpty() ) {
        QMessageBox::information( this, tr( "Delete Preset" ),
                                  tr( "No presets saved yet." ) );
        return;
    }

    bool ok = false;
    const auto name = QInputDialog::getItem( this, tr( "Delete Chart Preset" ),
                                             tr( "Select preset to delete:" ), presets.keys(), 0,
                                             false, &ok );
    if ( !ok || name.isEmpty() ) {
        return;
    }

    config.removeChartPreset( name );
    config.save();
}

void ChartPanel::exportPreset()
{
    if ( series_.isEmpty() ) {
        QMessageBox::information( this, tr( "Export" ),
                                  tr( "No series defined to export." ) );
        return;
    }

    const auto path = QFileDialog::getSaveFileName( this, tr( "Export Chart Preset" ), {},
                                                    tr( "JSON files (*.json)" ) );
    if ( path.isEmpty() ) {
        return;
    }

    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
        QMessageBox::warning( this, tr( "Export" ),
                              tr( "Cannot write to %1" ).arg( path ) );
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
        QMessageBox::warning( this, tr( "Import" ),
                              tr( "Cannot read %1" ).arg( path ) );
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
    extractData();
}
