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
#include <vector>

#include <QAction>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QString>

/// A single entry in the command palette.
struct CommandEntry {
    /// Display name shown in the palette (e.g. "Open File").
    QString name;

    /// Category shown as a prefix badge (e.g. "File", "View", "Plugin: DLT").
    QString category;

    /// Optional keyboard shortcut description (e.g. "Ctrl+O").
    QString shortcut;

    /// Callback invoked when the entry is selected.
    std::function<void()> action;

    /// Best fuzzy-match score (lower is better).  Updated during filtering.
    int score = 0;
};

/// VS Code-style command palette for LogSquirl.
///
/// Collects commands from the menu bar, plugin-contributed actions,
/// recent files and favorites into a single fuzzy-searchable list.
/// Activated via Ctrl+Shift+P (Cmd+Shift+P on macOS).
class CommandPalette : public QDialog {
    Q_OBJECT

public:
    explicit CommandPalette( QWidget* parent = nullptr );

    /// Replace the current command list.
    void setCommands( std::vector<CommandEntry> commands );

protected:
    bool eventFilter( QObject* obj, QEvent* event ) override;

private:
    /// Re-filter the list widget based on the current input text.
    void updateFilter();

    /// Execute the currently highlighted entry and close the palette.
    void acceptCurrent();

    /// Compute a fuzzy match score for `pattern` against `text`.
    /// Returns -1 if no match; otherwise a non-negative score
    /// (lower = better).  Consecutive matches and matches at word
    /// boundaries are scored favourably.
    static int fuzzyScore( const QString& pattern, const QString& text );

    QLineEdit* input_ = nullptr;
    QListWidget* list_ = nullptr;

    std::vector<CommandEntry> commands_;
};
