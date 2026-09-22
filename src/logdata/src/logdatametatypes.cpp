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

#include "logdatametatypes.h"

#include "linetypes.h"
#include "loadingstatus.h"
#include "logfiltereddataworker.h"
#include "searchsession.h"

#include <QMetaType>

#include <mutex>

void registerLogDataMetaTypes()
{
    // Called from constructors rather than from a static initializer, which
    // a static library's linker is free to drop.
    static std::once_flag registered;
    std::call_once( registered, [] {
        qRegisterMetaType<LoadingStatus>( "LoadingStatus" );
        qRegisterMetaType<MonitoredFileStatus>( "MonitoredFileStatus" );
        qRegisterMetaType<LinesCount>( "LinesCount" );
        qRegisterMetaType<LineNumber>( "LineNumber" );
        qRegisterMetaType<LineLength>( "LineLength" );
        qRegisterMetaType<SearchId>( "SearchId" );
        qRegisterMetaType<SearchSession::State>( "SearchSession::State" );
    } );
}
