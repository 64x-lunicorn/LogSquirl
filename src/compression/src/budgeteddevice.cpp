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
    consumed_ = 0;
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
    return inner_->isSequential();
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
    if ( pos > budgetBytes_ ) {
        exceeded_ = true;
        setErrorString( QStringLiteral( "The decompression budget is exhausted" ) );
        return false;
    }
    return inner_->seek( pos ) && QIODevice::seek( pos );
}

bool BudgetedDevice::budgetExceeded() const
{
    return exceeded_;
}

qint64 BudgetedDevice::position() const
{
    return isSequential() ? consumed_ : pos();
}

qint64 BudgetedDevice::readData( char* data, qint64 maxSize )
{
    if ( maxSize <= 0 ) {
        return 0;
    }
    const auto remaining = budgetBytes_ - position();
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
    if ( read > 0 ) {
        consumed_ += read;
    }
    else if ( read < 0 ) {
        setErrorString( inner_->errorString() );
    }
    return read;
}

qint64 BudgetedDevice::writeData( const char* /*data*/, qint64 /*maxSize*/ )
{
    return -1;
}
