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

#ifndef RECORDING_VIEWS_H
#define RECORDING_VIEWS_H

#include <memory>
#include <utility>
#include <vector>

#include <QString>

#include "viewinterface.h"

// The views of a Log File as a test sees them: the second adapter behind the
// seam the Session builds a Log File's views through (#248), next to the
// Crawler Widget. They show nothing; they keep the value they were built from
// and every change handed to them, in order.
class RecordingViews final : public ViewInterface {
public:
    explicit RecordingViews( ViewBuild build )
        : build_( std::move( build ) )
    {
    }

    // A factory the Session can be handed, which gives each Log File a
    // RecordingViews and keeps a pointer to it in `built`, in order of opening.
    static ViewFactory factory( std::vector<RecordingViews*>& built )
    {
        return [ &built ]( const ViewBuild& build ) {
            auto* views = new RecordingViews( build );
            built.push_back( views );
            return views;
        };
    }

    const ViewBuild& build() const
    {
        return build_;
    }

    // Every change handed over so far, the latest last.
    const std::vector<ViewChange>& changes() const
    {
        return changes_;
    }

    // Tells the Session of a change, as a view does of one it writes itself.
    void reportChange( Changed change ) const
    {
        if ( build_.changeReport ) {
            build_.changeReport( change );
        }
    }

private:
    class Context final : public ViewContextInterface {
    public:
        explicit Context( QString text )
            : text_( std::move( text ) )
        {
        }

        QString toString() const override
        {
            return text_;
        }

    private:
        QString text_;
    };

    void doApplyChange( const ViewChange& change ) override
    {
        changes_.push_back( change );
    }

    std::shared_ptr<const ViewContextInterface> doGetViewContext() const override
    {
        return std::make_shared<const Context>( build_.viewContext );
    }

    ViewBuild build_;
    std::vector<ViewChange> changes_;
};

#endif
