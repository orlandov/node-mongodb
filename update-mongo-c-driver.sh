#!/bin/sh

if [ ! -d mongo-c-driver ]; then
    git clone git://github.com/mongodb/mongo-c-driver.git
else
    git fetch
    git rebase origin/master
fi

cd mongo-c-driver
scons --c99
