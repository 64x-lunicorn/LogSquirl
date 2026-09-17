/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2007 QMUL.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include <QAbstractButton>
#include <QFile>
#include <QPainter>
#include <QPixmap>

#include "iconloader.h"
#include "log.h"
#include "theme.h"

#include <array>

constexpr std::array<int, 8> IconSizes{ 0, 16 };

void loadListEditIcons( QAbstractButton* add, QAbstractButton* remove, QAbstractButton* up,
                        QAbstractButton* down )
{
    IconLoader iconLoader;
    add->setIcon( iconLoader.load( "icons8-plus-16" ) );
    remove->setIcon( iconLoader.load( "icons8-minus-16" ) );
    up->setIcon( iconLoader.load( "icons8-up-16" ) );
    down->setIcon( iconLoader.load( "icons8-down-arrow-16" ) );
}

QIcon IconLoader::load( QString name )
{
    const bool invert = Theme::active().usesInverseIcons();
    QIcon icon;
    for ( int sz : IconSizes ) {
        QPixmap pmap( loadPixmap( name, sz, invert ) );
        if ( !pmap.isNull() )
            icon.addPixmap( pmap );
    }
    return icon;
}

QIcon IconLoader::loadCheckable( QString name )
{
    const auto& theme = Theme::active();
    const bool invert = theme.usesInverseIcons();
    const bool invertWhenChecked = theme.usesInverseIconsWhenChecked();

    QIcon icon = load( name );
    if ( invertWhenChecked == invert ) {
        return icon;
    }
    for ( int sz : IconSizes ) {
        QPixmap pmap( loadPixmap( name, sz, invertWhenChecked ) );
        if ( !pmap.isNull() )
            icon.addPixmap( pmap, QIcon::Normal, QIcon::On );
    }
    return icon;
}

bool IconLoader::shouldAutoInvert( QString /*name*/ ) const
{
    return true;
}

QPixmap IconLoader::loadPixmap( QString name, int size, bool invert ) const
{
    QString nonScalableName;
    QPixmap pmap;
    // attempt to load a pixmap with the right size and inversion
    nonScalableName = makeNonScalableFilename( name, size, invert );
    pmap = QPixmap( nonScalableName );
    if ( pmap.isNull() && invert ) {
        // if that failed, and we were asking for an inverted pixmap,
        // that may mean we don't have an inverted version of it. We
        // could either auto-invert or use the uninverted version
        nonScalableName = makeNonScalableFilename( name, size, false );
        pmap = QPixmap( nonScalableName );

        if ( !pmap.isNull() && shouldAutoInvert( name ) ) {
            pmap = invertPixmap( pmap );
        }
    }
    return pmap;
}

QString IconLoader::makeNonScalableFilename( QString name, int size, bool invert ) const
{
    if ( invert ) {
        if ( size == 0 ) {
            return QString( ":/images/%1_inverse.png" ).arg( name );
        }
        else {
            return QString( ":/images/%1-%2_inverse.png" ).arg( name ).arg( size );
        }
    }
    else {
        if ( size == 0 ) {
            return QString( ":/images/%1.png" ).arg( name );
        }
        else {
            return QString( ":/images/%1-%2.png" ).arg( name ).arg( size );
        }
    }
}

QPixmap IconLoader::invertPixmap( QPixmap pmap ) const
{
    // No suitable inverted icon found for black background; try to
    // auto-invert the default one
    QImage img = pmap.toImage().convertToFormat( QImage::Format_ARGB32 );
    for ( int y = 0; y < img.height(); ++y ) {
        for ( int x = 0; x < img.width(); ++x ) {
            QRgb rgba = img.pixel( x, y );
            QColor colour = QColor( qRed( rgba ), qGreen( rgba ), qBlue( rgba ), qAlpha( rgba ) );
            int alpha = colour.alpha();
            if ( colour.saturation() < 5 && colour.alpha() > 10 ) {
                colour.setHsv( colour.hue(), colour.saturation(), 255 - colour.value() );
                colour.setAlpha( alpha );
                img.setPixel( x, y, colour.rgba() );
            }
        }
    }
    pmap = QPixmap::fromImage( img );
    return pmap;
}