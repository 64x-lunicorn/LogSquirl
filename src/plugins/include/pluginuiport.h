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

#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QObject;

namespace logsquirl::plugins {

/// C-style callback function pointer used in the plugin host API.
using PluginCallbackFn = void ( * )( void* );

/**
 * A widget a plugin hands to the host.
 *
 * It wraps the `void*` the host callback table carries, so the plugin ABI
 * stays as it is. The plugin layer only passes it on; the UI layer that
 * implements the PluginUiPort is the only place that turns it back into a
 * widget.
 */
struct PluginWidgetHandle {
    void* widget = nullptr;

    bool operator==( const PluginWidgetHandle& ) const = default;
};

/**
 * The names of the submenus of the Plugins menu a menu path names, outermost
 * first. Spaces around a name and empty segments do not count, and a first
 * segment "Plugins" names the Plugins menu itself: "", "Plugins" and "/" all
 * name the Plugins menu, "Tools" and "Plugins/ Tools /" the same submenu.
 */
inline QStringList pluginSubmenuNames( const QString& menuPath )
{
    QStringList names;
    for ( const auto& segment : menuPath.split( u'/' ) ) {
        const auto name = segment.trimmed();
        if ( !name.isEmpty() ) {
            names.append( name );
        }
    }
    if ( !names.isEmpty() && names.front() == QStringLiteral( "Plugins" ) ) {
        names.removeFirst();
    }
    return names;
}

/** A pattern a plugin has the user test in the Regex Lab, and how it reads it. */
struct PluginPattern {
    QString pattern;
    bool matchesCase = true;

    bool operator==( const PluginPattern& ) const = default;
};

/**
 * Called once with the answer of a Regex Lab a plugin opened: the pattern the
 * user applied, or none when the user cancelled.
 */
using PluginRegexLabAnswer = std::function<void( std::optional<PluginPattern> applied )>;

/**
 * The text of Log Lines read for a plugin, as far as read: those selected in
 * the tab in front (#663), or a range of an open Log File (#858).
 */
struct PluginSelectedLogLines {
    /// In the order of the Log File.
    QStringList lines;
    /// The last of lines was cut short for the byte budget.
    bool lastCut = false;
    /// Log Lines asked for after lines were left unread for the byte budget.
    bool more = false;

    bool operator==( const PluginSelectedLogLines& ) const = default;
};

/** The view of Log Lines a context menu was opened in (#859). */
enum class PluginLogView {
    Text,    ///< The Text View.
    Table,   ///< The Table View.
    Filtered ///< A Filtered View.
};

/**
 * At most this many of the selected Log Lines a context menu hands a plugin's
 * item, the first ones (#859).
 */
inline constexpr std::size_t PluginContextMenuMaxLogLines = 1000;

/** What was selected as a context menu with plugin items opened (#859). */
struct PluginContextMenuContext {
    /// The Log File of the view, by the path its tab was added under.
    QString logFile{};
    PluginLogView view = PluginLogView::Text;
    /// The selected Log Lines, counted from 0 as the Log File's lines are,
    /// in its order: at most PluginContextMenuMaxLogLines, the first ones.
    std::vector<std::uint64_t> logLines{};
    /// How many Log Lines were selected: more than logLines holds when it
    /// holds only the first ones.
    std::uint64_t selectedLogLineCount = 0;
    /// The text selected within one Log Line or one cell; none when whole Log
    /// Lines are selected.
    std::optional<QString> selectedText{};

    bool operator==( const PluginContextMenuContext& ) const = default;
};

/// Called, on the UI thread, when the user chose an item of the context menu.
using PluginContextMenuCallbackFn
    = void ( * )( void* userData, const PluginContextMenuContext& context );

/** An item a plugin added to the context menu of the Log Lines (#859). */
struct PluginContextMenuAction {
    QString pluginId{};
    /// Slash-separated path of the submenus of the plugin section it goes into.
    QString menuPath{};
    QString label{};
    PluginContextMenuCallbackFn callback = nullptr;
    void* userData = nullptr;
    /// Given by the port, never twice: what names the item while a choice of
    /// it waits to be delivered.
    std::uint64_t serial = 0;
};

/** How going to a Log Line a plugin asked for ended (#663). */
enum class PluginLogLineJump {
    Shown,     ///< Selected and scrolled into view.
    NoLogFile, ///< The tab in front shows no Log File, or there is no window.
    OutOfRange ///< The Log File has no such line.
};

/**
 * The Plugin UI Port: everything the plugin layer needs from the user
 * interface to show what plugins contribute.
 *
 * The plugin layer calls it from the host callbacks, on whichever thread the
 * plugin called the host; an implementation that touches widgets moves that
 * work to their thread. Every call names the plugin it is for, so an
 * implementation can take all of a plugin's contributions away again.
 */
class PluginUiPort {
public:
    virtual ~PluginUiPort() = default;

    /** Show a plugin's widget in the status area. */
    virtual void addStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /** Take a status widget of a plugin away again; the plugin keeps owning it. */
    virtual void removeStatusWidget( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /** Show a plugin's widget as a sidebar tab titled label. */
    virtual void addSidebarTab( const QString& pluginId, const QString& label,
                                PluginWidgetHandle widget ) = 0;

    /** Take a sidebar tab of a plugin away again; the plugin keeps owning its widget. */
    virtual void removeSidebarTab( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /** Show a plugin's widget in the footer area. */
    virtual void addFooterWidget( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /** Take a footer widget of a plugin away again; the plugin keeps owning it. */
    virtual void removeFooterWidget( const QString& pluginId, PluginWidgetHandle widget ) = 0;

    /**
     * Add a menu action for a plugin.
     * @param menuPath  Slash-separated path of the submenus of the Plugins menu
     *                  the action goes into; empty or "Plugins" for the Plugins
     *                  menu itself.
     * @param label     Text of the action.
     * @param callback  Called with userData when the user triggers the action.
     */
    virtual void addMenuAction( const QString& pluginId, const QString& menuPath,
                                const QString& label, PluginCallbackFn callback, void* userData )
        = 0;

    /**
     * Take one menu action of a plugin away again (#857): the one added with
     * userData, which the Plugin Host makes unique per action. A submenu
     * created for it goes too once nothing is left in it. Nothing for an
     * action that is not there. It may come from the action's own callback,
     * so the action must not be deleted under its own trigger.
     */
    virtual void removeMenuAction( const QString& pluginId, void* userData ) = 0;

    /**
     * Add an item of a plugin to the context menu of the Log Lines (#859),
     * after those added before; userData, which the Plugin Host makes unique
     * per item, names it. A port that shows no Log Lines adds none.
     * @param menuPath  Slash-separated path of the submenus of the plugin
     *                  section the item goes into; empty for the section itself.
     * @param callback  Called with userData and what was selected when the
     *                  user chose the item.
     */
    virtual void addContextMenuAction( const QString& /* pluginId */, const QString& /* menuPath */,
                                       const QString& /* label */,
                                       PluginContextMenuCallbackFn /* callback */,
                                       void* /* userData */ )
    {
    }

    /** Take the context menu item added with userData away again; nothing when it is not there. */
    virtual void removeContextMenuAction( const QString& /* pluginId */, void* /* userData */ ) {}

    /**
     * The items of the context menu of the Log Lines, in the order they were
     * added, from any thread: what the views of Log Lines show as their menu
     * opens.
     */
    virtual std::vector<PluginContextMenuAction> contextMenuActions() const
    {
        return {};
    }

    /**
     * Take away everything a plugin still contributes: its menu actions, its
     * context menu items and any widget it did not remove itself. Called after the plugin was shut
     * down and before its library is released.
     */
    virtual void removeContributions( const QString& pluginId ) = 0;

    /** The widget a plugin's configuration dialog is opened on. */
    virtual PluginWidgetHandle configurationParent() = 0;

    /**
     * Open the Regex Lab for a plugin's pattern (#662), on the UI thread
     * only. The Lab reads the pattern as a regular expression and lets the
     * user change it and whether it matches case.
     *
     * answer is called once, on the UI thread, when the user applies or
     * cancels, or the Lab goes along with its window -- unless context is
     * destroyed first: then never. removeContributions() closes the plugin's
     * Labs.
     *
     * @return Whether a Lab opened; answer is never called when not. A port
     *         that shows no Regex Lab opens none.
     */
    virtual bool openRegexLab( const QString& /* pluginId */, const PluginPattern& /* pattern */,
                               QObject* /* context */, PluginRegexLabAnswer /* answer */ )
    {
        return false;
    }

    /**
     * Select a Log Line of the tab in front and scroll it into view, as Go to
     * line does (#663), on the UI thread only.
     * @param logLine  Counted from 0, as the Log File's lines are.
     * A port that shows no Log File shows none.
     */
    virtual PluginLogLineJump goToLogLine( std::uint64_t /* logLine */ )
    {
        return PluginLogLineJump::NoLogFile;
    }

    /**
     * The text of the Log Lines selected in the tab in front, in the order
     * of the Log File (#663), on the UI thread only: those of the view the
     * user was last in, a selection within a Log Line counting as the whole
     * Log Line. At most maxLines of them, each read from at most maxBytes of
     * its bytes, and none read after one that was cut or after their UTF-8
     * text passed maxBytes: a huge selection costs no more than that. None without an open Log
     * File; no lines without a selection.
     */
    virtual std::optional<PluginSelectedLogLines> selectedLogLines( std::size_t /* maxLines */,
                                                                    std::size_t /* maxBytes */ )
    {
        return std::nullopt;
    }

    /**
     * The name of the Encoding a Log File is read in (#803), on the UI thread
     * only: that of the tab in front for an empty logFile, else that of the
     * tab showing logFile. None when there is no such tab; a port that shows
     * no Log File shows none.
     */
    virtual std::optional<QString> logFileEncoding( const QString& /* logFile */ )
    {
        return std::nullopt;
    }

    /**
     * Mark Log Lines of a Log File as the user marks them (#866), on the UI
     * thread only: in every tab showing logFile, each tab skipping a Log Line
     * past its end or marked already.
     * @param logLines  Counted from 0, as the Log File's lines are.
     * A port that shows no Log File marks none.
     */
    virtual void addMarks( const QString& /* logFile */,
                           const std::vector<std::uint64_t>& /* logLines */ )
    {
    }

    /**
     * Remove Marks of a Log File (#866), on the UI thread only: in every tab
     * showing logFile, each tab skipping a Log Line it has not marked.
     * @param logLines  Counted from 0, as the Log File's lines are.
     */
    virtual void removeMarks( const QString& /* logFile */,
                              const std::vector<std::uint64_t>& /* logLines */ )
    {
    }

    /**
     * The Marks of a Log File (#866), on the UI thread only: counted from 0,
     * in the order of the Log File, each once, a Log Line marked in any tab
     * showing logFile. None when no tab shows it; a port that shows no Log
     * File shows none.
     */
    virtual std::optional<std::vector<std::uint64_t>> marks( const QString& /* logFile */ )
    {
        return std::nullopt;
    }
};

} // namespace logsquirl::plugins
