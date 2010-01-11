NAME
----

node-mongodb - An asynchronous Node interface to MongoDB

SYNOPSYS
--------

    var sys     = require("sys");
    var mongodb = require("./mongodb");

    var mongo = new mongodb.MongoDB();

    mongo.addListener("close", function () {
        sys.puts("Closing connection!");
    });

    mongo.addListener("connection", function () {
        var widgets = mongo.getCollection('widgets');

        mongo.getCollections().addCallback(function (collections) {
            sys.puts("the collections in the db are " + JSON.stringify(collections));
        });

        // remove widgets with shazbot > 0
        widgets.remove({ shazbot: { "$gt": 0 } });

        // actually, just remove all widgets
        widgets.remove();

        widgets.count().addCallback(function(count) {
            widgets.insert({ foo: 1,    shazbot: 1 });
            widgets.insert({ bar: "a",  shazbot: 2 });
            widgets.insert({ baz: 42.5, shazbot: 0 });

            // count all the widgets
            widgets.count().addCallback(function (count) {
                sys.puts("there are " + count + " widgets");
            });

            // count widgets with shazbot > 0
            widgets.count({ shazbot: { "$gt": 0 } }).addCallback(function (count) {
                sys.puts(count + " widget shazbots are > 0");
            });

            // count shazbots less than or equal to 1
            widgets.count({ shazbot: { "$lte": 1 } })
                   .addCallback(function (count) {
                assertEquals(2, count);
            });

            // return all widgets
            widgets.find().addCallback(function (results) {
                // ...
            });

            // return widgets with shazbot > 0
            widgets.find({ shazbot: { "$gt": 0 } }).addCallback(function (results) {
                // ...
            });

            // return only the shazbot field of every widget
            widgets.find({}, { "shazbot": true }).addCallback(function (results) {
                // update shazbot of first document with shazbot 0 to 420
                widgets.update({ shazbot: 0 }, { shazbot: 420 });

                widgets.find().addCallback(function (results) {
                    results.forEach(function(r) {
                        // ...
                    });

                    // close the connection
                    mongo.close();
                });
            });
        });
    });

    mongo.connect({
        hostname: '127.0.0.1',
        port: 27017,
        db: 'mylittledb'
    });

DESCRIPTION
-----------

This is an attempt at MongoDB bindings for Node. The important thing here is
to ensure that we never let ourselves or any libraries block on IO. As such,
I've tried to do my best to make sure that connect() and recv() never block,
but there may be bugs. The MongoDB C drivers are used to interface with the
database, but some core functions needed to be rewritten  to operate in a
non-blocking manner.

Installation
------------

- Make sure you have git installed.
- ./update-mongo-c-driver.sh
- node-waf configure build
- node test_mongo.js

BUGS
----

This package is EXPERIMENTAL, with emphasis on MENTAL. I am working on this in
my spare time to learn the Node, v8 and MongoDB API's.

The error handling in this extension needs to be improved substantially. Be
warned.

I would appreciate any and all patches, suggestions and constructive
criticisms.

ACKNOWLEDGEMENTS
----------------

- ryah's Node postgres driver was the foundation for this extension
- MongoDB C drivers
- The people in #node.js and #mongodb on freenode for answering my questions

SEE ALSO
--------

- http://github.com/ry/node_postgres
- http://github.com/mongodb/mongo-c-driver

AUTHOR
------

Orlando Vazquez (ovazquez@gmail.com)
