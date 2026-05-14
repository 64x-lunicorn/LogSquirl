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

#include <catch2/catch.hpp>

#include <QFont>
#include <QWidget>

#include "abstractlogview.h"
#include "logdata.h"
#include "quickfindpattern.h"

namespace {

// Minimal concrete subclass for testing AbstractLogView
class TestLogView : public AbstractLogView {
    Q_OBJECT
  public:
    TestLogView( const AbstractLogData* logData, const QuickFindPattern* qfp,
                 QWidget* parent = nullptr )
        : AbstractLogView( logData, qfp, parent )
    {
    }

  protected:
    AbstractLogData::LineType lineType( LineNumber ) const override
    {
        return {};
    }
};

} // namespace

SCENARIO( "AbstractLogView updateDisplaySize keeps charWidth_ safe", "[abstractlogview][viewport]" )
{
    LogData logData;
    QuickFindPattern qfp;

    GIVEN( "A log view widget created with default font" )
    {
        TestLogView view( &logData, &qfp );
        view.resize( 800, 600 );

        WHEN( "updateFont is called with a very small font" )
        {
            // A 1-pixel font may report zero width for "m" on some platforms
            QFont tinyFont( "Monospace", 1 );
            view.updateFont( tinyFont );

            THEN( "the view does not crash and remains in a valid state" )
            {
                // If charWidth_ were 0, this would trigger a division by zero
                // internally in getNbVisibleCols(). The show()/repaint() path
                // exercises that code.
                view.show();
                view.repaint();
                REQUIRE( true ); // Reaching here means no crash
            }
        }

        WHEN( "updateFont is called with a normal font" )
        {
            QFont normalFont( "Courier", 12 );
            view.updateFont( normalFont );

            THEN( "the view renders without crashing" )
            {
                view.show();
                view.repaint();
                REQUIRE( true );
            }
        }
    }
}

#include "abstractlogview_test.moc"
