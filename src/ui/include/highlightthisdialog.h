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

#include <QColor>
#include <QDialog>
#include <QList>
#include <QString>

#include "highlighter.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

// What "Highlight this..." of the context menu of the Text View and the
// Filtered View opens (#896): a compact dialog turning the text selected in a
// Log Line into a Highlighter. It proposes the selection, escaped, as the
// pattern, so that the Highlighter matches it as plain text; the user picks
// the colors, whether it colors the whole Log Line or only the match, and the
// Highlighter Set it goes into: one of the user's own, the active one by
// default, or a new one. The preview shows the Log Line as the Highlighter
// colors it.
//
// OK adds the Highlighter at the end of the set, activates the set unless it
// is active, and saves HighlighterSetCollection. Recoloring the views is the
// caller's.
class HighlightThisDialog : public QDialog {
    Q_OBJECT

public:
    // selectedText is the text selected in logLine, both as the view matches
    // its Highlighters against them.
    HighlightThisDialog( const QString& selectedText, const QString& logLine,
                         QWidget* parent = nullptr );

    // The Highlighter OK adds, as edited.
    Highlighter highlighter() const;

    // The id of the Highlighter Set chosen, empty for a new one.
    QString chosenSetId() const;

    // The Log Line of the preview, as rich text.
    QString previewHtml() const;

    // What the preview warns of, empty when the pattern matches the Log Line.
    QString previewWarning() const;

public Q_SLOTS:
    void accept() override;

private:
    void setColors( const QColor& foreColor, const QColor& backColor );
    void setChosen();
    void updatePreview();

    QString logLine_;
    // The user's own sets, as read when the dialog opened.
    QList<HighlighterSet> sets_;
    QColor foreColor_;
    QColor backColor_;
    // Whether the user picked the colors: until then they follow the set
    // chosen, as a new Highlighter of the Highlighters dialog does.
    bool colorsPicked_ = false;

    QLineEdit* pattern_ = nullptr;
    QPushButton* foreColorButton_ = nullptr;
    QPushButton* backColorButton_ = nullptr;
    QRadioButton* wholeLine_ = nullptr;
    QRadioButton* onlyMatch_ = nullptr;
    QComboBox* set_ = nullptr;
    QLineEdit* newSetName_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* warning_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
