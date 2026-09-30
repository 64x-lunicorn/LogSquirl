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

#include "regexlabsource.h"

#include <utility>

#include <QPointer>

#include "crawlerwidget.h"

namespace {

// The Log Lines as a sample: counted now, their text read when asked for.
RegexLabSample sampleOf( const CrawlerWidget& tab, logsquirl::vector<LineNumber> lines )
{
    RegexLabSample sample;
    sample.count = lines.size();
    sample.read = [ reader = tab.logLineTextReader(), lines = std::move( lines ) ]() {
        return reader( lines );
    };
    return sample;
}

} // namespace

RegexLabSampleSource regexLabSampleSource( CrawlerWidget& tab, const QString& name )
{
    const QPointer<CrawlerWidget> shown( &tab );

    RegexLabSampleSource source;
    source.name = name;
    source.tab = &tab;
    source.selectedLines = [ shown ]( LinesCount count ) {
        return shown.isNull() ? RegexLabSample{}
                              : sampleOf( *shown, shown->selectedLogLines( count ) );
    };
    source.linesAroundCurrentLine = [ shown ]( LinesCount count ) {
        return shown.isNull() ? RegexLabSample{}
                              : sampleOf( *shown, shown->logLinesAroundCurrentLine( count ) );
    };
    return source;
}
