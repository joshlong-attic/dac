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
import time
import gettext
import urllib
import webbrowser
from threading import Thread
from ConfigParser import ConfigParser
import gtk
import gconf
import nautilus
import pynotify

# Globals
APP_NAME = "nautilus-pastebin"
LOGGING = 1
MAINTAINER_MODE = False
APP_PATH = os.path.join(sys.prefix, "share", APP_NAME)

# Check whether running in maintainer mode
if os.path.expanduser('~') in os.path.realpath(__file__):
    sys.path.append(os.path.expanduser(os.path.join('~', '.nautilus-pastebin')))
    APP_PATH = os.path.expanduser(os.path.join('~', '.nautilus-pastebin'))
    MAINTAINER_MODE = True
from pastebin.core import *

gettext.install(APP_NAME)

class log:
    def log(self, message):
        if self.logger:
            self.logger.debug(message)
    def __init__(self):
        logger = None
        if LOGGING:
            import logging
            logger = logging.getLogger(APP_NAME)
            logger.setLevel(logging.DEBUG)
            ch = logging.StreamHandler()
            ch.setLevel(logging.DEBUG)
            logger.addHandler(ch)
        self.logger = logger

log = log().log

# By default, the program looks for presets in the following paths:
#  /etc/nautilus-pastebin/presets/
#  ~/.config/nautilus-pastebin/presets
PRESETS_PATHS = [
    os.path.join(sys.prefix, 'share', 'nautilus-pastebin', 'presets'),
    os.path.expanduser(os.path.join('~','.config','nautilus-pastebin', 'presets'))
]

class ConfigFileException(Exception):
    pass

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

pynotify.init("Nautilus pastebin extension")

class Settings(object):
    """Configuration wrapper"""
    GCONF_PATH = "/apps/%s/" % APP_NAME
    KEYMAP = {
        'preset' : 'string',
        'ask_confirmation' : 'bool',
        'show_notification' : 'bool',
        'open_browser' : 'bool'
    }

    def validate(self):
        return self.__preset_is_available(self.__settings['preset'])
    
    def get_pastebin_configuration(self):
        """Retrieve and returns the configuration related to the pastebin service."""
        preset = self.__settings['preset']
        return PastebinConfiguration(dict(self.__presets.items(preset)))

    def get_available_pastebins(self):
        """Returns a list of available presets."""
        return self.__presets.sections()

    def get_settings(self):
        """Returns application's global settings.'"""
        return self.__settings

    def __preset_is_available(self, preset):
        return self.__presets.has_section(preset)

    def __load_presets(self, presets):
        self.__presets.read(presets)

    def __retrieve_settings(self):
        self.__settings = {}
        for kname, ktype in self.KEYMAP.items():
            self.__settings[kname] = getattr(self.__gconf_client, 'get_%s' % ktype)(self.GCONF_PATH + kname)

    def __init__(self, presets):
        self.__gconf_client = gconf.client_get_default()
        self.__presets = ConfigParser()
        self.__load_presets(presets)
        self.__retrieve_settings()

class PastebinThread(Thread):
    def __init__(self, settings, pasteconf):
        self.settings = settings
        self.pasteconf = pasteconf
        self.wrapper = PastebinWrapperFactory().get_wrapper(pasteconf)
        Thread.__init__(self)

    def run(self):
        log ("PastebinThread started!")

        # Ask user to confirm the action
        if self.settings['ask_confirmation']:
            md = gtk.MessageDialog(None,
                message_format=_("The file will be sent to the selected pastebin: \
are you sure to continue?"),
                flags=gtk.DIALOG_MODAL,
                buttons=gtk.BUTTONS_YES_NO,
                type=gtk.MESSAGE_QUESTION
            )
            response = md.run()
            md.destroy()
            if response == gtk.RESPONSE_NO:
                return
        pasteurl = self.wrapper.paste()
        
        summary = ''
        message = ''
        icon = None
        helper = gtk.Button()
        
        if not pasteurl:
            summary = _("Unable to read or parse the result page.")
            message = _("It could be a server timeout or a change server side. Try later.")
            icon = helper.render_icon(gtk.STOCK_DIALOG_ERROR, gtk.ICON_SIZE_DIALOG)
        else:
            summary = 'File pasted to: '
            URLmarkup = '<a href="%(pasteurl)s">%(pasteurl)s</a>'
            message = URLmarkup % {'pasteurl':pasteurl}
            icon = helper.render_icon(gtk.STOCK_PASTE, gtk.ICON_SIZE_DIALOG)
            
            cb = gtk.clipboard_get('CLIPBOARD')
            cb.clear()
            cb.set_text(pasteurl)
            
            # Open a browser window
            if self.settings['open_browser']:
                webbrowser.open(pasteurl)
            
        # Show a bubble
        if self.settings['show_notification']:
            n = pynotify.Notification(summary, message)
            n.set_icon_from_pixbuf(icon)
            n.show()

class PastebinitExtension(nautilus.MenuProvider):
    def __init__(self):
        log ("Intializing nautilus-pastebin extension...")
    
    def get_file_items(self, window, files):
        if len(files)!=1:
            return
        filename = files[0]

        if filename.get_uri_scheme() != 'file' or filename.is_directory():
            return

        items = []
        #Called when the user selects a file in Nautilus.
        item = nautilus.MenuItem("NautilusPython::pastebin_item",
                                 _("Pastebin"),
                                 _("Send this file to a pastebin"))
        item.set_property('icon', "nautilus-pastebin")
        item.connect("activate", self.menu_activate_cb, files)
        items.append(item)
        return items

    def menu_activate_cb(self, menu, files):
        if len(files) != 1:
            return
        filename = files[0]

        settings = Settings(get_presets())
        settings.validate()
        pasteconf = settings.get_pastebin_configuration()

        if not pasteconf.set_format_by_mimetype(filename.get_mime_type()):
            n= pynotify.Notification(_("Unable to send the file"), _("The selected file cannot be pasted."))
            helper = gtk.Button()
            icon = helper.render_icon(gtk.STOCK_DIALOG_ERROR, gtk.ICON_SIZE_DIALOG)
            n.set_icon_from_pixbuf(icon)
            n.show()
            return

        pastefile = urllib.unquote(filename.get_uri()[7:])
        if not pasteconf.set_content(pastefile):
            return
        
        thread = PastebinThread(settings.get_settings(), pasteconf)
        thread.start()
        while (thread.isAlive()):
            time.sleep(0.09)
            while gtk.events_pending():
                gtk.main_iteration()

