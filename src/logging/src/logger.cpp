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

#include "logger.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>

namespace logging {

// log.h's inline needLogging() mirrors LogLevel with plain numbers.
static_assert( detail::logLevelOf( QtFatalMsg ) == static_cast<uint8_t>( LogLevel::Fatal ) );
static_assert( detail::logLevelOf( QtCriticalMsg ) == static_cast<uint8_t>( LogLevel::Error ) );
static_assert( detail::logLevelOf( QtWarningMsg ) == static_cast<uint8_t>( LogLevel::Warning ) );
static_assert( detail::logLevelOf( QtInfoMsg ) == static_cast<uint8_t>( LogLevel::Info ) );
static_assert( detail::logLevelOf( QtDebugMsg ) == static_cast<uint8_t>( LogLevel::Debug ) );

void logsquirlFileMessageHandler( QtMsgType type, const QMessageLogContext& context,
                                  const QString& msg );
void logsquirlConsoleMessageHandler( QtMsgType type, const QMessageLogContext& context,
                                     const QString& msg );
void logsquirlNoopMessageHandler( QtMsgType, const QMessageLogContext&, const QString& ) {}

class Logger {
public:
    static Logger& instance()
    {
        static Logger l;
        return l;
    }

    void fileMessageHandler( QtMsgType type, const QMessageLogContext& context, const QString& msg )
    {
        if ( !needLogging( type ) ) {
            return;
        }

        // qFormatLogMessage() must be inside the lock: it formats
        // %{time} via QDateTime/QLocale, which isn't safe to call
        // concurrently from multiple threads (this handler runs on
        // whichever thread logs -- search worker threads included) --
        // calling it unguarded corrupted the heap under concurrent
        // logging (crashes deep in QLocale::quoteString / QArrayData
        // reallocation on Windows CI, never locally).
        ScopedLock lock( mutex_ );

        auto messageToPrint = qFormatLogMessage( type, context, msg ).toUtf8();
        messageToPrint.append( '\n' );

        const auto flush = shouldFlush( type );

        if ( logFile_ ) {
            logFile_->write( messageToPrint );
            if ( flush ) {
                logFile_->flush();
            }
        }

        if ( isConsoleLogEnabled_ ) {
            writeToConsole( messageToPrint, flush );
        }
    }

    void consoleMessageHandler( QtMsgType type, const QMessageLogContext& context,
                                const QString& msg )
    {
        if ( !needLogging( type ) ) {
            return;
        }

        // See the identical comment in fileMessageHandler(): formatting
        // must happen under the lock, not before it.
        ScopedLock lock( mutex_ );

        auto messageToPrint = qFormatLogMessage( type, context, msg ).toUtf8();
        messageToPrint.append( '\n' );

        writeToConsole( messageToPrint, shouldFlush( type ) );
    }

    void enableLogging( bool isEnabled, uint8_t logLevel )
    {
        ScopedLock lock( mutex_ );

        qSetMessagePattern( "%{time} %{type} [%{threadid}] [%{function}@%{line}] %{message}" );

        isConsoleLogEnabled_ = isEnabled;
        logLevel_ = logLevel;

        setMessageHandler();
    }

    void enableFileLogging( bool isEnabled, uint8_t logLevel )
    {
        ScopedLock lock( mutex_ );

        qSetMessagePattern( "%{time} %{type} [%{threadid}] [%{function}@%{line}] %{message}" );

        isFileLogEnabled_ = isEnabled;
        logLevel_ = logLevel;

        if ( isEnabled && !logFile_ ) {
            auto logFileName
                = QString( "logsquirl_%1_%2.log" )
                      .arg( QDateTime::currentDateTime().toString( "yyyy-MM-dd_HH-mm-ss" ) )
                      .arg( QCoreApplication::applicationPid() );

            logFile_ = std::make_unique<QFile>( QDir::temp().filePath( logFileName ) );
            if ( !logFile_->open( QIODevice::WriteOnly | QIODevice::Append ) ) {
                logFile_.reset();
            }
        }
        else if ( !isEnabled && logFile_ ) {
            logFile_.reset();
        }

        setMessageHandler();
    }

    bool isAnyEnabled() const
    {
        return isConsoleLogEnabled_ || isFileLogEnabled_;
    }

private:
    Logger() = default;

    // Writing is buffered: flushing every message (std::endl) cost a
    // syscall per log line. Errors and fatal messages are still flushed
    // at once so they survive a crash that follows them; everything else
    // is flushed at least once a second and at shutdown (QFile and
    // std::cout flush when they are destroyed).
    bool shouldFlush( QtMsgType type )
    {
        const auto now = std::chrono::steady_clock::now();
        if ( type == QtFatalMsg || type == QtCriticalMsg || now - lastFlush_ >= FlushInterval ) {
            lastFlush_ = now;
            return true;
        }
        return false;
    }

    static void writeToConsole( const QByteArray& message, bool flush )
    {
        std::cout.write( message.constData(), message.size() );
        if ( flush ) {
            std::cout.flush();
        }
    }

    void setMessageHandler()
    {
        detail::maxLogLevel = static_cast<uint8_t>( logLevel_ );
        detail::isAnyLogEnabled = isAnyEnabled();

        if ( !isAnyEnabled() ) {
            qInstallMessageHandler( logging::logsquirlNoopMessageHandler );
        }
        else if ( logFile_ ) {
            qInstallMessageHandler( logging::logsquirlFileMessageHandler );
        }
        else {
            qInstallMessageHandler( logging::logsquirlConsoleMessageHandler );
        }
    }

private:
    static constexpr auto FlushInterval = std::chrono::seconds( 1 );

    std::mutex mutex_;
    using ScopedLock = std::scoped_lock<std::mutex>;

    std::chrono::steady_clock::time_point lastFlush_;

    std::atomic_bool isConsoleLogEnabled_ = false;
    std::atomic_bool isFileLogEnabled_ = false;

    std::atomic_int logLevel_ = 0;

    std::unique_ptr<QFile> logFile_;
};

void enableLogging( bool isEnabled, LogLevel logLevel )
{
    Logger::instance().enableLogging( isEnabled, static_cast<uint8_t>( logLevel ) );
}

void enableFileLogging( bool isEnabled, LogLevel logLevel )
{
    Logger::instance().enableFileLogging( isEnabled, static_cast<uint8_t>( logLevel ) );
}

void logsquirlFileMessageHandler( QtMsgType type, const QMessageLogContext& context,
                                  const QString& msg )
{
    Logger::instance().fileMessageHandler( type, context, msg );
}

void logsquirlConsoleMessageHandler( QtMsgType type, const QMessageLogContext& context,
                                     const QString& msg )
{
    Logger::instance().consoleMessageHandler( type, context, msg );
}

} // namespace logging