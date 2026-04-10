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

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QUuid>
#include <QVector>

#include "linetypes.h"

// A single extracted data point for a chart series.
struct ChartPoint {
    LineNumber line;      // Line number in the log file (for click-to-navigate).
    double xValue;        // X-axis coordinate (line number or extracted value).
    double value;         // Y-axis value (extracted numeric or 1.0 for count mode).
    QString xLabel;       // Optional display label for X-axis (raw timestamp text).
};

// Defines how to extract numeric values from log lines using a regex.
// The regex must contain at least one capture group; captureGroup selects
// which group provides the numeric value.
struct ChartSeriesDefinition {
    QString id;
    QString name;
    QColor color;
    QString pattern;
    int captureGroup = 1;
    bool visible = true;

    // X-axis extraction (optional; empty xPattern = use line number).
    QString xPattern;
    int xCaptureGroup = 1;
    // QDateTime format for parsing timestamps (empty = treat as numeric).
    QString xTimestampFormat;
    // Bucket size in milliseconds for time aggregation (0 = no bucketing).
    // When > 0, data points are grouped into time buckets and Y values summed.
    qint64 bucketSizeMs = 0;

    // Pre-compiled regex — rebuilt when pattern changes.
    QRegularExpression compiledRegex;
    QRegularExpression compiledXRegex;

    // Extracted data points.
    QVector<ChartPoint> points;

    // Compile the regex pattern. Returns true on success.
    bool compilePattern()
    {
        compiledRegex = QRegularExpression( pattern );
        if ( !xPattern.isEmpty() ) {
            compiledXRegex = QRegularExpression( xPattern );
        }
        return compiledRegex.isValid();
    }

    // Whether a custom X-axis regex is configured.
    bool hasCustomXAxis() const { return !xPattern.isEmpty(); }

    // Whether the X-axis uses timestamp parsing.
    bool isTimestampXAxis() const
    {
        return !xPattern.isEmpty() && !xTimestampFormat.isEmpty();
    }

    // Whether time-based aggregation (bucketing) is enabled.
    bool isBucketed() const { return bucketSizeMs > 0 && isTimestampXAxis(); }

    // Serialize to JSON for persistence.
    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj[ "id" ] = id;
        obj[ "name" ] = name;
        obj[ "color" ] = color.name();
        obj[ "pattern" ] = pattern;
        obj[ "captureGroup" ] = captureGroup;
        obj[ "visible" ] = visible;
        if ( !xPattern.isEmpty() ) {
            obj[ "xPattern" ] = xPattern;
            obj[ "xCaptureGroup" ] = xCaptureGroup;
            if ( !xTimestampFormat.isEmpty() ) {
                obj[ "xTimestampFormat" ] = xTimestampFormat;
            }
            if ( bucketSizeMs > 0 ) {
                obj[ "bucketSizeMs" ] = bucketSizeMs;
            }
        }
        return obj;
    }

    // Deserialize from JSON.
    static ChartSeriesDefinition fromJson( const QJsonObject& obj )
    {
        ChartSeriesDefinition def;
        def.id = obj[ "id" ].toString( QUuid::createUuid().toString() );
        def.name = obj[ "name" ].toString();
        def.color = QColor( obj[ "color" ].toString( "#2196F3" ) );
        def.pattern = obj[ "pattern" ].toString();
        def.captureGroup = obj[ "captureGroup" ].toInt( 1 );
        def.visible = obj[ "visible" ].toBool( true );
        def.xPattern = obj[ "xPattern" ].toString();
        def.xCaptureGroup = obj[ "xCaptureGroup" ].toInt( 1 );
        def.xTimestampFormat = obj[ "xTimestampFormat" ].toString();
        def.bucketSizeMs = static_cast<qint64>( obj[ "bucketSizeMs" ].toDouble( 0 ) );
        def.compilePattern();
        return def;
    }
};
