# Plugin Widgets Following the Active Window

A plugin's widgets — its status widget, its sidebar tab, its footer widget — stay
in the window they were put in. They do not move to another window when the user
brings that one to the front. They move only when the window holding them closes,
and then to the most recently active window that is left.

Plugin *menu actions* are a different matter: those are cheap to duplicate, so
every window gets them. This record is about widgets.

## Why this is out of scope

A plugin contributes **one** widget. The Plugin Host is handed a single `QWidget`
per contribution and hands it on to the Plugin UI Port; there is no second
instance to give a second window.

```cpp
// A plugin contributes a widget once, not once per window:
port->addStatusWidget( pluginId, PluginWidgetHandle{ qwidgetPtr } );
```

Because a `QWidget` has exactly one parent, "following the active window" can only
mean reparenting that one widget on every window switch. That is worse than what
it fixes:

- A sidebar tab the user is working in would be pulled out from under them the
  moment they click another window — mid-scroll, mid-selection, mid-typing.
- Reparenting drops focus and, depending on the widget, its selection and its
  scroll position. A plugin cannot defend against this; it never asked to be
  moved.
- A floating sidebar has no answer at all. It is not inside either window, so
  "the active one" names nothing to move it to.

The shape that would actually work is one widget instance per window, and that is
a change to the plugin API: a plugin would have to be able to build its widget
more than once, and to keep whatever state the copies share. That is a different,
much larger piece of work than the one this issue asked for, and nothing so far
suggests it is worth it — plugin widgets are status readouts and side panels, not
things a user drives from whichever window happens to be in front.

So: a widget exists once, it lives where it was put, and it moves on when its
window closes.

## What should be written down instead

The Plugin UI Port entry in `CONTEXT.md` should say this, so nobody rediscovers it
as a bug. Suggested sentence, to be added to that entry:

> A plugin's menu actions appear in every window, but each of its widgets exists
> once and is shown in one window: the most recently active one when the plugin
> contributed it. A widget moves to another window only when the window showing it
> closes; it does not follow the user from window to window.

## Prior requests

- #341: "Plugin widgets follow the active window"
