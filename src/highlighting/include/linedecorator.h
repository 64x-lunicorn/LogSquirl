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

#ifndef LOGSQUIRL_LINEDECORATOR_H
#define LOGSQUIRL_LINEDECORATOR_H

#include <optional>

#include <QColor>
#include <QString>

#include "abstractlogdata.h"
#include "containers.h"
#include "highlightedmatch.h"
#include "highlighter.h"
#include "linetypes.h"
#include "quickfindpattern.h"

// A Log Line's identity: its number and its text. The minimal information
// verdictFor() needs to look up a whole-line Highlighter match and check the
// Search Limits.
class LogLine {
  public:
    LogLine( LineNumber number, QString text )
        : number_{ number }
        , text_{ std::move( text ) }
    {
    }

    LineNumber number() const
    {
        return number_;
    }

    const QString& text() const
    {
        return text_;
    }

  private:
    LineNumber number_;
    QString text_;
};

// An inclusive range of Log Line numbers a Search is restricted to.
struct SearchLimits {
    LineNumber start{ 0_lnum };
    LineNumber end{ maxValue<LineNumber>() };

    bool contains( LineNumber line ) const
    {
        return line >= start && line <= end;
    }
};

// The facts about a whole Log Line that affect how any part of it looks.
// Decided once per line by LineDecorator::verdictFor.
class LineVerdict {
  public:
    LineVerdict() = default;

    LineVerdict( std::optional<HighlightColor> wholeLineHighlight,
                 AbstractLogData::LineType lineType, bool isOutsideSearchLimits )
        : wholeLineHighlight_{ wholeLineHighlight }
        , lineType_{ lineType }
        , isOutsideSearchLimits_{ isOutsideSearchLimits }
    {
    }

    std::optional<HighlightColor> wholeLineHighlight() const
    {
        return wholeLineHighlight_;
    }

    bool isMatch() const
    {
        return lineType_.testFlag( AbstractLogData::LineTypeFlags::Match );
    }

    bool isMark() const
    {
        return lineType_.testFlag( AbstractLogData::LineTypeFlags::Mark );
    }

    bool isContextLine() const
    {
        return lineType_.testFlag( AbstractLogData::LineTypeFlags::Context );
    }

    bool isOutsideSearchLimits() const
    {
        return isOutsideSearchLimits_;
    }

  private:
    std::optional<HighlightColor> wholeLineHighlight_;
    AbstractLogData::LineType lineType_ = AbstractLogData::LineTypeFlags::Plain;
    bool isOutsideSearchLimits_ = false;
};

// The finished visual result for a piece of displayed text: an ordered,
// non-overlapping sequence of colored spans, in the coordinate space of the
// text it was decorated from.
class Decoration {
  public:
    Decoration() = default;

    explicit Decoration( logsquirl::vector<HighlightedMatch> spans )
        : spans_{ std::move( spans ) }
    {
    }

    const logsquirl::vector<HighlightedMatch>& spans() const
    {
        return spans_;
    }

  private:
    logsquirl::vector<HighlightedMatch> spans_;
};

// The single owner of the precedence rule that turns a Line Verdict plus a
// piece of text into a Decoration. It decides which color wins where; it
// does not draw.
//
// Constructed once per repaint with the stable context: the active
// Highlighter Set, the QuickFind pattern, the Color Labels, the main-search
// colors and the Search Limits. Nothing is read from a singleton.
class LineDecorator {
  public:
    struct Context {
        HighlighterSet highlighterSet;
        std::optional<Highlighter> mainSearch;
        logsquirl::vector<Highlighter> colorLabels;
        QuickFindMatcher quickFind;
        QColor quickFindColor;
        SearchLimits searchLimits;
    };

    explicit LineDecorator( Context context )
        : context_{ std::move( context ) }
    {
    }

    // Decide the facts about a whole Log Line that affect how any part of
    // it looks. Nothing is read from a singleton.
    LineVerdict verdictFor( const LogLine& line, AbstractLogData::LineType lineType ) const;

    // Turn a run of text plus its Line Verdict into an ordered,
    // non-overlapping sequence of colored spans, in the coordinate space
    // of the text passed in. Precedence, low to high: whole-line
    // Highlighter, main search, Color Labels, QuickFind, selection.
    // Overlapping sources are resolved by splitting and overriding.
    Decoration decorate( const QString& text, const LineVerdict& verdict,
                         const std::optional<HighlightedMatch>& selection = std::nullopt ) const;

  private:
    Context context_;
};

#endif
