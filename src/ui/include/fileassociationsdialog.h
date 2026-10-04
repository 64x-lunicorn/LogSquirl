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
#include <QPointer>
#include <QStringList>

#include "fileassociationchoice.h"
#include "fileassociations.h"

class Configuration;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// The file types the user chooses from as a tree, grouped as the list groups
// them, one checkable row per type with its id under Qt::UserRole: what the
// File Associations page (#720) and the first-start dialog (#723) show.
namespace FileTypeChoices {

// Fills the tree with the types, every row unchecked.
void fill( QTreeWidget& tree );

// The row of the type with the id; nullptr if none.
QTreeWidgetItem* row( const QTreeWidget& tree, const QString& id );

// The ids of the checked rows, in the order of the list.
QStringList checkedIds( const QTreeWidget& tree );

} // namespace FileTypeChoices

// The choice as the settings keep it, and keeping it.
FileAssociationChoice fileAssociationChoice( const Configuration& config );
void keepFileAssociationChoice( Configuration& config, const FileAssociationChoice& choice );

// The question of the first start (#723): which file types LogSquirl opens,
// with the suggested ones checked. Later asks again at the next start, Don't
// ask again never asks again, Apply applies the choice as the File
// Associations page does.
class FirstStartFileAssociationsDialog : public QDialog {
    Q_OBJECT

public:
    enum class Answer { None, Later, DontAskAgain, Applied };

    FirstStartFileAssociationsDialog( FileAssociations& fileAssociations, const QStringList& checks,
                                      QWidget* parent = nullptr );

    // How the user answered; None while the dialog is open.
    Answer answer() const
    {
        return answer_;
    }

    // The ids of the types checked now.
    QStringList checkedIds() const;

    // The choice as the answer leaves it.
    void updateChoice( FileAssociationChoice& choice ) const;

    QTreeWidget* tree() const
    {
        return tree_;
    }
    QLabel* noteLabel() const
    {
        return noteLabel_;
    }
    QLabel* pageLabel() const
    {
        return pageLabel_;
    }
    QPushButton* laterButton() const
    {
        return laterButton_;
    }
    QPushButton* dontAskAgainButton() const
    {
        return dontAskAgainButton_;
    }
    QPushButton* applyButton() const
    {
        return applyButton_;
    }

Q_SIGNALS:
    // The user answered; the dialog closes.
    void answered( Answer answer );

private:
    void finish( Answer answer );
    void applyChoice();

    QPointer<FileAssociations> fileAssociations_;
    Answer answer_ = Answer::None;

    QTreeWidget* tree_ = nullptr;
    QLabel* noteLabel_ = nullptr;
    QLabel* pageLabel_ = nullptr;
    QPushButton* laterButton_ = nullptr;
    QPushButton* dontAskAgainButton_ = nullptr;
    QPushButton* applyButton_ = nullptr;
};
