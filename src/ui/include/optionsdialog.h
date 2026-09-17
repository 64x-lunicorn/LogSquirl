/*
 * Copyright (C) 2009, 2010, 2013 Nicolas Bonnefon and other contributors
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

#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QColor>
#include <QDialog>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QStyledItemDelegate>

#include "configuration.h"

#include "ui_optionsdialog.h"

class LogFormatCatalog;

// Records a shortcut in place in a shortcut cell of the shortcuts table: a
// click or Enter on the cell starts recording, Escape or Tab cancels it, and
// Backspace or Delete clears the shortcut, on the cell or while recording.
// A cell keeps its shortcut as portable text under Qt::UserRole.
class ShortcutRecordingDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ShortcutRecordingDelegate( QAbstractItemView* view );

    QWidget* createEditor( QWidget* parent, const QStyleOptionViewItem& option,
                           const QModelIndex& index ) const override;
    void setEditorData( QWidget* editor, const QModelIndex& index ) const override;
    void setModelData( QWidget* editor, QAbstractItemModel* model,
                       const QModelIndex& index ) const override;

    static void setShortcut( QAbstractItemModel* model, const QModelIndex& index,
                             const QKeySequence& keySequence );

Q_SIGNALS:
    void edited();

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    bool viewKeyPressed( const QKeyEvent* keyEvent );
    bool recorderKeyPressed( QWidget* recorder, const QKeyEvent* keyEvent );

    enum class Recording { Keep, Clear, Cancel };
    void endRecording( QWidget* recorder, Recording outcome );

    QAbstractItemView* view_;
};

// Implements the main option dialog box
class OptionsDialog : public QDialog, public Ui::OptionsDialog {
    Q_OBJECT

public:
    // The Log Formats tab lists the application's Log Format Catalog.
    explicit OptionsDialog( const LogFormatCatalog& logFormatCatalog, QWidget* parent = nullptr );

Q_SIGNALS:
    // Is emitted when new settings must be used
    void optionsChanged();

private Q_SLOTS:
    // Clears and updates the font size box with the sizes allowed
    // by the passed font family.
    void updateFontSize( const QString& fontFamily );
    // Update the content of the global Config() using parameters
    // from the dialog box.
    void updateConfigFromDialog();
    // Called when a ok/cancel/apply button is clicked.
    void onButtonBoxClicked( QAbstractButton* button );

    void changeMainColor();
    void changeQfColor();

    void checkShortcutsOnDuplicate() const;

private:
    void setupTabs();
    void setupFontList();
    void setupRegexp();
    void setupPolling();
    void setupSearchResultsCache();
    void setupLogging();
    void setupArchives();
    void setupIndexCache();
    void setupStyles();
    void setupEncodings();
    void setupLanguageList();
    void setupLogFormats( const LogFormatCatalog& logFormatCatalog );

    int updateTranslate();

    void buildShortcutsTable( bool useDefaultsOnly );

    int getRegexpTypeIndex( SearchRegexpType syntax ) const;
    SearchRegexpType getRegexpTypeFromIndex( int index ) const;

    int getRegexpEngineIndex( RegexpEngine engine ) const;
    RegexpEngine getRegexpEngineFromIndex( int index ) const;

    void updateDialogFromConfig();

    QColor mainSearchColor_;
    QColor qfSearchColor_;
};

#endif
