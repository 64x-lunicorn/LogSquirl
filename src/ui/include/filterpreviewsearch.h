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

#include <functional>
#include <memory>
#include <optional>

#include <QObject>
#include <QString>

#include "linetypes.h"
#include "regularexpressionpattern.h"
#include "searchsessionstate.h"

class LogData;
class LogFilteredData;
class QTimer;

// What the preview of Add as Predefined Filter... counts with (#958): a Search
// of its own over the Log File, run by the Search machinery off the UI thread
// as the Filtered View's is, so that the dialog stays responsive on a large Log
// File. It is none of the Open Log File's Searches -- the Search Set neither
// makes nor follows it -- but its pattern is prepared as theirs are, with the
// reader of the Log Format's fields (OpenLogFile::withFieldReader()), so that
// it finds what the Search with the filter finds. A pattern set waits a moment
// for the next edit (the Search runs once the user pauses typing); a pattern
// set while a Search runs stops that one. What a Search found is told only for
// the pattern last set.
class FilterPreviewSearch : public QObject {
    Q_OBJECT

public:
    // The pattern a Search of the Log File is requested with, as set: with
    // the reader of the Log Format's fields, say (#798).
    using PreparePattern = std::function<RegularExpressionPattern( RegularExpressionPattern )>;

    // How long a pattern set waits for the next before it is searched for.
    static constexpr int SettleMilliseconds = 250;

    struct Result {
        enum class State {
            // No pattern, or an empty one.
            Idle,
            // A Search is waiting or under way; matchCount is what it found so far.
            Searching,
            Done,
            // The pattern is not valid, or the Search failed: error says why.
            Failed,
        };
        State state = State::Idle;
        LinesCount matchCount;
        int progress = 0;
        // The first Log Line found and its text, once there is one.
        std::optional<LineNumber> firstMatch;
        QString firstLine;
        QString error;
    };

    // Searches the Log File of logData, which it holds on to, each pattern
    // prepared with prepare; none leaves it as it is.
    explicit FilterPreviewSearch( std::shared_ptr<const LogData> logData,
                                  PreparePattern prepare = {}, QObject* parent = nullptr );
    ~FilterPreviewSearch() override;

    FilterPreviewSearch( const FilterPreviewSearch& ) = delete;
    FilterPreviewSearch& operator=( const FilterPreviewSearch& ) = delete;

    // Searches for pattern once it has settled; an empty one searches for
    // nothing.
    void setPattern( const RegularExpressionPattern& pattern );

    Result result() const
    {
        return result_;
    }

Q_SIGNALS:
    void resultChanged();

private:
    void startSearch();
    void searchStateChanged( const SearchSessionState& state );
    void setResult( Result result );

    // Declared before the Search built from it, so it outlives it.
    std::shared_ptr<const LogData> logData_;
    std::unique_ptr<LogFilteredData> search_;
    PreparePattern prepare_;
    QTimer* settle_ = nullptr;
    // The pattern last set, prepared: what the Search tells is of it.
    RegularExpressionPattern pattern_;
    // The pattern was set and not yet searched for: what the Search tells is
    // of an earlier one.
    bool waiting_ = false;
    Result result_;
};
