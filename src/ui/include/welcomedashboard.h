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

#ifndef LOGSQUIRL_WELCOMEDASHBOARD_H
#define LOGSQUIRL_WELCOMEDASHBOARD_H

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace logsquirl::plugins {
class PluginManager;
}

/// Dashboard widget shown in the main window when no log tabs are open.
///
/// Displays the application logo, version info, recent/favorite files,
/// quick-action buttons, loaded plugin status, and keyboard shortcut hints.
/// Emits signals when the user clicks an item so MainWindow can open files.
class WelcomeDashboard : public QWidget {
    Q_OBJECT

  public:
    explicit WelcomeDashboard( QWidget* parent = nullptr );

    /// Refresh the recent files, favorites, and plugin status lists.
    /// Call this every time the dashboard becomes visible.
    void refresh();

    /// Provide a pointer to the PluginManager so the dashboard can
    /// display plugin status. Must be called before the first refresh().
    void setPluginManager( logsquirl::plugins::PluginManager* pm );

  Q_SIGNALS:
    /// Emitted when the user clicks a recent or favorite file entry.
    void openFileRequested( const QString& filePath );

    /// Emitted when the user clicks the "Open File" button.
    void openFileDialogRequested();

    /// Emitted when the user clicks the "Load Session" button.
    void loadSessionRequested();

  protected:
    void dragEnterEvent( QDragEnterEvent* event ) override;
    void dropEvent( QDropEvent* event ) override;

  private:
    /// Build the full widget layout (called once from constructor).
    void buildUi();

    /// Populate the recent-files section.
    void refreshRecentFiles();

    /// Populate the favorites section.
    void refreshFavorites();

    /// Populate the plugin-status section.
    void refreshPluginStatus();

    QVBoxLayout* recentFilesLayout_ = nullptr;
    QVBoxLayout* favoritesLayout_ = nullptr;
    QVBoxLayout* pluginStatusLayout_ = nullptr;
    QLabel* logoLabel_ = nullptr;

    logsquirl::plugins::PluginManager* pluginManager_ = nullptr;
};

#endif // LOGSQUIRL_WELCOMEDASHBOARD_H
