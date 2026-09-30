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
#include <QDate>
#include <QDateTime>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

#include "linetypes.h"
#include "lookuprunner.h"
#include "timelookup.h"

class AbstractLogData;
class LogFormatDefinition;
class QWidget;

// The time navigation of a Log File: Go to timestamp and the Search Limits
// given as a time range or as minutes around the current Log Line (#636).
//
// It is the glue between the time lookup, the lookup runner and the user:
// which prompt follows which lookup, which outcome is said how, that the
// source is taken again after every prompt (the Log Format may have changed
// while it was open), and which events cancel a running lookup. The time
// lookup itself runs on a worker thread; what it finds reaches the sink on
// the thread this object lives on.
//
// Built from three parts the coordinator (the Crawler Widget) hands in:
//  - a source: the Log File's data, its Log Format and when it was last
//    written, asked for anew each time they are needed;
//  - a sink: what comes out -- a Log Line to show, Search Limits to set (the
//    coordinator passes them on to the Open Log File), a status message, and
//    the minutes around a Log Line when the user chose other ones;
//  - a prompt: how the user is asked and told, the input dialogs in the app
//    (dialogPrompt()) and a scripted one in tests.
class TimeNavigation {
    Q_DECLARE_TR_FUNCTIONS( TimeNavigation )

public:
    // What a lookup reads, copied out of the coordinator: the worker builds
    // its own Timestamp reader from it, so nothing of the coordinator is
    // shared with the worker thread.
    struct Source {
        std::shared_ptr<const AbstractLogData> logData;
        // None when no Log Format was recognized.
        std::shared_ptr<const LogFormatDefinition> format;
        // The year of a Timestamp without one comes from when the Log File
        // was last written.
        QDate modified;
    };

    struct Sink {
        // Selects the Log Line and shows it.
        std::function<void( LineNumber )> showLogLine;
        // Limits the Search to the Log Lines [start, end).
        std::function<void( LineNumber start, LineNumber end )> setSearchLimits;
        // Said in the status bar; an empty text clears it.
        std::function<void( const QString& )> statusMessage;
        // The user chose other minutes before and after the current Log Line:
        // the value to remember for the next time.
        std::function<void( int minutes )> searchWindowChosen;
    };

    struct Prompt {
        // Asks for a line of text; none when the user cancelled.
        std::function<std::optional<QString>( const QString& title, const QString& label )> askText;
        // Asks for a number between min and max, starting at value; none when
        // the user cancelled.
        std::function<std::optional<int>( const QString& title, const QString& label, int value,
                                          int min, int max )>
            askNumber;
        // Tells the user something, and waits until it was read.
        std::function<void( const QString& title, const QString& text )> tell;
    };

    // The input dialogs and message boxes, modal over parent.
    static Prompt dialogPrompt( QWidget* parent );

    TimeNavigation( std::function<Source()> source, Sink sink, Prompt prompt );
    ~TimeNavigation();

    TimeNavigation( const TimeNavigation& ) = delete;
    TimeNavigation& operator=( const TimeNavigation& ) = delete;
    TimeNavigation( TimeNavigation&& ) = delete;
    TimeNavigation& operator=( TimeNavigation&& ) = delete;

    // Why Go to timestamp is not available for the source, empty when it is:
    // it needs a recognized Log Format with a timestamp field.
    QString goToTimestampUnavailableReason() const;
    // The same for the Search Limits given as a time.
    QString searchLimitsByTimeUnavailableReason() const;

    // Asks for a time and goes to the first Log Line at or after it. A time
    // typed without a date is on the date of the Log Line nearest to current
    // that has a Timestamp.
    void goToTimestamp( LineNumber current );
    // Asks for a start and an end time, and limits the Search to the Log
    // Lines between them.
    void setSearchLimitsToTimeRange( LineNumber current );
    // Asks for the minutes before and after current, starting at
    // windowMinutes, and limits the Search to them.
    void setSearchLimitsAroundLine( LineNumber current, int windowMinutes );

    // What makes the answer of a running lookup stale: it is cancelled, and
    // reports nothing.
    void reloaded();
    // A load that brought only appended Log Lines leaves the lookup running.
    void loaded( bool onlyAppended );
    void truncated();
    // Another Log Format was recognized.
    void formatChanged();
    // The Log Format was forgotten.
    void formatReset();

    bool isLookingUp() const;

    // Said in the status bar when a time lookup landed among Timestamps that
    // are not in time order.
    static QString notInTimeOrderNotice();

private:
    // The source, if a time lookup can read it.
    std::optional<Source> lookupSource() const;
    template <typename Result, typename Work, typename Done>
    void runLookup( Work work, Done done );
    void cancel();
    // Looks up the Timestamp near line, then calls then with it.
    void lookUpNearbyTimestamp( LineNumber line,
                                std::function<void( std::optional<QDateTime> )> then );
    void setSearchLimitsFromTimes( const QDateTime& start, const QDateTime& end );
    void showLookupResult( const timelookup::Result& result );

    std::function<Source()> source_;
    Sink sink_;
    Prompt prompt_;
    LookupRunner runner_;
};
