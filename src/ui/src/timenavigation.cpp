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

#include "timenavigation.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>

#include <utility>

#include "abstractlogdata.h"
#include "logformatdefinition.h"
#include "timestampreader.h"

namespace {

TimestampReader readerOf( const TimeNavigation::Source& source )
{
    return TimestampReader( *source.format, 0, source.modified );
}

} // namespace

TimeNavigation::Prompt TimeNavigation::dialogPrompt( QWidget* parent )
{
    Prompt prompt;
    prompt.askText
        = [ parent ]( const QString& title, const QString& label ) -> std::optional<QString> {
        bool ok = false;
        auto text = QInputDialog::getText( parent, title, label, QLineEdit::Normal, {}, &ok );
        if ( !ok ) {
            return std::nullopt;
        }
        return text;
    };
    prompt.askNumber = [ parent ]( const QString& title, const QString& label, int value, int min,
                                   int max ) -> std::optional<int> {
        bool ok = false;
        const auto number = QInputDialog::getInt( parent, title, label, value, min, max, 1, &ok );
        if ( !ok ) {
            return std::nullopt;
        }
        return number;
    };
    prompt.tell = [ parent ]( const QString& title, const QString& text ) {
        QMessageBox::information( parent, title, text );
    };
    return prompt;
}

TimeNavigation::TimeNavigation( std::function<Source()> source, Sink sink, Prompt prompt )
    : source_( std::move( source ) )
    , sink_( std::move( sink ) )
    , prompt_( std::move( prompt ) )
{
}

TimeNavigation::~TimeNavigation() = default;

QString TimeNavigation::goToTimestampUnavailableReason() const
{
    const auto format = source_().format;
    if ( !format ) {
        return tr( "Go to timestamp needs a Log Format: none was recognized for this Log File." );
    }
    if ( !TimestampReader::isAvailableFor( *format ) ) {
        return tr( "Go to timestamp is not available: the Log Format \"%1\" has no timestamp "
                   "field." )
            .arg( format->title() );
    }
    return {};
}

QString TimeNavigation::searchLimitsByTimeUnavailableReason() const
{
    const auto format = source_().format;
    if ( !format ) {
        return tr( "Search limits by time need a Log Format: none was recognized for this Log "
                   "File." );
    }
    if ( !TimestampReader::isAvailableFor( *format ) ) {
        return tr( "Search limits by time are not available: the Log Format \"%1\" has no "
                   "timestamp field." )
            .arg( format->title() );
    }
    return {};
}

QString TimeNavigation::notInTimeOrderNotice()
{
    return tr( "The Log File is not in time order here: the position may be off." );
}

std::optional<TimeNavigation::Source> TimeNavigation::lookupSource() const
{
    // The Log Format can change while a modal dialog is open (the file is
    // truncated or recognized anew), so nothing obtained here is kept across
    // a prompt.
    auto source = source_();
    if ( !source.logData || !source.format || !TimestampReader::isAvailableFor( *source.format ) ) {
        return std::nullopt;
    }
    return source;
}

template <typename Result, typename Work, typename Done>
void TimeNavigation::runLookup( Work work, Done done )
{
    // No progress and no cancel button: the status bar says it runs, and the
    // lookup is cancelled by whatever makes its answer stale.
    sink_.statusMessage( tr( "Looking up the time..." ) );
    runner_.start<Result>( std::move( work ), [ this, done = std::move( done ) ]( Result result ) {
        sink_.statusMessage( QString() );
        done( std::move( result ) );
    } );
}

void TimeNavigation::cancel()
{
    if ( runner_.isRunning() ) {
        runner_.cancel();
        sink_.statusMessage( QString() );
    }
}

bool TimeNavigation::isLookingUp() const
{
    return runner_.isRunning();
}

void TimeNavigation::reloaded()
{
    cancel();
}

void TimeNavigation::loaded( bool onlyAppended )
{
    // A lookup over the old lines has nothing to say about a Log File that
    // was loaded anew; appended lines leave it valid.
    if ( !onlyAppended ) {
        cancel();
    }
}

void TimeNavigation::truncated()
{
    cancel();
}

void TimeNavigation::formatChanged()
{
    cancel();
}

void TimeNavigation::formatReset()
{
    cancel();
}

void TimeNavigation::lookUpNearbyTimestamp( LineNumber line,
                                            std::function<void( std::optional<QDateTime> )> then )
{
    const auto source = lookupSource();
    if ( !source ) {
        return;
    }
    runLookup<std::optional<QDateTime>>(
        [ source = *source, line ]( const std::atomic<bool>& cancelled ) {
            return timelookup::timestampNear( line, *source.logData, readerOf( source ),
                                              &cancelled );
        },
        std::move( then ) );
}

void TimeNavigation::goToTimestamp( LineNumber current )
{
    if ( !goToTimestampUnavailableReason().isEmpty() ) {
        return;
    }

    // The date of the Log Line the user is at, for a time typed without one.
    lookUpNearbyTimestamp( current, [ this ]( std::optional<QDateTime> nearby ) {
        const auto title = tr( "Go to timestamp" );
        if ( !nearby ) {
            prompt_.tell( title, tr( "No Log Line near the current one has a timestamp." ) );
            return;
        }

        const auto text = prompt_.askText(
            title, tr( "Time, as HH:MM[:SS[.mmm]], optionally after a date as YYYY-MM-DD.\n"
                       "Without a date, %1 is used." )
                       .arg( QLocale().toString( nearby->date(), QLocale::ShortFormat ) ) );
        if ( !text || text->trimmed().isEmpty() ) {
            return;
        }

        const auto time = timelookup::parseTimeInput( *text, nearby->date() );
        if ( !time ) {
            prompt_.tell( title, tr( "\"%1\" is not a time. Use HH:MM, HH:MM:SS or "
                                     "YYYY-MM-DD HH:MM:SS." )
                                     .arg( text->trimmed() ) );
            return;
        }

        // The Log Format may have changed while the dialog was open.
        const auto source = lookupSource();
        if ( !source ) {
            return;
        }
        runLookup<std::optional<timelookup::Result>>(
            [ source = *source, time = *time ]( const std::atomic<bool>& cancelled ) {
                return timelookup::firstLineAtOrAfter( time, *source.logData, readerOf( source ),
                                                       { {}, &cancelled } );
            },
            [ this ]( const std::optional<timelookup::Result>& result ) {
                if ( result ) {
                    showLookupResult( *result );
                }
            } );
    } );
}

void TimeNavigation::showLookupResult( const timelookup::Result& result )
{
    sink_.showLogLine( result.line );
    if ( result.outOfOrder ) {
        sink_.statusMessage( notInTimeOrderNotice() );
    }

    switch ( result.position ) {
    case timelookup::Position::BeforeFirst:
        prompt_.tell( tr( "Go to timestamp" ),
                      tr( "The time is before the first timestamp in the Log File. "
                          "Went to the first line." ) );
        break;
    case timelookup::Position::AfterLast:
        prompt_.tell( tr( "Go to timestamp" ),
                      tr( "The time is after the last timestamp in the Log File. "
                          "Went to the last line." ) );
        break;
    case timelookup::Position::NoTimestamps:
        prompt_.tell( tr( "Go to timestamp" ),
                      tr( "No Log Line has a timestamp this Log Format can read." ) );
        break;
    case timelookup::Position::AtOrAfter:
        break;
    }
}

void TimeNavigation::setSearchLimitsToTimeRange( LineNumber current )
{
    if ( !searchLimitsByTimeUnavailableReason().isEmpty() ) {
        return;
    }
    // The date of the Log Line the user is at, for a time typed without one.
    lookUpNearbyTimestamp( current, [ this ]( std::optional<QDateTime> nearby ) {
        const auto title = tr( "Set search limits to time range" );
        if ( !nearby ) {
            prompt_.tell( title, tr( "No Log Line near the current one has a timestamp." ) );
            return;
        }

        const auto format
            = tr( "Time, as HH:MM[:SS[.mmm]], optionally after a date as YYYY-MM-DD.\n"
                  "Without a date, %1 is used." )
                  .arg( QLocale().toString( nearby->date(), QLocale::ShortFormat ) );
        const auto startText
            = prompt_.askText( title, tr( "Start (included).\n%1" ).arg( format ) );
        if ( !startText || startText->trimmed().isEmpty() ) {
            return;
        }
        const auto endText
            = prompt_.askText( title, tr( "End (not included).\n%1" ).arg( format ) );
        if ( !endText || endText->trimmed().isEmpty() ) {
            return;
        }

        const auto start = timelookup::parseTimeInput( *startText, nearby->date() );
        const auto end = timelookup::parseTimeInput( *endText, nearby->date() );
        if ( !start || !end ) {
            prompt_.tell( title, tr( "\"%1\" is not a time. Use HH:MM, HH:MM:SS or "
                                     "YYYY-MM-DD HH:MM:SS." )
                                     .arg( ( !start ? *startText : *endText ).trimmed() ) );
            return;
        }
        setSearchLimitsFromTimes( *start, *end );
    } );
}

void TimeNavigation::setSearchLimitsAroundLine( LineNumber current, int windowMinutes )
{
    if ( !searchLimitsByTimeUnavailableReason().isEmpty() ) {
        return;
    }
    lookUpNearbyTimestamp( current, [ this, windowMinutes ]( std::optional<QDateTime> center ) {
        const auto title = tr( "Set search limits around current line" );
        if ( !center ) {
            prompt_.tell( title, tr( "No Log Line near the current one has a timestamp." ) );
            return;
        }

        const auto minutes = prompt_.askNumber( title, tr( "Minutes before and after:" ),
                                                windowMinutes, 1, 24 * 60 );
        if ( !minutes ) {
            return;
        }
        if ( *minutes != windowMinutes ) {
            sink_.searchWindowChosen( *minutes );
        }

        setSearchLimitsFromTimes( center->addSecs( -*minutes * 60 ),
                                  center->addSecs( *minutes * 60 ) );
    } );
}

void TimeNavigation::setSearchLimitsFromTimes( const QDateTime& start, const QDateTime& end )
{
    using Outcome = timelookup::LimitsResult::Outcome;

    // Called after prompts: the source is taken anew, the Log Format may have changed.
    const auto source = lookupSource();
    if ( !source ) {
        return;
    }
    runLookup<timelookup::LimitsResult>(
        [ source = *source, start, end ]( const std::atomic<bool>& cancelled ) {
            return timelookup::searchLimitsForTimeRange( start, end, *source.logData,
                                                         readerOf( source ), &cancelled );
        },
        [ this ]( const timelookup::LimitsResult& result ) {
            const auto title = tr( "Set search limits by time" );
            switch ( result.outcome ) {
            case Outcome::Cancelled:
                return;
            case Outcome::Limits:
                // From here on ordinary Search Limits, lines like any others.
                sink_.setSearchLimits( result.start, result.end );
                if ( result.outOfOrder ) {
                    sink_.statusMessage( notInTimeOrderNotice() );
                }
                return;
            case Outcome::BeforeFile:
                prompt_.tell( title, tr( "The time range is before the first timestamp in "
                                         "the Log File. The search limits are unchanged." ) );
                return;
            case Outcome::AfterFile:
                prompt_.tell( title, tr( "The time range is after the last timestamp in the "
                                         "Log File. The search limits are unchanged." ) );
                return;
            case Outcome::NoTimestamps:
                prompt_.tell( title, tr( "No Log Line has a timestamp this Log Format can "
                                         "read. The search limits are unchanged." ) );
                return;
            case Outcome::EndNotAfterStart:
                prompt_.tell( title, tr( "The end is not after the start. The search limits "
                                         "are unchanged." ) );
                return;
            case Outcome::NoLogLines:
                prompt_.tell( title, tr( "No Log Line has a timestamp in the time range. The "
                                         "search limits are unchanged." ) );
                return;
            }
        } );
}
