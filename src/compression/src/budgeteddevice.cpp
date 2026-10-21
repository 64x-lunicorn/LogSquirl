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

#include "budgeteddevice.h"

#include <algorithm>
#include <vector>

#include <QString>

BudgetedDevice::BudgetedDevice( std::unique_ptr<QIODevice> inner, qint64 budgetBytes,
                                QObject* parent )
    : QIODevice( parent )
    , inner_( std::move( inner ) )
    , budgetBytes_( budgetBytes )
{
}

BudgetedDevice::~BudgetedDevice()
{
    if ( isOpen() ) {
        BudgetedDevice::close();
    }
}

bool BudgetedDevice::open( OpenMode mode )
{
    if ( mode & QIODevice::WriteOnly ) {
        setErrorString( QStringLiteral( "A budgeted device is read-only" ) );
        return false;
    }
    if ( !inner_->isOpen() && !inner_->open( QIODevice::ReadOnly ) ) {
        setErrorString( inner_->errorString() );
        return false;
    }
    exceeded_ = false;
    // Unbuffered, so that the position this device reports is the inner
    // device's and the budget is checked against the bytes actually taken.
    return QIODevice::open( QIODevice::ReadOnly | QIODevice::Unbuffered );
}

void BudgetedDevice::close()
{
    QIODevice::close();
    inner_->close();
}

bool BudgetedDevice::isSequential() const
{
    // Random-access even over a sequential inner device, whose seeks this
    // device emulates: a sequential device's pos() stays 0, and the tar
    // reader seeks to pos() plus an entry's size (#838).
    return false;
}

bool BudgetedDevice::atEnd() const
{
    return !isOpen() || inner_->atEnd();
}

qint64 BudgetedDevice::bytesAvailable() const
{
    return QIODevice::bytesAvailable() + inner_->bytesAvailable();
}

bool BudgetedDevice::seek( qint64 pos )
{
    if ( pos < 0 || !isOpen() ) {
        return false;
    }
    if ( pos > budgetBytes_ ) {
        exceeded_ = true;
        setErrorString( QStringLiteral( "The decompression budget is exhausted" ) );
        return false;
    }
    if ( !inner_->isSequential() ) {
        return inner_->seek( pos ) && QIODevice::seek( pos );
    }
    if ( pos < QIODevice::pos() ) {
        // Back to the start: reopened, the inner device decompresses from
        // its first byte again (#838).
        inner_->close();
        if ( !inner_->open( QIODevice::ReadOnly ) ) {
            setErrorString( inner_->errorString() );
            return false;
        }
        if ( !QIODevice::seek( 0 ) ) {
            return false;
        }
    }
    return skip( pos - QIODevice::pos() );
}

bool BudgetedDevice::skip( qint64 bytes )
{
    constexpr qint64 ChunkSize = 64 * 1024;
    std::vector<char> discarded( static_cast<std::size_t>( std::min( bytes, ChunkSize ) ) );
    while ( bytes > 0 ) {
        // Through read(), so the bytes count against the budget and pos().
        const auto read = this->read( discarded.data(), std::min( bytes, ChunkSize ) );
        if ( read <= 0 ) {
            return false;
        }
        bytes -= read;
    }
    return true;
}

bool BudgetedDevice::budgetExceeded() const
{
    return exceeded_;
}

qint64 BudgetedDevice::readData( char* data, qint64 maxSize )
{
    if ( maxSize <= 0 ) {
        return 0;
    }
    const auto remaining = budgetBytes_ - pos();
    if ( remaining <= 0 ) {
        // At the budget exactly: the end of the stream is not past it.
        char probe = 0;
        if ( inner_->read( &probe, 1 ) <= 0 ) {
            return 0;
        }
        exceeded_ = true;
        setErrorString( QStringLiteral( "The decompression budget is exhausted" ) );
        return -1;
    }
    const auto read = inner_->read( data, std::min( maxSize, remaining ) );
    if ( read < 0 ) {
        setErrorString( inner_->errorString() );
    }
    return read;
}

qint64 BudgetedDevice::writeData( const char* /*data*/, qint64 /*maxSize*/ )
{
    return -1;
}
