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

#include <QComboBox>
#include <QLabel>
#include <QRegularExpression>

#include "chartseriesdialog.h"
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
                if ( match.hasMatch() && match.lastCapturedIndex() >= s.captureGroup ) {
                    bool ok = false;
                    const double val = match.captured( s.captureGroup ).toDouble( &ok );
                    if ( ok ) {
                        s.points.append( { lineNum, val } );
                    }
                }
            }
        }
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
