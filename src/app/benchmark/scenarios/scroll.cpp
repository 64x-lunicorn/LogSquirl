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

// The scenario "scroll" (#669): scrolls a loaded Log File by a fixed script in
// the Text View or the Table View and reports the time of every frame -- every
// paint of the view's Viewport -- while it scrolls.
//
// The Log File is opened, loaded and shown first, and for the Table View the
// Table View is shown as its toggle shows it; none of that is measured. The
// scenario starts with the first step of the script: line_steps steps of one
// line down, then page_steps steps of one page down, then a jump to the end.
// A step is what the view's vertical scroll bar does when its arrow, its
// track below the handle or its end is used (QAbstractSlider::triggerAction:
// SliderSingleStepAdd, SliderPageStepAdd, SliderToMaximum); a mouse wheel and
// the keyboard scroll through the same scroll bar.
//
// A step is answered by the first paint of the view's Viewport that starts
// after it, and the next step comes once it is answered, from the event loop.
// A step that does not move the scroll bar -- the end was reached -- is
// answered by nothing and the next step follows at once.
//
// What is measured is the paint of the Viewport: the time from the moment its
// paint event reaches the Viewport to the moment the view's handler returns
// (PaintProbe), for every paint from the first step until the paint that
// answers the jump to the end has ended -- paints that answer no step
// included. Neither the Overview, the Filtered View, nor what the windowing
// system does with the painted frame is part of it.
//
// The run is prepared with the settings it measures under, written into its
// own settings before the application reads them: for the Table View Log
// Formats are recognized (the Log File needs one the Catalog knows), and ANSI
// color sequences are shown as the option says. With highlighters a
// Highlighter Set of HighlighterCount Highlighters is made in the run's own
// Highlighter Set Collection and activated before the window opens.
//
// Options:
//   view          text (the Text View, default) or table (the Table View)
//   highlighters  true or false (default): a Highlighter Set is active
//   ansi          the setting "ANSI color sequences": text (show as text,
//                 the setting's default), hide, or colors (show colors)
//   line_steps    steps of one line; 200
//   page_steps    steps of one page; 40
//
// Events:
//   scrolled_to_end  the paint that answered the jump to the end ended;
//                    data: frame_count, the frames until then
// Results:
//   view                 the view scrolled, text or table
//   frame_time           the duration of every frame: count, min_ms, p50_ms,
//                        p99_ms, max_ms, mean_ms, budget_ms (one frame at 60
//                        Hz, 1000/60) and over_budget_count, the frames longer
//                        than it
//   step_count           the steps of the script, line_step_count and
//                        page_step_count of them and the jump to the end
//   unmoved_step_count   the steps that moved nothing
//   highlighter_count    Highlighters of the active Highlighter Set
//   log_line_count       Log Lines of the Log File
//   log_file_bytes       its size

#include <iterator>
#include <memory>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QColor>
#include <QJsonObject>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>

#include "benchmarkscenario.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "frametimes.h"
#include "highlighter.h"
#include "highlighterset.h"
#include "loadedlogfile.h"
#include "logmainview.h"
#include "logtableview.h"
#include "mainwindow.h"
#include "paintprobe.h"
#include "scenariorun.h"

namespace logsquirl::benchmark {

namespace {

// The Highlighters a user colors a log with: the ERROR Log Lines whole, the
// level of a warning, numbers and names of the text. Most Log Lines have a
// match of several of them.
struct HighlighterRule {
    const char* pattern;
    bool onlyMatch;
    const char* fore;
    const char* back;
};
constexpr HighlighterRule HighlighterRules[] = {
    { "ERROR", false, "#ffffff", "#a01010" },
    { "WARN", true, "#000000", "#ffb000" },
    { "request [0-9]+", true, "#1040c0", "#e8f0ff" },
    { "worker-0[0-7]", true, "#007040", "#e0ffe8" },
    { "[0-9]+ ms", true, "#800080", "#ffe8ff" },
};
constexpr int HighlighterCount = static_cast<int>( std::size( HighlighterRules ) );

class Scroll : public Scenario {
public:
    void prepare( ScenarioRun& run ) override
    {
        const auto view = run.option( "view", "text" );
        if ( view != "text" && view != "table" ) {
            failure_ = QStringLiteral( "view is text or table, not '%1'" ).arg( view );
            return;
        }
        table_ = view == "table";

        const auto highlighters = run.option( "highlighters", "false" );
        if ( highlighters != "true" && highlighters != "false" ) {
            failure_
                = QStringLiteral( "highlighters is true or false, not '%1'" ).arg( highlighters );
            return;
        }

        const auto ansi = run.option( "ansi", "text" );
        AnsiColorSequences sequences = AnsiColorSequences::ShowAsText;
        if ( ansi == "hide" ) {
            sequences = AnsiColorSequences::Hide;
        }
        else if ( ansi == "colors" ) {
            sequences = AnsiColorSequences::ShowColors;
        }
        else if ( ansi != "text" ) {
            failure_ = QStringLiteral( "ansi is text, hide or colors, not '%1'" ).arg( ansi );
            return;
        }

        if ( !readSteps( run, "line_steps", 200, lineSteps_ )
             || !readSteps( run, "page_steps", 40, pageSteps_ ) ) {
            return;
        }

        // The run's own settings: DataLocation isolated them before this.
        auto& config = Configuration::getSynced();
        config.setAnsiColorSequences( sequences );
        // The Table View shows the Log Format recognized; the Text View is
        // measured as it starts, without one.
        config.setAutoDetectLogFormats( table_ );
        config.setAutoShowTableView( false );
        config.save();

        highlighters_ = highlighters == "true";
    }

    void start( ScenarioRun& run ) override
    {
        run_ = &run;
        if ( !failure_.isEmpty() ) {
            run.fail( failure_ );
            return;
        }
        if ( highlighters_ ) {
            activateHighlighterSet();
        }
        logFile_ = std::make_unique<LoadedLogFile>(
            run, QStringLiteral( "scroll" ),
            [ this ]( MainWindow&, CrawlerWidget& crawler ) { loaded( crawler ); } );
        logFile_->open();
    }

private:
    // Into the run's own Highlighter Set Collection, before the window that
    // reads it opens. Activating it compiles its Highlighters, which needs
    // the application set up: not from prepare().
    void activateHighlighterSet()
    {
        auto set = HighlighterSet::createNewSet( QStringLiteral( "Benchmark" ) );
        for ( const auto& rule : HighlighterRules ) {
            set.addHighlighter( Highlighter( QString::fromLatin1( rule.pattern ), false,
                                             rule.onlyMatch, QColor( rule.fore ),
                                             QColor( rule.back ) ) );
        }
        auto& collection = HighlighterSetCollection::getSynced();
        collection.setHighlighterSets( { set } );
        collection.activateSet( set.id() );
        collection.save();
        highlighterCount_ = HighlighterCount;
    }

    bool readSteps( ScenarioRun& run, const char* name, int fallback, int& steps )
    {
        bool isNumber = false;
        steps = run.option( name, QString::number( fallback ) ).toInt( &isNumber );
        if ( !isNumber || steps < 0 ) {
            failure_ = QStringLiteral( "%1 takes a number of steps, not '%2'" )
                           .arg( name, run.option( name ) );
            return false;
        }
        return true;
    }

    void loaded( CrawlerWidget& crawler )
    {
        // What the window read back, and the views paint with.
        if ( highlighters_ && HighlighterSetCollection::get().currentActiveSet().isEmpty() ) {
            run_->fail( QStringLiteral( "the Highlighter Set is not active in the window" ) );
            return;
        }

        if ( !table_ ) {
            view_ = crawler.findChild<LogMainView*>();
            if ( !view_ ) {
                run_->fail( QStringLiteral( "the Log File's tab has no Text View" ) );
                return;
            }
            watch();
            scrollFromEventLoop();
            return;
        }

        auto* table = crawler.findChild<LogTableView*>();
        auto* toggle = crawler.findChild<QToolButton*>( "tableViewToggle" );
        if ( table == nullptr || toggle == nullptr ) {
            run_->fail( QStringLiteral( "the Log File's tab has no Table View" ) );
            return;
        }
        if ( toggle->isHidden() ) {
            run_->fail( QStringLiteral(
                "no Log Format was recognized for the Log File: the Table View shows none" ) );
            return;
        }
        view_ = table;
        // Shown once a paint of the Table View shows Rows, which the toggle
        // reads in from the event loop.
        watch();
        toggle->click();
    }

    void watch()
    {
        probe_ = std::make_unique<PaintProbe>(
            view_->viewport(), [ this ]( Clock::time_point started, Clock::time_point ended ) {
                painted( started, ended );
            } );
    }

    void scrollFromEventLoop()
    {
        // Not from within a paint.
        QTimer::singleShot( 0, run_->context(), [ this ] {
            scrolling_ = true;
            run_->report().markScenarioStart();
            step();
        } );
    }

    void painted( Clock::time_point started, Clock::time_point ended )
    {
        if ( !scrolling_ ) {
            const auto* table = qobject_cast<QAbstractItemView*>( view_ );
            const auto* model = table != nullptr ? table->model() : nullptr;
            if ( !shown_ && model != nullptr && model->rowCount() > 0 ) {
                shown_ = true;
                scrollFromEventLoop();
            }
            return;
        }

        frames_.painted( started, ended );
        if ( !waiting_ || started <= stepped_ ) {
            return;
        }
        waiting_ = false;
        if ( stepsDone_ < stepCount() ) {
            QTimer::singleShot( 0, run_->context(), [ this ] { step(); } );
        }
        else {
            scrolledToEnd( ended );
        }
    }

    int stepCount() const
    {
        return lineSteps_ + pageSteps_ + 1;
    }

    void step()
    {
        if ( !view_ ) {
            run_->fail( QStringLiteral( "the view went" ) );
            return;
        }
        auto* bar = view_->verticalScrollBar();
        const auto action = stepsDone_ < lineSteps_ ? QAbstractSlider::SliderSingleStepAdd
                            : stepsDone_ < lineSteps_ + pageSteps_
                                ? QAbstractSlider::SliderPageStepAdd
                                : QAbstractSlider::SliderToMaximum;
        const auto before = bar->value();

        stepped_ = Clock::now();
        bar->triggerAction( action );
        ++stepsDone_;

        if ( bar->value() != before ) {
            waiting_ = true;
            return;
        }

        // Nothing moved, so nothing is painted for it.
        ++unmovedSteps_;
        if ( stepsDone_ < stepCount() ) {
            QTimer::singleShot( 0, run_->context(), [ this ] { step(); } );
        }
        else {
            scrolledToEnd( Clock::now() );
        }
    }

    void scrolledToEnd( Clock::time_point moment )
    {
        const auto* bar = view_ ? view_->verticalScrollBar() : nullptr;
        if ( bar == nullptr || bar->value() != bar->maximum() ) {
            run_->fail( QStringLiteral( "the jump to the end did not reach the end" ) );
            return;
        }

        auto& report = run_->report();
        report.eventAt( "scrolled_to_end", moment,
                        QJsonObject{ { "frame_count", static_cast<qint64>( frames_.count() ) } } );
        report.setResult( "view", table_ ? QStringLiteral( "table" ) : QStringLiteral( "text" ) );
        report.setResult( "frame_time", frames_.toJson() );
        report.setResult( "step_count", stepCount() );
        report.setResult( "line_step_count", lineSteps_ );
        report.setResult( "page_step_count", pageSteps_ );
        report.setResult( "unmoved_step_count", unmovedSteps_ );
        report.setResult( "highlighter_count", highlighterCount_ );
        report.setResult( "log_line_count", logFile_->logLineCount() );
        report.setResult( "log_file_bytes", logFile_->bytes() );
        run_->finish();
    }

    QString failure_;
    bool table_ = false;
    bool highlighters_ = false;
    int lineSteps_ = 0;
    int pageSteps_ = 0;
    int highlighterCount_ = 0;

    ScenarioRun* run_ = nullptr;
    std::unique_ptr<LoadedLogFile> logFile_;
    QPointer<QAbstractScrollArea> view_;
    std::unique_ptr<PaintProbe> probe_;
    FrameTimes frames_;
    bool shown_ = false;
    bool scrolling_ = false;
    bool waiting_ = false;
    Clock::time_point stepped_;
    int stepsDone_ = 0;
    int unmovedSteps_ = 0;
};

const ScenarioRegistration registration{
    "scroll", "Scrolls a loaded Log File in the Text View or the Table View; reports every frame",
    [] { return std::make_unique<Scroll>(); }
};

} // namespace

} // namespace logsquirl::benchmark
