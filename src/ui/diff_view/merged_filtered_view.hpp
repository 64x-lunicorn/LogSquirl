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

// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "abstractlogdata.h"
#include "abstractlogview.h"
#include "linetypes.h"

class LogFilteredData;
class AnchorSet;
class QuickFindPattern;

/// Which source file a merged row belongs to.
enum class MergedSource { FileA, FileB };

// ---------------------------------------------------------------------------
// MergedLogData — AbstractLogData backed by two LogFilteredData sources.
//
// Merges search results from both sources and sorts them using anchor-based
// interpolation so that matching lines appear interleaved in a synchronised
// order.
// ---------------------------------------------------------------------------
class MergedLogData : public AbstractLogData {
    Q_OBJECT

  public:
    explicit MergedLogData( QObject* parent = nullptr );

    /// Set the two data sources and the anchor set used for ordering.
    void setSources( LogFilteredData* dataA, LogFilteredData* dataB,
                     const AnchorSet* anchors );

    /// Rebuild the merged entry list from the current search results.
    void rebuild();

    /// Number of merged entries.
    std::size_t entryCount() const;

    /// Return the source file for the given merged index.
    MergedSource sourceAt( LineNumber index ) const;

    /// Return the original (0-based) line number in the source file.
    LineNumber originalLineAt( LineNumber index ) const;

    /// Line type: Match for all entries, Mark bit set for file-B lines.
    LineType lineTypeAt( LineNumber index ) const;

    /// Maximum original line number across both sources.
    LineNumber maxOriginalLine() const;

  protected:
    // --- AbstractLogData pure-virtual implementations ---
    QString doGetLineString( LineNumber line ) const override;
    QString doGetExpandedLineString( LineNumber line ) const override;
    logsquirl::vector<QString> doGetLines( LineNumber first_line,
                                           LinesCount number ) const override;
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first_line,
                                                   LinesCount number ) const override;
    LineNumber doGetLineNumber( LineNumber index ) const override;
    LinesCount doGetNbLine() const override;
    LineLength doGetMaxLength() const override;
    LineLength doGetLineLength( LineNumber line ) const override;
    void doSetDisplayEncoding( const char* encoding ) override;
    QTextCodec* doGetDisplayEncoding() const override;
    void doAttachReader() const override;
    void doDetachReader() const override;

  private:
    /// One merged row.
    struct Entry {
        MergedSource source;
        LineNumber filteredIndex; // index into source LogFilteredData
        LineNumber originalLine;  // line number in the original file (0-based)
        int64_t sortKey;          // anchor-mapped position for ordering
    };

    LogFilteredData* dataA_ = nullptr;
    LogFilteredData* dataB_ = nullptr;
    const AnchorSet* anchors_ = nullptr;
    std::vector<Entry> entries_;
    LineNumber maxOriginalLine_;
};

// ---------------------------------------------------------------------------
// MergedFilteredView — AbstractLogView that displays MergedLogData.
//
// Works exactly like a FilteredView but draws from two sources.  The line-
// number column shows "A:<n>" / "B:<n>" to identify the source file.
// ---------------------------------------------------------------------------
class MergedFilteredView : public AbstractLogView {
    Q_OBJECT

  public:
    MergedFilteredView( MergedLogData* data,
                        const QuickFindPattern* const quickFindPattern,
                        QWidget* parent = nullptr );

  Q_SIGNALS:
    /// Emitted when the user double-clicks / presses Enter on a row.
    void lineActivated( MergedSource source, LineNumber line );

  protected:
    // --- AbstractLogView overrides ---
    AbstractLogData::LineType lineType( LineNumber lineNumber ) const override;
    LineNumber displayLineNumber( LineNumber lineNumber ) const override;
    LineNumber maxDisplayLineNumber() const override;

  private:
    MergedLogData* mergedData_;
};
