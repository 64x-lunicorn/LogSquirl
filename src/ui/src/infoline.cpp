/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
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

#include "containers.h"

#include "infoline.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMenu>
#include <QPainter>
#include <QStyle>
#include <QToolTip>

#include "clipboard.h"

InfoLine::InfoLine()
{
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    setTextInteractionFlags( Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard );
}

void InfoLine::displayGauge( int completion )
{
    // The colors are read from the inherited palette on every update, so a
    // gauge shown across a Theme switch takes the new Theme's colors.
    const auto inherited
        = parentWidget() ? parentWidget()->palette() : QApplication::palette( this );

    int changeoverX = width() * completion / 100;

    // Create a gradient for the progress bar
    QLinearGradient linearGrad( changeoverX - 1, 0, changeoverX + 1, 0 );
    linearGrad.setColorAt( 0, inherited.color( QPalette::Highlight ) );
    linearGrad.setColorAt( 1, inherited.color( QPalette::Window ) );

    // Only the background role is set; every other role keeps following the
    // inherited palette.
    QPalette gaugePalette;
    gaugePalette.setBrush( backgroundRole(), QBrush( linearGrad ) );
    setPalette( gaugePalette );
    gaugeShown_ = true;
}

void InfoLine::hideGauge()
{
    if ( gaugeShown_ ) {
        // A palette with no role set makes the line inherit its palette again.
        setPalette( QPalette() );
    }
    gaugeShown_ = false;
}

void InfoLine::setElidesText( bool elides )
{
    elidesText_ = elides;
    updateGeometry();
    update();
}

QSize InfoLine::minimumSizeHint() const
{
    auto hint = QLabel::minimumSizeHint();
    if ( elidesText_ ) {
        // Room for the start of the text and the ellipsis.
        const auto margins = contentsMargins();
        hint.setWidth(
            std::min( hint.width(), fontMetrics().averageCharWidth() * 4
                                        + fontMetrics().horizontalAdvance( QChar( 0x2026 ) )
                                        + margins.left() + margins.right() ) );
    }
    return hint;
}

bool InfoLine::isElided() const
{
    return elidesText_ && fontMetrics().horizontalAdvance( text() ) > contentsRect().width();
}

bool InfoLine::event( QEvent* event )
{
    if ( event->type() == QEvent::ToolTip && toolTip().isEmpty() && isElided() ) {
        QToolTip::showText( static_cast<QHelpEvent*>( event )->globalPos(), text(), this );
        return true;
    }
    return QLabel::event( event );
}

// Custom painter: draw the background then call QLabel's painter
void InfoLine::paintEvent( QPaintEvent* paintEvent )
{
    // Fill the widget background
    QPainter painter( this );
    painter.fillRect( 0, 0, this->width(), this->height(), palette().brush( backgroundRole() ) );

    if ( isElided() ) {
        drawFrame( &painter );
        const auto textRect = contentsRect();
        const auto elided = fontMetrics().elidedText( text(), Qt::ElideRight, textRect.width() );
        style()->drawItemText(
            &painter, textRect,
            static_cast<int>( QStyle::visualAlignment( layoutDirection(), alignment() ) ),
            palette(), isEnabled(), elided, foregroundRole() );
        return;
    }
    painter.end();

    // Call the parent's painter
    QLabel::paintEvent( paintEvent );
}

void InfoLine::contextMenuEvent( QContextMenuEvent* event )
{
    QMenu menu( this );

    auto copySelection = menu.addAction( "Copy" );
    menu.addSeparator();
    auto selectAll = menu.addAction( "Select all" );

    copySelection->setEnabled( this->hasSelectedText() );
    connect( copySelection, &QAction::triggered, this,
             [ this ]( auto ) { sendTextToClipboard( this->selectedText() ); } );

    connect( selectAll, &QAction::triggered, this,
             [ this ]( auto ) { setSelection( 0, logsquirl::isize( this->text() ) ); } );

    menu.exec( event->globalPos() );
}
