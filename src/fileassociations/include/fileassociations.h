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

#include <QObject>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "filetypes.h"

// Where a file type stands with LogSquirl.
enum class FileAssociationState {
    // LogSquirl opens it.
    Default,
    // LogSquirl is offered for it, but another application opens it.
    Registered,
    // LogSquirl is not offered for it.
    NotRegistered,
};

// The state of each type of FileTypes::choices(), by id.
using FileAssociationStates = std::map<QString, FileAssociationState>;

// The state of the type with the id; NotRegistered for an id without one.
FileAssociationState stateOf( const FileAssociationStates& states, const QString& id );

// Whether LogSquirl opens the type with the id.
bool isDefault( const FileAssociationStates& states, const QString& id );

// What applying a choice did. A platform where the user confirms the choice
// elsewhere may still change its mind after apply() returned; the states
// tell, not the result.
struct FileAssociationResult {
    // The ids of the types that could not be changed.
    QStringList failed;
    // Why, in one line for the user; empty when everything went through.
    QString error;

    bool succeeded() const
    {
        return failed.isEmpty() && error.isEmpty();
    }

    // Nothing went through: every type of makeDefault and of release failed,
    // for the reason of error.
    static FileAssociationResult failedFor( const std::vector<FileType>& makeDefault,
                                            const std::vector<FileType>& release,
                                            const QString& error );
};

// Tells the state of a file type and applies the user's choice, one
// implementation per platform behind this one interface (#720): the
// File Associations page of the Options Dialog, the first-start dialog
// (#723) and the lost-association hint (#725) use it, and a test
// implementation stands in for the machine in their tests.
//
// The states are always what the system says, never what was asked for. A
// platform where the user confirms a choice outside LogSquirl (Windows' Default
// apps page, a confirmation of macOS) applies it as far as it can, returns,
// and emits statesChanged() once it learns more; whoever shows the states
// reads them again then.
class FileAssociations : public QObject {
    Q_OBJECT

public:
    ~FileAssociations() override;

    // Whether this run can associate file types at all.
    virtual bool isAvailable() const = 0;

    // When it cannot, why, in one short line for the user ("An AppImage
    // cannot ..."). Empty when it can.
    virtual QString unavailableReason() const = 0;

    // A line to show before Apply about what applying leads to outside
    // LogSquirl, such as a page of the system the user confirms the choice on.
    // Empty when it just happens.
    virtual QString applyNote() const;

    // Where the type stands now. Only asked while the run is available.
    virtual FileAssociationState state( const FileType& type ) const = 0;

    // Makes LogSquirl the default for each type of makeDefault, and gives back
    // each type of release LogSquirl is the default for. Giving back only
    // removes what LogSquirl set; it does not choose another application.
    virtual FileAssociationResult apply( const std::vector<FileType>& makeDefault,
                                         const std::vector<FileType>& release ) = 0;

    // The state of every type the user chooses from. A platform that can
    // read them faster together than one by one reads them so.
    virtual FileAssociationStates states() const;

    // Whether the platform has an entry "Open with LogSquirl" in the context
    // menu of every file, which opens files of any type, such as rotated logs
    // like app.log.1 (#724): Explorer's on Windows. None by default.
    virtual bool offersContextMenuEntry() const;

    // Whether every file's context menu has the entry now.
    virtual bool hasContextMenuEntry() const;

    // Adds the entry or removes it, for the current user.
    virtual FileAssociationResult setContextMenuEntry( bool shown );

    // Where a portable LogSquirl was when it applied the associations that
    // still point there, now that it was moved (#725): the executable they
    // open. Applying them again points them at this run. Empty when they
    // point at this run, and on a platform where nothing points at a place.
    virtual QString movedFrom() const;

Q_SIGNALS:
    // The states changed outside apply(), or after it returned.
    void statesChanged();
};

// What applying a choice changes: the checked types LogSquirl is not the
// default for yet become it, the unchecked ones it is the default for are
// given back, and every other type is left as it is.
struct FileAssociationPlan {
    std::vector<FileType> makeDefault;
    std::vector<FileType> release;

    bool isEmpty() const
    {
        return makeDefault.empty() && release.empty();
    }

    // The plan for checking the types with checkedIds among
    // FileTypes::choices(), the others unchecked, given their states now.
    static FileAssociationPlan of( const FileAssociationStates& states,
                                   const QStringList& checkedIds );

    // Applies the plan with the file associations: what Apply does on the
    // File Associations page and in the first-start dialog. Nothing for an
    // empty plan.
    std::optional<FileAssociationResult> applyWith( FileAssociations& fileAssociations ) const;
};

// The implementation of this platform and this run: xdg-mime on Linux and
// the other freedesktop.org systems, LaunchServices on macOS (#721), the
// registry of the current user on Windows (#722), and one that is never
// available on any other system.
std::unique_ptr<FileAssociations> createFileAssociations();
