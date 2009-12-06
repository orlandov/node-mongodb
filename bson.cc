#include <v8.h>

extern "C" {
    #define MONGO_HAVE_STDINT
    #include "bson.h"
}

using namespace v8;

const char *
ToCString(const v8::String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}

void
encodeString(bson_buffer *bb, const char *name, const Local<Value> element) {
    String::Utf8Value v(element);
    const char *value = ToCString(v);
    bson_append_string(bb, name, value);
}

void
encodeNumber(bson_buffer *bb, const char *name, const Local<Value> element) {
    double value = element->NumberValue();
    bson_append_double(bb, name, value);
}

void
encodeInteger(bson_buffer *bb, const char *name, const Local<Value> element) {
    double value = element->NumberValue();
    bson_append_double(bb, name, value);
}

void
encodeBoolean(bson_buffer *bb, const char *name, const Local<Value> element) {
    bool value = element->IsTrue();
    bson_append_bool(bb, name, value);
}

bson encodeObject(const Local<Value> element) {
    HandleScope scope;
    bson_buffer bb;
    bson_buffer_init(&bb);

    Local<Object> object = element->ToObject();
    Local<Array> properties = object->GetPropertyNames();

    for (int i = 0; i < properties->Length(); i++) {
        // get the property name and value
        Local<Value> prop_name = properties->Get(Integer::New(i));
        Local<Value> prop_val = object->Get(prop_name->ToString());

        // convert the property name to a c string
        v8::String::Utf8Value n(prop_name);
        const char *pname = ToCString(n);
       
        // append property using appropriate appender
        if (prop_val->IsString()) {
            encodeString(&bb, pname, prop_val);
        }
        else if (prop_val->IsInt32()) {
            encodeInteger(&bb, pname, prop_val);
        }
        else if (prop_val->IsNumber()) {
            encodeNumber(&bb, pname, prop_val);
        }
        else if (prop_val->IsBoolean()) {
            encodeBoolean(&bb, pname, prop_val);
        }
        else if (prop_val->IsObject()) {
            bson bson(encodeObject(prop_val));
            bson_append_bson(&bb, pname, &bson);
        }
    }

    bson bson;
    bson_from_buffer(&bson, &bb);
    return bson;
}

Handle<Value>
encode(const Arguments &args) {
    // TODO assert args.length > 0
    // TODO assert args.type == Object
    HandleScope scope;

    bson bson(encodeObject(args[0]));

    return String::New(bson.data, bson_size(&bson));
}

extern "C" void
init (Handle<Object> target) {
    HandleScope scope;
    target->Set(
        String::New("encode"),
        FunctionTemplate::New(encode)->GetFunction());
}

