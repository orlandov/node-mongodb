process.mixin(GLOBAL, require('mjsunit'));

sys = require("sys");
bson = require("./bson");

assertEquals(
    bson.encode({"hello": "world"}),
    "\x16\x00\x00\x00\x02hello\x00\x06\x00\x00\x00world\x00\x00");
