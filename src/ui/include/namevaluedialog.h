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

#include <memory>
#include <optional>

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

#include "choosergroups.h"
#include "groupchooser.h"
#include "groupedits.h"
#include "naminggroup.h"
#include "teamfolder.h"
#include "valuenamer.h"
#include "valueproposal.h"

class AddToGroupLayout;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

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
// It is laid out as Add as Predefined Filter... is, with AddToGroupLayout
// (#958): what is added (the value and its name, the Naming Rule, the
// Template), where it goes (the Naming Group, the Name Table), the preview,
// the warning, the buttons. The value and its name take one row, the focus in
// the name; More rows... shows the rows of the Name Table instead, to add and
// remove some, and a Name Table of more than one row shows them from the
// start. The preview shows the Log Line in one line -- only its
// message, when the caller says where that is -- with the value marked.
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
    ~NameValueDialog() override;

    // The preview shows only the message of the Log Line, which lies at
    // message: the body field of the tab's Log Format. A message the value
    // does not lie in is not taken.
    void showMessageOnly( logsquirl::valuenames::LineRange message );

    // What the preview shows, the Log Line or its message as it will be
    // shown, without markup and not elided.
    QString shownPreview() const
    {
        return shownPreview_;
    }

    // Whether the rows of the Name Table show, rather than the one row of the
    // value.
    bool showsRows() const;

    // The layout shared with Add as Predefined Filter....
    const AddToGroupLayout& addToGroupLayout() const
    {
        return *layout_;
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
    // The widgets, once proposal_ and captured_ are set; own are the user's
    // own groups, as read when the dialog opened.
    void build( QList<logsquirl::valuenames::NamingGroup> own );
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
    // More rows...: the rows of the Name Table show instead of the value's,
    // whatever table is chosen from now on.
    void showRows();
    // The rows of the Name Table show, or the value's one row does.
    void setRowsShown( bool shown );
    // The focus goes where the value's name is typed.
    void focusName();
    // A row of the table changed: the value's shows in its one row too.
    void rowChanged( QTableWidgetItem* item );
    // The value's one row shows the value's row of the table, or was edited
    // and goes into it.
    void syncValueRow();
    void valueRowEdited();
    void updatePreview();
    // The warning line and OK: OK is disabled while the new group's name
    // will not do.
    void updateWarning();

    QString logLine_;
    logsquirl::valuenames::NamingProposal proposal_;
    // The value an existing rule captures, of the user's own groups, when
    // opened on one.
    std::optional<logsquirl::valuenames::CapturedValue> captured_;
    QString shownPreview_;
    // What the preview warns of.
    QString previewWarning_;
    // The group whose tables the Name Table choice offers, as chosen then: a
    // new group's name may have changed since (GroupChooser::Choice::isSameGroup()).
    std::optional<GroupChooser::Choice> tablesOf_;
    // More rows... was clicked: the rows show for every table.
    bool rowsAskedFor_ = false;

    // Where the message lies in the Log Line, when the preview shows only it.
    std::optional<logsquirl::valuenames::LineRange> message_;
    // The row of the table that is the value's, -1 once it is removed.
    int valueRowIndex_ = -1;

    std::unique_ptr<AddToGroupLayout> layout_;
    GroupChooser* group_ = nullptr;
    // The user's own groups, as read when the dialog opened, and the Team
    // Naming Groups once it is given them: what group_ offers.
    std::unique_ptr<ChooserGroups<logsquirl::valuenames::NamingGroup>> groups_;
    QComboBox* table_ = nullptr;
    QLineEdit* newTableName_ = nullptr;
    QLineEdit* pattern_ = nullptr;
    QLineEdit* template_ = nullptr;
    // The value's one row.
    QWidget* valueRow_ = nullptr;
    QLineEdit* valueKey_ = nullptr;
    QLineEdit* valueName_ = nullptr;
    QPushButton* moreRows_ = nullptr;
    // The rows of the Name Table, with Add Row and Remove Row.
    QWidget* rowsBox_ = nullptr;
    QTableWidget* rows_ = nullptr;
    QPushButton* addRowButton_ = nullptr;
    QPushButton* removeRowButton_ = nullptr;
};
