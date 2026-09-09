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

// Micro-benchmarks for the decoration path: LineDecorator::verdictFor and
// LineDecorator::decorate, which run for every visible Log Line on every
// scroll. No QApplication is created and this binary links against
// logsquirl_highlighting only -- the Line Decorator is pure and needs no
// GUI to measure.
//
// Also benchmarks the raw-to-display column translation issue #80 replaced
// (AbstractLogView::drawTextArea used to re-expand the tab-expanded prefix
// from scratch for every match; it now builds one rawToDisplayColumns()
// mapping per line and looks up each match in O(1)). "old per-match
// re-expansion" below reimplements the replaced approach standalone, for a
// direct before/after comparison against "rawToDisplayColumns" on the same
// input -- this is not exercised by LineDecorator itself, since the
// translation step lives in the view, not the Decorator.
//
// See tests/benchmarks/README.md for how to run this and compare two runs.

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "linedecorator.h"

namespace {

HighlighterSet setWithHighlighters( int count )
{
    auto set = HighlighterSet::createNewSet( "benchmark" );
    for ( int i = 0; i < count; ++i ) {
        // Every pattern except the last is guaranteed not to match, so the
        // benchmark pays the cost of walking the whole set.
        set.addHighlighter( Highlighter{ QStringLiteral( "NEEDLE_%1_NOT_PRESENT" ).arg( i ),
                                         false, true, QColor{ Qt::white }, QColor{ Qt::blue } } );
    }
    set.addHighlighter(
        Highlighter{ "ERROR", false, false, QColor{ Qt::white }, QColor{ Qt::red } } );
    return set;
}

LineDecorator::Context contextWith( HighlighterSet highlighterSet = {},
                                    std::optional<Highlighter> mainSearch = std::nullopt,
                                    QuickFindMatcher quickFind = {} )
{
    return LineDecorator::Context{ std::move( highlighterSet ), std::move( mainSearch ), {},
                                   std::move( quickFind ), QColor{ Qt::cyan }, SearchLimits{} };
}

QString repeatedWord( const QString& word, int count, QChar separator = QChar{ ' ' } )
{
    QString text;
    text.reserve( ( word.size() + 1 ) * count );
    for ( int i = 0; i < count; ++i ) {
        if ( i > 0 ) {
            text.append( separator );
        }
        text.append( word );
    }
    return text;
}

} // namespace

TEST_CASE( "decoration path benchmarks", "[decoration-benchmark]" )
{
    BENCHMARK_ADVANCED( "common no-match line" )( Catch::Benchmark::Chronometer meter )
    {
        const LineDecorator decorator{ contextWith( setWithHighlighters( 1 ) ) };
        const LogLine line{ 0_lnum, "2026-09-09 12:00:00 INFO connection established" };

        meter.measure( [&] {
            const auto verdict = decorator.verdictFor( line, AbstractLogData::LineTypeFlags::Plain );
            return decorator.decorate( line.text(), verdict );
        } );
    };

    BENCHMARK_ADVANCED( "very long line" )( Catch::Benchmark::Chronometer meter )
    {
        const LineDecorator decorator{ contextWith( setWithHighlighters( 1 ) ) };
        const QString text = repeatedWord( "the quick brown fox jumps over the lazy dog,", 500 );
        const LogLine line{ 0_lnum, text };

        meter.measure( [&] {
            const auto verdict = decorator.verdictFor( line, AbstractLogData::LineTypeFlags::Plain );
            return decorator.decorate( line.text(), verdict );
        } );
    };

    BENCHMARK_ADVANCED( "many highlighters, one whole-line match" )( Catch::Benchmark::Chronometer meter )
    {
        const LineDecorator decorator{ contextWith( setWithHighlighters( 200 ) ) };
        const LogLine line{ 0_lnum, "an ERROR occurred while processing the request" };

        meter.measure( [&] {
            const auto verdict = decorator.verdictFor( line, AbstractLogData::LineTypeFlags::Plain );
            return decorator.decorate( line.text(), verdict );
        } );
    };

    BENCHMARK_ADVANCED( "many matches on one line" )( Catch::Benchmark::Chronometer meter )
    {
        Highlighter mainSearch{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };
        QRegularExpression qfRegex{ "wor" };
        QuickFindMatcher quickFind{ true, qfRegex };
        const LineDecorator decorator{
            contextWith( setWithHighlighters( 1 ), mainSearch, quickFind )
        };
        const QString text = repeatedWord( "word", 2000 );
        const LogLine line{ 0_lnum, text };

        meter.measure( [&] {
            const auto verdict = decorator.verdictFor( line, AbstractLogData::LineTypeFlags::Plain );
            return decorator.decorate( line.text(), verdict );
        } );
    };

    BENCHMARK_ADVANCED( "tab-heavy line" )( Catch::Benchmark::Chronometer meter )
    {
        const LineDecorator decorator{ contextWith( setWithHighlighters( 1 ) ) };
        QString text;
        for ( int i = 0; i < 200; ++i ) {
            text.append( QStringLiteral( "field%1\t" ).arg( i ) );
        }
        const LogLine line{ 0_lnum, text };

        meter.measure( [&] {
            const auto verdict = decorator.verdictFor( line, AbstractLogData::LineTypeFlags::Plain );
            return decorator.decorate( line.text(), verdict );
        } );
    };

    // Isolates the raw-to-display translation step from decorate() itself,
    // to measure issue #80's actual change directly: replacing per-match
    // prefix re-expansion with one rawToDisplayColumns() mapping per line.
    // Same input for both -- 2000 raw-space matches spread across one line.
    {
        Highlighter mainSearch{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };
        const LineDecorator decorator{ contextWith( setWithHighlighters( 1 ), mainSearch ) };
        const QString text = repeatedWord( "word", 2000 );
        const LogLine line{ 0_lnum, text };
        const auto verdict = decorator.verdictFor( line, AbstractLogData::LineTypeFlags::Plain );
        const auto rawSpans = decorator.decorate( text, verdict ).spans();

        BENCHMARK_ADVANCED( "translate to display space: old per-match re-expansion" )
        ( Catch::Benchmark::Chronometer meter )
        {
            meter.measure( [&] {
                auto spans = rawSpans;
                std::transform( spans.begin(), spans.end(), spans.begin(),
                                [ &text ]( const HighlightedMatch& match ) {
                                    const auto prefix
                                        = QStringView{ text }.left( match.startColumn().get() );
                                    const auto matchPart = QStringView{ text }.mid(
                                        match.startColumn().get(), match.size().get() );
                                    const auto expandedPrefixLength
                                        = untabify( prefix.toString() ).size();
                                    const LineLength startDelta
                                        = LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
                                            expandedPrefixLength - prefix.size() ) };
                                    const LineLength expandedMatchLength = LineLength{
                                        untabify( matchPart.toString(),
                                                 LineColumn{
                                                     type_safe::narrow_cast<LineColumn::UnderlyingType>(
                                                         expandedPrefixLength ) } )
                                            .size()
                                    };
                                    const auto lengthDelta
                                        = expandedMatchLength
                                          - LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
                                              matchPart.size() ) };
                                    return HighlightedMatch{ match.startColumn() + startDelta,
                                                             match.size() + lengthDelta,
                                                             match.foreColor(), match.backColor() };
                                } );
                return spans;
            } );
        };

        BENCHMARK_ADVANCED( "translate to display space: rawToDisplayColumns" )
        ( Catch::Benchmark::Chronometer meter )
        {
            meter.measure( [&] {
                auto spans = rawSpans;
                int furthestRawColumn = 0;
                for ( const auto& match : spans ) {
                    furthestRawColumn = std::max<int>(
                        furthestRawColumn,
                        static_cast<int>( match.startColumn().get() + match.size().get() ) );
                }
                const auto rawToDisplay
                    = rawToDisplayColumns( QStringView{ text }.left( furthestRawColumn ) );
                std::transform( spans.begin(), spans.end(), spans.begin(),
                                [ &rawToDisplay ]( const HighlightedMatch& match ) {
                                    const auto rawStart
                                        = static_cast<size_t>( match.startColumn().get() );
                                    const auto rawEnd
                                        = rawStart + static_cast<size_t>( match.size().get() );
                                    const auto displayStart = rawToDisplay[ rawStart ];
                                    const auto displayEnd = rawToDisplay[ rawEnd ];
                                    return HighlightedMatch{
                                        LineColumn{ type_safe::narrow_cast<LineColumn::UnderlyingType>(
                                            displayStart ) },
                                        LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
                                            displayEnd - displayStart ) },
                                        match.foreColor(), match.backColor()
                                    };
                                } );
                return spans;
            } );
        };
    }

    // The scenario the unlimited version of rawToDisplayColumns() regressed:
    // a long line with only one match near the start. Limiting the mapping
    // to the furthest raw column any match reaches (see the view's call
    // site) keeps this as cheap as the old per-match approach, instead of
    // paying for the whole line every time.
    {
        Highlighter mainSearch{ "fox", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };
        const LineDecorator decorator{ contextWith( setWithHighlighters( 1 ), mainSearch ) };
        // "fox" occurs once, a few characters in; the rest of the ~24,000
        // character line has nothing else to match.
        const QString text = QStringLiteral( "the quick brown fox jumps over " )
                              + repeatedWord( "the lazy dog,", 1700 );
        const LogLine line{ 0_lnum, text };
        const auto verdict = decorator.verdictFor( line, AbstractLogData::LineTypeFlags::Plain );
        const auto rawSpans = decorator.decorate( text, verdict ).spans();
        REQUIRE( rawSpans.size() == 1 );

        BENCHMARK_ADVANCED( "long line, one early match: old per-match re-expansion" )
        ( Catch::Benchmark::Chronometer meter )
        {
            meter.measure( [&] {
                auto spans = rawSpans;
                std::transform( spans.begin(), spans.end(), spans.begin(),
                                [ &text ]( const HighlightedMatch& match ) {
                                    const auto prefix
                                        = QStringView{ text }.left( match.startColumn().get() );
                                    const auto matchPart = QStringView{ text }.mid(
                                        match.startColumn().get(), match.size().get() );
                                    const auto expandedPrefixLength
                                        = untabify( prefix.toString() ).size();
                                    const LineLength startDelta
                                        = LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
                                            expandedPrefixLength - prefix.size() ) };
                                    const LineLength expandedMatchLength = LineLength{
                                        untabify( matchPart.toString(),
                                                 LineColumn{
                                                     type_safe::narrow_cast<LineColumn::UnderlyingType>(
                                                         expandedPrefixLength ) } )
                                            .size()
                                    };
                                    const auto lengthDelta
                                        = expandedMatchLength
                                          - LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
                                              matchPart.size() ) };
                                    return HighlightedMatch{ match.startColumn() + startDelta,
                                                             match.size() + lengthDelta,
                                                             match.foreColor(), match.backColor() };
                                } );
                return spans;
            } );
        };

        BENCHMARK_ADVANCED( "long line, one early match: rawToDisplayColumns (limited)" )
        ( Catch::Benchmark::Chronometer meter )
        {
            meter.measure( [&] {
                auto spans = rawSpans;
                int furthestRawColumn = 0;
                for ( const auto& match : spans ) {
                    furthestRawColumn = std::max<int>(
                        furthestRawColumn,
                        static_cast<int>( match.startColumn().get() + match.size().get() ) );
                }
                const auto rawToDisplay
                    = rawToDisplayColumns( QStringView{ text }.left( furthestRawColumn ) );
                std::transform( spans.begin(), spans.end(), spans.begin(),
                                [ &rawToDisplay ]( const HighlightedMatch& match ) {
                                    const auto rawStart
                                        = static_cast<size_t>( match.startColumn().get() );
                                    const auto rawEnd
                                        = rawStart + static_cast<size_t>( match.size().get() );
                                    const auto displayStart = rawToDisplay[ rawStart ];
                                    const auto displayEnd = rawToDisplay[ rawEnd ];
                                    return HighlightedMatch{
                                        LineColumn{ type_safe::narrow_cast<LineColumn::UnderlyingType>(
                                            displayStart ) },
                                        LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
                                            displayEnd - displayStart ) },
                                        match.foreColor(), match.backColor()
                                    };
                                } );
                return spans;
            } );
        };
    }
}
