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

#include <QDialog>

#include "chartseries.h"

class LogFormatDefinition;
class QCheckBox;
class QColorDialog;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QSpinBox;

// Dialog for adding or editing a single chart series definition.
// The user provides a name, regex pattern (with at least one capture group),
// the index of the capture group that contains the numeric value, and a color.
class ChartSeriesDialog : public QDialog {
    Q_OBJECT

  public:
    explicit ChartSeriesDialog( QWidget* parent = nullptr );

    // Pre-populate the dialog fields for editing an existing series.
    void setSeries( const ChartSeriesDefinition& def );

    // Pre-fill X-axis fields from a detected log format so the user
    // doesn't have to type the timestamp regex and format manually.
    void setFormatDefaults( const LogFormatDefinition* format );

    // Return the configured series definition.
    ChartSeriesDefinition series() const;

  private Q_SLOTS:
    void chooseColor();
    void validateAndAccept();

  private:
    QLineEdit* nameEdit_;
    QLineEdit* patternEdit_;
    QSpinBox* captureGroupSpin_;
    QPushButton* colorButton_;
    QColor selectedColor_{ "#2196F3" };

    // X-axis configuration.
    QGroupBox* xAxisGroup_;
    QLineEdit* xPatternEdit_;
    QSpinBox* xCaptureGroupSpin_;
    QCheckBox* xTimestampCheckbox_;
    QLineEdit* xTimestampFormatEdit_;
    QComboBox* bucketSizeCombo_;
};
