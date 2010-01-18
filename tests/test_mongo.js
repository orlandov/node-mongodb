#!/usr/bin/env node

process.mixin(GLOBAL, require('mjsunit'));

var jjj = JSON.stringify;

var sys = require("sys");

require.paths.push("lib");

var mongodb = require("mongodb");

var oid_hex = "123456789012345678901234";

var mongo = new mongodb.MongoDB();

var cb = 0;

mongo.addListener("close", function () {
    assertEquals(cb, 10);
    sys.puts("Tests done!");
});

mongo.addListener("connection", function () {
    cb++;
    mongo.getCollections().addCallback(function (collections) {
        cb++;
        collections.sort();
        assertEquals(["system.indexes", "widgets"], collections);
    });
    var widgets = mongo.getCollection('widgets');

    assertThrows("widgets.remove([1,2])");
    assertThrows("widgets.remove(1)");

    widgets.remove();

    widgets.count().addCallback(function(count) {
        cb++;
        assertEquals(0, count);

        widgets.insert({ _id: new mongodb.ObjectID(oid_hex), "dude": "lebowski" });

        widgets.insert({ foo: 1, shazbot: 1 });
        widgets.insert({ bar: "a", shazbot: 2 });
        widgets.insert({ baz: 42.5, shazbot: 0 });
        widgets.insert({ baz: 42.5, bucket: [1, 2] });

        widgets.find_one({ _id: new mongodb.ObjectID(oid_hex) }).addCallback(function (result) {
            cb++;
            assertEquals(result._id.toString(), oid_hex);
            assertEquals(result.dude, "lebowski");
        });

        widgets.count().addCallback(function (count) {
            cb++;
            assertEquals(5, count);
        });

        widgets.count({ shazbot: { "$lte": 1 } }).addCallback(function (count) {
            cb++;
            assertEquals(2, count);
        });

        widgets.find().addCallback(function (results) {
            cb++;
            assertEquals(results.length, 5);
        });

        widgets.find({ shazbot: { "$gt": 0 } }).addCallback(function (results) {
            cb++;
            assertEquals(results.length, 2);
            results.forEach(function (r) {
                // check that we had a validish oid
                assertTrue(r['_id'].toString().length == 24);
                assertEquals(r['baz'], undefined);
            });
        });

        widgets.find({}, { "shazbot": true }).addCallback(function (results) {
            cb++;
            var shazbots = [];
            results.forEach(function (r) {
                shazbots.push(r.shazbot);
                assertEquals(r['foo'], undefined);
                assertEquals(r['bar'], undefined);
                assertEquals(r['baz'], undefined);
            });
            shazbots.sort();
            assertEquals(shazbots, [0, 1, 2]);

            widgets.update({ shazbot: 0 }, { shazbot: 420 });

            widgets.find({ shazbot: {"$lt": 1000}}).addCallback(function (results) {
                cb++;
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
