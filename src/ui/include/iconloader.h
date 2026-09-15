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

// Loads an icon from the resources in the variant the active Theme asks for:
// the inverse variant when Theme::usesInverseIcons().
class IconLoader {
public:
    QIcon load( QString name );

private:
    bool shouldInvert() const;
    bool shouldAutoInvert( QString ) const;

    QPixmap loadPixmap( QString, int ) const;

    QPixmap invertPixmap( QPixmap ) const;

    QString makeNonScalableFilename( QString, int, bool ) const;
};

#endif // LOGSQUIRL_ICONLOADER_H
