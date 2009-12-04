#include <v8.h>

extern "C" {
    #define MONGO_HAVE_STDINT
    #include "bson.h"
}

using namespace v8;

const char* ToCString(const v8::String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}

Handle<Value>
encode(const Arguments &args) {
    // TODO assert args.length > 0
    // TODO assert args.type == Object
    
    HandleScope scope;
    Local<Object> obj = args[0]->ToObject();
    Local<Array> properties = obj->GetPropertyNames();

    bson        bson;
    bson_buffer bb;
    bson_oid_t  oid;

    bson_buffer_init(&bb);

    for (int i = 0; i < properties->Length(); i++) {
        // get the property name
        Local<Value> prop_name = properties->Get(Integer::New(i));
        Local<Value> prop_val = obj->Get(prop_name->ToString());

        // convert the name to a c string
        v8::String::Utf8Value n(prop_name);
        const char *name = ToCString(n);

        if (prop_val->IsString()) {
            String::Utf8Value v(prop_val);
            const char *value = ToCString(v);
            bson_append_string(&bb, name, value);
        }
        else if (prop_val->IsInt32()) {
            double value = prop_val->NumberValue();
            bson_append_double(&bb, name, value);
        }
        else if (prop_val->IsNumber()) {
            double value = prop_val->NumberValue();
            bson_append_double(&bb, name, value);
        }
        else if (prop_val->IsBoolean()) {
            char value = prop_val->IsTrue() ? 1 : 0;
            bson_append_bool(&bb, name, value);
        }
        else {
            printf("nothing!!\n");
        }
    }
    
    bson_from_buffer(&bson, &bb);
    return String::New(bson.data, bson_size(&bson));
}

extern "C" void
init (Handle<Object> target) {
    HandleScope scope;
    target->Set(
        String::New("encode"),
        FunctionTemplate::New(encode)->GetFunction());
}

