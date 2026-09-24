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

#include <optional>
#include <vector>

class QObject;

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

    // The Highlighter Sets of the Team Folder (Team groups): read-only here
    // and never stored, they come and go with the sync. Whether one is active
    // is the user's own, stored with the active sets. A Team set that is no
    // longer there is no longer active; setting them re-colors the combined
    // active set at once. Whether this changed a Team set or an activation.
    // dropUnknownActivations false keeps the activations of Team sets that are
    // not in sets: for the time before the first sync has delivered groups,
    // when an empty list means "not known yet", not "none".
    QList<HighlighterSet> teamHighlighterSets() const;
    bool setTeamHighlighterSets( const QList<HighlighterSet>& sets,
                                 bool dropUnknownActivations = true );

    const HighlighterSet& currentActiveSet() const;

    bool hasSet( const QString& setId ) const;
    bool hasSetByName( const QString& name ) const;

    QStringList activeSetIds() const;
    void activateSet( const QString& setId );
    void deactivateSet( const QString& setId );
    void deactivateAll();

    QList<QuickHighlighter> quickHighlighters() const;
    void setQuickHighlighters( const QList<QuickHighlighter>& quickHighlighters );

    // Lets the Color Labels follow the Theme for as long as context lives:
    // after every Theme::apply(), every Color Label whose colors are a
    // built-in Theme's takes the colors of the Theme just applied, and a
    // Color Label the user colored is kept (ADR-0006). Registered once, at
    // startup and before any view, so the labels are in place before the
    // views repaint.
    static void followTheme( QObject* context );

    // The Color Labels the Theme applied now would give this collection, and
    // nothing if it would leave every one of them as it is. The Highlighters
    // Dialog asks it of the copy it edits, so that copy follows the Theme too.
    std::optional<QList<QuickHighlighter>> colorLabelsOfTheme() const;

    // Reads/writes the current config in the QSettings object passed
    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

private:
    static constexpr int HighlighterSetCollection_VERSION = 2;

    void updateCombinedSet();

    // Gives every Color Label that still has a built-in Theme's colors those
    // of the Theme applied now. Whether any Color Label changed.
    bool applyThemeColorLabels();

private:
    QList<HighlighterSet> highlighters_;
    QStringList activeSets_;
    // The active ones of the Team sets, kept apart: a Team set is not known
    // until the first sync, and what the user changes in their own sets must
    // not drop its activation meanwhile.
    QStringList activeTeamSets_;
    QList<HighlighterSet> teamSets_;
    HighlighterSet combinedActiveSet_;

    QList<QuickHighlighter> quickHighlighters_;

    // To simplify this class interface, HighlightersDialog can access our
    // internal structure directly.
    friend class HighlightersDialog;
};

// The color of each Color Label slot, in slot order, as the Highlighter Set
// Collection currently holds them. The Decoration Setup is handed these
// rather than reaching for the collection itself: it is a library below the
// UI and knows nothing of that singleton.
inline std::vector<HighlightColor> colorLabelColors()
{
    const auto quickHighlighters = HighlighterSetCollection::get().quickHighlighters();
    std::vector<HighlightColor> colors;
    colors.reserve( static_cast<size_t>( quickHighlighters.size() ) );
    for ( const auto& quickHighlighter : quickHighlighters ) {
        colors.push_back( quickHighlighter.color );
    }
    return colors;
}

#endif
