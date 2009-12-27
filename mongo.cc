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

#include <mongo/stdafx.h>
#include <mongo/client/dbclient.h>
#include <mongo/db/dbmessage.h>
#include <mongo/util/message.h>

extern "C" {
    #define MONGO_HAVE_STDINT
    #include <bson.h>
    #include <mongo.h>
    #include <platform_hacks.h>

}

#define NS "test.widgets"


const char HEADER_SIZE = mongo::MsgDataHeaderSize;

const int chunk_size = 4094;

using namespace v8;

extern Local<Value> decodeObjectStr(const char *);
enum ReadState {
    STATE_READ_HEAD,
    STATE_READ_MESSAGE,
    STATE_PARSE_MESSAGE,
};

void node_mongo_find(mongo_connection* conn, const char* ns, bson* query, bson* fields, int nToReturn, int nToSkip, int options) {
    int sl;
    char * data;
    mongo_message * mm = mongo_message_create( 16 + /* header */
                                               4 + /*  options */
                                               strlen( ns ) + 1 + /* ns */
                                               4 + 4 + /* skip,return */
                                               bson_size( query ) +
                                               bson_size( fields ) ,
                                               0 , 0 , mongo_op_query );

    data = &mm->data;
    data = mongo_data_append32( data , &options );
    data = mongo_data_append( data , ns , strlen( ns ) + 1 );
    data = mongo_data_append32( data , &nToSkip );
    data = mongo_data_append32( data , &nToReturn );
    data = mongo_data_append( data , query->data , bson_size( query ) );
    if ( fields )
        data = mongo_data_append( data , fields->data , bson_size( fields ) );

    bson_fatal_msg( (data == ((char*)mm) + mm->head.len), "query building fail!" );

    mongo_message_send( conn , mm );
}

class Connection : public node::EventEmitter {
    public:

    static void
    Initialize (Handle<Object> target) {
        HandleScope scope;

        Local<FunctionTemplate> t = FunctionTemplate::New(New);

        t->Inherit(EventEmitter::constructor_template);
        t->InstanceTemplate()->SetInternalFieldCount(1);

        NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);
        NODE_SET_PROTOTYPE_METHOD(t, "find", Find);

        target->Set(String::NewSymbol("Connection"), t->GetFunction());
    }

    void StartReadWatcher() {
        printf("*** Starting read watcher\n");
        ev_io_start(EV_DEFAULT_ &read_watcher);
    }

    void StopReadWatcher() {
        printf("*** Stopping read watcher\n");
        ev_io_stop(EV_DEFAULT_ &read_watcher);
    }

    void StartWriteWatcher() {    
        printf("*** Starting write watcher\n");
        ev_io_start(EV_DEFAULT_ &write_watcher);
    }

    void StopWriteWatcher() {
        printf("*** Stopping write watcher\n");
        ev_io_stop(EV_DEFAULT_ &write_watcher);
    }

    bool Connect(const char *host, const int32_t port) {
        mongo_connection_options opts;
        memcpy(opts.host, host, strlen(host)+1);
        opts.host[strlen(host)] = '\0';
        opts.port = port;

        printf("connecting! %s %d\n", host, port);

        if (mongo_connect(conn, &opts)) {
            return false;
        }

        // enable non-blocking mode
        int sockflags = fcntl(conn->sock, F_GETFL, 0);
        fcntl(conn->sock, F_SETFL, sockflags | O_NONBLOCK);

        ev_io_set(&read_watcher,  conn->sock, EV_READ);
        ev_io_set(&write_watcher, conn->sock, EV_WRITE);

        StartWriteWatcher();

        Attach();

        return true;
    }

    void CheckBufferContents(void) {
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
            delete [] buf;
            buf = bufptr = NULL;
            buflen = 0;

            state = STATE_READ_HEAD;
            StopReadWatcher();
            StartWriteWatcher();
            printf("listening for input again\n");
        }
    }

    void ParseMessage(void) {
        HandleScope scope;
        printf("in parse message\n");

        mongo::QueryResult *data = reinterpret_cast<mongo::QueryResult *>(buf);
        printf("in read message\n");
        int len;
        len = data->len;
        char outbuffer[len];

        mongo_reply *out = reinterpret_cast<mongo_reply*>(outbuffer);

        out->head.len = len;
        out->head.id = data->id;
        out->head.responseTo = data->responseTo;
        out->head.op = data->operation();

        out->fields.flag = static_cast<int>(data->resultFlags());
        printf("flags were %d", out->fields.flag);

        out->fields.cursorID = data->cursorId;
        out->fields.start = data->startingFrom;
        out->fields.num = data->nReturned;

        printf("op was %d\n", out->head.op);
        printf("test1\n");

        memcpy(&out->objs, data->data(), data->dataLen());
        printf("test2.1\n");

        printf("test3\n");

        ParseReply(out);

        printf("foobar1\n");
        printf("foobar2\n");
    }

    void ParseReply(mongo_reply *out) {
        HandleScope scope;
        printf("parsing reply\n");

        cursor = static_cast<mongo_cursor*>(bson_malloc(sizeof(mongo_cursor)));
        cursor->mm = out;

        int sl = strlen(NS)+1;
        cursor->ns = static_cast<char *>(new char[sl]);

        memcpy(static_cast<void*>(const_cast<char*>(cursor->ns)), NS, sl);
        cursor->conn = conn;
        cursor->current.data = NULL;

        printf("test1\n");
        for (int i = results->Length(); AdvanceCursor(); i++){
            printf("item %d\n", i);
            Local<Value> val = decodeObjectStr(cursor->current.data);
            results->Set(Integer::New(i), val);
        }
        printf("test2\n");

        StopReadWatcher();
        StartWriteWatcher();
        printf("end of readresponse\n");

        return;
    }

    bool AdvanceCursor(void) {
        char* bson_addr;

        /* no data */
        if (!cursor->mm || cursor->mm->fields.num == 0)
            return false;

        /* first */
        if (cursor->current.data == NULL){
            bson_init(&cursor->current, &cursor->mm->objs, 0);
            return true;
        }

        bson_addr = cursor->current.data + bson_size(&cursor->current);
        if (bson_addr >= ((char*)cursor->mm + cursor->mm->head.len)){
            printf("i should be getting more here\n");
            get_more = true;

            // indicate that this is the last result
            return false;
        } else {
            printf("advancing cursor by one object\n");
            bson_init(&cursor->current, bson_addr, 0);
        }
    }

    bool ConsumeInput(void) {
        char *tmp;
        char readbuf[chunk_size];
        int32_t readbuflen;

        while (true) {
            readbuflen = read(conn->sock, readbuf, chunk_size);

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

    bool SendGetMore(void) {
        HandleScope scope;
        if (cursor->mm && cursor->mm->fields.cursorID){
            char* data;
            const int zero = 0;
            int sl = strlen(cursor->ns)+1;
            mongo_message * mm = mongo_message_create(16 /*header*/
                                                     +4 /*ZERO*/
                                                     +sl
                                                     +4 /*numToReturn*/
                                                     +8 /*cursorID*/
                                                     , 0, 0, mongo_op_get_more);
            data = &mm->data;
            data = mongo_data_append32(data, &zero);
            data = mongo_data_append(data, cursor->ns, sl);
            data = mongo_data_append32(data, &zero);
            data = mongo_data_append64(data, &cursor->mm->fields.cursorID);
            mongo_message_send(conn, mm);
            state = STATE_READ_HEAD;

            StartReadWatcher();
            StopWriteWatcher();

            return true;

        } else {

            delete [] cursor->ns;
            free(cursor);
            Emit("result", 1, reinterpret_cast<Handle<Value> *>(&results));
            results.Dispose();
            results.Clear();
            get_more = false;
            return false;
        }
    }


    bool Find(void) {
        bson query;
        bson_empty(&query);

        node_mongo_find(conn, "test.widgets", &query, 0, 0, 0, 0);
        StartReadWatcher();
    }

    protected:

    static Handle<Value>
    New (const Arguments& args) {
        HandleScope scope;

        Connection *connection = new Connection();
        connection->Wrap(args.This());
        return args.This();
    }

    Connection () : EventEmitter () {
        HandleScope scope;
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

    static Handle<Value>
    Connect (const Arguments &args) {
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
        HandleScope scope;
        String::Utf8Value host(args[0]->ToString());
        bool r = connection->Connect(*host, args[1]->Int32Value());

        return Undefined();
    }

    static Handle<Value>
    Find (const Arguments &args) {
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
        HandleScope scope;

        connection->Find();
    }

    void Event(int revents) {
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

    private:

    static void
    io_event (EV_P_ ev_io *w, int revents) {
        Connection *connection = static_cast<Connection *>(w->data);
        connection->Event(revents);
    }

    mongo_connection conn[1];

    // states
    bool get_more;
    ReadState state;

    mongo_cursor *cursor;

    Persistent<Array> results;

    char *buf;
    char *bufptr;
    int32_t buflen;

    ev_io read_watcher;
    ev_io write_watcher;
};

extern "C" void
init (Handle<Object> target) {
    HandleScope scope;
    Connection::Initialize(target);
}
