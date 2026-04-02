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

#include "pluginmanager.h"
#include "pluginrepository.h"

#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <map>
#include <optional>
#include <vector>

namespace logsquirl::plugins {

/**
 * Unified plugin management dialog.
 *
 * Combines installed plugin management and repository browsing into a
 * single card-based grid.  Each plugin is represented as a card showing
 * its icon, name, version, author, description, status badge, and
 * action buttons.
 */
class PluginDialog : public QDialog {
    Q_OBJECT

  public:
    explicit PluginDialog( PluginManager& manager, QWidget* parent = nullptr );

  private Q_SLOTS:
    void onCatalogReady();
    void onIconReady( const QString& pluginId, const QPixmap& icon );
    void onFetchError( const QString& message );
    void onFilterChanged( const QString& text );
    void onTabChanged( int index );
    void onDownloadFinished( const QString& archivePath );
    void onDownloadError( const QString& message );

  private:
    /** Distinct state for each plugin card. */
    enum class PluginState {
        NotInstalled, ///< Available in repo but not on disk
        Installed,    ///< On disk and enabled
        Disabled,     ///< On disk but not enabled
        UpdateReady   ///< Installed but newer version available
    };

    /** Merged data for a single plugin (local + remote). */
    struct MergedPlugin {
        QString id;
        QString name;
        QString author;
        QString description;
        QString license;
        QString installedVersion;
        QString latestVersion;
        PluginState state = PluginState::NotInstalled;
    };

    // ── Card widget ──────────────────────────────────────────────────

    /** A single plugin card shown in the grid. */
    class PluginCard : public QFrame {
      public:
        explicit PluginCard( const MergedPlugin& plugin, PluginDialog* parent );

        void setIcon( const QPixmap& icon );
        void updateState( const MergedPlugin& plugin );
        const QString& pluginId() const { return pluginId_; }

        QLabel* iconLabel = nullptr;
        QLabel* nameLabel = nullptr;
        QLabel* versionLabel = nullptr;
        QLabel* authorLabel = nullptr;
        QLabel* licenseLabel = nullptr;
        QLabel* descriptionLabel = nullptr;
        QLabel* statusBadge = nullptr;
        QPushButton* actionButton = nullptr;
        QPushButton* toggleButton = nullptr;

      private:
        QString pluginId_;
    };

    // ── Internal helpers ─────────────────────────────────────────────

    /** Rebuild merged plugin list from discovered + catalog data. */
    void rebuildMergedList();

    /** Rebuild the card grid from the merged list, applying filters. */
    void rebuildCards();

    /** Apply the current search/tab filter to existing cards. */
    void applyFilter();

    /** Install or update a plugin from the repository. */
    void installPlugin( const QString& pluginId );

    /** Toggle a plugin between enabled and disabled. */
    void togglePlugin( const QString& pluginId );

    /** Extract archive, discover, and load the plugin. */
    bool extractAndInstall( const QString& archivePath, const QString& pluginId );

    /** Update the tab badge counts. */
    void updateTabBadges();

    /** Return the style sheet for a status badge. */
    static QString badgeStyleSheet( PluginState state );

    /** Return the display text for a status badge. */
    static QString badgeText( PluginState state );

    // ── Members ──────────────────────────────────────────────────────

    PluginManager& manager_;
    PluginRepository repository_;

    QLineEdit* searchEdit_ = nullptr;
    QToolButton* tabAll_ = nullptr;
    QToolButton* tabInstalled_ = nullptr;
    QToolButton* tabUpdates_ = nullptr;
    QWidget* cardContainer_ = nullptr;
    QVBoxLayout* cardLayout_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QCheckBox* autoLoadCheck_ = nullptr;

    int activeTab_ = 0; // 0 = All, 1 = Installed, 2 = Updates

    std::vector<MergedPlugin> mergedPlugins_;
    std::map<QString, PluginCard*> cards_;
    QString currentInstallId_;
};

} // namespace logsquirl::plugins
