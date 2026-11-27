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
#include <QObject>

class QLayout;
class QPushButton;
class QRadioButton;
class QWidget;

// The rows of a dialog that make a Highlighter from what it is given -- Highlight
// This (#896), Add as Predefined Filter's Also highlight it (#986) -- saying how
// the Highlighter colors: the Colors row, the presets as swatches and the
// buttons picking a color freely, and the Highlight row, the whole Log Line or
// only the matched text. The dialog places the rows in its form.
//
// Until the user picks colors they follow the Highlighter Set chosen, as a
// new Highlighter of the Highlighters dialog does: the preset after the one of
// the Highlighter before.
class HighlighterColoring : public QObject {
    Q_OBJECT

public:
    // Makes the widgets of the rows, children of parent.
    explicit HighlighterColoring( QWidget* parent );

    // The fields of the two rows, which the dialog adds with their labels.
    QLayout* colorsField() const
    {
        return colors_;
    }
    QLayout* highlightField() const
    {
        return highlight_;
    }

    QColor foreColor() const
    {
        return foreColor_;
    }
    QColor backColor() const
    {
        return backColor_;
    }
    // Whether only the matched text is colored, not the whole Log Line.
    bool onlyMatch() const;

    // The set chosen holds that many Highlighters: unless the user picked
    // colors, they are those a new Highlighter of it takes.
    void followSet( qsizetype existingHighlighters );

Q_SIGNALS:
    // The colors or what is colored changed.
    void changed();

private:
    void setColors( const QColor& foreColor, const QColor& backColor );

    QColor foreColor_;
    QColor backColor_;
    // Whether the user picked the colors.
    bool colorsPicked_ = false;

    QLayout* colors_ = nullptr;
    QLayout* highlight_ = nullptr;
    QPushButton* foreColorButton_ = nullptr;
    QPushButton* backColorButton_ = nullptr;
    QRadioButton* wholeLine_ = nullptr;
    QRadioButton* onlyMatch_ = nullptr;
};
