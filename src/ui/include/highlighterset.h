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
 * Copyright (C) 2019 Anton Filimonov and other contributors
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

#ifndef highlighterSet_H
#define highlighterSet_H

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

#include "highlighter.h"
#include "persistable.h"

struct QuickHighlighter {
    QString name;
    HighlightColor color;
    bool useInCycle;
};

class HighlighterSetCollection final : public Persistable<HighlighterSetCollection> {
  public:
    static const char* persistableName()
    {
        return "HighlighterSetCollection";
    }

    QList<HighlighterSet> highlighterSets() const;
    void setHighlighterSets( const QList<HighlighterSet>& highlighters );

    const HighlighterSet& currentActiveSet() const;

    bool hasSet( const QString& setId ) const;
    bool hasSetByName( const QString& name ) const;

    QStringList activeSetIds() const;
    void activateSet( const QString& setId );
    void deactivateSet( const QString& setId );
    void deactivateAll();

    QList<QuickHighlighter> quickHighlighters() const;
    void setQuickHighlighters( const QList<QuickHighlighter>& quickHighlighters );

    // Reads/writes the current config in the QSettings object passed
    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

  private:
    static constexpr int HighlighterSetCollection_VERSION = 2;

    void updateCombinedSet();

  private:
    QList<HighlighterSet> highlighters_;
    QStringList activeSets_;
    HighlighterSet combinedActiveSet_;

    QList<QuickHighlighter> quickHighlighters_;

    // To simplify this class interface, HighlightersDialog can access our
    // internal structure directly.
    friend class HighlightersDialog;
};

#endif
