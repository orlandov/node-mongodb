var sys     = require("sys");
var mongodb = require("../lib/mongodb");

var mongo = new mongodb.MongoDB();

mongo.addListener("close", function () {
    sys.puts("Closing connection!");
});

mongo.addListener("connection", function () {
    var widgets = mongo.getCollection('widgets');

    mongo.getCollections(function (collections) {
        sys.puts("the collections in the db are " + JSON.stringify(collections));
    });

    // remove widgets with shazbot > 0
    widgets.remove({ shazbot: { "$gt": 0 } });

    // actually, just remove all widgets
    widgets.remove();

    widgets.count(null, function(count) {
        widgets.insert({ foo: 1,    shazbot: 1 });
        widgets.insert({ bar: "a",  shazbot: 2 });
        widgets.insert({ baz: 42.5, shazbot: 0 });

        // count all the widgets
        widgets.count(null, function (count) {
            sys.puts("there are " + count + " widgets");
        });

        // count widgets with shazbot > 0
        widgets.count({ shazbot: { "$gt": 0 } }, function (count) {
            sys.puts(count + " widget shazbots are > 0");
        });

        // count shazbots less than or equal to 1
        widgets.count({ shazbot: { "$lte": 1 } }, function (count) {
            sys.puts(2 == count);
        });

        // return all widgets
        widgets.find(null, null, function (results) {
            // ...
        });

        // return widgets with shazbot > 0
        widgets.find({ shazbot: { "$gt": 0 } }, null, function (results) {
            // ...
        });

        // return only the shazbot field of every widget
        widgets.find({}, { "shazbot": true }, function (results) {
            // update shazbot of first document with shazbot 0 to 420
            widgets.update({ shazbot: 0 }, { shazbot: 420 });

            widgets.find(null, null, function (results) {
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
