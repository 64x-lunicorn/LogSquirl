/*
 * Copyright (C) 2013 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QFNOTIFICATIONS_H
#define QFNOTIFICATIONS_H

#include <QCoreApplication>
#include <QFontMetrics>
#include <QObject>
#include <QWidget>

// Notifications sent by the QF for displaying to the user
// and their translation in UI text.
class QFNotification {
    Q_DECLARE_TR_FUNCTIONS( QFNotification )

public:
    explicit QFNotification( const QString& message = "" )
        : message_{ message }
    {
    }

    QString message() const
    {
        return message_;
    }

    // Max width of the message (in pixels)
    static int maxWidth( const QWidget* widget )
    {
        QFontMetrics fm = widget->fontMetrics();
        return qMax( fm.size( Qt::TextSingleLine, reachedBeginningOfFile() ).width(),
                     fm.size( Qt::TextSingleLine, reachedEndOfFile() ).width() );
    }

protected:
    // The notices, translated like the Search Line's (#846).
    static QString reachedEndOfFile();
    static QString reachedBeginningOfFile();
    static QString interrupted();

    // The message, and the Log Lines searched that the regex engine gave up
    // on when there were any: they may match, as the Search says of its
    // own (#689, #826).
    static QString withUndecided( const QString& message, int undecidedLines );

private:
    QString message_;
};

class QFNotificationReachedEndOfFile : public QFNotification {
public:
    explicit QFNotificationReachedEndOfFile( int undecidedLines = 0 )
        : QFNotification( withUndecided( reachedEndOfFile(), undecidedLines ) )
    {
    }
};

class QFNotificationReachedBegininningOfFile : public QFNotification {
public:
    explicit QFNotificationReachedBegininningOfFile( int undecidedLines = 0 )
        : QFNotification( withUndecided( reachedBeginningOfFile(), undecidedLines ) )
    {
    }
};

class QFNotificationInterrupted : public QFNotification {
public:
    QFNotificationInterrupted()
        : QFNotification( interrupted() )
    {
    }
};

class QFNotificationProgress : public QFNotification {
public:
    // Constructor taking the progress (in percent)
    explicit QFNotificationProgress( int progress_percent = 0 )
        : QFNotification( QObject::tr( "Searching (position %1 %)" ).arg( progress_percent ) )
    {
    }
};

#endif
