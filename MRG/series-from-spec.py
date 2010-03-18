#!/usr/bin/python -tt

import sys
import os
import os.path
import shutil

startdef = "# START OF PATCH DEFINITIONS"
enddef   = "# END OF PATCH DEFINITIONS"
startapply = "# START OF PATCH APPLICATIONS"
endapply   = "# END OF PATCH APPLICATIONS"


# check for optional specfile argument
try:
    specfile = sys.argv[1]
except:
    specfile = 'kernel-rt.spec'

prefix = os.path.dirname(specfile)

# read the specfile and extract the patches

try:
    f = open(specfile)
except:
    print "can't open %s to use as template" % specfile
    sys.exit(-1)

done = False
indefinitions = False
inapplications = False
patchnums = []
patchdef = {}
patchapply = []
patches = []

while True:
    line = f.readline()
    
    # eof check
    if line == "":
        break

    line = line.rstrip()

    # skip blank lines
    if line == "":
        continue

    # find our markers and change state appropriately
    if line.startswith(startdef):
        indefinitions = True
        continue
    elif line.startswith(enddef):
        indefinitions = False
        continue
    elif line.startswith(startapply):
        inapplications = True
        continue
    elif line.startswith(endapply):
        inapplications = False
        continue

    # skip comments
    if line[0] == "#": continue

    if indefinitions:
        try:
            (patchnum, patch) = line.strip().split(':')
        except:
            print "exception processing definition %s" % line
            sys.exit(-1)
        if patchnum.strip().startswith('#'): continue
        key = "%%%s" % patchnum.lower()
        patchdef[key] = patch.strip()
        patches.append(patch.strip())

    if inapplications:
        parts = line.split()
        if parts[0] == "#": continue
        if parts[0] == "ApplyPatch":
            patch = parts[1].strip()
        else:
            patch = patchdef[parts[0].strip()]
        if patch not in patches:
            print "skipping undefined patch %s" % patch
            continue
        patchapply.append(os.path.join(prefix, patch))

f.close()


os.mkdir('patches')
os.chdir('patches')
f = open('series', "w")
f.write("# series from specfile %s\n" % specfile)
for p in patchapply:
    f.write("%s\n" % p)
    shutil.copy("../%s" % p, p)

