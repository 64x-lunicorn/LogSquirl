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
#include <QTimer>
#include <QtConcurrent>

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

    // Connect the extraction watcher to the finish handler.
    connect( &extractionWatcher_, &QFutureWatcher<QVector<ChartSeriesDefinition>>::finished,
             this, &ChartPanel::onExtractionFinished );
}

ChartPanel::~ChartPanel()
{
    cancelExtraction();
}

void ChartPanel::setLogData( const std::shared_ptr<LogData>& logData )
{
    logData_ = logData;
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

    startAsyncExtraction();
}

// ---------------------------------------------------------------------------
// Async extraction
// ---------------------------------------------------------------------------

void ChartPanel::cancelExtraction()
{
    if ( cancelFlag_ ) {
        cancelFlag_->store( true );
    }
    extractionWatcher_.waitForFinished();
}

void ChartPanel::startAsyncExtraction()
{
    // Cancel any running extraction first.
    cancelExtraction();

    // Snapshot series definitions for the worker (without points).
    auto seriesCopy = series_;
    for ( auto& s : seriesCopy ) {
        s.points.clear();
    }

    auto logData = logData_;
    auto cancel = std::make_shared<std::atomic<bool>>( false );
    cancelFlag_ = cancel;

    const auto totalLines = logData->getNbLine().get();

    progressBar_->setRange( 0, 100 );
    progressBar_->setValue( 0 );
    progressBar_->setVisible( true );

    // Set up a timer to poll progress while the worker runs.
    auto* progressTimer = new QTimer( this );
    auto progressLinesProcessed = std::make_shared<std::atomic<uint64_t>>( 0 );
    connect( progressTimer, &QTimer::timeout, this,
             [ this, totalLines, progressLinesProcessed ]() {
                 if ( totalLines > 0 ) {
                     const auto processed = progressLinesProcessed->load();
                     const int pct = static_cast<int>(
                         ( processed * 100 ) / totalLines );
                     progressBar_->setValue( std::min( pct, 99 ) );
                 }
             } );
    progressTimer->start( 100 );

    // Capture the timer pointer so we can stop it when done.
    // Disconnect any previous progress-timer connection to prevent stale
    // lambda captures from firing on subsequent extractions.
    if ( progressConnection_ ) {
        disconnect( progressConnection_ );
    }
    progressConnection_ = connect(
        &extractionWatcher_,
        &QFutureWatcher<QVector<ChartSeriesDefinition>>::finished, this,
        [ progressTimer ]() {
            progressTimer->stop();
            progressTimer->deleteLater();
        } );

    auto future = QtConcurrent::run(
        [ seriesCopy, logData, cancel, totalLines,
          progressLinesProcessed ]() mutable -> QVector<ChartSeriesDefinition> {
            // ---------------------------------------------------------------
            // Pre-compute regex groups to avoid redundant matches per line.
            //
            // When using format-aware templates many series share the same
            // Y-pattern (the format's main regex) and/or the same X-pattern
            // (timestamp extraction).  Grouping them lets us run each unique
            // regex only once per line and distribute the result.
            //
            // Additionally, when xPattern == pattern we can reuse the
            // Y-match for X extraction — eliminating the separate X-regex
            // run entirely.
            // ---------------------------------------------------------------

            // Group series indices by unique Y-pattern string.
            struct RegexGroup {
                QRegularExpression regex;
                QVector<int> indices;
            };

            QHash<QString, RegexGroup> yGroups;
            for ( int si = 0; si < seriesCopy.size(); ++si ) {
                const auto& s = seriesCopy[ si ];
                if ( !s.compiledRegex.isValid() ) {
                    continue;
                }
                auto& g = yGroups[ s.pattern ];
                if ( g.indices.isEmpty() ) {
                    g.regex = s.compiledRegex;
                }
                g.indices.append( si );
            }

            // Collect unique X-regexes that differ from their Y-pattern.
            QHash<QString, QRegularExpression> uniqueXRegexes;
            for ( int si = 0; si < seriesCopy.size(); ++si ) {
                const auto& s = seriesCopy[ si ];
                if ( !s.hasCustomXAxis() || !s.compiledXRegex.isValid() ) {
                    continue;
                }
                if ( s.xPattern == s.pattern ) {
                    continue; // will reuse Y match
                }
                uniqueXRegexes.insert( s.xPattern, s.compiledXRegex );
            }

            // Track which series can reuse the Y-match for X extraction.
            QVector<bool> reuseYForX( seriesCopy.size(), false );
            for ( int si = 0; si < seriesCopy.size(); ++si ) {
                const auto& s = seriesCopy[ si ];
                if ( s.hasCustomXAxis() && s.xPattern == s.pattern ) {
                    reuseYForX[ si ] = true;
                }
            }

            // Cache QDate::currentDate() outside the hot loop.
            const auto currentYear = QDate::currentDate().year();

            constexpr uint64_t batchSize = 5000;

            for ( uint64_t start = 0; start < totalLines; start += batchSize ) {
                if ( cancel->load() ) {
                    return {};
                }

                const auto count = std::min( batchSize, totalLines - start );
                const auto lines = logData->getExpandedLines(
                    LineNumber( start ), LinesCount( count ) );

                for ( uint64_t i = 0;
                      i < static_cast<uint64_t>( lines.size() ); ++i ) {
                    const auto lineNum = LineNumber( start + i );
                    const auto& lineText
                        = lines[ static_cast<size_t>( i ) ];

                    // 1. Run each unique Y-regex once for this line.
                    QHash<QString, QRegularExpressionMatch> yCache;
                    for ( auto it = yGroups.cbegin();
                          it != yGroups.cend(); ++it ) {
                        auto m = it.value().regex.match( lineText );
                        if ( m.hasMatch() ) {
                            yCache.insert( it.key(), std::move( m ) );
                        }
                    }
                    if ( yCache.isEmpty() ) {
                        continue; // no series matches this line
                    }

                    // 2. Run each unique X-regex once (only those
                    //    that differ from Y-pattern).
                    QHash<QString, QRegularExpressionMatch> xCache;
                    for ( auto it = uniqueXRegexes.cbegin();
                          it != uniqueXRegexes.cend(); ++it ) {
                        auto m = it.value().match( lineText );
                        if ( m.hasMatch() ) {
                            xCache.insert( it.key(), std::move( m ) );
                        }
                    }

                    // 3. Timestamp parse cache: same raw text on the
                    //    same line always yields the same epoch-ms.
                    QHash<QString, double> tsCache;

                    // 4. Distribute cached matches to all series.
                    for ( int si = 0; si < seriesCopy.size(); ++si ) {
                        auto& s = seriesCopy[ si ];

                        auto yIt = yCache.constFind( s.pattern );
                        if ( yIt == yCache.cend() ) {
                            continue;
                        }
                        const auto& yMatch = yIt.value();

                        // Extract Y value.
                        double yVal = 1.0;
                        if ( s.captureGroup > 0
                             && yMatch.lastCapturedIndex()
                                    >= s.captureGroup ) {
                            bool ok = false;
                            yVal = yMatch.captured( s.captureGroup )
                                       .toDouble( &ok );
                            if ( !ok ) {
                                yVal = 1.0;
                            }
                        }

                        // Extract X value.
                        double xVal
                            = static_cast<double>( lineNum.get() );
                        QString xLabel;

                        if ( s.hasCustomXAxis() ) {
                            // Pick the match to read X from: either
                            // the Y-match (when patterns are the same)
                            // or the dedicated X-match.
                            const QRegularExpressionMatch* xMatchPtr
                                = nullptr;
                            if ( reuseYForX[ si ] ) {
                                xMatchPtr = &yMatch;
                            }
                            else {
                                auto xIt
                                    = xCache.constFind( s.xPattern );
                                if ( xIt != xCache.cend() ) {
                                    xMatchPtr = &xIt.value();
                                }
                            }

                            if ( xMatchPtr
                                 && xMatchPtr->lastCapturedIndex()
                                        >= s.xCaptureGroup ) {
                                const auto captured
                                    = xMatchPtr->captured(
                                        s.xCaptureGroup );

                                if ( s.isTimestampXAxis() ) {
                                    // Check per-line timestamp cache.
                                    auto tsIt
                                        = tsCache.constFind( captured );
                                    if ( tsIt != tsCache.cend() ) {
                                        xVal = tsIt.value();
                                        xLabel = captured;
                                    }
                                    else {
                                        auto dt
                                            = QDateTime::fromString(
                                                captured,
                                                s.xTimestampFormat );
                                        if ( dt.isValid() ) {
                                            if ( dt.date().year()
                                                 < 1970 ) {
                                                dt.setDate( QDate(
                                                    currentYear,
                                                    dt.date().month(),
                                                    dt.date()
                                                        .day() ) );
                                            }
                                            xVal = static_cast<double>(
                                                dt.toMSecsSinceEpoch() );
                                            xLabel = captured;
                                            tsCache.insert(
                                                captured, xVal );
                                        }
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

                        s.points.append(
                            { lineNum, xVal, yVal, xLabel } );
                    }
                }

                progressLinesProcessed->store( start + count );
            }

            // Aggregate into time buckets where configured.
            for ( auto& s : seriesCopy ) {
                if ( cancel->load() ) {
                    return {};
                }
                if ( !s.isBucketed() || s.points.isEmpty() ) {
                    continue;
                }

                const auto bucket
                    = static_cast<double>( s.bucketSizeMs );
                QVector<ChartPoint> bucketed;

                double bucketStart
                    = std::floor( s.points.first().xValue / bucket )
                      * bucket;
                double bucketSum = 0.0;
                LineNumber bucketLine = s.points.first().line;

                for ( const auto& pt : s.points ) {
                    const double ptBucket
                        = std::floor( pt.xValue / bucket ) * bucket;
                    if ( ptBucket != bucketStart ) {
                        const double mid
                            = bucketStart + bucket / 2.0;
                        const auto dt
                            = QDateTime::fromMSecsSinceEpoch(
                                static_cast<qint64>( mid ) );
                        bucketed.append(
                            { bucketLine, mid, bucketSum,
                              dt.toString( "HH:mm:ss" ) } );
                        bucketStart = ptBucket;
                        bucketSum = 0.0;
                        bucketLine = pt.line;
                    }
                    bucketSum += pt.value;
                }
                const double mid = bucketStart + bucket / 2.0;
                const auto dt = QDateTime::fromMSecsSinceEpoch(
                    static_cast<qint64>( mid ) );
                bucketed.append( { bucketLine, mid, bucketSum,
                                   dt.toString( "HH:mm:ss" ) } );

                s.points = bucketed;
            }

            return seriesCopy;
        } );

    extractionWatcher_.setFuture( future );
}

void ChartPanel::onExtractionFinished()
{
    progressBar_->setVisible( false );

    if ( cancelFlag_ && cancelFlag_->load() ) {
        // Extraction was cancelled — discard results.
        return;
    }

    const auto result = extractionWatcher_.result();
    if ( result.isEmpty() ) {
        return;
    }

    // Merge extracted points back into our series definitions.
    for ( int i = 0; i < series_.size() && i < result.size(); ++i ) {
        series_[ i ].points = result[ i ].points;
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
    dlg.setFormatDefaults( format_ );
    if ( dlg.exec() == QDialog::Accepted ) {
        series_.append( dlg.series() );
        rebuildSeriesCombo();
        extractData();
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
            addTemplateSeries(
                ChartTemplateGenerator::levelFrequencyTemplates( *format_, 1000 ) );
        } );
        levelMenu->addAction( tr( "All Levels (5 s buckets)" ), this, [ this ]() {
            addTemplateSeries(
                ChartTemplateGenerator::levelFrequencyTemplates( *format_, 5000 ) );
        } );
        levelMenu->addAction( tr( "All Levels (1 min buckets)" ), this, [ this ]() {
            addTemplateSeries(
                ChartTemplateGenerator::levelFrequencyTemplates( *format_, 60000 ) );
        } );

        levelMenu->addSeparator();

        // Individual levels
        static const QStringList kLevelOrder
            = { "fatal",   "critical", "error", "warning",
                "notice",  "info",     "debug", "trace" };

        for ( const auto& level : kLevelOrder ) {
            if ( !levelMappings.contains( level ) ) {
                continue;
            }
            levelMenu->addAction( tr( "%1 only" ).arg( level ), this,
                                  [ this, level ]() {
                                      auto all = ChartTemplateGenerator::
                                          levelFrequencyTemplates( *format_, 1000 );
                                      QVector<ChartSeriesDefinition> filtered;
                                      const auto target
                                          = QObject::tr( "Level: %1" ).arg( level );
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
            addTemplateSeries(
                ChartTemplateGenerator::messageRateTemplates( *format_, 1000 ) );
        } );
        rateMenu->addAction( tr( "per 5 Seconds" ), this, [ this ]() {
            addTemplateSeries(
                ChartTemplateGenerator::messageRateTemplates( *format_, 5000 ) );
        } );
        rateMenu->addAction( tr( "per 10 Seconds" ), this, [ this ]() {
            addTemplateSeries(
                ChartTemplateGenerator::messageRateTemplates( *format_, 10000 ) );
        } );
        rateMenu->addAction( tr( "per Minute" ), this, [ this ]() {
            addTemplateSeries(
                ChartTemplateGenerator::messageRateTemplates( *format_, 60000 ) );
        } );
    }

    // --- Numeric Fields ---
    {
        const auto numericDefs
            = ChartTemplateGenerator::numericFieldTemplates( *format_ );
        if ( !numericDefs.isEmpty() ) {
            auto* numMenu = templatesMenu_->addMenu( tr( "Numeric Fields" ) );
            for ( const auto& def : numericDefs ) {
                numMenu->addAction( def.name, this, [ this, def ]() {
                    addTemplateSeries( { def } );
                } );
            }
        }
    }

    // --- Field Occurrence ---
    {
        const auto fieldDefs
            = ChartTemplateGenerator::fieldOccurrenceTemplates( *format_, 1000 );
        if ( !fieldDefs.isEmpty() ) {
            auto* fieldMenu = templatesMenu_->addMenu( tr( "Field Occurrence" ) );
            for ( const auto& def : fieldDefs ) {
                fieldMenu->addAction( def.name, this, [ this, def ]() {
                    addTemplateSeries( { def } );
                } );
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
    extractData();
}
