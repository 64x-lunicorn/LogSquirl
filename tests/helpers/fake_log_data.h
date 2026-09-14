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

#ifndef FAKE_LOG_DATA_H
#define FAKE_LOG_DATA_H

#include <algorithm>
#include <utility>

#include <QStringList>

#include "abstractlogdata.h"

// A Log File held in memory, for tests that need an AbstractLogData but no
// file on disk, no indexing and no worker thread. Log Lines are served
// exactly as given: tabs are not expanded, and there is no Encoding.
class FakeLogData : public AbstractLogData {
public:
    FakeLogData() = default;

    explicit FakeLogData( QStringList lines )
        : lines_( std::move( lines ) )
    {
    }

    void setLines( const QStringList& lines )
    {
        lines_ = lines;
    }

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        return line.get() < static_cast<uint64_t>( lines_.size() )
                   ? lines_[ static_cast<qsizetype>( line.get() ) ]
                   : QString{};
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return doGetLineString( line );
    }
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        logsquirl::vector<QString> result;
        for ( uint64_t i = 0;
              i < count.get() && first.get() + i < static_cast<uint64_t>( lines_.size() ); ++i ) {
            result.push_back( doGetLineString( LineNumber( first.get() + i ) ) );
        }
        return result;
    }
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first,
                                                   LinesCount count ) const override
    {
        return doGetLines( first, count );
    }
    LineNumber doGetLineNumber( LineNumber index ) const override
    {
        return index;
    }
    LinesCount doGetNbLine() const override
    {
        return LinesCount( static_cast<LinesCount::UnderlyingType>( lines_.size() ) );
    }
    LineLength doGetMaxLength() const override
    {
        qsizetype maxLength = 0;
        for ( const auto& line : lines_ ) {
            maxLength = std::max( maxLength, line.size() );
        }
        return LineLength( static_cast<LineLength::UnderlyingType>( maxLength ) );
    }
    LineLength doGetLineLength( LineNumber line ) const override
    {
        return LineLength(
            static_cast<LineLength::UnderlyingType>( doGetLineString( line ).size() ) );
    }
    void doSetDisplayEncoding( const char* ) override {}
    QTextCodec* doGetDisplayEncoding() const override
    {
        return nullptr;
    }
    void doAttachReader() const override {}
    void doDetachReader() const override {}

private:
    QStringList lines_;
};

#endif // FAKE_LOG_DATA_H
