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

#include "backgroundrun.h"

#include <deque>

#include <QObject>
#include <QPointer>

#include "log.h"

// Delivers what the mailbox holds on the thread it was built on. Woken by a
// queued call QtCore makes on its own, by name and without arguments: what is
// delivered travels under the mailbox's mutex, which ThreadSanitizer sees,
// not in a queued functor or argument, which it cannot (ADR 0007).
class BackgroundRunRelay : public QObject {
    Q_OBJECT

public:
    explicit BackgroundRunRelay( std::shared_ptr<BackgroundRunMailbox::State> state )
        : state_( std::move( state ) )
    {
    }

    // Deletes this relay, now or, when it is delivering, once it is done.
    void dispose();

public Q_SLOTS:
    void deliver();

private:
    std::shared_ptr<BackgroundRunMailbox::State> state_;
    // How many deliveries are on the stack (a delivery may run an event loop
    // of its own). Only on the thread it delivers on.
    int delivering_ = 0;
};

struct BackgroundRunMailbox::State {
    std::mutex mutex;
    std::deque<std::function<void()>> pending;
    // Null once closed.
    BackgroundRunRelay* relay = nullptr;
};

void BackgroundRunRelay::deliver()
{
    // One at a time: a delivery may close the mailbox, or destroy whoever
    // owns it and this relay with it.
    const QPointer<BackgroundRunRelay> alive( this );
    const auto state = state_;
    ++delivering_;
    while ( alive ) {
        std::function<void()> delivery;
        {
            std::lock_guard lock( state->mutex );
            if ( state->pending.empty() ) {
                --delivering_;
                return;
            }
            delivery = std::move( state->pending.front() );
            state->pending.pop_front();
        }
        delivery();
    }
}

void BackgroundRunRelay::dispose()
{
    // Deleting an object while it handles an event can crash.
    if ( delivering_ > 0 ) {
        deleteLater();
    }
    else {
        delete this;
    }
}

BackgroundRunMailbox::BackgroundRunMailbox()
    : state_( std::make_shared<State>() )
{
    state_->relay = new BackgroundRunRelay( state_ );
}

BackgroundRunMailbox::~BackgroundRunMailbox()
{
    close();
}

void BackgroundRunMailbox::post( std::function<void()> delivery )
{
    std::lock_guard lock( state_->mutex );
    if ( state_->relay == nullptr ) {
        return;
    }
    const bool wasEmpty = state_->pending.empty();
    state_->pending.push_back( std::move( delivery ) );
    // Whatever is already pending has a delivery on its way, which takes
    // this one too.
    if ( wasEmpty ) {
        QMetaObject::invokeMethod( state_->relay, "deliver", Qt::QueuedConnection );
    }
}

void BackgroundRunMailbox::close()
{
    BackgroundRunRelay* relay = nullptr;
    std::deque<std::function<void()>> dropped;
    {
        std::lock_guard lock( state_->mutex );
        relay = std::exchange( state_->relay, nullptr );
        dropped.swap( state_->pending );
    }
    // Deleted on the thread it delivers on; a delivery still queued for it
    // goes with it. Outside the lock: what is dropped may hold anything.
    if ( relay != nullptr ) {
        relay->dispose();
    }
}

namespace background_run_detail {

void reportShutdownTimeout( const QString& name )
{
    LOG_ERROR << name << " did not finish within 10 s -- giving up";
}

QString describeFailure( const QString& name, const std::exception& failure )
{
    const auto description = QString( "%1 failed: %2" ).arg( name, failure.what() );
    LOG_ERROR << description;
    return description;
}

QString describeUnknownFailure( const QString& name )
{
    const auto description = QString( "%1 failed" ).arg( name );
    LOG_ERROR << description;
    return description;
}

} // namespace background_run_detail

#include "backgroundrun.moc"
