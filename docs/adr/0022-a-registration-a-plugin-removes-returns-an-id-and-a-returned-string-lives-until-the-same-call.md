# A registration a plugin removes returns an id, and a string the host returns lives until the same call

The plugin API grew one function at a time, and each settled its own rules. Every widget registration had an `unregister_*` taking the widget pointer back; `register_menu_action` returned `void` and had no counterpart, so a plugin's menu items went only when it was unloaded. `register_encoding_callback` said a second registration replaces the first and `NULL` removes it; `register_active_file_callback` did the same without saying so. `get_selected_log_lines` and `get_log_file_encoding` kept their strings until the same plugin called the same function again; `get_active_file_path` said "until the next host API call", and in fact shared one buffer between all plugins, so one plugin's call overwrote the string another plugin held. Context menu items (#859) and further registrations were about to add more of each (#857).

## Decision

- **A registration that has no handle of the plugin's own to remove it by returns an id.** `register_menu_action_ex` returns an `int` greater than 0, never handed out twice while the plugin is loaded, or a negative value when nothing was registered; `unregister_menu_action( handle, id )` removes it, and does nothing for an id it does not know or no longer knows. The ids come from one counter per loaded plugin, shared by every registration that returns one, so an id never names two things of a plugin. A registration of a widget keeps being removed by the widget pointer, which already names it.
- **A removal holds once it returns.** The host marks the registration removed before it asks the Plugin UI Port to take it away, and its trampoline drops a trigger that comes later, also one queued before the removal. The port takes the item away on the window's thread and deletes it from the event loop, since the removal may come from the item's own callback.
- **Every `register_*` function says** whether a second call replaces the first registration or adds another, and how a registration is removed. A plugin has one callback of each kind (`register_active_file_callback`, `register_encoding_callback`): a second registration replaces the first, and `NULL` removes it. Whatever a plugin still has registered goes when it is unloaded.
- **A string the host returns is valid until the same plugin calls the same function again, or is shut down**, whatever other plugins call meanwhile: the host keeps one buffer per plugin and function. A string the host passes to a callback is valid during the call only; a string a plugin passes is copied before the call returns. The header says this once for the table and again, in the same words, at each function.
- `register_menu_action` stays as it is: its items still go only when the plugin is unloaded, and no id removes them.

## Considered Options

- **Removing a menu item by its label, or by its callback and user data.** Neither needs a new return value, but labels are what the user reads and may repeat across submenus, and a plugin may register one callback for several items. An id names exactly one registration.
- **A handle struct the host allocates per registration.** An opaque pointer works as well as an int, but a plugin cannot tell a stale one from a live one, and freeing it is one more rule. An int that is never reused while the plugin is loaded is safe to pass back at any time.
- **Changing `register_menu_action` to return the id.** Its type is part of the frozen table: a plugin built against the older header calls it as `void`, and a host returning an `int` through it is not portable.
- **Keeping the shared buffer of `get_active_file_path` and narrowing its documentation to "until any plugin's next call".** Shorter than the other functions' lifetimes, and a plugin cannot know when another plugin calls.

## Consequences

- A later registration that returns an id (#859 context menu items, and those after it) takes its id from the same per-plugin counter, keeps its record in the plugin's context until the context goes, and drops a queued trigger the same way.
- A removed menu action's record stays in the plugin's context until the plugin is unloaded, so that a trigger still queued finds it marked; a plugin that registers and removes items in a loop grows that list by a few dozen bytes each time.
- A plugin's items in one submenu have different labels: `register_menu_action_ex` fails for a menu path and label the plugin already has an item of, as the Plugin UI Port shows no second item of a label in a submenu. Items of the same label in different submenus are items of their own (#870); paths that name the same submenu count as the same.
