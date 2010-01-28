require.paths.push("lib");

var sys     = require("sys"),
    mongo   = require("mongo"),
    mongodb = require("mongodb");

process.mixin(GLOBAL, require('assert'));
jjj = JSON.stringify;

var bson = { encode: mongo.encode, decode: mongo.decode };

/* OID */
var oid_hex = "123456789012345678901234";
var oid = new mongodb.ObjectID(oid_hex);
equal(oid.toString(), oid_hex);

var mongo = new mongodb.MongoDB();

throws(function () { new mongodb.ObjectID("12345"); });
throws(function () { new mongodb.ObjectID(); });
throws(function () { new mongodb.ObjectID([1,2]); });
throws(function () { new mongodb.ObjectID(1); });

var comparisons = [
    // strings
    [
        { "hello": "world" },
        "\x16\x00\x00\x00\x02hello\x00\x06\x00\x00\x00world\x00\x00"
    ],
    [
        { "javascript": "rocks", "mongodb": "rocks" },
        "\x2e\x00\x00\x00"          + // size
        "\x02javascript\x00"        + // key
        "\x06\x00\x00\x00rocks\x00" + // value
        "\x02mongodb\x00"           + // key
        "\x06\x00\x00\x00rocks\x00" + // value
        "\x00"
    ],

    // number
    [
        { "hello": 1 },
        "\x10\x00\x00\x00"                 + // size
        "\x10hello\x00"                    + // key
        "\x01\x00\x00\x00" + // value
        "\x00"
    ],
    [
        { "hello": 4.20 },
        "\x14\x00\x00\x00"                 + // size
        "\x01hello\x00"                    + // key
        "\xcd\xcc\xcc\xcc\xcc\xcc\x10\x40" +
        "\x00"
    ],

    // boolean
    [
        { "hello": true },
        "\x0d\x00\x00\x00" + // size
        "\x08hello\x00"    + // key
        "\x01"             + // value
        "\x00"
    ],
    [
        { "hello": false },
        "\x0d\x00\x00\x00" + // size
        "\x08hello\x00"    + // key
        "\x00"             + // value
        "\x00"
    ],

    // arrays
    [
        { 'mahbucket': [ 'foo', 'bar', 'baz' ] },
        "\x36\x00\x00\x00"        + // size
        "\x04mahbucket\0"         + // type, key
        "\x26\x00\x00\x00"        +
        "\x02"+"0\0"              + // item 0
        "\x04\x00\x00\x00foo\x00" +
        "\x02"+"1\0"              + // item 1
        "\x04\x00\x00\x00bar\x00" +
        "\x02"+"2\0"              + // item 2
        "\x04\x00\x00\x00baz\x00" +
        "\x00" +
        "\x00"
    ],
    [
        { 'mahbucket': [ { 'foo': 'bar' } ] },
        "\x2a\x00\x00\x00"        + // size
        "\x04mahbucket\0"         + // type, key
        "\x1a\x00\x00\x00"        +
        "\x03"+"0\0"              + // item 0
        "\x12\x00\x00\x00"        + // size
        "\x02foo\x00"             + // key
        "\x04\x00\x00\x00bar\x00" + // value
        "\x00" +
        "\x00" +
        "\x00"
    ],
    [
        { 'mahbucket': [ [ "foo", "bar" ], ["baz", "qux"] ] },
        "\x51\x00\x00\x00"        + // size
        "\x04mahbucket\0"         + // type, key

        "\x41\x00\x00\x00"        + // size of top level array

        "\x04"+"0\0"              + // first sub array

        "\x1b\x00\x00\x00"        + // size of inner array
        "\x02"+"0\0"              + // item 0
        "\x04\x00\x00\x00foo\x00" +
        "\x02"+"1\0"              + // item 1
        "\x04\x00\x00\x00bar\x00" +
        "\x00"                    +

        "\x04"+"1\0"              + // second sub array

        "\x1b\x00\x00\x00"        + // size of inner array
        "\x02"+"0\0"              + // item 0
        "\x04\x00\x00\x00baz\x00" +
        "\x02"+"1\0"              + // item 1
        "\x04\x00\x00\x00qux\x00" +
        "\x00" +

        "\x00" +
        "\x00"
    ],

    // nested objects
    [
        { "great-old-ones": { "cthulhu": true } },
        "\x24\x00\x00\x00"     + // size
        "\x03great-old-ones\0" + // type, key
                                 // value:
        "\x0f\x00\x00\x00"     + //   size
        "\x08cthulhu\x00"      + //   type, key
        "\x01"                 + //   value
        "\x00"                 + //   eoo
        "\x00"
    ],
];

// Test decoding and encoding of objects
var i = 0;
comparisons.forEach(function (comp) {
    sys.puts("test #" + i++);
    deepEqual(bson.encode(comp[0]), comp[1]);
    deepEqual(bson.decode(comp[1]), comp[0]);
});
