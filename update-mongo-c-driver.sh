#!/bin/sh

if [ ! -d mongo-c-driver ]; then
    git clone git://github.com/orlandov/mongo-c-driver.git
fi

cd mongo-c-driver
git fetch
git rebase origin/master

scons --c99
