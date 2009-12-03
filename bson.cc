#include <v8.h>
/* #include <mongoclient> */

using namespace v8;

Handle<Value>
encode(const Arguments &args) {
    HandleScope scope;
    return String::New("hello");
}

extern "C" void
init (Handle<Object> target) {
    HandleScope scope;
    target->Set(
        String::New("encode"),
        FunctionTemplate::New(encode)->GetFunction());
}
