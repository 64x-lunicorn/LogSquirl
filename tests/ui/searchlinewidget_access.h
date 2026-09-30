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

#include <QComboBox>
#include <QToolButton>

#include "infoline.h"
#include "searchlinewidget.h"

// The widgets of the Search Line, which the tests click and read as the user
// does: the Search Line widget's own tests, and those of the Crawler Widget
// that search through it (#638).
struct SearchLineWidgetTest {};

template <>
struct SearchLineWidget::access_by<SearchLineWidgetTest> {
    static QComboBox* patternEdit( const SearchLineWidget& line )
    {
        return line.patternEdit_;
    }

    static QToolButton* matchCaseButton( const SearchLineWidget& line )
    {
        return line.matchCaseButton_;
    }

    static QToolButton* useRegexpButton( const SearchLineWidget& line )
    {
        return line.useRegexpButton_;
    }

    static QToolButton* inverseButton( const SearchLineWidget& line )
    {
        return line.inverseButton_;
    }

    static QToolButton* booleanButton( const SearchLineWidget& line )
    {
        return line.booleanButton_;
    }

    static QToolButton* autoRefreshButton( const SearchLineWidget& line )
    {
        return line.autoRefreshButton_;
    }

    static QToolButton* searchButton( const SearchLineWidget& line )
    {
        return line.searchButton_;
    }

    static QToolButton* stopButton( const SearchLineWidget& line )
    {
        return line.stopButton_;
    }

    static QToolButton* clearButton( const SearchLineWidget& line )
    {
        return line.clearButton_;
    }

    static QToolButton* keepResultsButton( const SearchLineWidget& line )
    {
        return line.keepResultsButton_;
    }

    static InfoLine* infoLine( const SearchLineWidget& line )
    {
        return line.infoLine_;
    }
};

using SearchLineAccess = SearchLineWidget::access_by<SearchLineWidgetTest>;
