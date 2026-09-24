/*
 * Copyright (C) 2009, 2010, 2011 Nicolas Bonnefon and other contributors
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

// This file implements class HighlighterSetCollection

#include <algorithm>
#include <array>
#include <iterator>

#include <QApplication>
#include <QSettings>

#include "containers.h"
#include "log.h"
#include "theme.h"

#include "highlighterset.h"

QList<HighlighterSet> HighlighterSetCollection::highlighterSets() const
{
    return highlighters_;
}

void HighlighterSetCollection::setHighlighterSets( const QList<HighlighterSet>& highlighters )
{
    highlighters_ = highlighters;

    activeSets_.erase( std::remove_if( activeSets_.begin(), activeSets_.end(),
                                       [ this ]( const auto& setId ) { return !hasSet( setId ); } ),
                       activeSets_.end() );
    updateCombinedSet();
}

QList<HighlighterSet> HighlighterSetCollection::teamHighlighterSets() const
{
    return teamSets_;
}

bool HighlighterSetCollection::setTeamHighlighterSets( const QList<HighlighterSet>& sets,
                                                       bool dropUnknownActivations )
{
    const auto sameSets = std::equal(
        teamSets_.cbegin(), teamSets_.cend(), sets.cbegin(), sets.cend(),
        []( const auto& a, const auto& b ) { return a.id() == b.id() && a.sameAs( b ); } );
    const auto activeBefore = activeTeamSets_.size();

    teamSets_ = sets;
    if ( dropUnknownActivations ) {
        activeTeamSets_.erase(
            std::remove_if( activeTeamSets_.begin(), activeTeamSets_.end(),
                            [ this ]( const auto& setId ) {
                                return std::none_of(
                                    teamSets_.cbegin(), teamSets_.cend(),
                                    [ &setId ]( const auto& set ) { return set.id() == setId; } );
                            } ),
            activeTeamSets_.end() );
    }
    updateCombinedSet();
    return !sameSets || activeTeamSets_.size() != activeBefore;
}

const HighlighterSet& HighlighterSetCollection::currentActiveSet() const
{
    return combinedActiveSet_;
}

void HighlighterSetCollection::updateCombinedSet()
{
    combinedActiveSet_.highlighterList_.clear();

    for ( const HighlighterSet& set : logsquirl::as_const( highlighters_ ) ) {
        if ( !activeSets_.contains( set.id() ) ) {
            continue;
        }
        combinedActiveSet_.highlighterList_.append( set.highlighterList_ );
    }
    for ( const HighlighterSet& set : logsquirl::as_const( teamSets_ ) ) {
        if ( activeTeamSets_.contains( set.id() ) ) {
            combinedActiveSet_.highlighterList_.append( set.highlighterList_ );
        }
    }

    combinedActiveSet_.compile();
}

QStringList HighlighterSetCollection::activeSetIds() const
{
    return activeSets_ + activeTeamSets_;
}

void HighlighterSetCollection::activateSet( const QString& setId )
{
    LOG_INFO << "activating set " << setId;
    if ( !hasSet( setId ) || activeSetIds().contains( setId ) ) {
        LOG_WARNING << "Set not found or already active";
        return;
    }

    const bool isTeamSet
        = std::any_of( teamSets_.cbegin(), teamSets_.cend(),
                       [ &setId ]( const auto& set ) { return set.id() == setId; } );
    ( isTeamSet ? activeTeamSets_ : activeSets_ ).append( setId );
    updateCombinedSet();
}

void HighlighterSetCollection::deactivateSet( const QString& setId )
{
    LOG_INFO << "deactivating set " << setId;
    activeSets_.removeAll( setId );
    activeTeamSets_.removeAll( setId );
    updateCombinedSet();
}

void HighlighterSetCollection::deactivateAll()
{
    LOG_INFO << "deactivating all sets";
    activeSets_.clear();
    activeTeamSets_.clear();
    updateCombinedSet();
}

bool HighlighterSetCollection::hasSet( const QString& setId ) const
{
    const auto hasId = [ &setId ]( const auto& s ) { return s.id() == setId; };
    return std::any_of( highlighters_.begin(), highlighters_.end(), hasId )
           || std::any_of( teamSets_.begin(), teamSets_.end(), hasId );
}

bool HighlighterSetCollection::hasSetByName( const QString& setName ) const
{
    return std::any_of( highlighters_.begin(), highlighters_.end(),
                        [ setName ]( const auto& s ) { return s.name() == setName; } );
}

void HighlighterSetCollection::followTheme( QObject* context )
{
    Theme::whenApplied( context, [] {
        // Every Theme switch runs this, most of them between Themes that give
        // the Color Labels the same colors. The held copy decides whether
        // there is anything to do at all, so only a switch that really
        // recolors a Color Label syncs the settings store (#301) and writes to
        // it -- and it reads the store again first, so the write does not drop
        // what another instance saved in the meantime.
        if ( !HighlighterSetCollection::get().colorLabelsOfTheme().has_value() ) {
            return;
        }

        auto& collection = HighlighterSetCollection::getSynced();
        if ( collection.applyThemeColorLabels() ) {
            collection.save();
        }
    } );
}

std::optional<QList<QuickHighlighter>> HighlighterSetCollection::colorLabelsOfTheme() const
{
    const auto& themeLabels = Theme::active().colorLabels();

    auto labels = quickHighlighters_;
    auto changed = false;
    const auto slots = std::min( static_cast<std::size_t>( labels.size() ), themeLabels.size() );
    for ( std::size_t slot = 0; slot < slots; ++slot ) {
        auto& color = labels[ static_cast<int>( slot ) ].color;
        if ( !Theme::isBuiltInColorLabel( slot, color.foreColor, color.backColor ) ) {
            continue;
        }

        const auto& themeLabel = themeLabels[ slot ];
        if ( Theme::sameColorLabelColor( color.foreColor, themeLabel.foreColor )
             && Theme::sameColorLabelColor( color.backColor, themeLabel.backColor ) ) {
            continue;
        }

        color.foreColor = themeLabel.foreColor;
        color.backColor = themeLabel.backColor;
        changed = true;
    }

    return changed ? std::optional{ labels } : std::nullopt;
}

bool HighlighterSetCollection::applyThemeColorLabels()
{
    auto labels = colorLabelsOfTheme();
    if ( !labels ) {
        return false;
    }

    quickHighlighters_ = std::move( *labels );
    return true;
}

QList<QuickHighlighter> HighlighterSetCollection::quickHighlighters() const
{
    return quickHighlighters_;
}

void HighlighterSetCollection::setQuickHighlighters(
    const QList<QuickHighlighter>& quickHighlighters )
{
    quickHighlighters_ = quickHighlighters;
}

void HighlighterSetCollection::saveToStorage( QSettings& settings ) const
{
    LOG_INFO << "HighlighterSetCollection::saveToStorage, v" << HighlighterSetCollection_VERSION;

    settings.beginGroup( "HighlighterSetCollection" );
    settings.setValue( "version", HighlighterSetCollection_VERSION );
    settings.setValue( "active_sets", activeSets_ );
    settings.setValue( "active_team_sets", activeTeamSets_ );

    LOG_INFO << activeSets_;

    settings.remove( "sets" );
    settings.beginWriteArray( "sets" );
    for ( int i = 0; i < highlighters_.size(); ++i ) {
        settings.setArrayIndex( i );
        highlighters_[ i ].saveToStorage( settings );
    }
    settings.endArray();
    settings.remove( "quick" );
    settings.beginWriteArray( "quick" );
    for ( int i = 0; i < quickHighlighters_.size(); ++i ) {
        settings.setArrayIndex( i );
        settings.setValue( "name", quickHighlighters_[ i ].name );
        // An invalid color means the Log Line's own color. Written as a name
        // it would come back as opaque black; an empty string comes back
        // invalid.
        const auto storedColor = []( const QColor& color ) {
            return color.isValid() ? color.name( QColor::HexArgb ) : QString{};
        };
        settings.setValue( "fore_colour", storedColor( quickHighlighters_[ i ].color.foreColor ) );
        settings.setValue( "back_colour", storedColor( quickHighlighters_[ i ].color.backColor ) );
        settings.setValue( "cycle", quickHighlighters_[ i ].useInCycle );
    }
    settings.endArray();
    settings.endGroup();
}

void HighlighterSetCollection::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "HighlighterSetCollection::retrieveFromStorage";

    highlighters_.clear();
    quickHighlighters_.clear();

    if ( settings.contains( "HighlighterSetCollection/version" ) ) {
        settings.beginGroup( "HighlighterSetCollection" );
        if ( settings.value( "version" ).toInt() <= HighlighterSetCollection_VERSION ) {
            int size = settings.beginReadArray( "sets" );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );
                HighlighterSet highlighterSet;
                highlighterSet.retrieveFromStorage( settings );
                highlighters_.append( std::move( highlighterSet ) );
            }
            settings.endArray();

            activeSets_ = settings.value( "active_sets" ).toStringList();
            activeTeamSets_ = settings.value( "active_team_sets" ).toStringList();

            auto currentSet = settings.value( "current" ).toString();
            settings.remove( "current" );
            if ( !currentSet.isEmpty() ) {
                activateSet( currentSet );
                settings.setValue( "active_sets", activeSets_ );
            }

            size = settings.beginReadArray( "quick" );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );
                QuickHighlighter quickHighlighter;
                quickHighlighter.color.foreColor
                    = QColor( settings.value( "fore_colour" ).toString() );
                quickHighlighter.color.backColor
                    = QColor( settings.value( "back_colour" ).toString() );
                quickHighlighter.useInCycle = settings.value( "cycle", true ).toBool();

                quickHighlighter.name
                    = settings.value( "name", QString( "Color label %1" ).arg( i + 1 ) ).toString();

                quickHighlighters_.append( std::move( quickHighlighter ) );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown version of HighlighterSetCollection, ignoring it...";
        }

        settings.endGroup();
    }

    // A Color Label the settings do not have yet gets the colors of the Theme
    // applied now, as one that still has a built-in Theme's colors does on
    // every switch (ADR-0006).
    const std::array<QString, ColorLabelCount> defaultNames{
        QApplication::tr( "Color label 1" ), QApplication::tr( "Color label 2" ),
        QApplication::tr( "Color label 3" ), QApplication::tr( "Color label 4" ),
        QApplication::tr( "Color label 5" ), QApplication::tr( "Color label 6" ),
        QApplication::tr( "Color label 7" ), QApplication::tr( "Color label 8" ),
        QApplication::tr( "Color label 9" ),
    };

    const auto& themeLabels = Theme::active().colorLabels();
    QList<QuickHighlighter> defaultLabels;
    for ( std::size_t slot = 0; slot < ColorLabelCount; ++slot ) {
        defaultLabels.append( { defaultNames[ slot ],
                                { themeLabels[ slot ].foreColor, themeLabels[ slot ].backColor },
                                true } );
    }

    if ( quickHighlighters_.size() < defaultLabels.size() ) {
        LOG_WARNING << "Got " << quickHighlighters_.size() << " quick highlighters";
        std::copy( defaultLabels.begin() + quickHighlighters_.size(), defaultLabels.end(),
                   std::back_inserter( quickHighlighters_ ) );
    }
    else if ( quickHighlighters_.size() > defaultLabels.size() ) {
        LOG_WARNING << "Got " << quickHighlighters_.size() << " quick highlighters";

        quickHighlighters_.erase( quickHighlighters_.begin() + defaultLabels.size(),
                                  quickHighlighters_.end() );
    }

    HighlighterSet oldHighlighterSet;
    oldHighlighterSet.retrieveFromStorage( settings );
    if ( !oldHighlighterSet.isEmpty() ) {
        LOG_INFO << "Importing old HighlighterSet";
        activateSet( oldHighlighterSet.id() );
        highlighters_.append( std::move( oldHighlighterSet ) );
        settings.remove( "HighlighterSet" );
        saveToStorage( settings );
    }

    LOG_INFO << "Loaded " << highlighters_.size() << " highlighter sets";
    updateCombinedSet();
}
