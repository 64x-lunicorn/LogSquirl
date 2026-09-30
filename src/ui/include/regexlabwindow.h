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

#ifndef LOGSQUIRL_REGEXLABWINDOW_H
#define LOGSQUIRL_REGEXLABWINDOW_H

#include <functional>

#include <QString>
#include <QWidget>

#include "containers.h"
#include "linetypes.h"
#include "lookuprunner.h"
#include "regexlab.h"
#include "regexpengine.h"
#include "regularexpressionpattern.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

// Where the Regex Lab takes sample Log Lines from: the tab it is tied to.
// Asked only when the user asks for a sample, never continuously. Without a
// Log File, both are empty and only pasted text is a sample.
struct RegexLabSampleSource {
    // The tab's name, for the window's title.
    QString name;
    // The text of the selected Log Lines, and of the Displayed Lines around
    // the current line; at most as many as asked for.
    std::function<logsquirl::vector<QString>( LinesCount )> selectedLines;
    std::function<logsquirl::vector<QString>( LinesCount )> linesAroundCurrentLine;

    bool hasLogFile() const
    {
        return selectedLines && linesAroundCurrentLine;
    }
};

// The Regex Lab (#659): a window, not modal, that shows what a pattern does
// on sample Log Lines, with the Search's engine and options. It changes no
// Search, Highlighter or filter by itself.
//
// The pattern and its options come in with setPattern() and go out with
// pattern(). Whoever opens the Lab to edit a pattern of its own offers Apply
// (offerApply()) and hears applied() or cancelled(), exactly one of them,
// on the UI thread; opened from the menu, the Lab offers neither and only
// copies the pattern. A caller that goes away while the Lab is open is let
// go of by Qt with its connections.
class RegexLabWindow : public QWidget {
    Q_OBJECT

public:
    // Which sample the pattern is evaluated on.
    enum class Sample { SelectedLines, LinesAroundCurrentLine, PastedText };

    explicit RegexLabWindow( RegexpEngine engine, QWidget* parent = nullptr );
    ~RegexLabWindow() override;

    RegexLabWindow( const RegexLabWindow& ) = delete;
    RegexLabWindow& operator=( const RegexLabWindow& ) = delete;

    // The pattern and the options it is read with, as the Search Line has
    // them.
    void setPattern( const RegularExpressionPattern& pattern );
    RegularExpressionPattern pattern() const;

    // The engine a Search runs on, which the Lab matches with.
    void setEngine( RegexpEngine engine );

    // Ties the Lab to a tab, and takes a sample from it: the selected Log
    // Lines, or the lines around the current one when none is selected.
    void setSampleSource( RegexLabSampleSource source );

    Sample sample() const;
    void setSample( Sample sample );
    // Takes the sample from the tab again.
    void refreshSample();

    // Shows Apply and Cancel instead of Close.
    void offerApply( bool offer );

    // What the last evaluation shown found.
    const regexlab::Result& result() const;

    // How far an evaluation goes.
    static regexlab::Bounds bounds();

Q_SIGNALS:
    // An evaluation's result is shown.
    void evaluated();
    // With Apply offered: the user applied this pattern, or cancelled. The
    // Lab closes after either.
    void applied( const RegularExpressionPattern& pattern );
    void cancelled();

protected:
    void closeEvent( QCloseEvent* event ) override;

private:
    void buildWidgets();
    void updateTitle();
    void updateSampleChoices();

    // The pattern or its options changed: the evaluation in flight is let go
    // of, and a new one starts once the user pauses.
    void patternEdited();
    void sampleChosen();
    void pastedTextEdited();
    void evaluate();
    void showResult( regexlab::Result result );
    void showGroups();
    void showStatus();

    logsquirl::vector<QString> sampleLines() const;
    void copyPattern();
    void apply();

    RegexpEngine engine_;
    RegexLabSampleSource source_;
    // The sample last taken from the tab, and the text the user pasted.
    logsquirl::vector<QString> logFileSample_;
    QString pastedText_;
    bool isShowingLogFileSample_ = false;

    regexlab::Result result_;
    bool hasPattern_ = false;
    bool isApplyOffered_ = false;
    bool isAnswered_ = false;

    QLineEdit* patternEdit_ = nullptr;
    QCheckBox* matchCase_ = nullptr;
    QCheckBox* useRegexp_ = nullptr;
    QCheckBox* inverse_ = nullptr;
    QCheckBox* logicalCombination_ = nullptr;
    QPushButton* copyPattern_ = nullptr;
    QLabel* error_ = nullptr;
    QComboBox* sampleChoice_ = nullptr;
    QPushButton* refreshSample_ = nullptr;
    QPlainTextEdit* sampleText_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* warning_ = nullptr;
    QTableWidget* groups_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;

    QTimer* debounce_ = nullptr;
    // Evaluates off the UI thread, one pattern at a time: a newer one lets
    // go of the older, whose result is never shown, and closing the window
    // stops it.
    LookupRunner runner_;
};

#endif
