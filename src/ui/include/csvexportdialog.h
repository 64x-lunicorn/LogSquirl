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

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <QChar>
#include <QDialog>
#include <QString>

class QCheckBox;
class QListWidget;
class QPushButton;
class QRadioButton;

// The dialog Export as CSV: which Rows and columns to write, the separator
// and the header row, then the file to write to. The Table View opens it; a
// view with rows of another kind hands its own texts, extra row options and
// extra columns in the Setup.
//
// The separator and the header row start as the last export left them, and
// an export remembers them (Configuration); the rows and columns start as the
// Setup says.
class CsvExportDialog : public QDialog {
    Q_OBJECT

public:
    // A column offered for export.
    struct Column {
        QString name;
        bool checked = true;
    };
    // A checkbox under the choice of rows, such as whether to include some
    // kind of Log Line.
    struct RowOption {
        QString text;
        bool enabled = true;
        bool checked = false;
    };
    struct Setup {
        // The texts of the two choices of rows; empty for "All rows" and
        // "Selected rows".
        QString allRowsText;
        QString selectedRowsText;
        // Whether rows are selected: only then can the selected ones be
        // exported, and that is then the choice made at first.
        bool hasSelection = false;
        std::vector<RowOption> rowOptions;
        // In the order they are written.
        std::vector<Column> columns;
        // The file proposed to write to.
        QString proposedFileName;
    };
    // What the user chose.
    struct Choices {
        bool selectedRowsOnly = false;
        // Whether each of the Setup's row options is checked.
        std::vector<bool> rowOptions;
        // The indices of the checked columns in the Setup, in its order.
        std::vector<size_t> columns;
        QChar separator = QLatin1Char( ',' );
        bool header = true;
        // Ends with ".csv".
        QString fileName;
    };

    explicit CsvExportDialog( Setup setup, QWidget* parent = nullptr );

    Choices choices() const;

    // Asks for the file to write to, given the proposed one; empty when the
    // user cancels. A QFileDialog unless a test hands its own.
    using FileNameChooser = std::function<QString( QWidget* parent, const QString& proposed )>;
    void setFileNameChooser( FileNameChooser chooser );

    // Asks whether to replace a file that exists; true to replace it. Asked
    // only when ".csv" is added to the file chosen, since the file dialog
    // asked about the name without it. A QMessageBox unless a test hands its
    // own.
    using OverwriteConfirmer = std::function<bool( QWidget* parent, const QString& fileName )>;
    void setOverwriteConfirmer( OverwriteConfirmer confirmer );

    // Runs the dialog; the choices when the user chose a file and exported.
    static std::optional<Choices> ask( QWidget* parent, Setup setup );

private:
    void updateExportButton();
    // Asks for the file, and remembers the separator and header row.
    void exportChosen();

    Setup setup_;
    FileNameChooser chooseFileName_;
    OverwriteConfirmer confirmOverwrite_;
    QString fileName_;

    QRadioButton* allRows_ = nullptr;
    QRadioButton* selectedRows_ = nullptr;
    std::vector<QCheckBox*> rowOptions_;
    QListWidget* columns_ = nullptr;
    QRadioButton* comma_ = nullptr;
    QRadioButton* semicolon_ = nullptr;
    QRadioButton* tab_ = nullptr;
    QCheckBox* header_ = nullptr;
    QPushButton* export_ = nullptr;
};
