#!/usr/bin/env python3
# A small window to drag files from (like a file manager), for the drop tests.
import sys
import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gdk, Gio, Gtk

uris = [Gio.File.new_for_path(path).get_uri() for path in sys.argv[1:]]

window = Gtk.Window(title="Drag Source")
button = Gtk.Button(label="drag me")
button.set_size_request(240, 200)
button.drag_source_set(Gdk.ModifierType.BUTTON1_MASK, [], Gdk.DragAction.COPY | Gdk.DragAction.MOVE)
button.drag_source_add_uri_targets()
button.connect("drag-data-get", lambda widget, context, data, info, time: data.set_uris(uris))
window.add(button)
window.connect("destroy", Gtk.main_quit)
window.show_all()
Gtk.main()
