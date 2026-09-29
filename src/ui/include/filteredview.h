/*
 * Copyright (C) 2009, 2010, 2012 Nicolas Bonnefon and other contributors
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

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#ifndef FILTEREDVIEW_H
#define FILTEREDVIEW_H

#include "abstractlogview.h"

#include "csvexport.h"
#include "csvexportdialog.h"
#include "logfiltereddata.h"

#include <functional>
#include <memory>

#include <QDate>
#include <QKeyEvent>
#include <QString>

class LogFormatDefinition;

// Class implementing the filtered (bottom) view widget: a text view showing
// the Displayed Lines of a Search, one after another.
class FilteredView : public AbstractLogView {
    Q_OBJECT
public:
    FilteredView( LogFilteredData* newLogData, const QuickFindPattern* const quickFindPattern,
                  bool initialTextWrap, QWidget* parent = nullptr );

    // What is visible in the view.
    using Visibility = LogFilteredData::Visibility;
    void setVisibility( Visibility visi );
    Visibility visibility() const;

    // Also shows the Filtered View's line numbers as it says.
    void setPresentationPolicy( const PresentationPolicy& policy ) override;

    // What the CSV export needs to know of the Log File beyond its Log Lines:
    // the Log Format it recognized, none when it recognized none, the date it
    // was last written, and its path.
    struct RecognizedFormat {
        std::shared_ptr<const LogFormatDefinition> format;
        QDate modificationDate;
        QString logFilePath;
    };
    // Asked whenever the context menu opens and whenever an export starts, so
    // the view never holds on to a Log Format the Log File has given up.
    // Without it, the view offers no Export as CSV.
    void setRecognizedFormat( std::function<RecognizedFormat()> recognizedFormat );

    // What the dialog Export as CSV starts with: all shown lines or the
    // selected ones, Include Context Lines (unchecked, and only enabled while
    // Context Lines are shown), then the Line and Type columns (unchecked) and
    // every column of the Table View.
    CsvExportDialog::Setup csvExportSetup() const;
    // Exports the Displayed Lines, or the selected ones, to a CSV file as
    // chosen in that dialog, in the order the view shows them; Context Lines
    // only when chosen. Their Log Line numbers and types are copied here, on
    // the UI thread; the text is read off it.
    void exportCsvTo( const CsvExportDialog::Choices& choices );

protected:
    std::function<void()> exportAsCsvAction() override;

private:
    // The columns the CSV export offers: Line, Type with types, one per Log
    // Line exported, then every column of the Table View for format.
    std::vector<CsvColumn> csvColumns( const LogFormatDefinition* format,
                                       logsquirl::vector<AbstractLogData::LineType> types ) const;
    // Opens the dialog Export as CSV, and exports as chosen there.
    void exportAsCsv();

    LogFilteredData* logFilteredData_;
    std::function<RecognizedFormat()> recognizedFormat_;
};

#endif
