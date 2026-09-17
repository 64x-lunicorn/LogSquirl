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

#include "pathline.h"
#include "theme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QImage>
#include <QMainWindow>
#include <QTest>
#include <QToolBar>
#include <QVBoxLayout>

#include <catch2/catch.hpp>

// Widgets the Themes drew differently, beyond color (#264).

namespace {

const std::initializer_list<QLatin1String> BuiltInThemes{ Theme::LightKey, Theme::DarkKey,
                                                          Theme::HighContrastKey };

} // namespace

SCENARIO( "The tool bar's path field reads as a read-only field in every Theme", "[ui][theme]" )
{
    for ( const auto& name : BuiltInThemes ) {
        GIVEN( "a path field in a tool bar under " + std::string( name.data() ) )
        {
            Theme::apply( name );
            QMainWindow window;
            auto* toolBar = window.addToolBar( QStringLiteral( "Tool bar" ) );
            auto* pathLine = new PathLine();
            // As MainWindow::createToolBars() sets it up.
            pathLine->setFrameStyle( QFrame::StyledPanel );
            pathLine->setFrameShadow( QFrame::Sunken );
            pathLine->setLineWidth( 0 );
            pathLine->setBackgroundRole( QPalette::Window );
            pathLine->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
            pathLine->setText( QStringLiteral( "/tmp/a.log" ) );
            toolBar->addWidget( pathLine );
            window.resize( 600, 120 );
            window.show();
            REQUIRE( QTest::qWaitForWindowExposed( &window ) );
            QTest::qWait( 20 );

            THEN( "it has the edge of an input and the window's background inside" )
            {
                const auto image = pathLine->grab().toImage();
                const auto& theme = Theme::active();
                const auto middle = image.height() / 2;
                REQUIRE( image.pixelColor( 0, middle ).name()
                         == theme.color( ColorToken::InputBorder ).name() );
                REQUIRE( image.pixelColor( image.width() - 1, middle ).name()
                         == theme.color( ColorToken::InputBorder ).name() );
                REQUIRE( image.pixelColor( image.width() - 10, middle ).name()
                         == theme.color( ColorToken::Window ).name() );
            }

            Theme::apply( Theme::defaultTheme() );
        }
    }
}

SCENARIO( "A combo box popup highlights its current item in every Theme", "[ui][theme]" )
{
    for ( const auto& name : BuiltInThemes ) {
        GIVEN( "an open combo box popup under " + std::string( name.data() ) )
        {
            Theme::apply( name );
            QWidget window;
            auto* layout = new QVBoxLayout( &window );
            auto* combo = new QComboBox;
            combo->addItems( { "First", "Second", "Third" } );
            combo->setCurrentIndex( 1 );
            layout->addWidget( combo );
            window.resize( 300, 100 );
            window.show();
            REQUIRE( QTest::qWaitForWindowExposed( &window ) );

            combo->showPopup();
            QTest::qWait( 50 );
            auto* view = combo->view();
            REQUIRE( view->isVisible() );

            THEN( "the current item is filled with Highlight, the others are not" )
            {
                const auto image = view->viewport()->grab().toImage();
                const auto pixelOf = [ & ]( int row ) {
                    const auto rect = view->visualRect( view->model()->index( row, 0 ) );
                    const auto ratio = image.devicePixelRatio();
                    return image.pixelColor( static_cast<int>( ( rect.right() - 4 ) * ratio ),
                                             static_cast<int>( rect.center().y() * ratio ) );
                };
                const auto highlight = Theme::active().color( ColorToken::Highlight ).name();
                REQUIRE( pixelOf( 1 ).name() == highlight );
                REQUIRE( pixelOf( 0 ).name() != highlight );
            }

            combo->hidePopup();
            Theme::apply( Theme::defaultTheme() );
        }
    }
}
