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

#ifndef LOGSQUIRL_ICONLOADER_H
#define LOGSQUIRL_ICONLOADER_H

#include <QIcon>

class QAbstractButton;

// Loads an icon from the resources in the variant the active Theme asks for:
// the inverse variant when Theme::usesInverseIcons().
class IconLoader {
public:
    QIcon load( QString name );

    // An icon for a checkable button: its On state, which a checked button
    // shows, is in the variant Theme::usesInverseIconsWhenChecked() asks for
    // (#257). Only for buttons: a tab bar, menu or item view also shows the On
    // state of a selected tab, checked action or open item, on another
    // background.
    QIcon loadCheckable( QString name );

private:
    bool shouldAutoInvert( QString ) const;

    QPixmap loadPixmap( QString, int, bool invert ) const;

    QPixmap invertPixmap( QPixmap ) const;

    QString makeNonScalableFilename( QString, int, bool ) const;
};

// Sets the plus, minus, up and down arrow icons of a list editor's add,
// remove, move up and move down buttons, in the active Theme's variant.
void loadListEditIcons( QAbstractButton* add, QAbstractButton* remove, QAbstractButton* up,
                        QAbstractButton* down );

#endif // LOGSQUIRL_ICONLOADER_H
