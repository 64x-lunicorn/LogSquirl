/*
 * Copyright (C) 2022 Anton Filimonov and other contributors
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

#ifndef LOGSQUIRL_RUNNABLE_LAMBDA_H
#define LOGSQUIRL_RUNNABLE_LAMBDA_H

#include <QRunnable>

template <typename TRunnable>
class RunnableWrapper : public QRunnable {
  public:
    RunnableWrapper( TRunnable&& runnable )
        : runnable_( std::move( runnable ) )
    {
        setAutoDelete( true );
    }

    void run() override
    {
        runnable_();
    }

  private:
    TRunnable runnable_;
};

template <typename TRunnable>
RunnableWrapper<TRunnable>* createRunnable( TRunnable&& runnable )
{
    return new RunnableWrapper<TRunnable>( std::move( runnable ) );
}

#endif