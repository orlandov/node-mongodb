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
Connection = require("./mongo").Connection

c = new Connection();

// XXX TODO this should trigger an event
c.connect("127.0.0.1", 27017);

c.addListener("connect", function () {
    sys.puts("connect is never emitted, but it should be!");        
});

c.addListener("result", function (result) {
    sys.puts("result was " +JSON.stringify(result));
    sys.puts("\n");
});

c.find({foo:1});
