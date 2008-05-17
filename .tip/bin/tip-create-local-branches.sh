#!/bin/sh

BS=`git branch -a | grep origin | grep -v master | grep -v HEAD | grep -v " linus" | sed s@origin/@@`

for B in $BS
do
    git branch $B origin/$B
done
