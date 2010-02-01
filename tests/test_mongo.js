#!/usr/bin/env node

process.mixin(GLOBAL, require('assert'));

var jjj = JSON.stringify;

var sys = require("sys");

require.paths.push("lib");

var mongodb = require("mongodb");

var oid_hex = "123456789012345678901234";

var mongo = new mongodb.MongoDB();

var cb = 0;

mongo.addListener("close", function () {
    equal(cb, 11);
    sys.puts("Tests done!");
});

mongo.addListener("connection", function () {
    cb++;
    mongo.getCollections().addCallback(function (collections) {
        cb++;
        collections.sort();
        deepEqual(collections, ["system.indexes", "widgets"]);
    });
    var widgets = mongo.getCollection('widgets');

    throws("widgets.remove([1,2])");
    throws("widgets.remove(1)");

    widgets.remove();

    widgets.count().addCallback(function(count) {
        cb++;
        equal(count, 0);

        widgets.insert({ _id: new mongodb.ObjectID(oid_hex), "dude": "lebowski" });

        widgets.insert({ foo: 1, shazbot: 1 });
        widgets.insert({ bar: "a", shazbot: 2 });
        widgets.insert({ baz: 42.5, shazbot: 0 });
        widgets.insert({ baz: 420, bucket: [1, 2] });

        widgets.find({ baz: 420 }).addCallback(function (result) {
            cb++;
            deepEqual(result[0].bucket, [1, 2]);
        });

        widgets.find_one({ _id: new mongodb.ObjectID(oid_hex) }).addCallback(function (result) {
            cb++;
            equal(result._id.toString(), oid_hex);
            equal(result.dude, "lebowski");
        });

        widgets.count().addCallback(function (count) {
            cb++;
            equal(5, count);
        });

        widgets.count({ shazbot: { "$lte": 1 } }).addCallback(function (count) {
            cb++;
            equal(2, count);
        });

        widgets.find().addCallback(function (results) {
            cb++;
            equal(5, results.length);
        });

        widgets.find({ shazbot: { "$gt": 0 } }).addCallback(function (results) {
            cb++;
            equal(results.length, 2);
            results.forEach(function (r) {
                // check that we had a validish oid
                ok(r['_id'].toString().length == 24);
                equal(r['baz'], undefined);
            });
        });

        widgets.find({}, { "shazbot": true }).addCallback(function (results) {
            cb++;
            var shazbots = [];
            results.forEach(function (r) {
                shazbots.push(r.shazbot);
                equal(r['foo'], undefined);
                equal(r['bar'], undefined);
                equal(r['baz'], undefined);
            });
            shazbots.sort();
            deepEqual(shazbots, [0, 1, 2]);

            widgets.update({ shazbot: 0 }, { shazbot: 420 });

            widgets.find({ shazbot: {"$lt": 1000}}).addCallback(function (results) {
                cb++;
                for (var i = 0; i < results.length; i++) {
                    notEqual(results[i].shazbot, 0);
                }

                mongo.close();
            });
        });
    });
});

mongo.connect({
    hostname: '127.0.0.1',
    port: 27017,
    db: 'node_mongodb_test'
});
