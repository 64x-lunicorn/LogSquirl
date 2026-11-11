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
#include <utility>

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

#include "groupedits.h"
#include "naminggroup.h"
#include "teamfolder.h"
#include "teamgroupedits.h"
#include "valuenamer.h"
#include "valueproposal.h"

class GroupChooser;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// What "Name this value..." of the context menu of the Text View and the
// Filtered View opens (#883): a compact dialog naming one value of a Log
// Line, prefilled from what the user pointed at. It proposes a Naming Rule
// with the value as its capture group, a Name Table named after the text
// before the value, with the value entered as a key, the template
// "{name} ({value})" and a Naming Group; all are editable, and the preview
// shows the Log Line as it will look. The user picks the Naming Group with the
// Group Chooser (#956), as Add as Predefined Filter... picks its Filter Group:
// one of the user's own, a Team Naming Group, or a new one of either. The Name
// Table choice offers the tables of the group chosen, a rule only using the
// Name Tables of its own group.
//
// OK adds the rule, and the table unless it is one the group has, to the
// group -- the operation the Value Names dialog's Add buttons use -- and checks
// the group. A group of the user's own is saved in ValueNamesCollection; a
// Team Naming Group is published through publishRequested, based on the
// revision it was given with, so that a change somebody else made meanwhile
// is asked about. Showing Value Names in the tab, and remembering the group
// chosen, are the caller's.
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
    // value logsquirl::valuenames::proposeNaming() proposes for.
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

    // Offers the Team Naming Groups too, the Team Folder being on: under a
    // heading of their own, with New Team Naming Group.... Only when editable
    // can they be chosen. revisions holds the revision of each group's file,
    // by id. Without a call there is no Team section.
    void showTeamGroups( const QList<logsquirl::valuenames::NamingGroup>& groups, bool editable,
                         const QHash<QString, QString>& revisions );

    // Proposes the group of the id, the one chosen last; the first of the
    // user's own when it is gone. A group that has a Name Table of the name
    // proposed is chosen all the same, and so is the group of a value an
    // existing rule captures.
    void proposeGroup( const QString& groupId );

    // The id of the Naming Group chosen; empty for a new one until OK made
    // it, its id after that.
    QString chosenGroupId() const;

    // The Group Chooser.
    GroupChooser* groupChooser() const
    {
        return group_;
    }

public Q_SLOTS:
    void accept() override;

Q_SIGNALS:
    // OK changed a Team Naming Group or made one: the requests to publish it.
    void publishRequested( const QList<logsquirl::teamfolder::PublishRequest>& requests );

private:
    // The widgets, once proposal_, groups_ and captured_ are set.
    void build();
    // Hands the Group Chooser the groups, choosing the one of the id unless
    // fixedGroupId() names one.
    void offerGroups( const QString& chosenId );
    // The group chosen whatever was chosen last: that of the value captured,
    // else the first that has a Name Table of the name proposed; empty for
    // none.
    QString fixedGroupId() const;
    // The rows as edited, a row left blank left out.
    QList<logsquirl::valuenames::NameRow> editedRows() const;
    // The group chosen, as the dialog was given it; none for a new group.
    const logsquirl::valuenames::NamingGroup* chosenGroup() const;
    // The chosen group as OK makes it, in a copy: what the preview shows.
    logsquirl::valuenames::NamingGroup groupAfter() const;
    // The tables of the chosen group to choose from, and the rows of the
    // chosen table.
    void populateTables();
    void populateRows();
    // The Group Chooser changed: another group offers its tables, a new
    // group's name changing only checks it.
    void groupChosen();
    void tableChosen();
    void addRow();
    void removeRow();
    void updatePreview();
    // The warning line and OK: OK is disabled while the new group's name
    // will not do.
    void updateWarning();

    QString logLine_;
    logsquirl::valuenames::NamingProposal proposal_;
    // The user's own groups, as read when the dialog opened.
    QList<logsquirl::valuenames::NamingGroup> groups_;
    // The Team groups, when the dialog is given them.
    bool showsTeam_ = false;
    TeamGroupEdits<logsquirl::valuenames::NamingGroup> teamEdits_;
    // The value an existing rule captures, of groups_, when opened on one.
    std::optional<logsquirl::valuenames::CapturedValue> captured_;
    QString shownPreview_;
    // What the preview warns of.
    QString previewWarning_;
    // The group whose tables the Name Table choice offers: its section and id,
    // an empty id for a new group.
    std::optional<std::pair<int, QString>> tablesOf_;
    // The id of the group OK made.
    QString madeGroupId_;

    GroupChooser* group_ = nullptr;
    QComboBox* table_ = nullptr;
    QLineEdit* newTableName_ = nullptr;
    QLineEdit* pattern_ = nullptr;
    QLineEdit* template_ = nullptr;
    QTableWidget* rows_ = nullptr;
    QPushButton* addRowButton_ = nullptr;
    QPushButton* removeRowButton_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* warning_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
