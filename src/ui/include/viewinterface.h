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
#include <utility>

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

// ViewInterface represents a high-level view on a log file.
// This a pure virtual class (interface) which is subclassed
// for each type of view.
class ViewInterface {
public:
    // Set the Open Log File this view shows. It decides what a change of
    // the Log File on disk means; the view only shows it. Ownership stays
    // with the caller but is shared
    void setData( std::shared_ptr<OpenLogFile> openLogFile )
    {
        doSetData( std::move( openLogFile ) );
    }

    // Set the (shared) quickfind pattern object
    void setQuickFindPattern( std::shared_ptr<QuickFindPattern> qfp )
    {
        doSetQuickFindPattern( qfp );
    }

    // Set the (shared) search history object
    void setSavedSearches( SavedSearches* saved_searches )
    {
        doSetSavedSearches( saved_searches );
    }

    // Hand over the Decoration Policy the views of this Log File color Log
    // Lines under. The view holds it and repaints; nothing is torn down, no
    // Presentation is rebuilt and no Search is run again by a new one arriving
    void setDecorationPolicy( const DecorationPolicy& policy )
    {
        doSetDecorationPolicy( policy );
    }

    // Hand over the Presentation Policy the views of this Log File show and
    // scroll under. The view holds it; nothing is torn down and no
    // Presentation is rebuilt by a new one arriving
    void setPresentationPolicy( const PresentationPolicy& policy )
    {
        doSetPresentationPolicy( policy );
    }

    // Hand over the QuickFind Policy the views of this Log File search
    // under. The view holds it
    void setQuickFindPolicy( const QuickFindPolicy& policy )
    {
        doSetQuickFindPolicy( policy );
    }

    // Hand over the Watch Policy this Log File is followed under. The view
    // holds it: what it takes from it is whether following is offered at
    // all, which a changed Policy has to be able to take away and give back
    // without the Log File being opened again
    void setWatchPolicy( const WatchPolicy& policy )
    {
        doSetWatchPolicy( policy );
    }

    // Hand over the File Access Policy this Log File was opened under. The
    // view takes the Encoding a Log File is read with by default from it,
    // which is settled when the file is opened
    void setFileAccessPolicy( const FileAccessPolicy& policy )
    {
        doSetFileAccessPolicy( policy );
    }

    // Tell the views of this Log File that the Highlighter Set Collection
    // changed. They read the active Highlighter Sets when they paint, but
    // the colors of the Color Labels are held alongside their words, so
    // those are read again before everything is painted anew
    void applyHighlighterSetChange()
    {
        doApplyHighlighterSetChange();
    }

    // Tell the views of this Log File that the settings changed. The Policies
    // arrive on their own Axes; what has none -- the font, the shortcuts
    // (CONTEXT.md, Settings Policy), how long the Search history is -- the
    // view reads again. Every open Log File is told, not only the one the
    // current tab shows, and a tab switch tells nobody (#245)
    void rereadSettingsWithoutPolicy()
    {
        doRereadSettingsWithoutPolicy();
    }

    // Hand over whom the view tells of a change it writes itself -- a
    // Highlighter Set ticked in a view's menu, a zoom -- so that the change
    // reaches every open Log File, and not only this one. The Session hands
    // itself over
    void setChangeReport( std::function<void( Changed )> report )
    {
        doSetChangeReport( std::move( report ) );
    }

    // For save/restore of the context
    void setViewContext( const QString& view_context )
    {
        doSetViewContext( view_context );
    }
    // (returned object ownership is transferred to the caller)
    std::shared_ptr<const ViewContextInterface> context( void ) const
    {
        return doGetViewContext();
    }

    // To allow polymorphic destruction
    virtual ~ViewInterface() = default;

protected:
    // Virtual functions (using NVI)
    virtual void doSetData( std::shared_ptr<OpenLogFile> openLogFile ) = 0;
    virtual void doSetQuickFindPattern( std::shared_ptr<QuickFindPattern> qfp ) = 0;
    virtual void doSetSavedSearches( SavedSearches* saved_searches ) = 0;
    virtual void doSetDecorationPolicy( const DecorationPolicy& policy ) = 0;
    virtual void doSetPresentationPolicy( const PresentationPolicy& policy ) = 0;
    virtual void doSetQuickFindPolicy( const QuickFindPolicy& policy ) = 0;
    virtual void doSetWatchPolicy( const WatchPolicy& policy ) = 0;
    virtual void doSetFileAccessPolicy( const FileAccessPolicy& policy ) = 0;
    virtual void doApplyHighlighterSetChange() = 0;
    virtual void doRereadSettingsWithoutPolicy() = 0;
    virtual void doSetChangeReport( std::function<void( Changed )> report ) = 0;
    virtual void doSetViewContext( const QString& view_context ) = 0;
    virtual std::shared_ptr<const ViewContextInterface> doGetViewContext( void ) const = 0;
};
#endif
