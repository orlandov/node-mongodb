NAME
====

node-mongodb - An asynchronous Node interface to MongoDB

SYNOPSYS
========

    sys = require("sys");
    mongodb = require("./mongodb");

    var mongo = new mongodb.MongoDB();

    mongo.addListener("connection", function () {
        var widgets = mongo.getCollection('widgets');

        widgets.find({}, {}).addCallback(function (result) {
            sys.puts(JSON.stringify(result));
        });
        widgets.find({}, { "hello": true }).addCallback(function (result) {
            sys.puts(JSON.stringify(result));
        });
    });

    mongo.connect({
        hostname: '127.0.0.1',
        port: 27017,
        db: 'test'
    });

DESCRIPTION
===========

This is an attempt at MongoDB bindings for Node. The important thing here
is to ensure that we never let ourselves or any libraries block on IO. As
such, I've tried to do my best to make sure that connect() and recv() never
block, but there may be bugs. The MongoDB C drivers are used to interface with
the database, but some core functions needed to be rewritten  to operate in a
non-blocking manner.

Installation
------------

- Make sure you have git installed.
- ./update-mongo-c-driver.sh
- node-waf configure build
- node test_mongo.js

BUGS
====

This package is EXPERIMENTAL, with emphasis on MENTAL. I am working on this in
my spare time to learn the Node, v8 and MongoDB API's.

The error handling in this extension needs to be improved substantially. Be
warned.

I would appreciate any and all patches, suggestions and constructive
criticisms.

ACKNOWLEDGEMENTS
================

- ryah's Node postgres driver was the foundation for this extension
- MongoDB C drivers
- The people in #node.js and #mongodb on freenode for answering my questions

SEE ALSO
========

- http://github.com/ry/node_postgres
- http://github.com/mongodb/mongo-c-driver

AUTHOR
======

Orlando Vazquez (ovazquez@gmail.com)
