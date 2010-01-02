NAME
====

node-mongodb - An asynchronous Node.js interface to MongoDB

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

This is an attempt at MongoDB bindings for Node.JS. The important thing here
is to ensure that we never let ourselves or any libraries block on IO. As
such, I've tried to do my best to make sure that connect() and recv() never
block, but there may be bugs. The MongoDB C drivers are used to interface with
the database to the extent that the functions do not block.

Installation
------------

- Make sure you have git installed.
- ./update-mongo-c-driver.sh
- node-waf configure build
- node test_mongo.js

BUGS
====

This package is EXPERIMENTAL, with emphasis on MENTAL. I am working on this in
my spare time to learn the node.js, v8 and mongo API's. I am mixing C and C++.
I am probably doing it totally wrong.

The error handling in this extension needs to be improved substantially. Be
warned.

I would appreciate any and all patches, suggestions and constructive
criticisms.

ACKNOWLEDGEMENTS
================

- ryah's Node.JS postgres driver was the foundation for this extension
- MongoDB C drivers
- #node.js and #mongodb on freenode for answering my questions

SEE ALSO
========

- http://github.com/ry/node_postgres
- http://github.com/mongodb/mongo-c-driver

AUTHOR
======

Orlando Vazquez <ovazquez/gmail/com>


