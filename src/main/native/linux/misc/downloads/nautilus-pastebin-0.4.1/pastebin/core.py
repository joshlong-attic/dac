#!/usr/bin/python
# -*- coding: utf-8 -*-
# nautilus-pastebin - Nautilus extension to paste a file to a pastebin service
# Written by:
#    Alessio Treglia <quadrispro@ubuntu.com>
# Copyright (C) 2009-2010, Alessio Treglia
#
# Part of this code has been taken from pastebinit script
# pastebinit was written by: 
#    Stéphane Graber <stgraber@stgraber.org>
#    Daniel Bartlett <dan@f-box.org>
#    David Paleino <d.paleino@gmail.com>
# URL: http://www.stgraber.org/download/projects/pastebin/
# Copyright (C) 2006-2009, Stéphane Graber, Daniel Bartlett, David Paleino
# Distributed under the terms of the GNU GPL-2 license
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
import re
import mimetypes
import urllib
import xmlrpclib

from ConfigParser import ConfigParser

# Custom urlopener to handle 401's
class PasteURLOpener(urllib.FancyURLopener):
    """Do the HTTP requests."""
    def http_error_401(self, url, fp, errcode, errmsg, headers, data=None):
            return None

class PastebinConfiguration(object):
    PARAMETERS = [
        "website",
        "wrapper",
        "content",
        "user",
        "jabberid",
        "version",
        "format",
        "parentid",
        "permatag",
        "title",
        "username",
        "password",
        "default_mime"
    ]

    def __set_default_parameters(self):
        for k in PastebinConfiguration.PARAMETERS:
            self.__dict__.setdefault(k,'')
            if k in self._optionsDict.keys():
                setattr(self, k, self._optionsDict[k])

    def __get_mapping_by_name(self, optname):
        map = {}
        if optname in self._optionsDict:
            exec(self._optionsDict[optname])
        return map

    def __init__(self, optionsDict):
        self._optionsDict = optionsDict
        self.__set_default_parameters()
        self.aliases = self.__get_mapping_by_name('aliases')
        self.extras = self.__get_mapping_by_name('extras')
        self.mimetypes = self.__get_mapping_by_name('mimetypes')
        self.mimetypes.setdefault('text/plain', self._optionsDict['default_mime'])

    def set_format_by_mimetype(self, mimetype):
        if mimetype in self.mimetypes.keys():
            self.format = self.mimetypes[mimetype]
        elif mimetype.split('/')[0] == 'text':
            self.format = self.default_mime # self.mimetypes['text/plain']
        else:
            return False
        return True
    
    def set_content(self, pastefile):
        fp = open(pastefile)
        content = fp.read()
        fp.close()
        if not content:
            return False
        # OK, let's go...
        self.content = content
        self.title = os.path.split(pastefile)[-1]
        return True

class PastebinWrapperFactory(object):
    """Singleton factory which returns a wrapper object dinamically."""
    __instance = None
    class __impl(object):
        """Implementation of the class' methods"""
        def get_wrapper(self, pasteconf):
            wrapper = pasteconf.wrapper
            obj = None
            try:
                exec("obj = %sWrapper(pasteconf)" % wrapper)
            except Exception, e:
                pass
            return obj

    def __getattr__(self, attr):
        return getattr(self.__instance, attr)
    def __setattr__(self, attr, value):
        return setattr(self.__instance, attr, value)

    def __init__(self):
        if PastebinWrapperFactory.__instance is None:
            PastebinWrapperFactory.__instance = PastebinWrapperFactory.__impl()
        self.__dict__['_PastebinWrapperFactory__instance'] = PastebinWrapperFactory.__instance

class GenericPastebinWrapper(object):
    NOT_EMPTY_FIELDS = ['website', 'content', 'format', 'title', 'default_mime']

    def validate(self):
        """Do the validation of the configuration file."""
        if self.conf.user == '':
            self.conf.user = os.environ.get('USER')
        return self._check_not_empty() and self._check_aliases() and self._check_extras()

    def hook_before_setup_parameters(self): pass
    def hook_after_setup_parameters(self): pass

    def setup_parameters(self):
        # Execute before-setup hook
        self.hook_before_setup_parameters()
        # Execute the setup
        self._setup_parameters()
        # Execute after-setup hook
        self.hook_after_setup_parameters()

    def paste(self):
        if not self.validate():
            return None

        self.setup_parameters()

        website = self.conf.website        
        params = self.params
        pasteurl = None

        if not re.search(".*/", website):
            website += "/"

        reLink=None
        tmp_page=""
        if params.__contains__("page"):
            website+=params['page']
            tmp_page=params['page']
            params.__delitem__("page")
        if params.__contains__("regexp"):
            reLink=params['regexp']
            params.__delitem__("regexp")
        # Convert to a format usable with the HTML POST
        params = urllib.urlencode(params)

        try:
            url_opener = PasteURLOpener()
            # Send the informations and be redirected to the final page
            page = url_opener.open(website, params)
            # Check whether we have to apply a regexp
            if reLink:
                if website.count(tmp_page) > 1:
                    website = website.rstrip(tmp_page)
                else:
                    website = website.replace(tmp_page, "")
                pasteurl = website + "/" + re.split(reLink, page.read())[1]
            else:
                pasteurl = page.url

        except Exception, value:
            pasteurl = None
        return pasteurl

    def _check_aliases(self):
        return all(
            map(
                lambda x: x in self.conf.__dict__, self.conf.aliases.keys())
                )

    def _check_not_empty(self):
        """Verify whether all the opts' keys are non-empty."""
        return all(map(lambda x: getattr(self.conf, x) != '', self.NOT_EMPTY_FIELDS))

    def _check_extras(self): return True

    def _setup_parameters(self):
        # Set the aliases
        for k in self.conf.aliases.keys():
            for v in self.conf.aliases[k]:
                self.params[v] = getattr(self.conf, k)
        # Set the extras
        for k in self.conf.extras.keys():
            self.params[k] = self.conf.extras[k]

    def __init__(self, pastebinConfiguration):
        """Class constructor."""
        try:
            toAdd = getattr(cls, 'not_empty_fields')
            self.NOT_EMPTY_FIELDS.extend(toAdd)
        except:
            pass
        self.conf = pastebinConfiguration
        self.params = {}


class PasteyNetWrapper(GenericPastebinWrapper):

    def _parentFixup(self, website, parentid):
        if parentid == "":
            return ""
        url_opener = PasteURLOpener()
        page = url_opener.open(website + '/' + parentid, None)
        matches = re.split('<input.*?name="parent".*?value="(.*?)"', page.read())
        if len(matches) <= 1 or re.match(parentid, matches[1]) == None:
            # The obfuscated version didn't begin with the partial version,
            # or unable to find the obfuscated version for some reason!
            # Create a paste with no parent (should we throw, instead?)
            return ""
        return matches[1]

    def hook_after_setup_parameters(self):
        self.params['parent'] = self._parentFixup(self.conf.website, self.conf.parentid)
        # return params

class PasteDebianNetXMLRPCWrapper(GenericPastebinWrapper):
    def paste(self):
        if not self.validate():
            return None
        server = xmlrpclib.Server(self.conf.website)
        self.setup_parameters()
        response = server.paste.addPaste(
            self.params['pastetext'],
            self.params['name'],
            self.params['expire'],
            self.params['lang']
        )
        if response['rc'] == 0:
            return "http://paste.debian.net/%s" % response['id']
        else:
            return None

class PasteLispOrgXMLRPCWrapper(GenericPastebinWrapper):
    def paste(self):
        if not self.validate():
            return None
        server = xmlrpclib.Server(self.conf.website)
        self.setup_parameters()
        response = server.newpaste(
            self.params['channel'],
            self.conf.user,
            self.conf.title,
            self.conf.content,
            self.conf.format
        )
        regex = re.compile(self.params['regexp'])
        found = regex.findall(response)
        if not found:
            return None
        return found[0] # the URL

class CodeBulixOrgWrapper(GenericPastebinWrapper):
    """Handler of http://code.bulix.org pastebin.
    
    This code was inspired by the paste.py script available at:
    http://code.bulix.org/paste.py
    """
    def hook_after_setup_parameters(self):
        page = urllib.urlopen(self.conf.website).read()
        antispam_hidden_fields_re = re.compile(
            '(<input type="hidden" name="__antispam_rand[0-9]" value="[0-9]" />)'
        )
        antispam_field_index_re = re.compile('name="(__antispam_rand[0-9])"')
        antispam_field_value_re = re.compile('value="([0-9])"')

        field_lst_raw = antispam_hidden_fields_re.findall(page)
        # Begin to populate the parameters map.
        for i in field_lst_raw:
            self.params[antispam_field_index_re.findall(i)[0]] = \
                antispam_field_value_re.findall(i)[0]
        self.params['__antispam_result'] = \
            int(self.params['__antispam_rand1']) + int(self.params['__antispam_rand2'])

