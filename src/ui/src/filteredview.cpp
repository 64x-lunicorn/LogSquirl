/*
 * Copyright (C) 2009, 2010, 2012, 2017 Nicolas Bonnefon and other contributors
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

// This file implements the FilteredView concrete class.
// Most of the actual drawing and event management is done in AbstractLogView
// Only behaviour specific to the filtered (bottom) view is implemented here.

#include <cassert>

#include "filteredview.h"
#include "logdata.h"
#include "shortcuts.h"

FilteredView::FilteredView( LogFilteredData* newLogData,
                            const QuickFindPattern* const quickFindPattern, bool initialTextWrap,
                            QWidget* parent )
    : AbstractLogView( newLogData, quickFindPattern, initialTextWrap, parent )
{
    // We keep a copy of the filtered data for fast lookup of the line type
    logFilteredData_ = newLogData;
}

void FilteredView::setVisibility( Visibility visi )
{
    assert( logFilteredData_ );

    logFilteredData_->setVisibility( visi );

    updateData();
}

FilteredView::Visibility FilteredView::visibility() const
{
    assert( logFilteredData_ );

    return logFilteredData_->visibility();
}

// For the filtered view, a line is always matching!
AbstractLogData::LineType FilteredView::lineType( LineNumber lineNumber ) const
{
    // line in filteredview corresponds to index
    return logFilteredData_->lineTypeByIndex( lineNumber );
}

LineNumber FilteredView::displayLineNumber( LineNumber lineNumber ) const
{
    // Display a 1-based index
    return logFilteredData_->getMatchingLineNumber( lineNumber ) + 1_lcount;
}

LineNumber FilteredView::lineIndex( LineNumber lineNumber ) const
{
    return logFilteredData_->getLineIndexNumber( lineNumber );
}

LineNumber FilteredView::maxDisplayLineNumber() const
{
    return LineNumber( logFilteredData_->getNbTotalLines().get() );
}

QuickFindLines FilteredView::quickFindLines() const
{
    // The worker reads the Log File's text, which is safe off the UI thread,
    // and never the LogFilteredData, which the UI thread goes on changing.
    return QuickFindLines::someLogLines( logFilteredData_->sourceLogData(),
                                         logFilteredData_->copyDisplayedLines() );
}

DisplayedLinesReader FilteredView::linesToSave() const
{
    // As for QuickFind, the save reads the Log File's text and never the
    // LogFilteredData, which the UI thread goes on changing.
    return [ logFile = &logFilteredData_->sourceLogData(),
             lines = std::make_shared<const SearchResultArray>(
                 logFilteredData_->copyDisplayedLines() ) ]( LineNumber first, LinesCount count ) {
        logsquirl::vector<QString> text;
        text.reserve( count.get() );
        for ( auto position = first.get(); position < first.get() + count.get(); ++position ) {
            const auto logLine = lineAtPosition( *lines, LineNumber( position ) );
            text.push_back( logLine.has_value() ? logFile->getLineString( *logLine ) : QString{} );
        }
        return text;
    };
}

void FilteredView::doRegisterShortcuts()
{
    LOG_INFO << "Registering shortcuts for filtered view";
    AbstractLogView::doRegisterShortcuts();
    // Next Mark goes down and previous Mark up, as in the main view (#233).
    registerShortcut( ShortcutAction::LogViewNextMark, [ this ] {
        const auto nbLines = logFilteredData_->getNbLine();
        for ( auto i = getViewPosition() + 1_lcount; i < nbLines; ++i ) {
            if ( lineType( i ).testFlag( LogFilteredData::LineTypeFlags::Mark ) ) {
                selectAndDisplayLine( i );
                break;
            }
        }
    } );
    registerShortcut( ShortcutAction::LogViewPrevMark, [ this ] {
        for ( auto i = getViewPosition(); i > 0_lnum; ) {
            --i;
            if ( lineType( i ).testFlag( LogFilteredData::LineTypeFlags::Mark ) ) {
                selectAndDisplayLine( i );
                break;
            }
        }
    } );
}
