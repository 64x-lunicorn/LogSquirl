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

#ifndef SOURCE_BODY_FORMAT_H
#define SOURCE_BODY_FORMAT_H

// A Log File recognized as a test's own Log Format, so that its tab has a
// Table View (#944): the Session's Log Format Catalog holds a Log Format that
// reads every Log Line as a source word and a body, and Format Recognition is
// on. A test opens its Log File in a Session made with these, and waits until
// the tab's State names the Log Format.

#include <memory>
#include <utility>

#include <QHash>
#include <QString>

#include "logformatcatalog.h"
#include "logformatdefinition.h"
#include "settingspolicies.h"
#include "test_policies.h"

// A Log Format Catalog that holds the one Log Format name: a source word,
// blanks, and the body.
inline std::shared_ptr<LogFormatCatalog> sourceBodyFormatCatalog( const QString& name )
{
    LogFormatDefinition format;
    format.setName( name );
    format.setTitle( name );
    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<source>\w+)\s+(?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setBodyField( "body" );

    auto catalog = std::make_shared<LogFormatCatalog>();
    catalog->addFormat( std::move( format ) );
    return catalog;
}

// The test Policies, with Format Recognition on.
inline SettingsPolicies recognizingPolicies()
{
    auto policies = testSettingsPolicies();
    policies.recognition.enabled = true;
    return policies;
}

#endif
