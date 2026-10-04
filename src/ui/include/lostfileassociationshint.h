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

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "fileassociations.h"

class QLabel;
class QPushButton;

// A quiet hint, shown in the status bar of the main window, that file types
// the user chose to open in LogSquirl no longer open in it, as when another
// application or a Windows update took them over, or that their associations
// point at where a moved portable LogSquirl was (#725). It names the types
// and offers Restore and Dismiss; either one ends it.
class LostFileAssociationsHint : public QWidget {
    Q_OBJECT

public:
    // The types are the ids of the lost ones; movedFrom is where a moved
    // portable LogSquirl was, empty otherwise.
    LostFileAssociationsHint( FileAssociations& fileAssociations, const QStringList& lost,
                              const QString& movedFrom, QWidget* parent = nullptr );

    QStringList lost() const
    {
        return lost_;
    }

    QLabel* textLabel() const
    {
        return textLabel_;
    }
    QPushButton* restoreButton() const
    {
        return restoreButton_;
    }
    QPushButton* dismissButton() const
    {
        return dismissButton_;
    }

Q_SIGNALS:
    // The user dismissed the hint for the lost types.
    void dismissed( const QStringList& lost );
    // The hint is done with, after Restore or Dismiss.
    void finished();

private:
    void restore();

    QPointer<FileAssociations> fileAssociations_;
    QStringList lost_;

    QLabel* textLabel_ = nullptr;
    QPushButton* restoreButton_ = nullptr;
    QPushButton* dismissButton_ = nullptr;
};
