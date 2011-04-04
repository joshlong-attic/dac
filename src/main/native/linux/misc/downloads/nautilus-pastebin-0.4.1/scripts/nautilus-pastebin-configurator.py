#!/usr/bin/python
# -*- coding: utf-8 -*-
# nautilus-pastebin - Nautilus extension to paste a file to a pastebin service
# Written by:
#    Alessio Treglia <quadrispro@ubuntu.com>
# Copyright (C) 2009-2010, Alessio Treglia
#
# This package is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This package is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this package; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
#

import os
import sys
import gettext
import gtk
import gconf
from ConfigParser import ConfigParser

# Globals
APP_NAME = "nautilus-pastebin"
MAINTAINER_MODE = False
APP_PATH = os.path.join(sys.prefix, "share", APP_NAME)

# Check whether running in maintainer mode
if os.path.expanduser('~') in os.path.realpath(__file__):
    sys.path.append(os.path.expanduser(os.path.join('~', '.nautilus-pastebin')))
    APP_PATH = os.path.expanduser(os.path.join('~', '.nautilus-pastebin'))
    MAINTAINER_MODE = True

# By default, the program looks for presets in the following paths:
#  /etc/nautilus-pastebin/presets/
#  ~/.config/nautilus-pastebin/presets
PRESETS_PATHS = [
    os.path.join(sys.prefix, 'share', 'nautilus-pastebin', 'presets'),
    os.path.expanduser(os.path.join('~','.config','nautilus-pastebin', 'presets'))
]

gettext.install(APP_NAME)

def get_presets():
    presets = []

    if MAINTAINER_MODE:
        PRESETS_PATHS.append(os.path.join(APP_PATH, 'data', 'presets'))

    for path in PRESETS_PATHS:
        if os.path.exists(path):
            append_these = filter(
                lambda x: x.endswith('.conf'), os.listdir(path)
            )
            presets.extend(
                map(
                    lambda x: os.path.join(path,x),
                    append_these
                )
            )
    return presets

def getSearchResPaths():
    return [
        os.path.join(sys.prefix, "share", "nautilus-pastebin"),
        os.path.join(sys.prefix, "share", "pixmaps"),
        os.path.join(sys.prefix, "local", "share", "nautilus-pastebin"),
        os.path.expanduser(os.path.join("~", ".nautilus-pastebin", "data")),
        os.getcwd()
    ]

def getPath(path):
    search_paths = getSearchResPaths()
    for search in search_paths:
        fullpath = os.path.join(search, path)
        if os.path.exists(fullpath):
            return fullpath
    else:
        raise IOError(_("Can't find %s in any known prefix!") % kwargs[component])

GLADE_FILE = getPath('nautilus-pastebin-configurator.glade')
ICON_FILE = getPath('nautilus-pastebin.png')
USERCONFIG_DIR = os.path.expanduser(os.path.join('~', '.config', 'nautilus-pastebin'))

class Settings(object):
    GCONF_PATH = "/apps/nautilus-pastebin/"
    KEYMAP = {
        'preset' : 'string',
        'ask_confirmation' : 'bool',
        'show_notification' : 'bool',
        'open_browser' : 'bool'
    }

    def update_configuration(self):
        for kname, ktype in self.KEYMAP.items():
            kvalue = getattr(self, kname)
            getattr(self.__gconf_client, 'set_%s' % ktype)(self.GCONF_PATH + kname, kvalue)

    def __retrieve_settings(self):
        for kname, ktype in self.KEYMAP.items():
            setattr(self, kname, getattr(self.__gconf_client, 'get_%s' % ktype)(self.GCONF_PATH + kname))

    def __load_presets(self):
        cp = ConfigParser()
        cp.read(get_presets())
        self.presets = cp.sections()

    def __init__(self):
        self.__gconf_client = gconf.client_get_default()
        self.__retrieve_settings()
        self.__load_presets()

class Controller(object):

    def showErrorMessage(self, message):
        md = gtk.MessageDialog(parent=self.dialogMain,
            message_format=message,
            buttons=gtk.BUTTONS_CLOSE,
            type = gtk.MESSAGE_ERROR,
            flags = gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT)
        md.run()
        md.destroy()

    def quit(self, *args):
        gtk.main_quit()

    def on_button_close_clicked(self, widget, data=None):
        self.dialogMain.destroy()
        self._retrieve_config_from_ui()
        self.settings.update_configuration()
        self.quit()

    def _retrieve_config_from_ui(self):
        params = self.settings.KEYMAP.keys()
        for i in params:
            if i == 'preset':
                setattr(self.settings, i, self.builder.get_object("combobox_pastebin").get_active_text())
            else:
                setattr(self.settings, i, self.builder.get_object(i).get_active())

    def _setup_widgets(self):
        builder = self.builder
        presets = self.settings.presets
        # Set the icon
        self.dialogMain.set_icon_from_file(ICON_FILE)
        # Populate the combobox widget
        comboboxPastebin = builder.get_object("combobox_pastebin")
        comboboxPastebin.set_model(gtk.ListStore(str))
        cell = gtk.CellRendererText()
        comboboxPastebin.pack_start(cell, True)
        comboboxPastebin.add_attribute(cell, 'text', 0)

        [comboboxPastebin.append_text(k) for k in presets]
        comboboxPastebin.set_active(presets.index(self.settings.preset))
        
        # Set the boolean flags
        for k in [option for option in self.settings.KEYMAP.keys() if option is not 'preset']:
            builder.get_object(k).set_active(
                getattr(self.settings, k)
            )

    def __init__(self):
        builder = gtk.Builder()
        builder.add_from_file(GLADE_FILE)
        self.settings = Settings()
        self.dialogMain = builder.get_object('dialog_main')
        self.tableOptions = builder.get_object('table_options')
        builder.connect_signals(self)
        self.dialogMain.connect('delete_event', self.quit)
        
        self.builder = builder
        
        self._setup_widgets()
        self.dialogMain.show_all()
       
def main():
    c = Controller()
    gtk.main()
    
if __name__ == "__main__":
    main()

