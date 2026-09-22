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

#pragma once

#include <chrono>
#include <memory>

#include <QMenu>
#include <QProgressBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "chartextraction.h"
#include "chartseries.h"
#include "chartwidget.h"

class LogData;
class LogFormatDefinition;
class QAction;
class QComboBox;

// Container panel housing the ChartWidget and a toolbar for managing
// chart series.  The panel scans log lines with the user-defined regex
// patterns and feeds extracted numeric values into the chart.
//
// Intended to be embedded as a third pane in the CrawlerWidget QSplitter
// (below the filtered view).  Hidden by default; toggled via View menu.
class ChartPanel : public QWidget {
    Q_OBJECT

public:
    explicit ChartPanel( QWidget* parent = nullptr );
    ~ChartPanel() override;

    // Assign the log data source.  Must be called before extractData().
    void setLogData( const std::shared_ptr<LogData>& logData );

    // Provide the detected log format definition so the panel can offer
    // format-aware quick-add templates.  Pass nullptr to clear.
    void setLogFormat( const LogFormatDefinition* format );

    // Brings the chart up to date with the Log File: only the Log Lines
    // appended since the last extraction are extracted, unless the series
    // changed or the Log File was truncated. Appended Log Lines are picked up
    // after the update delay.
    void extractData();

    // The Log File was truncated or loaded again from its start: the next
    // extraction starts from the first Log Line. A running one is cancelled
    // without waiting for it.
    void logFileTruncated();

    // How long extractData() waits before extracting appended Log Lines.
    void setUpdateDelay( std::chrono::milliseconds delay );

    // Return a copy of the current series definitions (for persistence).
    QVector<ChartSeriesDefinition> seriesDefinitions() const;

    // Restore series definitions (e.g. from saved config).
    void setSeriesDefinitions( const QVector<ChartSeriesDefinition>& defs );

    // Create count-mode chart series from the given filter patterns.
    // Used by the "Show Filter Frequency" feature to visualise how
    // often each search filter matches across the log file. The series
    // match case as the Search does: matchCase is its Match case.
    void addFilterFrequencySeries( const QStringList& patterns, bool matchCase );

Q_SIGNALS:
    // Propagated from the chart widget when the user clicks a data point.
    void lineSelected( LineNumber line );

private Q_SLOTS:
    void addSeries();
    void addSeriesWizard();
    void editSeries();
    void removeSeries();
    void fitView();
    void onSeriesComboChanged( int index );

    // Preset management
    void savePreset();
    void loadPreset();
    void deletePreset();
    void exportPreset();
    void importPreset();

private:
    void rebuildSeriesCombo();
    void rebuildTemplatesMenu();
    void addTemplateSeries( const QVector<ChartSeriesDefinition>& defs );

    // The series changed: extract them from the first Log Line.
    void seriesChanged();
    void onExtractionStarted();
    void onExtracted( bool fromStart );
    void showProgress();

    ChartWidget* chartWidget_;
    QToolBar* toolBar_;
    QProgressBar* progressBar_;
    QComboBox* seriesCombo_;

    QAction* addAction_;
    QAction* wizardAction_;
    QAction* editAction_;
    QAction* removeAction_;
    QAction* fitAction_;

    QAction* savePresetAction_;
    QAction* loadPresetAction_;
    QAction* deletePresetAction_;
    QAction* exportPresetAction_;
    QAction* importPresetAction_;

    // Format-aware templates
    QToolButton* templatesButton_ = nullptr;
    QMenu* templatesMenu_ = nullptr;

    QVector<ChartSeriesDefinition> series_;
    std::shared_ptr<LogData> logData_;
    const LogFormatDefinition* format_ = nullptr;

    // Extracts the points on a worker thread, following the Log File.
    ChartExtraction extraction_;
    QTimer progressTimer_;
};
