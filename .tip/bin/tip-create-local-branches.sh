#!/bin/sh

BS=`git branch -a | grep origin | grep -v master | grep -v HEAD | sed s@origin/@@`

for B in $BS
do
    git branch $B origin/$B
done
