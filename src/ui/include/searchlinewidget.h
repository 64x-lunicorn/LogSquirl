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

#include <map>
#include <optional>

#include <QList>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "linetypes.h"
#include "predefinedfilter.h"
#include "regularexpressionpattern.h"
#include "searchautorefresh.h"
#include "searchline.h"
#include "searchsession.h"
#include "settingspolicies.h"
#include "shortcuts.h"

class IconLoader;
class InfoLine;
class QComboBox;
class QCompleter;
class QMenu;
class QShortcut;
class QToolButton;

// The Search Line as the user sees it (#638): the pattern's combo box and its
// completer, the five buttons that say how the pattern is read, the Search,
// Stop, Clear and Keep Results buttons, the info line and the shortcuts of
// the buttons. It holds the Search Line and mirrors it: a button the user
// sets reaches the Search Line, and what the Search Line says -- its flags,
// its pattern, its display -- reaches the buttons and the info line.
//
// It exchanges values and signals with the Crawler Widget only. It is told
// what the Search Session does and asks for a Search to run or stop; it never
// starts one itself (#538): an edited pattern answers whether the Search is to
// run, and the Crawler Widget asks for it once the edit is shown.
//
// Its texts are translated in the context of the Crawler Widget, which showed
// them before the Search Line was a widget of its own.
class SearchLineWidget : public QWidget {
    Q_OBJECT

public:
    // The line starts in the state the QuickFind Policy says, with the
    // search history offered in its combo box and the latest Search of it as
    // the pattern.
    SearchLineWidget( const QuickFindPolicy& startingState, const QStringList& history,
                      QWidget* parent = nullptr );

    // A Policy arriving later changes whether an edited pattern runs the
    // Search at once. It leaves the buttons as the user has set them.
    void setQuickFindPolicy( const QuickFindPolicy& policy );

    SearchLine::Flags flags() const;
    // Sets the buttons; flagsChanged() is sent once when any of them changed.
    void setFlags( const SearchLine::Flags& flags );

    QString pattern() const;
    // The Search the pattern and the buttons ask for.
    RegularExpressionPattern request() const;

    // Edit the pattern with a word the user chose, as the Search Line does.
    // The buttons the edit switched are set before the pattern is shown, and
    // the pattern edit gets the focus. True when the Search is to run now:
    // the caller asks for it with requestSearch() once this has returned.
    bool add( const QString& word );
    bool exclude( const QString& word );
    bool replace( const QString& word );
    bool useFilters( const QList<PredefinedFilter>& filters );

    // What the Search Session does, told as it happens; the buttons and the
    // info line show what the Search Line makes of it.
    void requested( const SearchSession::State& state );
    void progressed( const SearchSession::State& state, SearchAutoRefresh::State autoRefresh );
    void stopped( SearchAutoRefresh::State autoRefresh, LinesCount matchCount );
    void cleared();
    void settled( SearchAutoRefresh::State autoRefresh, LinesCount matchCount );

    SearchLine::Display display() const;

    // The search history offered while typing; the pattern stays.
    void setHistory( const QStringList& history );
    // No search history any more, and no pattern.
    void clearHistory();

    // The shortcuts of the five flag buttons and of Keep Results, as
    // configured, registered anew. They answer wherever the focus is in scope
    // -- the Crawler Widget, so that they fire from its views too (ADR 0016).
    void registerShortcuts( const ShortcutAction::ConfiguredShortcuts& configuredShortcuts,
                            QWidget* scope );

    // The buttons' icons, in the active Theme's variant.
    void loadIcons( IconLoader& iconLoader );

    // The pattern edit's own context menu -- undo, cut, copy, paste -- for
    // the caller to add its entries to.
    QMenu* createStandardContextMenu() const;

public Q_SLOTS:
    // What the Search button and Return do: asks for the Search to run, and
    // says whether Keep Results was set, which it then no longer is.
    void requestSearch();

Q_SIGNALS:
    // The Search is to run; with keepResults the current one is kept and the
    // new one gets a tab of its own.
    void searchRequested( bool keepResults );
    // The Search is to stop.
    void stopRequested();
    // The buttons now say flags; they said previous before.
    void flagsChanged( SearchLine::Flags flags, SearchLine::Flags previous );
    // The user typed in the pattern.
    void patternEdited();
    // The user asked for the context menu of the pattern edit.
    void contextMenuRequested();

public:
    template <class T>
    struct access_by;

protected:
    // Keeps Return, which the pattern edit's line edit has taken, from the
    // combo box around it (#648).
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    // A button the user set: the Search Line takes all of them.
    void takeFlagsFromButtons();
    // The buttons show the Search Line's flags, which were previous; sends
    // flagsChanged() once if they differ.
    void showFlags( const SearchLine::Flags& previous );
    // Shows the pattern the Search Line was edited to, the buttons first, and
    // passes runNow on.
    bool showEditedPattern( const SearchLine::Flags& previous, bool runNow );
    // Mirrors what the Search Line shows: its text, gauge and buttons.
    void showDisplay();
    // The info line's palette for an error: the default palette in the
    // Theme's error colors.
    QPalette errorPalette() const;

    SearchLine searchLine_;

    QComboBox* patternEdit_;
    // When the Return key press the line edit has already taken was made (#648).
    std::optional<quint64> takenReturnAt_;
    QCompleter* completer_;

    QToolButton* matchCaseButton_;
    QToolButton* useRegexpButton_;
    QToolButton* inverseButton_;
    QToolButton* booleanButton_;
    QToolButton* autoRefreshButton_;

    QToolButton* clearButton_;
    QToolButton* searchButton_;
    QToolButton* keepResultsButton_;
    QToolButton* stopButton_;

    InfoLine* infoLine_;
    // The palette the info line shows when not in error, captured again with
    // every Theme (ADR 0004).
    QPalette defaultPalette_;

    // Parented to the scope they answer in, not to this widget.
    std::map<QString, QShortcut*> shortcuts_;

    // The buttons are being set to the Search Line's flags: their toggles are
    // not the user's.
    bool showingFlags_ = false;
};
