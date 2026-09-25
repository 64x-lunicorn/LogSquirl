/*
 * Copyright (C) 2021 Anton Filimonov
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

#ifndef LOGSQUIRL_ISSUE_REPORTER_H
#define LOGSQUIRL_ISSUE_REPORTER_H

#include <QCoreApplication>
#include <QString>
#include <QUrl>

enum class IssueTemplate { Crash, Exception, Bug };

class IssueReporter {
    Q_DECLARE_TR_FUNCTIONS( IssueReporter )

public:
    static void askUserAndReportIssue( IssueTemplate issueTemplate,
                                       const QString& information = {} );
    static void reportIssue( IssueTemplate issueTemplate, const QString& information = {} );

    // The GitHub address of a new issue with its body prefilled: the template,
    // the information and the build and system it comes from (#444).
    static QUrl issueUrl( IssueTemplate issueTemplate, const QString& information = {} );
};

#endif
