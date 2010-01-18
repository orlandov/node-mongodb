#!/bin/sh

for f in `ls tests/test_*.js`; do
    node $f
done
