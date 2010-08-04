#!/usr/bin/python
# script to sanity check config file values

import sys, os
import re

set_regex = re.compile(r'^(?P<key>CONFIG.+)=(?P<value>.+)$')
unset_regex = re.compile(r'^# (?P<key>CONFIG.+) is not set$')

class Config(object):

    def __init__(self, file):
        self.file = file
        f = open(file)
        self.lines = f.readlines()
        f.close()
        self.configs = {}

    def check_values(self):
        i = 0
        conflicts = 0
        while i < len(self.lines):
            l = self.lines[i].strip()
            m = set_regex.match(l)
            if m:
                key = m.group('key').strip()
                val = m.group('value').strip()
            else:
                m = unset_regex.match(l)
                if m:
                    key = m.group('key').strip()
                    val = None
                else:
                    i += 1
                    continue
            if self.configs.has_key(key):
                print "conflicting definition for %s at line %d" % (key, i+1)
                print "    previous definition at line %d" % (self.configs[key][0])
                print "    line %d:  %s = %s" % (self.configs[key][0], key, self.configs[key][1])
                print "    line %d:  %s = %s" % (i+1, key, val)
                conflicts += 1
            else:
                self.configs[key] = (i, val)
            i += 1
        return conflicts

if __name__ == "__main__":
    total = 0
    for f in sys.argv[1:]:
        c = Config(f)
        conflicts = c.check_values()
        if conflicts:
            print "%d conflicts found" % conflicts
            total += conflicts
    sys.exit(total)
