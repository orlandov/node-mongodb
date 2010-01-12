require.paths.push("lib");

var sys     = require("sys"),
    mongo   = require("mongo"),
    mongodb = require("mongodb");

process.mixin(GLOBAL, require('mjsunit'));
jjj = JSON.stringify;

var bson = { encode: mongo.encode, decode: mongo.decode };

function xxdCompare(actual, expected) {
    var proc = process.createChildProcess("xxd", ["-i"]);
    var buffer = '';

    proc.addListener("output", function (data) {
        if(data) buffer += data;
    });

    proc.addListener("exit", function (code) {
        // start the second process

        var proc2 = process.createChildProcess("xxd", ["-i"]);
        var buffer2 = '';

        proc2.addListener("output", function (data) {
            if(data) buffer2 += data;
        });

        proc2.addListener("exit", function (code) {
            sys.puts("---------------");
            sys.puts(buffer);
            sys.puts(buffer2);
            sys.puts("===============");
            assertEquals(buffer, buffer2);    
        });

        proc2.write(actual, 'binary');
        proc2.close();
    });

    proc.write(expected, 'binary');
    proc.close();
}


function xxdPrint(str) {
    var proc = process.createChildProcess("/usr/bin/xxd", ["-i"]);
    var buffer = '';

    proc.addListener("output", function (data) {
        if (data) buffer += data;
    });
    proc.addListener("exit", function (code) {
        sys.puts(buffer);        
    });
    proc.write(str, 'binary');
    proc.close();
}

/* OID */
var oid_hex = "123456789012345678901234";
var oid = new mongodb.ObjectID(oid_hex);
assertEquals(oid.toString(), oid_hex);

var mongo = new mongodb.MongoDB();

assertThrows("new mongodb.ObjectID(\"12345\")");;
assertThrows("new mongodb.ObjectID()");;
assertThrows("new mongodb.ObjectID([1,2])");;
assertThrows("new mongodb.ObjectID(1)");;

/* Encoding */

// strings
{
    xxdCompare(
        bson.encode({ "hello": "world" }),
        "\x16\x00\x00\x00\x02hello\x00\x06\x00\x00\x00world\x00\x00");

    xxdCompare(
        bson.encode({ "javascript": "rocks", "mongodb": "rocks" }),
        "\x2e\x00\x00\x00"          + // size
        "\x02javascript\x00"        + // key
        "\x06\x00\x00\x00rocks\x00" + // value
        "\x02mongodb\x00"           + // key
        "\x06\x00\x00\x00rocks\x00" + // value
        "\x00"
    );
}

// numbers
{

    xxdCompare(
        bson.encode({ "hello": 1 }),
        "\x10\x00\x00\x00"                 + // size
        "\x10hello\x00"                    + // key
        "\x01\x00\x00\x00" + // value
        "\x00");

    xxdCompare(
        bson.encode({ "hello": 4.20 }),
        "\x14\x00\x00\x00"                 + // size
        "\x01hello\x00"                    + // key
        "\xcd\xcc\xcc\xcc\xcc\xcc\x10\x40" +
        "\x00");
}

// booleans
{
    xxdCompare(
        bson.encode({ "hello": true }),
        "\x0d\x00\x00\x00" + // size
        "\x08hello\x00"    + // key
        "\x01"             + // value
        "\x00");

    xxdCompare(
        bson.encode({ "hello": false }),
        "\x0d\x00\x00\x00" + // size
        "\x08hello\x00"    + // key
        "\x00"             + // value
        "\x00");
}

// arrays
xxdCompare(
    bson.encode({ 'mahbucket': [ 'foo', 'bar', 'baz' ] }),
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
);

// array with nested object
xxdCompare(
    bson.encode({ 'mahbucket': [ { 'foo': 'bar' } ] }),
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
);

// array with nested array
xxdCompare(
    bson.encode({ 'mahbucket': [ [ "foo", "bar" ], ["baz", "qux"] ] }),
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
);

// nested objects
xxdCompare(
    bson.encode({ "great-old-ones": { "cthulhu": true } }),
    "\x24\x00\x00\x00"     + // size
    "\x03great-old-ones\0" + // type, key
                             // value:
    "\x0f\x00\x00\x00"     + //   size
    "\x08cthulhu\x00"      + //   type, key
    "\x01"                 + //   value
    "\x00"                 + //   eoo
    "\x00"
);

/* Decoding */

// strings
{
    var o = bson.decode(
        "\x16\x00\x00\x00\x02hello\x00\x06\x00\x00\x00world\x00\x00");
    assertEquals(o.hello, "world");

    o = bson.decode(
        "\x2e\x00\x00\x00"          + // size
        "\x02javascript\x00"        + // key
        "\x06\x00\x00\x00rocks\x00" + // value
        "\x02mongodb\x00"           + // key
        "\x06\x00\x00\x00rocks\x00" + // value
        "\x00"
    );
    assertEquals(o.javascript, "rocks");
    assertEquals(o.mongodb, "rocks");
}

// numbers
{

    var o = bson.decode(
           "\x14\x00\x00\x00"                 + // size
           "\x10hello\x00"                    + // key
           "\x01\x00\x00\x00" + // value
           "\x00");
    assertEquals(1, o.hello);

    o = bson.decode(
        "\x14\x00\x00\x00"                 + // size
        "\x01hello\x00"                    + // key
        "\xcd\xcc\xcc\xcc\xcc\xcc\x10\x40" + // value
        "\x00");
    assertEquals(4.20, o.hello);
}

// objects
{
    var o = bson.decode(
        "\x1c\x00\x00\x00"     + // size
        "\x03ronnie\0"         + // type, key
                                 // value:
        "\x0f\x00\x00\x00"     + //   size
        "\x08cthulhu\x00"      + //   type, key
        "\x01"                 + //   value
        "\x00"                 + //   eoo
        "\x00");

    assertEquals(true, o.ronnie.cthulhu);
}
