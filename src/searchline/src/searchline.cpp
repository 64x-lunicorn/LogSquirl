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

#include "searchline.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include "regularexpression.h"

// The texts are translated in the context of the Crawler Widget, which showed
// them before the Search Line was a class of its own, so that the
// translations keep matching them (#399).

SearchLine::SearchLine( const QuickFindPolicy& startingState )
    : flags_{ .matchCase = !startingState.searchIgnoreCaseDefault,
              .useRegexp = startingState.mainRegexpType == SearchRegexpType::ExtendedRegexp,
              .inverse = false,
              .booleanCombination = startingState.searchLogicalCombiningDefault,
              .autoRefresh = startingState.searchAutoRefreshDefault }
    , autoRun_( startingState.autoRunSearchOnPatternChange )
{
}

void SearchLine::setQuickFindPolicy( const QuickFindPolicy& policy )
{
    autoRun_ = policy.autoRunSearchOnPatternChange;
}

SearchLine::Flags SearchLine::flags() const
{
    return flags_;
}

void SearchLine::setFlags( const Flags& flags )
{
    flags_ = flags;
}

QString SearchLine::pattern() const
{
    return pattern_;
}

void SearchLine::setPattern( const QString& pattern )
{
    pattern_ = pattern;
}

QString SearchLine::escaped( const QString& word, bool isRegexp ) const
{
    auto subPattern = ( !isRegexp && flags_.useRegexp ) ? QRegularExpression::escape( word ) : word;

    if ( flags_.booleanCombination ) {
        subPattern = quoteSubPattern( subPattern );
    }

    return subPattern;
}

QString& SearchLine::combine( QString& pattern, const QString& subPattern ) const
{
    if ( !pattern.isEmpty() ) {
        if ( flags_.booleanCombination ) {
            pattern.append( " or " );
        }
        else if ( flags_.useRegexp ) {
            pattern.append( '|' );
        }
    }

    return pattern.append( subPattern );
}

bool SearchLine::isPlain() const
{
    return !flags_.useRegexp && !flags_.booleanCombination;
}

void SearchLine::switchToLogicalCombination()
{
    if ( flags_.booleanCombination ) {
        return;
    }

    // The pattern so far becomes its first sub-pattern, an empty one none
    // (#407).
    if ( !pattern_.isEmpty() ) {
        pattern_ = quoteSubPattern( pattern_ );
    }
    flags_.booleanCombination = true;
}

bool SearchLine::add( const QString& word )
{
    // A fixed string has no alternatives: they are written in a logical
    // combination (#408).
    if ( isPlain() && !pattern_.isEmpty() ) {
        switchToLogicalCombination();
    }

    combine( pattern_, escaped( word ) );
    return autoRun_;
}

bool SearchLine::exclude( const QString& word )
{
    // The exclusion is written in a logical combination.
    switchToLogicalCombination();

    if ( !pattern_.isEmpty() ) {
        pattern_.append( " and " );
    }
    pattern_.append( "not(" ).append( escaped( word ) ).append( ')' );
    return autoRun_;
}

bool SearchLine::replace( const QString& word )
{
    pattern_ = escaped( word );
    return autoRun_;
}

bool SearchLine::useFilters( const QList<PredefinedFilter>& filters )
{
    // Several filters are alternatives, which a fixed string has not (#408).
    if ( isPlain() && filters.size() > 1 ) {
        flags_.booleanCombination = true;
    }

    QString pattern;
    for ( const auto& filter : filters ) {
        combine( pattern, escaped( filter.pattern, filter.useRegex ) );
    }
    pattern_ = std::move( pattern );
    return autoRun_;
}

RegularExpressionPattern SearchLine::request() const
{
    return RegularExpressionPattern( pattern_, flags_.matchCase, flags_.inverse,
                                     flags_.booleanCombination, !flags_.useRegexp );
}

void SearchLine::requested( const SearchSession::State& state )
{
    display_.offerIssueReport.clear();

    if ( state.phase != SearchSession::Phase::InvalidPattern ) {
        display_.buttons = Buttons::Stop;
        display_.visible = false;
        display_.isError = false;
    }
    else {
        showError( QCoreApplication::translate( "CrawlerWidget", "Error in expression" ) + ": "
                   + state.errorString );
        display_.visible = true;
    }
}

void SearchLine::progressed( const SearchSession::State& state,
                             SearchAutoRefresh::State autoRefresh )
{
    using Phase = SearchSession::Phase;

    // No Search: nothing to tell, and nothing to take back of what whoever
    // drove the Search Session idle has shown.
    if ( state.phase == Phase::Idle ) {
        return;
    }

    display_.offerIssueReport.clear();
    display_.visible = true;

    switch ( state.phase ) {
    case Phase::Idle:
        break;
    case Phase::Running:
        // 0 % and 100 % are left out, so that a short Search does not flash.
        if ( state.progress > 0 ) {
            const auto progress = QString::number( state.progress );
            const auto matches = QString::number( state.matchCount.get() );
            // Some languages translate the plural the same as the singular,
            // so the whole text is chosen.
            display_.text
                = QCoreApplication::translate( "CrawlerWidget", "Search in progress (%1 %)..." )
                      .arg( progress )
                  + ( state.matchCount.get() > 1
                          ? QCoreApplication::translate( "CrawlerWidget",
                                                         " %1 matches found so far." )
                                .arg( matches )
                          : QCoreApplication::translate( "CrawlerWidget",
                                                         " %1 match found so far." )
                                .arg( matches ) );
            display_.gauge = state.progress;
        }
        break;
    case Phase::Complete:
        settled( autoRefresh, state.matchCount );
        showDone();
        break;
    case Phase::Failed:
        // The Search Session tells a failure only here.
        showError( QCoreApplication::translate( "CrawlerWidget", "Search failed" ) );
        display_.offerIssueReport = state.errorString;
        showDone();
        break;
    case Phase::Interrupted:
    case Phase::InvalidPattern:
        // Whoever stopped the Search or requested the invalid pattern has
        // already said so; a bare phase would only guess.
        showDone();
        break;
    }
}

void SearchLine::stopped( SearchAutoRefresh::State autoRefresh, LinesCount matchCount )
{
    // An interrupted Search tells no completion, so the gauge and the buttons
    // are put back here.
    settled( autoRefresh, matchCount );
    showDone();
}

void SearchLine::cleared()
{
    settled( SearchAutoRefresh::State::NoSearch, 0_lcount );
}

void SearchLine::settled( SearchAutoRefresh::State autoRefresh, LinesCount matchCount )
{
    using State = SearchAutoRefresh::State;

    QString text;
    switch ( autoRefresh ) {
    case State::NoSearch:
        break;
    case State::Static:
    case State::Autorefreshing:
        // Some languages translate the plural the same as the singular, so
        // the whole text is chosen.
        text = matchCount.get() > 1
                   ? QCoreApplication::translate( "CrawlerWidget", "%1 matches found" )
                         .arg( matchCount.get() )
                   : QCoreApplication::translate( "CrawlerWidget", "%1 match found" )
                         .arg( matchCount.get() );
        break;
    case State::FileTruncated:
    case State::TruncatedAutorefreshing:
        text = QCoreApplication::translate( "CrawlerWidget", "File truncated on disk" );
        break;
    }

    display_.offerIssueReport.clear();
    showText( text );
    display_.visible = !text.isEmpty();
}

SearchLine::Display SearchLine::display() const
{
    return display_;
}

void SearchLine::showText( const QString& text )
{
    display_.text = text;
    display_.isError = false;
}

void SearchLine::showError( const QString& text )
{
    display_.text = text;
    display_.isError = true;
    // The gauge is drawn in a palette of its own, which would hide the error
    // colors: an error typed over a running Search takes it down (#399).
    display_.gauge.reset();
}

void SearchLine::showDone()
{
    display_.gauge.reset();
    display_.buttons = Buttons::Search;
}
