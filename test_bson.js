process.mixin(GLOBAL, require('mjsunit'));

sys = require("sys");
bson = require("./bson");

function xxdCompare(actual, expected) {
    var proc = process.createChildProcess("xxd", ["-i"]);
    var buffer = '';

    proc.addListener("output", function (data) {
        buffer += data;
    });

    proc.addListener("exit", function (code) {
        // start the second process

        var proc2 = process.createChildProcess("xxd", ["-i"]);
        var buffer2 = '';

        proc2.addListener("output", function (data) {
            buffer2 += data;
        });

        proc2.addListener("exit", function (code) {
            assertEquals(buffer, buffer2);    
        });

        proc2.write(actual);
        proc2.close();
    });

    proc.write(expected);
    proc.close();
}

function xxdPrint(str) {
    var proc = process.createChildProcess("xxd");
    var buffer = '';

    proc.addListener("output", function (data) {
        sys.puts("got a line");
        buffer += data;
    });
    proc.addListener("exit", function (code) {
        sys.puts("buffer was ");
        sys.puts(buffer);        
    });
    proc.write(str);
    proc.close();
}

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
        "\x14\x00\x00\x00"                 + // size
        "\x01hello\x00"                    + // key
        "\x00\x00\x00\x00\x00\x00\xfd\x3f" + // value
        "\x00");

    xxdCompare(
        bson.encode({ "hello": 3.141 }),
        "\x14\x00\x00\x00"                 + // size
        "\x01hello\x00"                    + // key
        "\x54\x5b\xfd\x20\x09\x40" + // value
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


