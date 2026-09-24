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

#pragma once

#include <functional>
#include <memory>

#include <QTimer>
#include <QWidget>

#include "valuecount.h"

class AbstractLogData;
class QAction;
class QLabel;
class QProgressBar;
class QTableView;
class ValueCountModel;

// The Chart Panel's tab that shows a Value Count as a table (a model and a view) of value, count
// and share, the most frequent value first. It counts on a worker thread,
// shows how far it got, and can be stopped; a stopped count shows nothing. It
// is a snapshot: "Count again" takes a new one.
class ValueCountTab : public QWidget {
    Q_OBJECT

public:
    // Makes what reads the value of a Log Line. It is called for every count,
    // so that no count shares it with a cancelled one still finishing.
    using ValueOfLineFactory = std::function<ValueOfLine()>;

    // description says what is counted, and is shown above the table. The
    // count starts at once.
    ValueCountTab( std::shared_ptr<const AbstractLogData> logData, QString description,
                   ValueOfLineFactory valueOfLine, QWidget* parent = nullptr );
    ~ValueCountTab() override;

    // The count runs, or shows what it counted, from a new snapshot.
    void countAgain();
    // Stops the count without waiting for it.
    void stop();

    bool isCounting() const;
    const QString& description() const;

Q_SIGNALS:
    // The user clicked a value.
    void valueClicked( const QString& value );

private:
    void onFinished( const ValueCountResult& result );
    void showProgress();
    void showStatus( const QString& text );

    std::shared_ptr<const AbstractLogData> logData_;
    QString description_;
    ValueOfLineFactory valueOfLine_;

    ValueCounter counter_;
    QTimer progressTimer_;

    QAction* stopAction_;
    QAction* countAgainAction_;
    QLabel* statusLabel_;
    QProgressBar* progressBar_;
    ValueCountModel* model_;
    QTableView* table_;
};
