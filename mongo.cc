#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include <v8.h>

#include <node.h>
#include <node_object_wrap.h>
#include <node_events.h>

extern "C" {
    #define MONGO_HAVE_STDINT
    #include <bson.h>
    #include <mongo.h>
    #include <platform_hacks.h>
}
#include "bson.h"

#define DEBUGMODE 0
#define pdebug(...) do{if(DEBUGMODE)printf(__VA_ARGS__);}while(0)

const int chunk_size(4094);
const int headerSize(sizeof(mongo_header) + sizeof(mongo_reply_fields));

using namespace v8;

enum ReadState {
    STATE_READ_HEAD,
    STATE_READ_REPLY,
    STATE_PARSE_REPLY,
};

void setNonBlocking(int sock) {
    int sockflags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, sockflags | O_NONBLOCK);
}

void non_blocking_mongo_find(mongo_connection* conn, const char* ns, bson* query, bson* fields, int nToReturn, int nToSkip, int options) {
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

        Local<FunctionTemplate> t = FunctionTemplate::New(Connection::New);

        t->Inherit(node::EventEmitter::constructor_template);
        t->InstanceTemplate()->SetInternalFieldCount(1);

        NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);
        NODE_SET_PROTOTYPE_METHOD(t, "close", Close);
        NODE_SET_PROTOTYPE_METHOD(t, "find", Find);
        NODE_SET_PROTOTYPE_METHOD(t, "insert", Insert);
        NODE_SET_PROTOTYPE_METHOD(t, "update", Update);
        NODE_SET_PROTOTYPE_METHOD(t, "remove", Remove);

        target->Set(String::NewSymbol("Connection"), t->GetFunction());
    }

    void StartReadWatcher() {
        pdebug("*** Starting read watcher\n");
        ev_io_start(EV_DEFAULT_ &read_watcher);
    }

    void StopReadWatcher() {
        pdebug("*** Stopping read watcher\n");
        ev_io_stop(EV_DEFAULT_ &read_watcher);
    }

    void StartWriteWatcher() {
        pdebug("*** Starting write watcher\n");
        ev_io_start(EV_DEFAULT_ &write_watcher);
    }

    void StopWriteWatcher() {
        pdebug("*** Stopping write watcher\n");
        ev_io_stop(EV_DEFAULT_ &write_watcher);
    }

    void StartConnectWatcher() {
        pdebug("*** Starting connect watcher\n");
        ev_io_start(EV_DEFAULT_ &connect_watcher);
    }

    void StopConnectWatcher() {
        pdebug("*** Stopping connect watcher\n");
        ev_io_stop(EV_DEFAULT_ &connect_watcher);
    }

    void CreateConnection(mongo_connection_options *options) {
    //    MONGO_INIT_EXCEPTION(&conn->exception);

        conn->left_opts = (mongo_connection_options *)bson_malloc(sizeof(mongo_connection_options));
        conn->right_opts = NULL;

        if ( options ){
            memcpy( conn->left_opts , options , sizeof( mongo_connection_options ) );
        } else {
            strcpy( conn->left_opts->host , "127.0.0.1" );
            conn->left_opts->port = 27017;
        }

        MongoCreateSocket();
    }

    mongo_conn_return MongoCreateSocket() {
        conn->sock = 0;
        conn->connected = 0;

        memset(conn->sa.sin_zero, 0, sizeof(conn->sa.sin_zero));
        conn->sa.sin_family = AF_INET;
        conn->sa.sin_port = htons(conn->left_opts->port);
        conn->sa.sin_addr.s_addr = inet_addr(conn->left_opts->host);
        conn->addressSize = sizeof(conn->sa);

        conn->sock = socket( AF_INET, SOCK_STREAM, 0 );
        if (conn->sock <= 0){
            return mongo_conn_no_socket;
        }

        setNonBlocking(conn->sock);
        int res = connect(conn->sock, (struct sockaddr*) &conn->sa, conn->addressSize);

        assert(res < 0);
        assert(errno == EINPROGRESS);

        ev_io_set(&connect_watcher,  conn->sock, EV_WRITE);
        StartConnectWatcher();
    }

    void Connected() {
        StopConnectWatcher();
        setsockopt( conn->sock, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(one) );

        conn->connected = 1;

        Emit(String::New("connection"), 0, NULL);
    }

    void Connect(const char *host, const int32_t port) {
        HandleScope scope;

        mongo_connection_options opts;
        memcpy(opts.host, host, strlen(host)+1);
        opts.host[strlen(host)] = '\0';
        opts.port = port;

        CreateConnection(&opts);

        setNonBlocking(conn->sock);

        ev_io_set(&read_watcher,  conn->sock, EV_READ);
        ev_io_set(&write_watcher, conn->sock, EV_WRITE);

        StartWriteWatcher();
    }

    void Close() {
        HandleScope scope;

        StopWriteWatcher();
        StopReadWatcher();

        mongo_destroy(conn);

        Emit(String::New("close"), 0, NULL);
    }

    void CheckBufferContents() {
        if (state == STATE_READ_HEAD) {
            if (buflen >= headerSize) {
                memcpy(&head, bufptr, headerSize);
                bufptr += headerSize;
                state = STATE_READ_REPLY;
            }
        }
        if (state == STATE_READ_REPLY) {
            int len;
            bson_little_endian32(&len, &head.len);

            if (len-buflen == 0) {
                state = STATE_PARSE_REPLY;
            }
        }
        if (state == STATE_PARSE_REPLY) {
            ParseReply();
            delete [] buf;
            buf = bufptr = NULL;
            buflen = 0;

            state = STATE_READ_HEAD;
            StopReadWatcher();
            StartWriteWatcher();
        }
    }

    void ParseReply() {
        HandleScope scope;

        int len;
        bson_little_endian32(&len, &head.len);

        char replybuf[len];
        mongo_reply *out = reinterpret_cast<mongo_reply*>(replybuf);

        out->head.len = len;
        bson_little_endian32(&out->head.id, &head.id);
        bson_little_endian32(&out->head.responseTo, &head.responseTo);
        bson_little_endian32(&out->head.op, &head.op);

        bson_little_endian32(&out->fields.flag, &fields.flag);
        bson_little_endian64(&out->fields.cursorID, &fields.cursorID);
        bson_little_endian32(&out->fields.start, &fields.start);
        bson_little_endian32(&out->fields.num, &fields.num);

        memcpy(&out->objs, bufptr, len-sizeof(head)-sizeof(fields));

        cursor->mm = out;
        cursor->current.data = NULL;

        for (int i = results->Length(); AdvanceCursor(); i++){
            Local<Value> val = decodeObjectStr(cursor->current.data);
            results->Set(Integer::New(i), val);
        }

        // if this is the last cursor
        if (!cursor->mm || ! fields.cursorID) {
            EmitResults();
            get_more = false;
        }

        StopReadWatcher();
        StartWriteWatcher();
    }

    void FreeCursor() {
        free((void*)cursor->ns);
        free(cursor);
        cursor = NULL;
    }

    void EmitResults() {
        FreeCursor();

        Emit(String::New("result"), 1, reinterpret_cast<Handle<Value> *>(&results));

        // XXX better way to do this?
        results.Dispose();
        results.Clear();
        Handle<Array> r = Array::New();
        results = Persistent<Array>::New(r);
    }

    bool AdvanceCursor() {
        char* bson_addr;

        /* no data */
        if (!cursor->mm || cursor->mm->fields.num == 0)
            return false;

        /* first */
        if (cursor->current.data == NULL){
            bson_init(&cursor->current, &cursor->mm->objs, 0);
            return true;
        }

        // new cursor position
        bson_addr = cursor->current.data + bson_size(&cursor->current);

        if (bson_addr >= ((char*)cursor->mm + cursor->mm->head.len)){
            // current cursor is out of data
            if (! fields.cursorID) {
                // cursorID is blank, so there's no more results
                get_more = false;
            }
            else {
                // cursor isn't empty so we should fetch more results
                get_more = true;
            }

            // indicate that this is the last result
            return false;
        } else {
            // advance cursor by one object
            bson_init(&cursor->current, bson_addr, 0);

            return true;
        }
        return false;
    }

    bool ConsumeInput() {
        char *tmp;
        char readbuf[chunk_size];
        int32_t readbuflen;

        for (;;) {
            readbuflen = read(conn->sock, readbuf, chunk_size);

            if (readbuflen == -1 && errno == EAGAIN) {
                // no more input to consume
                pdebug("len == -1 && errno == EAGAIN\n");
            }
            else if (readbuflen <= 0) {
                // socket problem?
                pdebug("length error on read %d errno = %d\n", readbuflen, errno);
            }
            else {
                tmp = static_cast<char *>(new char[buflen+readbuflen]);
                memset(tmp, 0, buflen+readbuflen);

                if (buf) {
                    memcpy(tmp, buf, buflen);
                }
                memcpy(tmp+buflen, readbuf, readbuflen);
                if (buf) {
                    delete [] buf;
                }
                buflen = buflen + readbuflen;
                bufptr = tmp + (bufptr - buf);
                buf = tmp;
                break;
            }
        }
    }

    void RequestMore() {
        HandleScope scope;

        char* data;
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
        data = mongo_data_append64(data, &fields.cursorID);
        mongo_message_send(conn, mm);
        state = STATE_READ_HEAD;

        StartReadWatcher();
        StopWriteWatcher();
    }


    bool Find(const char *ns, bson *query=0, bson *query_fields=0,
              int nToReturn=0, int nToSkip=0) {

        cursor = static_cast<mongo_cursor*>(
                     bson_malloc(sizeof(mongo_cursor)));

        int sl = strlen(ns)+1;
        cursor->ns = static_cast<char*>(bson_malloc(sl));

        memcpy(static_cast<void*>(const_cast<char*>(cursor->ns)), ns, sl);
        cursor->conn = conn;

        non_blocking_mongo_find(conn, ns, query, query_fields, nToReturn,
            nToSkip, 0);

        StartReadWatcher();
    }

    void Insert(const char *ns, bson obj) {
        mongo_insert(conn, ns, &obj);
    }

    void Remove(const char *ns, bson cond) {
        mongo_remove(conn, ns, &cond);
    }

    void Update(const char *ns, bson cond, bson obj) {
        mongo_update(conn, ns, &cond, &obj, 0);
    }

    protected:

    static Handle<Value>
    New(const Arguments& args) {
        HandleScope scope;

        Connection *connection = new Connection();
        connection->Wrap(args.This());
        return args.This();
    }

    Connection() : node::EventEmitter() {
        HandleScope scope;
        Handle<Array> r = Array::New();
        results = Persistent<Array>::New(r);

        cursor = false;
        get_more = false;
        buflen = 0;
        buf = bufptr = NULL;
        state = STATE_READ_HEAD;

        ev_init(&read_watcher, io_event);
        read_watcher.data = this;
        ev_init(&write_watcher, io_event);
        write_watcher.data = this;
        ev_init(&connect_watcher, connect_event);
        connect_watcher.data = this;
    }

    static Handle<Value>
    Connect(const Arguments &args) {
        HandleScope scope;

        // XXX check args.Length
        String::Utf8Value host(args[0]->ToString());
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
        connection->Connect(*host, args[1]->Int32Value());

        return Undefined();
    }

    static Handle<Value>
    Close(const Arguments &args) {
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

        HandleScope scope;

        connection->Close();

        return Undefined();
    }

    static Handle<Value>
    Find(const Arguments &args) {
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

        HandleScope scope;

        // TODO assert ns != undefined (args.Length > 0)
        String::Utf8Value ns(args[0]->ToString());

        bson query_bson;
        bson query_fields_bson;
        int nToReturn(0), nToSkip(0);

        if (args.Length() > 1 && !args[1]->IsUndefined()) {
            Local<Object> query(args[1]->ToObject());
            query_bson = encodeObject(query);
        }
        else {
            bson_empty(&query_bson);
        }

        if (args.Length() > 2 && !args[2]->IsUndefined()) {
            Local<Object> query_fields(args[2]->ToObject());
            query_fields_bson = encodeObject(query_fields);
        }
        else {
            bson_empty(&query_fields_bson);
        }

        if (args.Length() > 3 && !args[3]->IsUndefined()) {
            nToReturn = args[3]->Int32Value();
        }

        if (args.Length() > 4 && !args[4]->IsUndefined()) {
            nToSkip = args[4]->Int32Value();
        }

        connection->Find(*ns, &query_bson, &query_fields_bson, nToReturn, nToSkip);

        bson_destroy(&query_bson);
        bson_destroy(&query_fields_bson);
        return Undefined();
    }

    static Handle<Value>
    Insert(const Arguments &args) {
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

        HandleScope scope;

        String::Utf8Value ns(args[0]->ToString());
        // TODO assert ns != undefined (args.Length > 0)

        bson obj;

        // XXX check args > 1
        Local<Object> query(args[1]->ToObject());
        obj = encodeObject(query);

        connection->Insert(*ns, obj);

        bson_destroy(&obj);
        return Undefined();
    }

    static Handle<Value>
    Update(const Arguments &args) {
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

        HandleScope scope;

        String::Utf8Value ns(args[0]->ToString());
        // TODO assert ns != undefined (args.Length > 0)

        bson cond;
        bson obj;

        if (args.Length() > 1 && !args[1]->IsUndefined()) {
            Local<Object> query(args[1]->ToObject());
            cond = encodeObject(query);
        }
        else {
            bson_empty(&cond);
        }

        if (args.Length() > 2 && !args[2]->IsUndefined()) {
            Local<Object> query(args[2]->ToObject());
            obj = encodeObject(query);
        }
        else {
            bson_empty(&obj);
        }

        connection->Update(*ns, cond, obj);

        bson_destroy(&cond);
        bson_destroy(&obj);
        return Undefined();
    }

    static Handle<Value>
    Remove(const Arguments &args) {
        HandleScope scope;
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
        String::Utf8Value ns(args[0]->ToString());
        // TODO assert ns != undefined (args.Length > 0)

        bson cond;
        if (args.Length() > 1 && !args[1]->IsUndefined()) {
            Local<Object> query(args[1]->ToObject());
            cond = encodeObject(query);
        }
        else {
            bson_empty(&cond);
        }

        connection->Remove(*ns, cond);

        bson_destroy(&cond);
        return Undefined();
    }

    void Event(EV_P_ ev_io *w, int revents) {
        if (revents & EV_WRITE) {
            pdebug("!!! got a write event\n");
            StopWriteWatcher();
            if (get_more) {
                RequestMore();
                return;
            }
            else {
                Emit(String::New("ready"), 0, NULL);
                return;
            }
        }
        if (revents & EV_READ) {
            pdebug("!!! got a read event\n");
            ConsumeInput();
            CheckBufferContents();
            return;
        }
        if (revents & EV_ERROR) {
            pdebug("!!! got an error event\n");
        }
    }

    private:

    static void
    connect_event(EV_P_ ev_io *w, int revents) {
        pdebug("!!! got a connect event\n");
        Connection *connection = static_cast<Connection *>(w->data);
        connection->Connected();
    }

    static void
    io_event (EV_P_ ev_io *w, int revents) {
        Connection *connection = static_cast<Connection *>(w->data);
        connection->Event(w, revents);
    }

    mongo_connection conn[1];

    // states
    bool get_more;
    ReadState state;

    mongo_header head;
    mongo_reply_fields fields;
    mongo_cursor *cursor;

    Persistent<Array> results;

    char *buf;
    char *bufptr;
    int32_t buflen;

    ev_io read_watcher;
    ev_io write_watcher;
    ev_io connect_watcher;
};

extern "C" void
init (Handle<Object> target) {
    pdebug("headersize was %d\n", headerSize);
    HandleScope scope;
    ObjectID::Initialize(target);
    Connection::Initialize(target);
}
