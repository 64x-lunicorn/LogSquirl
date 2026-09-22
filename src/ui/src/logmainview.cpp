/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2017 Nicolas Bonnefon
 * and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements the LogMainView concrete class.
// Most of the actual drawing and event management is done in AbstractLogView
// Only behaviour specific to the main (top) view is implemented here.

#include "logmainview.h"

#include "abstractlogdata.h"
#include "logfiltereddata.h"
#include "overview.h"

LogMainView::LogMainView( const LogData* newLogData, const QuickFindPattern* const quickFindPattern,
                          Overview* overview, OverviewWidget* overview_widget, bool initialTextWrap,
                          QWidget* parent )
    : AbstractLogView( newLogData, std::make_unique<EveryLogLine>( newLogData ), quickFindPattern,
                       initialTextWrap, parent )
    , logFile_( newLogData )
    , filteredData_( nullptr )
{
    // The main data has a real (non NULL) Overview
    setOverview( overview, overview_widget );
}

// Just update our internal record.
void LogMainView::useNewFiltering( LogFilteredData* filteredData )
{
    filteredData_ = filteredData;
    setLineMapping( std::make_unique<EveryLogLine>( logFile_, filteredData_ ) );

    if ( getOverview() != nullptr )
        getOverview()->setFilteredData( filteredData_ );
}

QString LogMainView::selectedText() const
{
    return getSelectedText();
}

OptionalLineNumber LogMainView::logLineAt( const QPoint& pos ) const
{
    return logLineAtPoint( pos );
}

void LogMainView::showLogLine( LineNumber line )
{
    selectAndDisplayLine( line );
}

void LogMainView::showLogLinePortion( LineNumber line, LinesCount nLines, LineColumn startCol,
                                      LineLength nSymbols )
{
    selectPortionAndDisplayLine( line, nLines, startCol, nSymbols );
}

void LogMainView::updateDecorations()
{
    AbstractLogView::updateDecorations();
}

void LogMainView::rereadLogLines()
{
    AbstractLogView::rereadLogLines();
}

void LogMainView::updateFont( const QFont& font )
{
    AbstractLogView::updateFont( font );
}

void LogMainView::registerShortcuts()
{
    AbstractLogView::registerShortcuts();
}

void LogMainView::setDecorationPolicy( const DecorationPolicy& policy )
{
    AbstractLogView::setDecorationPolicy( policy );
}

void LogMainView::setPresentationPolicy( const PresentationPolicy& policy )
{
    AbstractLogView::setPresentationPolicy( policy );
    setLineNumbersVisible( policy.mainLineNumbersVisible );
    // Both Presentations share the one Overview: the Text View makes room
    // for it, or takes the room back.
    setOverviewVisible( policy.overviewVisible );
}

void LogMainView::setQuickFindPolicy( const QuickFindPolicy& )
{
    // The selected text goes to the window's QuickFind, which reads the
    // Policy itself.
}

void LogMainView::allowFollowMode( bool allow )
{
    AbstractLogView::allowFollowMode( allow );
}

void LogMainView::setColorLabels( const std::vector<QStringList>& labels )
{
    setQuickHighlighters( labels );
}

void LogMainView::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    AbstractLogView::setSearchLimits( startLine, endLine );
}

void LogMainView::setSearchPattern( const RegularExpressionPattern& pattern )
{
    AbstractLogView::setSearchPattern( pattern );
}

void LogMainView::saveSelectedTo( const QString& filename )
{
    AbstractLogView::saveSelectedTo( filename );
}
