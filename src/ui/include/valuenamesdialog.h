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

#include <functional>

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

#include "nametablecsv.h"
#include "naminggroup.h"
#include "valuenamer.h"

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;
class QVBoxLayout;

// The text of a CSV file: UTF-8 or UTF-16 with its byte order mark, UTF-8
// without one if it reads as UTF-8, else the ANSI code page a spreadsheet
// writes -- the system's on Windows, windows-1252 elsewhere.
QString decodeCsvFile( const QByteArray& bytes );

// Where a CSV import puts the rows it reads.
enum class CsvImportTarget {
    // A new Name Table, named after the file.
    NewTable,
    // The selected table, its rows replaced.
    ReplaceRows,
    // The selected table, after its rows.
    AppendRows,
};

// Asks how a CSV text is read into a Name Table: which column holds the key
// and which the name (1 and 2 unless chosen otherwise), whether the first
// record is a header, and where the rows go. Shows the separator detected and
// the first records.
class NameTableCsvImportDialog : public QDialog {
    Q_OBJECT

public:
    // selectedTable is the Name Table selected in the Value Names dialog,
    // empty when none is: only then can the rows go into it.
    NameTableCsvImportDialog( const QString& text, bool caseSensitive,
                              const QString& selectedTable = {}, QWidget* parent = nullptr );

    // What was chosen, columns 0-based.
    logsquirl::valuenames::CsvImportOptions options() const;

    CsvImportTarget target() const;

    // The separator detected, as shown: "comma", "semicolon" or "tab".
    QString separatorShown() const;

private:
    void updateAcceptable();

    QChar separator_;
    bool caseSensitive_ = false;
    QLabel* separatorLabel_ = nullptr;
    QSpinBox* keyColumn_ = nullptr;
    QSpinBox* nameColumn_ = nullptr;
    QLabel* sameColumns_ = nullptr;
    QCheckBox* hasHeader_ = nullptr;
    QComboBox* target_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

// The Value Names dialog (#647), built like the Predefined Filters Dialog:
// the user's own Naming Groups on the left; on the right the selected
// group's Naming Rules, which Name Table each capture group of the selected
// rule uses, and the group's Name Tables with the rows of the selected one;
// below, a preview line showing a sample Log Line with the group's Value
// Names and what is wrong with the group.
//
// It edits a copy of the groups, checks included (a rule renamed keeps its
// check). OK and Apply hand them to the Value Names Collection, save it and,
// if anything changed, say valueNamesChanged().
class ValueNamesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ValueNamesDialog( QWidget* parent = nullptr );

    // The groups as edited here.
    const QList<logsquirl::valuenames::NamingGroup>& groups() const
    {
        return groups_;
    }

    // Reads the CSV text into a new Name Table of that name, or into the
    // selected one, replacing or after its rows; into a new table when none is
    // selected. Import CSV... after its file and options were chosen.
    void importCsvText( const QString& text, const logsquirl::valuenames::CsvImportOptions& options,
                        CsvImportTarget target, const QString& newTableName = {} );

    // The rows of the selected Name Table as CSV: what Export CSV... writes.
    QString exportCsvText() const;

    // Adds the rows of the text -- copied from a spreadsheet, tab-separated --
    // to the selected Name Table: what Paste does with the clipboard.
    void pasteRows( const QString& text );

    // The sample Log Line of the preview as shown, without its markup.
    QString shownPreview() const
    {
        return shownPreview_;
    }

Q_SIGNALS:
    // OK or Apply changed the Value Names Collection.
    void valueNamesChanged();

private Q_SLOTS:
    void addGroup();
    void removeGroup();
    void moveGroupUp();
    void moveGroupDown();
    void groupSelected();
    void groupRenamed( const QString& name );

    void addRule();
    void removeRule();
    void moveRuleUp();
    void moveRuleDown();
    void ruleSelected();
    void ruleEdited( QTableWidgetItem* item );

    void addTable();
    void removeTable();
    void tableSelected();
    void tableRenamed( QListWidgetItem* item );
    void caseSensitivityChanged( bool caseSensitive );
    void addRow();
    void removeRows();
    void rowEdited( QTableWidgetItem* item );
    void importCsv();
    void exportCsv();
    void paste();

    // The group was edited: its namer and warnings are made again.
    void updatePreview();
    // The sample Log Line changed: only it is named again.
    void showPreviewLine();
    void groupNameFinished();
    void resolveDialog( QAbstractButton* button );

private:
    logsquirl::valuenames::NamingGroup* currentGroup();
    const logsquirl::valuenames::NamingGroup* currentGroup() const;
    const logsquirl::valuenames::NameTable* currentTable() const;
    // Changes the rules or the tables of the selected group: the group
    // hands them out as values only.
    void
    changeRules( const std::function<void( QList<logsquirl::valuenames::NamingRule>& )>& change );
    void
    changeTables( const std::function<void( QList<logsquirl::valuenames::NameTable>& )>& change );

    void populateGroups( int selectRow );
    void populateRules( int selectRow );
    void populateCaptureGroups();
    void populateTables( int selectRow );
    void populateRows();
    void updateButtons();
    // The table of the selected rule's capture group, chosen in its row.
    void captureGroupTableChosen( const QString& group, const QString& numbered,
                                  const QString& table );
    // Shows the warnings of a CSV import or a paste with the group's.
    void showCsvWarnings( const QList<logsquirl::valuenames::CsvImportWarning>& warnings );
    void loadIcons();
    // Every group named, and no two alike: an exported group's file is
    // named after it.
    void makeGroupNamesUnique();
    bool eventFilter( QObject* watched, QEvent* event ) override;

    QList<logsquirl::valuenames::NamingGroup> groups_;
    int groupRow_ = -1;
    int ruleRow_ = -1;
    int tableRow_ = -1;
    bool updating_ = false;
    QString shownPreview_;
    // Built from the group as last edited, all its rules checked.
    logsquirl::valuenames::ValueNamer previewNamer_;
    QStringList csvWarnings_;

    // Left: the groups. Stage 4 adds Export, Import and the Team groups to
    // this layout, as the Predefined Filters Dialog has them.
    QVBoxLayout* leftLayout_ = nullptr;
    QListWidget* groupList_ = nullptr;
    QToolButton* addGroupButton_ = nullptr;
    QToolButton* removeGroupButton_ = nullptr;
    QToolButton* upGroupButton_ = nullptr;
    QToolButton* downGroupButton_ = nullptr;

    QWidget* groupEditor_ = nullptr;
    QLineEdit* groupName_ = nullptr;

    QTableWidget* rulesTable_ = nullptr;
    QToolButton* addRuleButton_ = nullptr;
    QToolButton* removeRuleButton_ = nullptr;
    QToolButton* upRuleButton_ = nullptr;
    QToolButton* downRuleButton_ = nullptr;
    QTableWidget* captureGroups_ = nullptr;

    QListWidget* tablesList_ = nullptr;
    QToolButton* addTableButton_ = nullptr;
    QToolButton* removeTableButton_ = nullptr;
    QCheckBox* caseSensitive_ = nullptr;
    QTableWidget* rowsTable_ = nullptr;
    QToolButton* addRowButton_ = nullptr;
    QToolButton* removeRowButton_ = nullptr;
    QToolButton* importCsvButton_ = nullptr;
    QToolButton* exportCsvButton_ = nullptr;
    QToolButton* pasteButton_ = nullptr;

    QLineEdit* previewInput_ = nullptr;
    QLabel* previewResult_ = nullptr;
    QListWidget* warnings_ = nullptr;

    QDialogButtonBox* buttonBox_ = nullptr;
};
