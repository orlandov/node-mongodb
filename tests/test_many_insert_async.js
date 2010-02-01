#!/usr/bin/env node

process.mixin(GLOBAL, require('mjsunit'));

require.paths.push("lib");
var mongodb = require( 'mongodb' );
var sys = require( 'sys' );
var mongo = new mongodb.MongoDB();

jjj = JSON.stringify
mongo.addListener('connection', function( ) { 
    var test = mongo.getCollection( 'widgets' );
    test.remove();

    var i = 1000;
    function doInserts() {
        if (!i--) return;
        test.insert( { i: i } );
        process.nextTick(arguments.callee);
    }

    doInserts();
});

mongo.addListener('close', function() {
    sys.puts("Tests done!");
});

mongo.connect( {
    hostname: '127.0.0.1',
    port: 27017,
    db: '__node_mongodb_test'
} );
