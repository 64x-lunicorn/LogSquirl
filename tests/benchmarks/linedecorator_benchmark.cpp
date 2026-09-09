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
}
