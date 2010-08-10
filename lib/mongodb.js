var sys   = require("sys"),
    mongo = require("../lib/mongo");

function Collection(mongo, db, name) {
    this.mongo = mongo;
    this.ns = db + "." + name;
    this.db = db;
    this.name = name;
}

Collection.prototype.find = function(query, fields, callback) {
    this.mongo.addQuery(callback, this.ns, query, fields);
}

jjj = JSON.stringify

Collection.prototype.insert = function(obj) {
    this.mongo.connection.insert(this.ns, obj);
}

Collection.prototype.update = function(cond, obj) {
    this.mongo.connection.update(this.ns, cond, obj);
}

Collection.prototype.remove = function(query) {
    this.mongo.connection.remove(this.ns, query);
}

Collection.prototype.find_one = function(query, fields, ns, callback) {
    this.mongo.addQuery(function (results) {
        // XXX what if result.Length < 1
        callback(results[0]);
    }, ns || this.ns, query, fields, 1);
}

Collection.prototype.count = function(query, callback) {
    ns = this.db + ".$cmd";
    var cmd = {
        "count": this.name,
        "query": query
    }

    this.find_one(cmd, {}, ns, function (result) {
        callback(result.n);
    });
}

function MongoDB() {
    this.myID = Math.random();
    this.connection = new mongo.Connection();

    var self = this;

    this.connection.addListener("close", function () {
        self.emit("close");
    });

    this.connection.addListener("ready", function () {
        self.dispatch();
    });

    this.connection.addListener("connection", function () {
        self.emit("connection", self);
    });

    this.connection.addListener("result", function(result) {
        var callback = self.currentQuery[0];
        callback(result);
        self.currentQuery = null;
    });
}

sys.inherits(MongoDB, process.EventEmitter);

MongoDB.prototype.connect = function(args) {
    this.queries = [];
    this.hostname = args.hostname || "127.0.0.1";
    this.port = args.port || 27017;
    this.db = args.db;

    this.connection.connect(this.hostname, this.port);
}

MongoDB.prototype.close = function() {
    this.connection.close();
}

MongoDB.prototype.addQuery = function(callback, ns, query, fields, limit, skip ) {
    var q = [ callback, ns ];
    if (query) q.push(query);
    if (fields) q.push(fields);
    if (limit) q.push(limit);
    if (skip) q.push(skip);
    this.queries.push(q);
    this.dispatch();
}

MongoDB.prototype.dispatch = function() {
    if (this.currentQuery || !this.queries.length) return;
    this.currentQuery = this.queries.shift();
    this.connection.find.apply(this.connection, this.currentQuery.slice(1));
}

MongoDB.prototype.getCollection = function(name) {
    return new Collection(this, this.db, name);
}

MongoDB.prototype.getCollections = function(callback) {
    this.addQuery(function (results) {
        var collections = [];
        results.forEach(function (r) {
            if (r.name.indexOf("$") != -1)
                return;
            collections.push(r.name.slice(r.name.indexOf(".")+1));
        });
		callback(collections);
	}, this.db + ".system.namespaces");

}

exports.MongoDB = MongoDB;
exports.ObjectID = mongo.ObjectID
