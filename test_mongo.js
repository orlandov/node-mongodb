#!/usr/bin/env node

process.mixin(GLOBAL, require('mjsunit'));

sys = require("sys");
mongodb = require("./mongodb");

var mongo = new mongodb.MongoDB();

mongo.addListener("connection", function () {
    var widgets = mongo.getCollection('widgets');

//     widgets.find().addCallback(function (result) {
//         sys.puts(JSON.stringify(result));
//     });
// 
//     widgets.find({}, { "hello": true }).addCallback(function (result) {
//         sys.puts(JSON.stringify(result));
//     });

    // must implement skip/limit before this will work
    widgets.count().addCallback(function(count) {
        sys.puts("count result was " + count);
    });

    widgets.insert({ shazbot: Math.random() });
});

mongo.connect({
    hostname: '127.0.0.1',
    port: 27017,
    db: 'test'
});
