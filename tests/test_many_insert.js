#!/usr/bin/env node

// thanks to sifu@github for the testcase

assert = require('assert');
deepEqual = assert.deepEqual;

require.paths.push("lib");
var mongodb = require('mongodb');
var sys = require('sys');
var mongo = new mongodb.MongoDB();
mongo.addListener('connection', function( ) { 
    var test = mongo.getCollection( 'widgets' );
    test.remove();

    var i = 1000;
    while( i-- ) {
        test.insert( { i: i } );
    }

    test.count(null, function (count) {
        deepEqual(count, 1000);
        mongo.close();
    });
});

mongo.addListener('close', function() {
    sys.puts("Tests done!");
});

mongo.connect( {
    hostname: '127.0.0.1',
    port: 27017,
    db: '__node_mongodb_test'
} );
