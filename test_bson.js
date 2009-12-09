process.mixin(GLOBAL, require('mjsunit'));

sys = require("sys");
bson = require("./bson");

function xxdCompare(actual, expected) {
    var proc = process.createChildProcess("xxd", ["-i"]);
    var buffer = '';

    proc.addListener("output", function (data) {
        sys.puts('output');
        if(data) buffer += data;
    });

    proc.addListener("exit", function (code) {
        // start the second process
        sys.puts("second proc");

        var proc2 = process.createChildProcess("xxd", ["-i"]);
        var buffer2 = '';

        proc2.addListener("output", function (data) {
            sys.puts('got here');
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
        sys.puts("got a line");
        if (data) buffer += data;
    });
    proc.addListener("exit", function (code) {
        sys.puts("xxdPrint");
        sys.puts("buffer was ");
        sys.puts(buffer);        
    });
    proc.write(str, 'binary');
    proc.close();
}

/*
    Encoding
*/

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

// nested objects
{
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
}

/*
    Decoding
*/

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
