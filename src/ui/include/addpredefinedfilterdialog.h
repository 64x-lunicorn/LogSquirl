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

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "choosergroups.h"
#include "filterpreviewsearch.h"
#include "predefinedfilter.h"
#include "predefinedfilters.h"
#include "regularexpressionpattern.h"
#include "searchline.h"
#include "teamfolder.h"

class AddToGroupLayout;
class LogData;
class QCheckBox;
class QLineEdit;
class GroupChooser;

// What "Add as Predefined Filter..." of the context menu of the Text View and
// the Filtered View opens (#897): a compact dialog turning the text selected
// in a Log Line into a Predefined Filter. It proposes a name made from the
// selection, one no filter of the Filter Group has, and the selection as the
// pattern, as plain text: the filter finds it whether the Search reads regular
// expressions or not. Regular expression, unchecked by default, turns the
// pattern into the selection escaped as a regex, to edit into one. The user
// picks the Filter Group with the Group Chooser (#955) -- one of the user's
// own, a Team group, or a new one of either -- and whether the Search runs
// with the filter now.
//
// It is laid out as Name This Value is, with AddToGroupLayout (#958): what is
// added, where it goes, the preview, the warning, the buttons. Given the Log
// File (previewIn()), the preview tells how many Log Lines the pattern finds
// there and shows the first one with what it matches marked; it follows the
// pattern and Regular expression as they change, counting off the UI thread.
//
// OK adds the Predefined Filter at the end of the group. A group of the
// user's own is saved in PredefinedFiltersCollection, as the Predefined
// Filters dialog does; a Team group is published through publishRequested,
// based on the revision it was given with, so that a change somebody else
// made meanwhile is asked about. Running the Search, refreshing the Filters
// panel and remembering the group chosen are the caller's.
class AddPredefinedFilterDialog : public QDialog {
    Q_OBJECT

public:
    // The longest name proposed; a longer selection is cut short.
    static constexpr qsizetype MaxProposedNameLength = 40;

    // The name proposed for a Predefined Filter of the selected text: the
    // text on one line, its white space collapsed, cut short when long.
    static QString proposedName( const QString& selectedText );

    // The most matches of the first Log Line found the preview marks.
    static constexpr qsizetype MaxPreviewMarks = 50;

    explicit AddPredefinedFilterDialog( const QString& selectedText, QWidget* parent = nullptr );
    ~AddPredefinedFilterDialog() override;

    // Previews the filter in the Log File of logData, which the dialog holds
    // on to while it is open: it is searched as the Search of the tab reads
    // the filter, with the buttons of its Search Line, searchFlags, and its
    // pattern prepared as the tab's Searches prepare theirs (prepare, from
    // CrawlerWidget::searchPreparation()), so that it finds what the Search
    // with it finds. Without a call there is no Log File to preview in.
    void previewIn( std::shared_ptr<const LogData> logData, const SearchLine::Flags& searchFlags,
                    FilterPreviewSearch::PreparePattern prepare = {} );

    // What the preview found last.
    FilterPreviewSearch::Result previewResult() const;

    // The layout shared with Name This Value.
    const AddToGroupLayout& addToGroupLayout() const
    {
        return *layout_;
    }

    // Offers the Team groups too, the Team Folder being on: under a heading
    // of their own, with New Team Filter Group.... Only when editable can
    // they be chosen. revisions holds the revision of each group's file, by
    // id. Without a call there is no Team section.
    void showTeamGroups( const QList<PredefinedFilterSet>& groups, bool editable,
                         const QHash<QString, QString>& revisions );

    // Proposes the group of the id, the one chosen last; the Default Filter
    // Group when it is gone.
    void proposeGroup( const QString& groupId );

    // The Predefined Filter OK adds, as edited.
    PredefinedFilter filter() const;

    // The id of the Filter Group chosen; empty for a new one until OK made
    // it, its id after that.
    QString chosenGroupId() const;

    // The Group Chooser.
    GroupChooser* groupChooser() const
    {
        return group_;
    }

    // Whether the Search is to run with the filter once it is added.
    bool applyNow() const;

    // Why OK is disabled: the pattern is empty or no valid regex, the Filter
    // Group chosen has a filter of the name, or the new group's name will not
    // do. Empty when it is enabled or the name is empty.
    QString warning() const;

public Q_SLOTS:
    void accept() override;

Q_SIGNALS:
    // OK changed a Team group or made one: the requests to publish it.
    void publishRequested( const QList<logsquirl::teamfolder::PublishRequest>& requests );

private:
    void updateState();
    // Another group is chosen: a name still as proposed is proposed again,
    // free in that group.
    void groupChanged();
    // Regular expression was checked or unchecked: the pattern is turned into
    // a regex, or back into the plain text it was turned from.
    void regexToggled( bool regex );

    // The names of the filters of the Filter Group chosen.
    QStringList chosenGroupNames() const;

    // The pattern the preview searches for, as edited.
    RegularExpressionPattern previewPattern() const;
    // The pattern or Regular expression changed: the preview searches again.
    void searchPreview();
    // The preview's Search told what it found.
    void showPreview();

    // The selection the dialog was opened for.
    QString selectedText_;
    // The plain text the pattern was before Regular expression turned it into
    // a regex.
    QString plainPattern_;
    // The name last proposed: changed by the user once the name is another.
    QString proposedFilterName_;

    std::unique_ptr<AddToGroupLayout> layout_;
    QLineEdit* name_ = nullptr;
    QLineEdit* pattern_ = nullptr;
    QCheckBox* regex_ = nullptr;
    GroupChooser* group_ = nullptr;
    QCheckBox* applyNow_ = nullptr;
    // The user's own groups, as read when the dialog opened, and the Team
    // groups once it is given them: what group_ offers.
    std::unique_ptr<ChooserGroups<PredefinedFilterSet>> groups_;

    // How the tab's Search reads a pattern.
    SearchLine::Flags searchFlags_;
    std::unique_ptr<FilterPreviewSearch> previewSearch_;
};
