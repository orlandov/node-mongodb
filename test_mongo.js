/*

// this is just me messing around with API ideas, please ignore
   
sys = require('sys');
mongodb = require('mongodb');

conn = mongodb.createConnection('localhost', 'mydb');

conn.addListener("connection", function (conn) {
        sys.puts("connected");
        });

conn.addListener("close", function (e) {
        sys.puts("connection closed");        
        });

var posts = conn.selectCollection('posts');

posts.find();

posts.find({ "author": "orlando" }, { "author": 1, "age": 1 }, function () {
    // foo        
});

posts.findOne({ "id": "myid" });

posts.count();

*/

process.mixin(GLOBAL, require('mjsunit'));

sys = require("sys");
mongodb = require("./mongodb");

var mongo = new mongodb.MongoDB();

mongo.connect({
    hostname: '127.0.0.1',
    port: 27017,
    db: 'test'
});

var test = mongo.getCollection('widgets');

test.find({}, {}).addCallback(function (result) {
    sys.puts(JSON.stringify(result));        
});

mongo.dispatch();
