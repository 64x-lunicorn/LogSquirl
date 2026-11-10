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

#ifndef OPEN_LOG_FILE_OF_H
#define OPEN_LOG_FILE_OF_H

// The Open Log File a tab shows, for a test that drives or observes the Log
// File itself -- its Searches, Marks, Log Lines, loads -- beyond what the
// tab's CrawlerWidget::state() says (#944). A CrawlerWidget is observed
// through its State only and hands out no Open Log File: the test takes it
// from the Session that holds it, or keeps its own share of the one the
// Session built the CrawlerWidget with.

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include <QVariant>

#include "crawlerwidget.h"
#include "openlogfile.h"
#include "session.h"
#include "viewinterface.h"

struct OpenLogFileOfView {};

template <>
struct Session::access_by<OpenLogFileOfView> {
    static OpenLogFile& openLogFile( const Session& session, const ViewInterface& view )
    {
        return *session.findOpenFileFromView( &view )->openLogFile;
    }
};

// The Open Log File of view, a view of a Log File open in session.
inline OpenLogFile& openLogFileOf( const Session& session, const ViewInterface& view )
{
    return Session::access_by<OpenLogFileOfView>::openLogFile( session, view );
}

namespace open_log_file_of {
// The property the Open Log File a CrawlerWidget was built with is kept in,
// as a weak share: the CrawlerWidget's own share keeps it alive.
inline constexpr const char* BuiltWith = "openLogFileOfTest_builtWith";
} // namespace open_log_file_of

// A Session's view factory: builds a CrawlerWidget as the plain one does, and
// keeps a weak share of the Open Log File it was built with on it, for
// openLogFileOf( crawler ).
inline ViewInterface* buildCrawlerWidget( const ViewBuild& build )
{
    auto* crawler = new CrawlerWidget( build );
    crawler->setProperty( open_log_file_of::BuiltWith,
                          QVariant::fromValue( std::weak_ptr<OpenLogFile>( build.openLogFile ) ) );
    return crawler;
}

// The Open Log File crawler was built with; crawler is one buildCrawlerWidget()
// built.
inline OpenLogFile& openLogFileOf( const CrawlerWidget& crawler )
{
    const auto openLogFile = crawler.property( open_log_file_of::BuiltWith )
                                 .value<std::weak_ptr<OpenLogFile>>()
                                 .lock();
    REQUIRE( openLogFile != nullptr );
    return *openLogFile;
}

#endif
