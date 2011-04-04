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
from distutils.core import setup
from DistUtilsExtra.command import *

def get_presets():
    path = 'data/presets'
    dot_conf_files = filter(lambda x: x.endswith('.conf'), os.listdir(path))
    return map(
        lambda x: os.path.join(path,x),
        dot_conf_files
    )

def get_app_path_files():
    return [
        'data/nautilus-pastebin-notification',
        'data/nautilus-pastebin-configurator.glade'
    ]

pkg_data_files = [
    ('share/pixmaps/', ['data/nautilus-pastebin.png']),
    ('share/applications/', ['data/nautilus-pastebin-configurator.desktop']),
    ('share/gconf/schemas/', ['data/nautilus-pastebin.schemas']),
    ('share/nautilus-pastebin/', get_app_path_files()),
    ('share/nautilus-pastebin/presets/', get_presets())
]

pkg_scripts = [
    'scripts/nautilus-pastebin.py',
    'scripts/nautilus-pastebin-configurator.py'
]

pkg_short_dsc = "Nautilus extension to send files to a pastebin"

pkg_long_dsc = """nautilus-pastebin is a Nautilus extension written in Python, which allows users to upload text-only files to a pastebin service just by right-clicking on them. Users can also add their favorite service just by creatine new presets."""

setup(name='nautilus-pastebin',
    version='0.4.1',
    platforms=['all'],
    author='Alessio Treglia',
    author_email='quadrispro@ubuntu.com',
    license='GPL-2',
    url='https://launchpad.net/nautilus-pastebin',
    description=pkg_short_dsc,
    long_description=pkg_long_dsc,
    packages=['pastebin'],
    data_files=pkg_data_files,
    scripts=pkg_scripts,
    cmdclass = { "build" : build_extra.build_extra,
        "build_i18n" :  build_i18n.build_i18n,
        "build_icons" :  build_icons.build_icons,
        "clean": clean_i18n.clean_i18n,
        }
    )

