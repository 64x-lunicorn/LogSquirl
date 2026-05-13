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
class QComboBox;
class QLineEdit;
class QLabel;
class QSpinBox;

// Wizard-style dialog for quickly building a chart series from a detected log format.
// Instead of requiring manual regex input, the user picks from detected fields
// and optionally adds a filter regex to narrow down which lines to include.
class ChartWizardDialog : public QDialog {
    Q_OBJECT

  public:
    explicit ChartWizardDialog( const LogFormatDefinition* format,
                                QWidget* parent = nullptr );

    // Return the configured series definition ready for extraction.
    ChartSeriesDefinition series() const;

  private Q_SLOTS:
    void onYFieldChanged( int index );
    void chooseColor();
    void validateAndAccept();

  private:
    void populateFieldCombos();

    const LogFormatDefinition* format_;

    QLineEdit* nameEdit_;
    QComboBox* xFieldCombo_;
    QComboBox* yFieldCombo_;
    QComboBox* bucketSizeCombo_;
    QLineEdit* filterEdit_;
    QPushButton* colorButton_;
    QLabel* previewLabel_;

    QColor selectedColor_{ "#2196F3" };
};
