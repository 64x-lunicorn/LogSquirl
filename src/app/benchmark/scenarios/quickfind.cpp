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

// The scenario "quickfind" (#668): types a pattern into the QuickFind bar
// character by character on a loaded Log File, as a user does, and reports
// for each keystroke the time until the Matches on screen are marked.
//
// The Log File is opened, loaded and shown first, and QuickFind is opened as
// Find in the Edit menu opens it, searching the Text View; none of that is
// measured. The scenario starts with the first keystroke. QuickFind is
// incremental, as it is by default: each keystroke changes the pattern the
// Text View marks on screen and moves it to the next match.
//
// A keystroke is a key press and release handed to the QuickFind bar's
// pattern edit. It is answered by the first paint of the Text View's Viewport
// that starts after it: that paint marks the Matches of the pattern as typed
// so far. The next keystroke comes once it is answered and at least
// keystroke_interval_ms after the one before, as from a user typing.
//
// Options:
//   pattern                what is typed (required)
//   keystroke_interval_ms  the least time between two keystrokes; 100
//
// Events:
//   keystroke_marked  a keystroke was answered: the paint ended;
//                     data: typed, the pattern so far, and latency_ms, the
//                     time from the keystroke to the end of the paint
// Results:
//   keystroke_count    the keystrokes typed, one per character of pattern
//   keystroke_latency  the latencies of all of them: count, min_ms, p50_ms,
//                      p99_ms, max_ms, mean_ms
//   log_line_count     Log Lines of the Log File
//   log_file_bytes     its size

#include <algorithm>
#include <chrono>
#include <memory>

#include <QAction>
#include <QCoreApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QTimer>

#include "benchmarkscenario.h"
#include "crawlerwidget.h"
#include "inputlatency.h"
#include "loadedlogfile.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "paintprobe.h"
#include "quickfindwidget.h"
#include "scenariorun.h"

namespace logsquirl::benchmark {

namespace {

class QuickFindTyping : public Scenario {
public:
    void start( ScenarioRun& run ) override
    {
        run_ = &run;
        pattern_ = run.option( "pattern" );
        if ( pattern_.isEmpty() ) {
            run.fail( QStringLiteral( "quickfind needs the option pattern=<what is typed>" ) );
            return;
        }
        int interval = 0;
        QString failure;
        if ( !run.numberOption( "keystroke_interval_ms", 100, 0, interval, failure ) ) {
            run.fail( failure );
            return;
        }
        interval_ = std::chrono::milliseconds{ interval };

        logFile_ = std::make_unique<LoadedLogFile>(
            run, QStringLiteral( "quickfind" ),
            [ this ]( MainWindow& window, CrawlerWidget& crawler ) { open( window, crawler ); } );
        logFile_->open();
    }

private:
    void open( MainWindow& window, CrawlerWidget& crawler )
    {
        auto* find = window.findChild<QAction*>( "findAction" );
        auto* bar = window.findChild<QuickFindWidget*>();
        mainView_ = crawler.findChild<LogMainView*>();
        if ( find == nullptr || bar == nullptr || !mainView_ ) {
            run_->fail( QStringLiteral( "the window has no Find, QuickFind bar or Text View" ) );
            return;
        }
        edit_ = bar->findChild<QLineEdit*>();
        if ( !edit_ ) {
            run_->fail( QStringLiteral( "the QuickFind bar has no pattern edit" ) );
            return;
        }

        // Opens the QuickFind bar on the Text View, which has the focus.
        find->trigger();

        probe_ = std::make_unique<PaintProbe>(
            mainView_->viewport(), [ this ]( Clock::time_point started, Clock::time_point ended ) {
                painted( started, ended );
            } );

        // From the event loop, once the bar is shown.
        QTimer::singleShot( 0, run_->context(), [ this ] {
            run_->report().markScenarioStart();
            type();
        } );
    }

    // The next character of the pattern, as a key press and release.
    void type()
    {
        if ( !edit_ ) {
            run_->fail( QStringLiteral( "the QuickFind bar went" ) );
            return;
        }
        const auto character = pattern_.at( typed_ );
        const auto key = static_cast<int>( character.toUpper().unicode() );
        const auto modifiers = character.isUpper() ? Qt::ShiftModifier : Qt::NoModifier;
        QKeyEvent press{ QEvent::KeyPress, key, modifiers, QString{ character } };
        QKeyEvent release{ QEvent::KeyRelease, key, modifiers, QString{ character } };

        lastKeystroke_ = Clock::now();
        latency_.inputHandled( lastKeystroke_ );
        QCoreApplication::sendEvent( edit_, &press );
        QCoreApplication::sendEvent( edit_, &release );
        ++typed_;
    }

    void painted( Clock::time_point started, Clock::time_point ended )
    {
        const auto answered = latency_.painted( started, ended );
        if ( answered.empty() ) {
            return;
        }
        for ( const auto latencyMs : answered ) {
            run_->report().eventAt(
                "keystroke_marked", ended,
                QJsonObject{ { "typed", pattern_.left( typed_ ) }, { "latency_ms", latencyMs } } );
        }

        if ( typed_ < pattern_.size() ) {
            // Not from within the paint; no sooner than a user types.
            const auto due = lastKeystroke_ + interval_;
            const auto wait = std::max( Clock::duration::zero(), due - Clock::now() );
            QTimer::singleShot( std::chrono::ceil<std::chrono::milliseconds>( wait ),
                                run_->context(), [ this ] { type(); } );
            return;
        }

        // Every keystroke reached the bar: it holds what was typed.
        if ( !edit_ || edit_->text() != pattern_ ) {
            run_->fail( QStringLiteral( "the QuickFind bar holds '%1', not the pattern typed" )
                            .arg( edit_ ? edit_->text() : QString{} ) );
            return;
        }

        auto& report = run_->report();
        report.setResult( "keystroke_count", static_cast<qint64>( typed_ ) );
        report.setResult( "keystroke_latency", latency_.distribution().toJson() );
        report.setResult( "log_line_count", logFile_->logLineCount() );
        report.setResult( "log_file_bytes", logFile_->bytes() );
        run_->finish();
    }

    ScenarioRun* run_ = nullptr;
    QString pattern_;
    std::chrono::milliseconds interval_{ 100 };
    std::unique_ptr<LoadedLogFile> logFile_;
    QPointer<LogMainView> mainView_;
    QPointer<QLineEdit> edit_;
    std::unique_ptr<PaintProbe> probe_;
    InputLatency latency_;
    Clock::time_point lastKeystroke_;
    qsizetype typed_ = 0;
};

const ScenarioRegistration registration{
    "quickfind",
    "Types a QuickFind pattern on a loaded Log File; reports each keystroke until marked",
    [] { return std::make_unique<QuickFindTyping>(); }
};

} // namespace

} // namespace logsquirl::benchmark
