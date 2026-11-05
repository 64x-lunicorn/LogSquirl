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

#include <optional>

#include <QDialog>
#include <QList>
#include <QString>

#include "groupedits.h"
#include "naminggroup.h"
#include "valuenamer.h"
#include "valueproposal.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// What "Name this value..." of the context menu of the Text View and the
// Filtered View opens (#883): a compact dialog naming one value of a Log
// Line, prefilled from what the user pointed at. It proposes a Naming Rule
// with the value as its capture group, a Name Table named after the text
// before the value, with the value entered as a key, the template
// "{name} ({value})" and a Naming Group, a new one or one of the user's own;
// all are editable, and the preview shows the Log Line as it will look.
//
// OK adds the rule, and the table unless it is one the group has, to the
// group -- the operation the Value Names dialog's Add buttons use -- checks
// the group, and saves ValueNamesCollection. Showing Value Names in the tab
// is the caller's.
//
// Opened on a value a Naming Rule of the user's own groups already captures
// (#885), by "Name <value> in <table>..." or "Rename <table> <value>...", it
// shows that rule and its Name Table, with the value entered as a new key or,
// for a Named Value, the row naming it selected; only the rows are edited,
// and OK changes them in that table, adding no rule and no table.
class NameValueDialog : public QDialog {
    Q_OBJECT

public:
    // selection is the range of the Log Line the user pointed at; it holds a
    // value (logsquirl::valuenames::valueIn()).
    NameValueDialog( const QString& logLine, logsquirl::valuenames::LineRange selection,
                     QWidget* parent = nullptr );

    // captured is what logsquirl::valuenames::capturedValueIn() found in the
    // Log Line among ValueNamesCollection::groups(), of one of the user's own
    // groups. When the group, its rule or its table is gone meanwhile, the
    // dialog proposes a new rule for the value as the other constructor does.
    NameValueDialog( const QString& logLine, const logsquirl::valuenames::CapturedValue& captured,
                     QWidget* parent = nullptr );

    // The Log Line of the preview as it will be shown, without its markup.
    QString shownPreview() const
    {
        return shownPreview_;
    }

    // What the preview warns of, empty when the regex matches the Log Line.
    QString previewWarning() const;

    // The rule and the table OK creates, as edited.
    logsquirl::valuenames::RuleWithTable ruleWithTable() const;

public Q_SLOTS:
    void accept() override;

private:
    // The widgets, once proposal_, groups_ and captured_ are set.
    void build();
    // The rows as edited, a row left blank left out.
    QList<logsquirl::valuenames::NameRow> editedRows() const;
    // The row of groups_ chosen, -1 for a new group.
    int chosenGroup() const;
    // The chosen group as OK makes it, in a copy: what the preview shows.
    logsquirl::valuenames::NamingGroup groupAfter() const;
    // The tables of the chosen group to choose from, and the rows of the
    // chosen table.
    void populateTables();
    void populateRows();
    void groupChosen();
    void tableChosen();
    void addRow();
    void removeRow();
    void updatePreview();

    QString logLine_;
    logsquirl::valuenames::NamingProposal proposal_;
    // The user's own groups, as read when the dialog opened.
    QList<logsquirl::valuenames::NamingGroup> groups_;
    // The value an existing rule captures, of groups_, when opened on one.
    std::optional<logsquirl::valuenames::CapturedValue> captured_;
    QString shownPreview_;

    QComboBox* group_ = nullptr;
    QLineEdit* newGroupName_ = nullptr;
    QComboBox* table_ = nullptr;
    QLineEdit* newTableName_ = nullptr;
    QLineEdit* pattern_ = nullptr;
    QLineEdit* template_ = nullptr;
    QTableWidget* rows_ = nullptr;
    QPushButton* addRowButton_ = nullptr;
    QPushButton* removeRowButton_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* warning_ = nullptr;
};
