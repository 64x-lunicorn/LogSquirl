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

#include <QDate>
#include <QList>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace logsquirl::plugins {
class PluginCatalog;
class PluginHost;
} // namespace logsquirl::plugins

/// Dashboard widget shown in the main window when no log tabs are open.
///
/// Displays the application logo, version info, quick-action buttons, cards
/// for recent files, favorites and plugin status in one column, keyboard
/// shortcut hints, and a footer with links to the project at its bottom edge.
/// Once LogSquirl has been used for a while, a line above the footer asks for
/// a star on GitHub until the user follows its link or closes it (#934).
/// Emits signals when the user clicks an item so MainWindow can open files.
class WelcomeDashboard : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeDashboard( QWidget* parent = nullptr );

    /// Refresh the recent files, favorites, and plugin status lists.
    /// Call this every time the dashboard becomes visible.
    void refresh();

    /// Provide the Plugin Catalog and the Plugin Host so the dashboard can
    /// show which plugins are installed and loaded. Both must outlive the
    /// dashboard. Must be called before the first refresh().
    void setPlugins( const logsquirl::plugins::PluginCatalog* catalog,
                     const logsquirl::plugins::PluginHost* host );

    /// Where refresh() takes the day from to decide whether to ask for a
    /// star: the system's date unless a test hands in its own.
    void setToday( std::function<QDate()> today );

Q_SIGNALS:
    /// Emitted when the user clicks a recent or favorite file entry.
    void openFileRequested( const QString& filePath );

    /// Emitted when the user clicks the "Open File" button.
    void openFileDialogRequested();

    /// Emitted when the user clicks the "Load Session" button.
    void loadSessionRequested();

    /// Emitted when the user clicks "Report an Issue" in the footer.
    void reportIssueRequested();

    /// Emitted when the user clicks "Documentation" in the footer.
    void documentationRequested();

protected:
    void dragEnterEvent( QDragEnterEvent* event ) override;
    void dropEvent( QDropEvent* event ) override;
    void changeEvent( QEvent* event ) override;

private:
    /// Build the full widget layout (called once from constructor).
    void buildUi();

    /// Gives the section titles the application font, bold and a quarter
    /// larger.
    void updateTitleFonts();

    /// Write every text of the Dashboard in the current language (#935).
    void retranslate();

    /// Populate the recent-files section.
    void refreshRecentFiles();

    /// Populate the favorites section.
    void refreshFavorites();

    /// Populate the plugin-status section.
    void refreshPluginStatus();

    /// Write the footer's links in the Theme's secondary text color.
    void updateFooterLinks();

    /// Do what the footer's link to target stands for.
    void activateFooterLink( const QString& target );

    /// Build the line asking for a star, hidden, above the footer's links.
    void buildStarRequest();

    /// Write the star request's text and close icon in the Theme's colors.
    void updateStarRequest();

    /// Show the star request when it is due today, hide it otherwise.
    void refreshStarRequest();

    /// The user followed the star request's link or closed it: never again.
    void dismissStarRequest();

    QVBoxLayout* recentFilesLayout_ = nullptr;
    QVBoxLayout* favoritesLayout_ = nullptr;
    QVBoxLayout* pluginStatusLayout_ = nullptr;
    QLabel* logoLabel_ = nullptr;
    QList<QLabel*> titleLabels_;
    QPushButton* openFileButton_ = nullptr;
    QPushButton* loadSessionButton_ = nullptr;
    QLabel* recentFilesTitle_ = nullptr;
    QLabel* favoritesTitle_ = nullptr;
    QLabel* pluginsTitle_ = nullptr;
    QLabel* dropHint_ = nullptr;

    /// The footer at the bottom edge, below the scrolled content: one line
    /// per entry, the links line last. A line added above the links goes in
    /// at index 0.
    QVBoxLayout* footerLayout_ = nullptr;
    QLabel* footerLinks_ = nullptr;

    /// The line asking for a star, at index 0 of the footer while it is there.
    QWidget* starRequest_ = nullptr;
    QLabel* starRequestText_ = nullptr;
    QToolButton* starRequestClose_ = nullptr;
    std::function<QDate()> today_ = [] { return QDate::currentDate(); };

    const logsquirl::plugins::PluginCatalog* pluginCatalog_ = nullptr;
    const logsquirl::plugins::PluginHost* pluginHost_ = nullptr;
};

#endif // LOGSQUIRL_WELCOMEDASHBOARD_H
