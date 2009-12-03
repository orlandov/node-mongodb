process.mixin(GLOBAL, require('mjsunit'));
sys = require("sys");

bson = require("./bson");
sys.puts(bson.encode({"hello": "world"}));
