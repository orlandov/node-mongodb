#include <v8.h>

extern "C" {
    #define MONGO_HAVE_STDINT
    #include "bson.h"
}

using namespace v8;

Handle<Value>
encode(const Arguments &args) {
    // TODO assert args.length > 0

    HandleScope scope;

    bson_buffer bb;
    bson obj;
    bson_oid_t oid;
    char *buffer;

   // bson_oid_gen(&oid);
    {
        bson_buffer_init(&bb);
     //   bson_append_oid(&bb, "_id", &oid);
        bson_append_string(&bb, "hello", "world");
        bson_from_buffer(&obj, &bb);
    }

    return String::New(obj.data, bson_size(&obj));
}

extern "C" void
init (Handle<Object> target) {
    HandleScope scope;
    target->Set(
        String::New("encode"),
        FunctionTemplate::New(encode)->GetFunction());
}
