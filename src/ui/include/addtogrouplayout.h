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

#include <QCoreApplication>
#include <QLabel>
#include <QList>
#include <QString>
#include <QStringList>

class QDialog;
class QDialogButtonBox;
class QFormLayout;
class QLayout;
class QWidget;

// One line of text with parts of it marked, as a preview shows a Log Line
// (#958): never wrapped, and elided where it is wider than the label -- around
// the first mark, so that what is marked stays in sight.
class LinePreview : public QLabel {
    Q_OBJECT

public:
    // How the marked parts show: underlined, as the views mark a Named Value,
    // or in the selection's colors, as a Match stands out.
    enum class MarkStyle { Underline, Highlight };

    struct Mark {
        qsizetype start = 0;
        qsizetype length = 0;
    };

    explicit LinePreview( QWidget* parent = nullptr );

    // Shows text, with the marks: in order, not overlapping, within it. Tabs
    // and line breaks show as blanks.
    void setLine( const QString& text, const QList<Mark>& marks, MarkStyle style );
    // Shows a remark instead of a line, greyed: why there is none.
    void setRemark( const QString& remark );

    // What shows, without markup: the line as elided, an ellipsis where it is
    // cut; the remark when there is one.
    QString shownText() const
    {
        return shownText_;
    }
    // The parts of shownText() that are marked.
    QStringList markedTexts() const
    {
        return markedTexts_;
    }

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void resizeEvent( QResizeEvent* event ) override;
    void changeEvent( QEvent* event ) override;

private:
    void render();

    QString line_;
    QList<Mark> marks_;
    MarkStyle style_ = MarkStyle::Underline;
    QString remark_;
    QString shownText_;
    QStringList markedTexts_;
};

// The layout the dialogs that add one thing from the Log Line to a group share
// (#958): Add as Predefined Filter... and Name This Value. From top to bottom,
// in one form whose labels line up: what is added, where it goes (the Group
// Chooser first), the preview of the result in one line; below the form the
// warning line, then OK and Cancel. Every dialog using it is as wide, spaced
// and ordered as the others.
//
// The dialog adds its rows to a section in any order; they show in the order
// of the sections.
class AddToGroupLayout {
    Q_DECLARE_TR_FUNCTIONS( AddToGroupLayout )

public:
    // The sections of the form, top to bottom.
    enum class Section { What, Where, Preview };

    // The width the dialogs open with.
    static constexpr int DialogWidth = 600;

    // Lays out dialog, which it takes no ownership of.
    explicit AddToGroupLayout( QDialog* dialog );

    // Adds a row to the end of the section; an empty label leaves the label
    // column blank, as for a check box.
    void addRow( Section section, const QString& label, QWidget* field );
    void addRow( Section section, const QString& label, QLayout* field );

    // Shows the warning, or hides the line when it is empty.
    void setWarning( const QString& warning );

    // Sizes the dialog to DialogWidth and the height its rows need.
    void fitDialog();

    // The preview row: the line, and right of it a note on it, hidden until
    // the dialog sets one (how many Log Lines a filter finds).
    LinePreview* preview() const
    {
        return preview_;
    }
    QLabel* previewNote() const
    {
        return previewNote_;
    }
    QLabel* warning() const
    {
        return warning_;
    }
    QDialogButtonBox* buttons() const
    {
        return buttons_;
    }
    QFormLayout* form() const
    {
        return form_;
    }

    // The sections of the rows of form(), in their order.
    QList<Section> rowSections() const
    {
        return rowSections_;
    }

private:
    // The row of form() a row added to the end of section goes to.
    int endOf( Section section ) const;
    void inserted( Section section, int row );

    QDialog* dialog_;
    QFormLayout* form_ = nullptr;
    LinePreview* preview_ = nullptr;
    QLabel* previewNote_ = nullptr;
    QLabel* warning_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QList<Section> rowSections_;
};
