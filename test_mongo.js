#!/usr/bin/env node

process.mixin(GLOBAL, require('mjsunit'));

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
