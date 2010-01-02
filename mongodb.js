sys = require("sys");

Connection = require("./mongo").Connection

function Collection(mongo, db, name) {
    this.mongo = mongo;
    this.ns = db + "." + name;
    sys.puts("new collection " + this.ns);
}

Collection.prototype.find = function(query, fields) {
    var promise = new process.Promise;
    promise.collection = this;

    this.mongo.addQuery(promise, query, fields);

    return promise;
}

function MongoDB() {
    this.connection = new Connection;

    self = this;

    this.connection.addListener("ready", function () {
        sys.puts("got ready event");
        self.dispatch();
    });

    this.connection.addListener("connection", function () {
        self.emit("connection");
    });

    this.connection.addListener("result", function(result) {
        sys.puts("got result in mongodb\n");
        var promise = self.currentQuery.promise;
        self.currentQuery = null;
        promise.emitSuccess(result);
    });
}

sys.inherits(MongoDB, process.EventEmitter);

MongoDB.prototype.connect = function(args) {
    self = this;

    this.queries = [];
    this.hostname = args.hostname || "127.0.0.1";
    this.port = args.port || 27017;
    this.db = args.db;

    this.connection.connect(this.hostname, this.port);
}

MongoDB.prototype.addQuery = function(promise, query, fields ) {
    this.queries.push({ 'promise': promise, 'query': query, 'fields': fields });
}

MongoDB.prototype.dispatch = function() {
    if (this.currentQuery || !this.queries.length) return;

    this.currentQuery = this.queries.shift();
    sys.puts("doing he query\n");
    this.connection.find(
        this.currentQuery.promise.collection.ns,
        this.currentQuery.query,
        this.currentQuery.fields);
}

MongoDB.prototype.getCollection = function(name) {
    return new Collection(this, this.db, name);
}

exports.MongoDB = MongoDB;
