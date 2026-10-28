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

#include "plugincontextmenu.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <algorithm>
#include <memory>
#include <utility>

using logsquirl::plugins::PluginContextMenuContext;
using logsquirl::plugins::PluginContextMenuMaxLogLines;
using logsquirl::plugins::PluginLogView;

std::vector<PresentationMenu::PluginEntry>
pluginContextMenuEntries( QObject& view, const PluginContextMenuSource& source, PluginLogView kind,
                          const logsquirl::vector<LineNumber>& selectedLogLines,
                          std::optional<QString> selectedText )
{
    std::vector<PresentationMenu::PluginEntry> entries;
    if ( !source ) {
        return entries;
    }
    auto offer = source();
    if ( offer.actions.empty() ) {
        return entries;
    }

    // One for all entries: only the first Log Lines a plugin is handed are
    // copied, however many are selected, in the order of the Log File even
    // where the Table View's Rows are sorted otherwise.
    auto context = std::make_shared<PluginContextMenuContext>();
    context->logFile = std::move( offer.logFile );
    context->view = kind;
    std::vector<LineNumber> first( std::min( static_cast<std::size_t>( selectedLogLines.size() ),
                                             PluginContextMenuMaxLogLines ) );
    std::partial_sort_copy( selectedLogLines.begin(), selectedLogLines.end(), first.begin(),
                            first.end() );
    context->logLines.reserve( first.size() );
    for ( const auto line : first ) {
        context->logLines.push_back( line.get() );
    }
    context->selectedLogLineCount = selectedLogLines.size();
    context->selectedText = std::move( selectedText );

    for ( const auto& action : offer.actions ) {
        entries.push_back( PresentationMenu::PluginEntry{
            .menuPath = action.menuPath,
            .label = action.label,
            .choose
            = [ view = QPointer<QObject>( &view ), source, serial = action.serial, context ] {
                  if ( !view ) {
                      return;
                  }
                  // After the menu has closed and the event it was opened
                  // from has returned; dropped with the view, by Qt, when
                  // the view goes first -- an entry chosen before may
                  // close its tab.
                  QMetaObject::invokeMethod(
                      view.data(),
                      [ source, serial, context ] {
                          const auto now = source();
                          const auto item = std::ranges::find(
                              now.actions, serial,
                              &logsquirl::plugins::PluginContextMenuAction::serial );
                          if ( item != now.actions.end() && item->callback != nullptr ) {
                              item->callback( item->userData, *context );
                          }
                      },
                      Qt::QueuedConnection );
              } } );
    }
    return entries;
}
