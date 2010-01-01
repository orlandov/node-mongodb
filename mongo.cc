#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <v8.h>

#include <node.h>
#include <node_events.h>

#include <fcntl.h>

const int VERSION = 1;
const int VERSION_MINOR = 0;

#include <mongo/client/dbclient.h>
#include <mongo/db/dbmessage.h>
#include <mongo/util/message.h>
#include <v8_wrapper.h>

#include "cursor.h"
#include "mongo.h"

using namespace std;
using namespace v8;
using namespace node_mongo;

namespace node_mongo {

void Connection::Initialize (Handle<Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);
    NODE_SET_PROTOTYPE_METHOD(t, "find", Find);

    target->Set(String::NewSymbol("Connection"), t->GetFunction());
}

bool Connection::Connect(const char *host, const int port) {

    printf("connecting! %s %d\n", host, port);

    conn->connect("localhost:27017");

    printf("setting socket flags\n");
    // enable non-blocking mode
    int sock = conn->port().sock;
    int sockflags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, sockflags | O_NONBLOCK);

    printf("setting socket watchers\n");
    ev_io_set(&read_watcher,  sock, EV_READ);
    ev_io_set(&write_watcher, sock, EV_WRITE);

    StartWriteWatcher();

    Attach();

    return true;
}

void Connection::CheckBufferContents(void) {
    if (state == STATE_READ_HEAD) {
        if (buflen > HEADER_SIZE) {
            printf("got enough for the head\n");
            printf("memcpy'd\n");
            bufptr += HEADER_SIZE;
            state = STATE_READ_MESSAGE;
        }
    }
    if (state == STATE_READ_MESSAGE) {
        mongo::MsgData *data = reinterpret_cast<mongo::MsgData *>(buf);
        printf("in read message\n");
        int len;
        len = data->len;
        printf("read message length was %d\n", len);

        if (len-buflen == 0) {
            printf("its at zero!\n");
            state = STATE_PARSE_MESSAGE;
        }
    }
    if (state == STATE_PARSE_MESSAGE) {
        ParseMessage();
        GetResults();
        delete [] buf;
        buf = bufptr = NULL;
        buflen = 0;

        state = STATE_READ_HEAD;
        StopReadWatcher();
        StartWriteWatcher();
        printf("listening for input again\n");
    }
}

void Connection::ParseMessage(void) {
    HandleScope scope;
    printf("in parse message\n");

    mongo::QueryResult *data = reinterpret_cast<mongo::QueryResult *>(buf);
    printf("in read message\n");
    node_cursor->setData(data);
    node_cursor->dataReceived();
}

void Connection::GetResults() {
    HandleScope scope;

    printf("in parse reply\n");
    for (int i = results->Length(); node_cursor->more(); i++){
        mongo::BSONObj obj(node_cursor->next());
        printf("item %d\n", i);

        Local<Value> val = mongo::mongoToV8(obj, false, false);
        //Local<Value> val = String::New("foo");
        //printf("item was %s\n", obj.toString().c_str());
        results->Set(Integer::New(i), val);
    }
    get_more = true;
    printf("test2\n");

    StopReadWatcher();
    StartWriteWatcher();
    printf("end of readresponse\n");
}

bool Connection::ConsumeInput(void) {
    char *tmp;
    char readbuf[chunk_size];
    int32_t readbuflen;

    while (true) {
        readbuflen = read(conn->port().sock, readbuf, chunk_size);

        // no more input to consume
        if (readbuflen == -1 && errno == EAGAIN) {
            printf("len == -1 && errno == EAGAIN\n");
        }
        else if (readbuflen <= 0) {
            printf("length error on read %d errno = %d\n", readbuflen, errno);
        }
        else {
            printf("buf is %d bytes\n", buflen);
            printf("read %d bytes\n", readbuflen);
            tmp = new char[buflen+readbuflen];
            memset(tmp, 0, buflen+readbuflen);

            if (buf) {
                memcpy(tmp, buf, buflen);
            }
            memcpy(tmp+buflen, readbuf, readbuflen);
            if (buf) {
                printf("deleting old buf\n");
                delete [] buf;
            }
            buflen = buflen + readbuflen;
            bufptr = tmp + (bufptr - buf);
            buf = tmp;
            printf("buf is %d bytes\n\n", buflen);
            break;
        }
    }
}

bool Connection::SendGetMore(void) {
    HandleScope scope;
    printf("mabe sending out for more\n");
    if (!node_cursor->isDead()) {
        printf("sending out for more\n");
        node_cursor->requestMore();
        state = STATE_READ_HEAD;
        StartReadWatcher();
        StopWriteWatcher();

        return true;
    }
    else {
        printf("dont have to send out, cursor id was 0!\n");
        // clean up cursor here
        Emit("result", 1, reinterpret_cast<Handle<Value> *>(&results));
        results.Dispose();
        results.Clear();
        get_more = false;
        return false;
    }
}

bool Connection::Find(void) {
    mongo::Query q;
    const std::string ns("test.widgets");
    node_cursor.reset(new NodeMongoCursor(conn.get(), ns, q.obj, 0, 0, 0, 0));
    node_cursor->init();
    node_cursor->reallySend();

    StartReadWatcher();
}

Handle<Value> Connection::New (const Arguments& args) {
    HandleScope scope;

    Connection *connection = new Connection();
    connection->Wrap(args.This());
    return args.This();
}

Connection::Connection () : EventEmitter () {
    HandleScope scope;
    conn.reset(new NonBlockingConnection());
    Handle<Array> r = Array::New();
    results = Persistent<Array>::New(r);

    get_more = false;
    buflen = 0;
    buf = bufptr = NULL;
    state = STATE_READ_HEAD;

    ev_init(&read_watcher, io_event);
    read_watcher.data = this;
    ev_init(&write_watcher, io_event);
    write_watcher.data = this;
}

Handle<Value> Connection::Connect(const Arguments &args) {
    Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
    HandleScope scope;
    String::Utf8Value host(args[0]->ToString());
    bool r = connection->Connect(*host, args[1]->Int32Value());

    return Undefined();
}

Handle<Value> Connection::Find(const Arguments &args) {
    Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
    HandleScope scope;

    if (args.Length() > 1) {
        printf("made it to here\n");
        mongo::v8ToMongo(args[0]->ToObject());
        printf("made it to here?\n");
    }
    
    connection->Find();
    return Undefined();
}

void Connection::Event(int revents) {
    if (revents & EV_WRITE) {
        printf("!!! got a write event\n");
        StopWriteWatcher();
        if (get_more) {
            SendGetMore();
        }
    }
    if (revents & EV_READ) {
        printf("!!! got a read event\n");
        ConsumeInput();
        CheckBufferContents();
    }
    if (revents & EV_ERROR) {
        printf("!!! got an error event\n");
    }
}

void Connection::io_event (EV_P_ ev_io *w, int revents) {
    Connection *connection = static_cast<Connection *>(w->data);
    connection->Event(revents);
};

extern "C" void
init (Handle<Object> target) {
    HandleScope scope;
    Connection::Initialize(target);
}

}
