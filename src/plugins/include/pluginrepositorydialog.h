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

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace logsquirl::plugins {

/**
 * Dialog for browsing and installing plugins from the remote repository.
 *
 * Shows a table of available plugins with name, version, description,
 * and an install/update button.  Search/filter is provided via a text
 * field at the top.
 */
class PluginRepositoryDialog : public QDialog {
    Q_OBJECT

  public:
    explicit PluginRepositoryDialog( PluginManager& manager, QWidget* parent = nullptr );

  private Q_SLOTS:
    void onIndexReady();
    void onFetchError( const QString& message );
    void onFilterChanged( const QString& text );
    void onInstallClicked();
    void onDownloadFinished( const QString& archivePath );
    void onDownloadError( const QString& message );

  private:
    void populateTable();

    PluginManager& manager_;
    PluginRepository repository_;

    QLineEdit* filterEdit_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
};

} // namespace logsquirl::plugins
