/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#ifndef LOGSQUIRL_LOG_H
#define LOGSQUIRL_LOG_H

#include <QDebugStateSaver>
#include <QMessageLogContext>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <cassert>

#define LOG_IF_( severity )                                                                        \
    if ( !logging::needLogging( severity ) ) {                                                     \
        ;                                                                                          \
    }                                                                                              \
    else

#define LOG_DEBUG LOG_IF_( QtDebugMsg ) qDebug().nospace()
#define LOG_INFO LOG_IF_( QtInfoMsg ) qInfo().nospace()
#define LOG_WARNING LOG_IF_( QtWarningMsg ) qWarning().nospace()
#define LOG_ERROR LOG_IF_( QtCriticalMsg ) qCritical().nospace()

namespace logging {
namespace detail {
// Written by the Logger when logging is (re)configured, read on every
// LOG_* statement. Kept here so needLogging() inlines into the macro
// instead of costing a call into the logging library per statement.
inline std::atomic_bool isAnyLogEnabled = false;
inline std::atomic<uint8_t> maxLogLevel = 0;

// Numeric values match logging::LogLevel (logger.h).
constexpr uint8_t logLevelOf( QtMsgType type ) noexcept
{
    switch ( type ) {
    case QtFatalMsg:
        return 1;
    case QtCriticalMsg:
        return 2;
    case QtWarningMsg:
        return 3;
    case QtInfoMsg:
        return 4;
    case QtDebugMsg:
        return 5;
    default:
        return 4;
    }
}
} // namespace detail

inline bool needLogging( QtMsgType type ) noexcept
{
    return detail::isAnyLogEnabled.load( std::memory_order_relaxed )
           && detail::logLevelOf( type ) <= detail::maxLogLevel.load( std::memory_order_relaxed );
}
} // namespace logging

template <typename T>
QDebug operator<<( QDebug dbg, const std::optional<T>& opt )
{
    QDebugStateSaver saver( dbg );
    opt ? dbg << *opt : dbg << "<empty>";

    return dbg;
}

inline QDebug operator<<( QDebug dbg, const std::chrono::microseconds& duration )
{
    QDebugStateSaver saver( dbg );
    dbg << static_cast<float>( duration.count() ) / 1000.f << " ms";

    return dbg;
}

inline QDebug operator<<( QDebug dbg, const std::string& str )
{
    QDebugStateSaver saver( dbg );
    dbg << QString::fromStdString( str );

    return dbg;
}

#endif