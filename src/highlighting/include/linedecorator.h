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
#include <QPalette>
#include <QString>
#include <QStringView>

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

// The range of Log Line numbers a Search is restricted to. Half-open, as
// every Presentation holds it: start is the first Log Line searched, end the
// Log Line after the last one, so no caller converts either end.
struct SearchLimits {
    LineNumber start{ 0_lnum };
    LineNumber end{ maxValue<LineNumber>() };

    bool contains( LineNumber line ) const
    {
        return line >= start && line < end;
    }
};

// The facts about a whole Log Line that affect how any part of it looks.
// Decided once per line by LineDecorator::verdictFor.
class LineVerdict {
public:
    LineVerdict() = default;

    LineVerdict( std::optional<HighlightColor> wholeLineHighlight,
                 AbstractLogData::LineType lineType, bool isOutsideSearchLimits,
                 logsquirl::vector<HighlightedMatch> highlighterSpans = {},
                 bool isSelectedAsWhole = false )
        : wholeLineHighlight_{ wholeLineHighlight }
        , lineType_{ lineType }
        , isOutsideSearchLimits_{ isOutsideSearchLimits }
        , isSelectedAsWhole_{ isSelectedAsWhole }
        , highlighterSpans_{ std::move( highlighterSpans ) }
    {
    }

    std::optional<HighlightColor> wholeLineHighlight() const
    {
        return wholeLineHighlight_;
    }

    // The full set of spans the Highlighter Set produced for this Log Line,
    // exactly as HighlighterSet::matchLine returned them: a single
    // full-line span when a whole-line Highlighter applies, the individual
    // matches of any word-only (highlight-just-the-match) Highlighters, or
    // both layered together when a word-only rule and a whole-line rule
    // both match -- HighlighterSet::matchLine resolves that layering by
    // priority, not by picking one kind over the other.
    const logsquirl::vector<HighlightedMatch>& highlighterSpans() const
    {
        return highlighterSpans_;
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

    // Whether the line is selected as a whole. Such a line shows the
    // selection colors with only its QuickFind matches on top.
    bool isSelectedAsWhole() const
    {
        return isSelectedAsWhole_;
    }

private:
    std::optional<HighlightColor> wholeLineHighlight_;
    AbstractLogData::LineType lineType_ = AbstractLogData::LineTypeFlags::Plain;
    bool isOutsideSearchLimits_ = false;
    bool isSelectedAsWhole_ = false;
    logsquirl::vector<HighlightedMatch> highlighterSpans_;
};

// The finished visual result for a piece of displayed text: an ordered,
// non-overlapping sequence of colored spans that covers the whole text, in
// the coordinate space of the text it was decorated from. Text no source
// colors carries the line's own colors, which also color whatever a
// Presentation draws beyond the end of the text.
class Decoration {
public:
    Decoration() = default;

    Decoration( logsquirl::vector<HighlightedMatch> spans, HighlightColor lineColors )
        : spans_{ std::move( spans ) }
        , lineColors_{ std::move( lineColors ) }
    {
    }

    const logsquirl::vector<HighlightedMatch>& spans() const
    {
        return spans_;
    }

    // The line's own colors: those of any text no source colors, and of the
    // space beyond the end of the text.
    const HighlightColor& lineColors() const
    {
        return lineColors_;
    }

    // This Decoration of rawText, moved to the display columns of rawText's
    // tab expansion, which is displayLength long. Tab expansion is the Text
    // View's step, not the Line Decorator's: the Line Decorator decorates
    // raw text, and the Text View moves the finished Decoration once.
    Decoration inDisplayColumns( QStringView rawText, LineLength displayLength ) &&;

private:
    logsquirl::vector<HighlightedMatch> spans_;
    HighlightColor lineColors_;
};

// A span given in the display columns of rawText's tab expansion, moved to
// raw columns: it covers every raw character any of its display columns
// shows, so a span over part of an expanded tab covers the whole tab.
HighlightedMatch inRawColumns( QStringView rawText, const HighlightedMatch& displaySpan );

// The colors a Presentation's palette gives a Log Line: its text when
// nothing else colors it, the text of a line outside the Search Limits, and
// the selection. The Line Decorator decides which of them a line gets.
struct LinePalette {
    QColor text;
    QColor base;
    QColor subduedText;
    QColor selectedText;
    QColor selection;

    // The line colors of a Qt palette, as both Presentations take them.
    static LinePalette fromPalette( const QPalette& palette );
};

// Where a Presentation shows whether a Log Line is a Match or a Mark. The
// Text View draws a gutter bullet; the Table View has no gutter, so the
// Line Decorator colors the line's background instead.
enum class LineStatusDisplay {
    InGutter,
    AsBackground,
};

// The colors that show what a Log Line *is* rather than what its text says:
// whether it is a Match, a Mark, or both at once.
//
// Defined once here because every Presentation shows the same three facts,
// each in the only place it has: the Text View paints them as a gutter
// bullet, the Table View -- which has no gutter -- as the row background,
// the overview strip as a line. They have to agree, and a comment asking two
// copies to stay in step is not what keeps them agreeing.
struct LineStatusColors {
    // The color of a Log Line the current Search selected.
    static QColor match();

    // The color of a Log Line the user flagged by hand.
    static QColor mark();

    // The color of a Log Line that is a Mark and a Match at once. Its own
    // color, so that neither fact hides the other.
    static QColor markedMatch();
};

// The single owner of the precedence rule that turns a Line Verdict plus a
// piece of text into a Decoration. It decides which color wins where; it
// does not draw.
//
// Constructed once per repaint with the stable context: the active
// Highlighter Set, the QuickFind pattern, the Color Labels, the main-search
// colors, the Search Limits and the Presentation's palette. Nothing is read
// from a singleton.
class LineDecorator {
public:
    struct Context {
        HighlighterSet highlighterSet;
        std::optional<Highlighter> mainSearch;
        logsquirl::vector<Highlighter> colorLabels;
        QuickFindMatcher quickFind;
        QColor quickFindColor;
        SearchLimits searchLimits;
        LinePalette palette;
        LineStatusDisplay lineStatus = LineStatusDisplay::InGutter;
    };

    explicit LineDecorator( Context context )
        : context_{ std::move( context ) }
    {
    }

    // Decide the facts about a whole Log Line that affect how any part of
    // it looks. A line selected as a whole shows no Highlighter, so none is
    // matched against it. Nothing is read from a singleton.
    LineVerdict verdictFor( const LogLine& line, AbstractLogData::LineType lineType,
                            bool isSelectedAsWhole = false ) const;

    // The line's own colors under a Line Verdict: the colors of text no
    // source colors. Precedence, high to low: selected as a whole (the
    // selection colors), outside the Search Limits (subdued text), a
    // whole-line Highlighter, then Match and Mark where the Presentation
    // shows them as a background. A Context Line's text is dimmed on top.
    HighlightColor lineColorsFor( const LineVerdict& verdict ) const;

    // Turn a run of text plus its Line Verdict into a Decoration covering
    // the whole text, in the coordinate space of the text passed in.
    // Precedence, low to high: the line's own colors, whole-line
    // Highlighter, main search, Color Labels, QuickFind, selection.
    // Overlapping sources are resolved by splitting and overriding. A line
    // outside the Search Limits shows only QuickFind and selection; a line
    // selected as a whole shows only QuickFind.
    Decoration decorate( const QString& text, const LineVerdict& verdict,
                         const std::optional<HighlightedMatch>& selection = std::nullopt ) const;

private:
    Context context_;
};

#endif
