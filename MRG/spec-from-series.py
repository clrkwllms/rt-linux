#!/usr/bin/python -tt

import sys
import os
import os.path
import filecmp

startdef = "# START OF PATCH DEFINITIONS"
enddef   = "# END OF PATCH DEFINITIONS"
startapply = "# START OF PATCH APPLICATIONS"
endapply   = "# END OF PATCH APPLICATIONS"

specfile = 'kernel-rt.spec'

# get the series file
try:
    series = sys.argv[1]
except:
    print "usage: spec-from-series <path-to-series>"
    sys.exit(-1)

patches = []
defines = []
applies = []

try:
    f = open(series)
except:
    print "Can't open series file %s" % series
    sys.exit(-1)
    
count = 0
while True:
    line = f.readline()
    # eof check
    if len(line) == 0: 
        break
    l = line.strip()
    # discard comments and empty lines
    if len(l) == 0 or l[0] == '#': continue
    count += 1
    patches.append(l)
    defines.append("Patch%d: %s" % (count, l))
    applies.append("ApplyPatch %s" % l)
f.close()
    
# now use the current specfile for a template
try:
    f = open(specfile)
except:
    print "can't open %s to use as template" % specfile
    sys.exit(-1)

done = False
skipping = False
indef = False
new = []
oldpatches = []

while True:
    line = f.readline()
    if line == "":
        break
    line = line.rstrip()
    if line.startswith(startdef):
        new.append(line)
        for d in defines:
            new.append(d)
        indef = True
        skipping = True
    elif line.startswith(enddef):
        indef = False
        skipping = False
    elif line.startswith(startapply):
        new.append(line)
        for a in applies:
            new.append(a)
        skipping = True
    elif line.startswith(endapply):
        skipping = False

    if indef:
        if line.startswith('#'): continue
        (notused, patch) = line.strip().split(':')
        oldpatches.append(patch.strip())
        
    if skipping:
        continue

    new.append(line)

f.close()

rmpatches = []
addpatches = []
copypatches = []

# find the patches we need to add to cvs
for p in patches:
    if p not in oldpatches:
        addpatches.append(p)
    else:
        copypatches.append(p)

# and find the ones no longer needed
for p in oldpatches:
    if p not in patches:
        rmpatches.append(p)

# write the cvs cleanup script
f = open("cvs-cleanup.sh", "w")
f.write("#!/bin/sh\n")
for p in rmpatches:
    f.write("rm %s\n" % p)
    f.write("cvs rm %s\n" % p)
seriespath = os.path.dirname(series)
for p in addpatches:
    f.write("cp %s .\n" % os.path.join(seriespath, p))
    f.write("cvs add %s\n" % p)
for p in copypatches:
    s = os.path.join(seriespath, p)
    if not filecmp.cmp(p, s):
        print "%s has changed" % s
        f.write("cp %s .\n" % s)
f.close()

# write the new specfile
f = open(specfile + ".new", "w")
for line in new:
    f.write(line+"\n")
f.close()

