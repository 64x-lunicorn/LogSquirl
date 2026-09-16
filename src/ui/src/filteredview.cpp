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

FilteredView::FilteredView( LogFilteredData* newLogData,
                            const QuickFindPattern* const quickFindPattern, bool initialTextWrap,
                            QWidget* parent )
    : AbstractLogView( newLogData, std::make_unique<FilteredViewLines>( newLogData ),
                       quickFindPattern, initialTextWrap, parent )
{
    // Kept for what is visible in the view
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
