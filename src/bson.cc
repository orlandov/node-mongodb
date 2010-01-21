#include <v8.h>
#include <node.h>
#include <node_object_wrap.h>
#include <sstream>
extern "C" {
    #define MONGO_HAVE_STDINT
    #include <bson.h>
}

#include "bson.h"

using namespace std;
using namespace v8;

Persistent<FunctionTemplate> ObjectID::constructor_template;

void ObjectID::Initialize(Handle<Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(ObjectID::New);
    constructor_template = Persistent<FunctionTemplate>::New(t);
    constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
    constructor_template->SetClassName(String::NewSymbol("ObjectID"));

    NODE_SET_PROTOTYPE_METHOD(ObjectID::constructor_template, "toString", ObjectID::ToString);

    target->Set(String::NewSymbol("ObjectID"), constructor_template->GetFunction());
}

Handle<Value> ObjectID::New(const Arguments &args) {
    HandleScope scope;

    if (args.Length() < 1
           || !args[0]->IsString()
           || (args[0]->IsString()
                && args[0]->ToString()->Length() != 24)) {
        return ThrowException(Exception::Error(
                String::New("Argument must be 24 character hex string")));
    }

    String::Utf8Value hex(args[0]->ToString());

    // XXX where should this be deleted?
    ObjectID *o = new ObjectID((const char *) *hex);
    o->Wrap(args.This());
    return args.This();
}

void ObjectID::str(char *str) {
    bson_oid_to_string(&oid, str);
}

Handle<Value>
ObjectID::ToString(const Arguments &args) {
    ObjectID *o = ObjectWrap::Unwrap<ObjectID>(args.This());

    HandleScope scope;
    char hex[25];
    o->str(hex);
    return String::New(hex);
}

const char *
ToCString(const String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}

inline void
encodeString(bson_buffer *bb, const char *name, const Local<Value> element) {
    String::Utf8Value v(element);
    const char *value(ToCString(v));
    bson_append_string(bb, name, value);
}

inline void
encodeNumber(bson_buffer *bb, const char *name, const Local<Value> element) {
    double value(element->NumberValue());
    bson_append_double(bb, name, value);
}

inline void
encodeInteger(bson_buffer *bb, const char *name, const Local<Value> element) {
    int value(element->NumberValue());
    bson_append_int(bb, name, value);
}

inline void
encodeBoolean(bson_buffer *bb, const char *name, const Local<Value> element) {
    bool value(element->IsTrue());
    bson_append_bool(bb, name, value);
}

void 
encodeObjectID(bson_buffer *bb, const char *name, const Local<Value> element) {
    // get at the delicious wrapped object centre
    Local<Object> obj = element->ToObject();
    assert(!obj.IsEmpty());
    assert(obj->InternalFieldCount() > 0);
    ObjectID *o = static_cast<ObjectID*>(Handle<External>::Cast(
                obj->GetInternalField(0))->Value());
    bson_oid_t oid;
    char oid_hex[25];
    o->str(oid_hex);
    bson_oid_from_string(&oid, oid_hex);
    bson_append_oid(bb, name, &oid);
}

void
encodeArray(bson_buffer *bb, const char *name, const Local<Value> element) {
    Local<Array> a = Array::Cast(*element);
    bson_buffer *arr = bson_append_start_array(bb, name);

    for (int i = 0, l=a->Length(); i < l; i++) {
        Local<Value> val = a->Get(Number::New(i));
        stringstream keybuf;
        string keyval;
        keybuf << i << endl;
        keybuf >> keyval;
        
        if (val->IsString()) {
            encodeString(arr, keyval.c_str(), val);
        }
        else if (val->IsInt32()) {
            encodeInteger(arr, keyval.c_str(), val);
        }
        else if (val->IsNumber()) {
            encodeNumber(arr, keyval.c_str(), val);
        }
        else if (val->IsBoolean()) {
            encodeBoolean(arr, keyval.c_str(), val);
        }
        else if (val->IsArray()) {
            encodeArray(arr, keyval.c_str(), val);
        }
        else if (val->IsObject()) {
            bson bson(encodeObject(val));
            bson_append_bson(arr, keyval.c_str(), &bson);
            bson_destroy(&bson);
        }
    }
    bson_append_finish_object(arr);
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
        String::Utf8Value n(prop_name);
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
        else if (prop_val->IsArray()) {
            encodeArray(&bb, pname, prop_val);
        }
        else if (prop_val->IsObject()
                 && ObjectID::constructor_template->HasInstance(prop_val)) {
            encodeObjectID(&bb, pname, prop_val);
        }
        else if (prop_val->IsObject()) {
            bson bson(encodeObject(prop_val));
            bson_append_bson(&bb, pname, &bson);
            bson_destroy(&bson);
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
    Handle<Value> ret = node::Encode(bson.data, bson_size(&bson), node::BINARY);
    bson_destroy(&bson);
    return ret;
}

// Decoding functions

Handle<Value>
decodeString(bson_iterator *i) {
    HandleScope scope;
    const char *val = bson_iterator_string(i);
    Local<Value> str = String::New(val);
    return scope.Close(str);
}

Handle<Value>
decodeObject(bson_iterator *i) {
    HandleScope scope;
    bson bson;
    bson_iterator_subobject(i, &bson);
    Handle<Value> sub = decodeObjectStr(bson.data);
    return scope.Close(sub);
}

Handle<Value>
decodeObjectID(bson_iterator *i) {
    HandleScope scope;
    char hex_oid[25];
    bson_oid_t *oid = bson_iterator_oid(i);
    bson_oid_to_string(oid, hex_oid);
    Handle<Value> argv[1];
    argv[0] = String::New(hex_oid);

    Handle<Value> obj =
        ObjectID::constructor_template->GetFunction()->NewInstance(1, argv);

    return scope.Close(obj);
}

Handle<Value>
decodeDouble(bson_iterator *i) {
    HandleScope scope;
    double val = bson_iterator_double_raw(i);
    Local<Value> obj = Number::New(val);
    return scope.Close(obj);
}

Handle<Value>
decodeInteger(bson_iterator *i) {
    HandleScope scope;
    double val = bson_iterator_int_raw(i);
    Local<Value> obj = Integer::New(val);
    return scope.Close(obj);
}

Handle<Value>
decodeBool(bson_iterator *i) {
    HandleScope scope;
    bson_bool_t val = bson_iterator_bool(i);
    Handle<Value> obj = Boolean::New(val);
    return scope.Close(obj);
}

Local<Value>
decodeObjectStr(const char *buf) {
    HandleScope scope;

    bson_iterator it;
    bson_iterator_init(&it, buf);
    Local<Object> obj = Object::New();

    while (bson_iterator_next(&it)) {
        bson_type type = bson_iterator_type(&it);
        const char *key = bson_iterator_key(&it);

        switch (type) {
            case bson_string:
                obj->Set(String::New(key), decodeString(&it));
                break;

            case bson_array:
                bson bson;
                bson_iterator sub;
                bson_iterator_subiterator(&it, &sub);

                while (bson_iterator_next(&sub)) {
                    bson_type type = bson_iterator_type(&sub);
                    printf("got in here %d\n",type);
                    
                    const char *key = bson_iterator_key(&sub);
                    const char *val = bson_iterator_value(&sub);

                    Handle<Value> subobj = decodeObjectStr(val);
                    obj->Set(String::New(key), subobj);
                }
                break;

            case bson_object:
                obj->Set(String::New(key), decodeObject(&it));
                break;

            case bson_oid:
                obj->Set(String::New(key), decodeObjectID(&it));
                break;

            case bson_double:
                obj->Set(String::New(key), decodeDouble(&it));
                break;

            case bson_int:
                obj->Set(String::New(key), decodeInteger(&it));
                break;

            case bson_bool:
                obj->Set(String::New(key), decodeBool(&it));
                break;
        }
    }

    return scope.Close(obj);
}

Handle<Value>
decodeObject(const Local<Value> str) {
    HandleScope scope;
    size_t buflen = str->ToString()->Length();
    char buf[buflen];
    node::DecodeWrite(buf, buflen, str, node::BINARY);
    return decodeObjectStr(buf);
}

Handle<Value>
decode(const Arguments &args) {
    HandleScope scope;
    return decodeObject(args[0]);
}
