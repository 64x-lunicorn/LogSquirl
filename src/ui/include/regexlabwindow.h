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

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

#include <QColor>
#include <QFlags>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
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
class RegexLabMarks;
class QPushButton;
class QTableWidget;
class QTimer;

// Log Lines the Regex Lab takes as its sample: how many there are, and how to
// read their text -- the whole text, as a Search matches it. read is called
// off the UI thread and must hold on to whatever it reads from.
struct RegexLabSample {
    std::size_t count = 0;
    std::function<logsquirl::vector<QString>()> read;
};

// Where the Regex Lab takes sample Log Lines from: the tab it is tied to.
// Asked only when the user asks for a sample, never continuously. Without a
// Log File, both are empty and only pasted text is a sample.
// regexLabSampleSource() (regexlabsource.h) makes one for a tab.
struct RegexLabSampleSource {
    // The tab's name, for the window's title.
    QString name;
    // The tab itself: once it is destroyed, the Lab has no Log File.
    QPointer<QObject> tab;
    // The selected Log Lines, and the Log Lines around the current line of
    // the view last in focus; at most as many as asked for.
    std::function<RegexLabSample( LinesCount )> selectedLines;
    std::function<RegexLabSample( LinesCount )> linesAroundCurrentLine;

    bool hasLogFile() const
    {
        return selectedLines && linesAroundCurrentLine;
    }
};

// What an editor opens the Regex Lab with (#660): the engine a Search runs
// on, whether a Search starts out matching case, and where the tab in front
// takes its sample from, asked for as the Lab opens. The editors are dialogs
// that know no tab, so whoever opens them hands this over. Without a sample
// source, only pasted text is a sample.
struct RegexLabAccess {
    RegexpEngine searchEngine = RegexpEngine::Vectorscan;
    bool searchMatchesCase = false;
    std::function<RegexLabSampleSource()> sampleSource;
};

// The Regex Lab (#659): a window, not modal, that shows what a pattern does
// on sample Log Lines, with the Search's engine and options. It changes no
// Search, Highlighter or filter by itself.
//
// The pattern and its options come in with setPattern() and go out with
// pattern(). Whoever opens the Lab to edit a pattern of its own offers Apply
// (offerApply()) and hears exactly one of applied() or cancelled(), on the UI
// thread: Apply, Cancel, closing the window and destroying it -- also along
// with its parent, which may be a modal dialog -- each answer once, and
// nothing is answered after that. Opened from the menu, the Lab offers
// neither and only copies the pattern. A receiver that goes away while the
// Lab is open is disconnected by Qt, so the Lab never calls into it.
class RegexLabWindow : public QWidget {
    Q_OBJECT

public:
    // Which sample the pattern is evaluated on.
    enum class Sample { SelectedLines, LinesAroundCurrentLine, PastedText };

    // The options a pattern is read with.
    enum class Option {
        MatchCase = 0x1,
        UseRegexp = 0x2,
        Inverse = 0x4,
        LogicalCombination = 0x8,
    };
    Q_DECLARE_FLAGS( Options, Option )

    // The colors a mark is shown in instead of the Lab's own.
    struct MarkColors {
        QColor text;
        QColor background;
    };

    explicit RegexLabWindow( RegexpEngine engine, QWidget* parent = nullptr );
    ~RegexLabWindow() override;

    RegexLabWindow( const RegexLabWindow& ) = delete;
    RegexLabWindow& operator=( const RegexLabWindow& ) = delete;

    // The pattern and the options it is read with, as the Search Line has
    // them.
    void setPattern( const RegularExpressionPattern& pattern );
    RegularExpressionPattern pattern() const;

    // The options whoever opened the Lab keeps with its pattern (#660): only
    // those can be changed. One it does not keep is shown, but cannot be
    // changed, when it is in shownFixed -- it still decides how the pattern
    // is read -- and hidden otherwise. It keeps the value setPattern() gave
    // it. All options are kept until this is called.
    void setOptionsKept( Options kept, Options shownFixed = {} );

    // What of a matching line is marked, and in which colors (#660): each
    // match in the Lab's color for its sub-pattern until this is called. A
    // change evaluates the pattern again.
    void setMarking( regexlab::Marking marking, std::optional<MarkColors> colors = std::nullopt );
    regexlab::Marking marking() const;

    // The engine a Search runs on, which the Lab matches with. A change
    // evaluates the pattern again.
    void setEngine( RegexpEngine engine );
    RegexpEngine engine() const;

    // Ties the Lab to a tab, and takes a sample from it: the selected Log
    // Lines, or the lines around the current one when none is selected.
    void setSampleSource( RegexLabSampleSource source );

    Sample sample() const;
    void setSample( Sample sample );
    // Takes the sample from the tab again. The text is read off the UI
    // thread; sampleTaken() says when it is shown.
    void refreshSample();

    // Shows Apply and Cancel instead of Close.
    void offerApply( bool offer );

    // What the last evaluation shown found.
    const regexlab::Result& result() const;

    // How far an evaluation goes.
    static regexlab::Bounds bounds();

Q_SIGNALS:
    // A sample from the tab is shown.
    void sampleTaken();
    // An evaluation's result is shown.
    void evaluated();
    // With Apply offered: the user applied this pattern, or did not. The Lab
    // closes after applied().
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
    // Shows the sample chosen; one from the tab is taken, the one given if
    // any.
    void sampleChosen( std::optional<RegexLabSample> taken = std::nullopt );
    void takeSample( std::optional<RegexLabSample> taken );
    void pastedTextEdited();
    // The pasted text as sample lines: the first bounds().maxLines of them,
    // without the empty line after a final line feed.
    std::shared_ptr<const logsquirl::vector<QString>> pastedLines();
    void evaluate();
    void showResult( regexlab::Result result );
    void showGroups();
    void showError();
    void showStatus();

    void copyPattern();
    void apply();
    // Says cancelled() unless the Lab has answered already.
    void cancel();

    RegexpEngine engine_;
    RegexLabSampleSource source_;
    QMetaObject::Connection tabDestroyed_;
    // The whole text of the sample last taken from the tab, and the text the
    // user pasted while another sample is shown.
    std::shared_ptr<const logsquirl::vector<QString>> logFileSample_;
    QString pastedText_;
    // The sample the text shows; pasted text until one is chosen.
    Sample shownSample_ = Sample::PastedText;
    bool isShowingLogFileSample_ = false;
    // Whether the pasted text last evaluated had more lines than are
    // evaluated.
    bool hasMorePastedLines_ = false;

    regexlab::Result result_;
    // The pattern and options the result shown was evaluated with.
    RegularExpressionPattern evaluatedPattern_;
    regexlab::Marking marking_ = regexlab::Marking::Matches;
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
    QLabel* errorMarker_ = nullptr;
    QComboBox* sampleChoice_ = nullptr;
    QPushButton* refreshSample_ = nullptr;
    QPlainTextEdit* sampleText_ = nullptr;
    RegexLabMarks* marks_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* warning_ = nullptr;
    QTableWidget* groups_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;

    QTimer* debounce_ = nullptr;
    // Reads a sample from the tab, and evaluates, off the UI thread, one at
    // a time each: a newer one lets go of the older, whose result is never
    // shown, and closing the window stops both.
    LookupRunner sampleReader_;
    LookupRunner runner_;
};

Q_DECLARE_OPERATORS_FOR_FLAGS( RegexLabWindow::Options )

#endif
