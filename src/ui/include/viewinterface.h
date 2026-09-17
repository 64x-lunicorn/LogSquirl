/*
 * Copyright (C) 2013, 2014 Nicolas Bonnefon and other contributors
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

#ifndef VIEWINTERFACE_H
#define VIEWINTERFACE_H

#include <functional>
#include <memory>
#include <optional>

#include <QString>

#include "changed.h"
#include "settingspolicies.h"

class OpenLogFile;
class SavedSearches;
class QuickFindPattern;

// ViewContextInterface represents the private information
// the concrete view will be able to save and restore.
// It can be marshalled to persistent storage.
class ViewContextInterface {
public:
    virtual ~ViewContextInterface() = default;

    virtual QString toString() const = 0;
};

// Everything a Log File's views are built from, handed over in one value
// (#248): nothing arrives later that the views need before they can show the
// Log File, so there is no order to get wrong.
struct ViewBuild {
    // The Open Log File the views show. It decides what a change of the Log
    // File on disk means, and it holds the Log Format Catalog its Format
    // Recognition runs on -- the Session's one Catalog. Ownership is shared.
    std::shared_ptr<OpenLogFile> openLogFile;

    // The QuickFind pattern every Log File of the Session shares.
    std::shared_ptr<QuickFindPattern> quickFindPattern;

    // The Policies the Session holds when the Log File is opened. A view
    // takes the Decoration, Presentation, QuickFind and Watch Policies from
    // them, which changes later replace, and the File Access Policy, which
    // is settled when the Log File is opened.
    SettingsPolicies policies;

    // The Search history every Log File of the Session shares.
    SavedSearches* savedSearches = nullptr;

    // The view context saved with the Session for this Log File, restored
    // once the views are built; empty when there is none.
    QString viewContext;

    // Whom the views tell of a change they write themselves -- a Highlighter
    // Set ticked in a view's menu, a zoom -- so that the change reaches every
    // open Log File, and not only this one. Empty when the views are not
    // opened through a Session.
    std::function<void( Changed )> changeReport;
};

// What changed for the views of an open Log File, handed to them in one
// value: each Axis that changed carries its new Policy, an Axis that did not
// change is empty and is not handed out again.
struct ViewChange {
    // The views hold it and repaint; nothing is torn down, no Presentation is
    // rebuilt and no Search is run again.
    std::optional<DecorationPolicy> decoration{};
    // The views show and scroll under it; nothing is torn down.
    std::optional<PresentationPolicy> presentation{};
    // How the views' Search line reads its pattern and what the Table View
    // hands a QuickFind. The window's QuickFind bar reads it from the Session.
    std::optional<QuickFindPolicy> quickFind{};
    // Whether following is offered at all, taken away and given back without
    // the Log File being opened again.
    std::optional<WatchPolicy> watch{};

    // The settings changed: what has no Policy -- the font, the shortcuts
    // (CONTEXT.md, Settings Policy), how long the Search history is -- is
    // read again, after the Policies above are applied.
    bool rereadSettingsWithoutPolicy = false;

    // Only the font changed, as a zoom changes it: it is read again, and
    // nothing else is.
    bool font = false;

    // The Highlighter Set Collection changed. The views read the active
    // Highlighter Sets when they paint, but the colors of the Color Labels
    // are held alongside their words, so those are read again before
    // everything is painted anew.
    bool highlighterSets = false;

    bool isEmpty() const
    {
        return !decoration && !presentation && !quickFind && !watch && !rereadSettingsWithoutPolicy
               && !font && !highlighterSets;
    }

    bool operator==( const ViewChange& ) const = default;
};

// ViewInterface represents the views of one Log File, as the Session sees
// them. They are built from a ViewBuild by the factory handed to the
// Session; after that, the Session has two things to say to them: what
// changed, and -- to save the Session -- what their view context is (#248).
//
// The widget is one adapter; the tests' recording fake is the other.
class ViewInterface {
public:
    // Hand over what changed. Every open Log File is told, not only the one
    // the current tab shows, and a tab switch tells nobody (#245).
    void applyChange( const ViewChange& change )
    {
        doApplyChange( change );
    }

    // For saving the Session: restoring it goes through ViewBuild.
    // (returned object ownership is transferred to the caller)
    std::shared_ptr<const ViewContextInterface> context( void ) const
    {
        return doGetViewContext();
    }

    // To allow polymorphic destruction
    virtual ~ViewInterface() = default;

protected:
    // Virtual functions (using NVI)
    virtual void doApplyChange( const ViewChange& change ) = 0;
    virtual std::shared_ptr<const ViewContextInterface> doGetViewContext( void ) const = 0;
};

// Builds the views of one Log File from everything they are built from. The
// ownership of the views is given to whoever called the Session.
using ViewFactory = std::function<ViewInterface*( const ViewBuild& )>;

#endif
