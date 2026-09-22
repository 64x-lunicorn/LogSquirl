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

#include <optional>

#include <QList>
#include <QString>

#include "linetypes.h"
#include "predefinedfilter.h"
#include "regularexpressionpattern.h"
#include "searchautorefresh.h"
#include "searchsession.h"
#include "settingspolicies.h"

// The Search Line: the pattern of a Search, the buttons that say how it is
// read, and what the line says about the Search that runs (#399).
//
// It knows no widget. Each event goes in -- the user edits the pattern or a
// button, a word is added to the Search, the Search is requested, progresses,
// stops -- and the Crawler Widget then mirrors flags(), pattern() and
// display(). The search history offered while typing is not part of it.
class SearchLine {
public:
    // How the pattern is read: the buttons of the line.
    struct Flags {
        bool matchCase = false;
        bool useRegexp = false;
        bool inverse = false;
        // The pattern is a logical combination of quoted sub-patterns.
        bool booleanCombination = false;
        bool autoRefresh = false;

        bool operator==( const Flags& ) const = default;
    };

    // Which buttons show: Search and Clear while no Search runs, Stop while
    // one does.
    enum class Buttons { Search, Stop };

    // What the line shows.
    struct Display {
        Buttons buttons = Buttons::Search;
        QString text;
        bool visible = false;
        // The text is an error, shown in the Theme's error colors.
        bool isError = false;
        // The Search's progress, in percent, drawn behind the text.
        std::optional<int> gauge;
        // A failure of the Search the user is offered to report as an issue:
        // told by the event that brought it, empty after the next one.
        QString offerIssueReport;

        bool operator==( const Display& ) const = default;
    };

    // The line starts in the state the QuickFind Policy says, which also
    // says whether a pattern edited by adding a word runs the Search at once.
    explicit SearchLine( const QuickFindPolicy& startingState );

    // A Policy arriving later changes whether an edited pattern runs the
    // Search at once. It leaves the buttons as the user has set them.
    void setQuickFindPolicy( const QuickFindPolicy& policy );

    Flags flags() const;
    void setFlags( const Flags& flags );

    QString pattern() const;
    void setPattern( const QString& pattern );

    // Edit the pattern with a word the user chose; each is escaped as the
    // buttons say. True when the Search is to run now (auto-run).
    //
    // Adds the word as an alternative to the pattern.
    bool add( const QString& word );
    // Keeps the pattern but not the word: switches the logical combination
    // on, which the exclusion is written in.
    bool exclude( const QString& word );
    // The word becomes the pattern.
    bool replace( const QString& word );
    // The Predefined Filters, as alternatives, become the pattern.
    bool useFilters( const QList<PredefinedFilter>& filters );

    // The Search the pattern and the buttons ask for.
    RegularExpressionPattern request() const;

    // A Search was requested: it runs, or its pattern is in error.
    void requested( const SearchSession::State& state );
    // The Search tells how far it came, or that it is done one way or
    // another.
    void progressed( const SearchSession::State& state, SearchAutoRefresh::State autoRefresh );
    // The user stopped the Search; the Filtered View holds the Matches it
    // found until then.
    void stopped( SearchAutoRefresh::State autoRefresh, LinesCount matchCount );
    // The Search was replaced by none: the pattern is empty.
    void cleared();
    // Nothing runs; the line says what is known of the Search: its Matches,
    // a Log File truncated under it, or nothing when there is none.
    void settled( SearchAutoRefresh::State autoRefresh, LinesCount matchCount );

    Display display() const;

private:
    // The word as a sub-pattern of the pattern: escaped when the pattern is
    // read as a regexp and the word is not one, quoted in a logical
    // combination.
    QString escaped( const QString& word, bool isRegexp = false ) const;
    // Appends the sub-pattern to pattern as an alternative.
    QString& combine( QString& pattern, const QString& subPattern ) const;

    // Shows text, not as an error.
    void showText( const QString& text );
    void showError( const QString& text );
    // The gauge goes and the Search and Clear buttons come back.
    void showDone();

    Flags flags_;
    bool autoRun_ = false;
    QString pattern_;
    Display display_;
};
