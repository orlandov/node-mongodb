#!/usr/bin/env node

process.mixin(GLOBAL, require('mjsunit'));

var sys = require("sys");
var mongodb = require("./mongodb");

var oid_hex = "123456789012345678901234";
var oid = new mongodb.ObjectID(oid_hex);
assertEquals(oid.toString(), oid_hex);

var mongo = new mongodb.MongoDB();

mongo.addListener("close", function () {
    sys.puts("Tests done!");
});

mongo.addListener("connection", function () {
    var widgets = mongo.getCollection('widgets');

    widgets.remove();

    widgets.count().addCallback(function(count) {
        assertEquals(0, count);

        widgets.insert({ foo: 1, shazbot: 1 });
        widgets.insert({ bar: "a", shazbot: 2 });
        widgets.insert({ baz: 42.5, shazbot: 0 });

        widgets.count().addCallback(function (count) {
            assertEquals(3, count);
        });

        widgets.find().addCallback(function (results) {
            assertEquals(results.length, 3);
        });

        widgets.find({ shazbot: { "$gt": 0 } }).addCallback(function (results) {
            assertEquals(results.length, 2);
            for (var i = 0; i < results.length; i++) {
                // check that we had a validish oid
                assertTrue(results[i]['_id'].toString().length == 24);
                assertEquals(results[i]['baz'], undefined);
            }
        });

        widgets.find({}, { "shazbot": true }).addCallback(function (results) {
            var shazbots = [];
            for (var i = 0; i < results.length; i++) {
                shazbots.push(results[i].shazbot);
                assertEquals(results[i]['foo'], undefined);
                assertEquals(results[i]['bar'], undefined);
                assertEquals(results[i]['baz'], undefined);
            }
            shazbots.sort();
            assertEquals(shazbots, [0, 1, 2]);

            widgets.update({ shazbot: 0 }, { shazbot: 420 });

            widgets.find().addCallback(function (results) {
                for (var i = 0; i < results.length; i++) {
                    assertTrue(results[i].shazbot != 0);
                }

                mongo.close();
            });
        });
    });
});

mongo.connect({
    hostname: '127.0.0.1',
    port: 27017,
    db: '__node_mongodb_test'
});
