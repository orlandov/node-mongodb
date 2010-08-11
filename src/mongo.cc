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

#define DEBUGMODE 1
#define pdebug(...) do{if(DEBUGMODE)printf(__VA_ARGS__);}while(0)

const int chunk_size(4094);
const int headerSize(sizeof(mongo_header) + sizeof(mongo_reply_fields));

using namespace v8;

void setNonBlocking(int sock) {
    int sockflags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, sockflags | O_NONBLOCK);
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
        NODE_SET_PROTOTYPE_METHOD(t, "close",   Close);
        NODE_SET_PROTOTYPE_METHOD(t, "find",    Find);
        NODE_SET_PROTOTYPE_METHOD(t, "insert",  Insert);
        NODE_SET_PROTOTYPE_METHOD(t, "update",  Update);
        NODE_SET_PROTOTYPE_METHOD(t, "remove",  Remove);

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

    void MongoCreateSocket() {
        conn->sock = 0;
        conn->connected = 0;

        memset(conn->sa.sin_zero, 0, sizeof(conn->sa.sin_zero));
        conn->sa.sin_family = AF_INET;
        conn->sa.sin_port = htons(conn->left_opts->port);
        conn->sa.sin_addr.s_addr = inet_addr(conn->left_opts->host);
        conn->addressSize = sizeof(conn->sa);

        conn->sock = socket( AF_INET, SOCK_STREAM, 0 );
        if (conn->sock <= 0){
            //return mongo_conn_no_socket;
            // throw exception here?
        }

        setNonBlocking(conn->sock);
        int res = connect(conn->sock, (struct sockaddr*) &conn->sa, conn->addressSize);

        // make sure we've gotten a non-blocking connection
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

        Ref();
        StartWriteWatcher();
	StartReadWatcher();
    }

    void Close() {
        pdebug("--- in Close()\n");
        HandleScope scope;
        close = true;
    }

    void reallyClose() {
        HandleScope scope;
        StopWriteWatcher();
        StopReadWatcher();

        if (writebuf) {
            delete [] writebuf;
            writebuf = NULL;
            writebuflen = 0;
        }

        if (buf) {
            delete [] buf;
            buf = NULL;
            buflen = 0;
        }

        buf = writebuf = NULL;

        mongo_destroy(conn);

        Emit(String::New("close"), 0, NULL);

        Unref();
    }

    void CheckBuffer() {
        if (buflen < headerSize) return;

        mongo_header reply_head;
        mongo_reply_fields reply_fields;

        memcpy(&reply_head, bufptr, sizeof(reply_head));
        bufptr += sizeof(reply_head);
        memcpy(&reply_fields, bufptr, sizeof(reply_fields));
        bufptr += sizeof(reply_fields);

        int len;
        bson_little_endian32(&len, &reply_head.len);

        if (len-buflen == 0) {
            // we've gotten the full response
            ParseReply(reply_head, reply_fields);

            delete [] buf;
            buf = bufptr = NULL;
            buflen = 0;

            StopReadWatcher();
            StartWriteWatcher();
        }
    }

    void ParseReply(mongo_header reply_head, mongo_reply_fields reply_fields) {
        HandleScope scope;

        int len;
        bson_little_endian32(&len, &reply_head.len);

        char replybuf[len];

        mongo_reply *reply = reinterpret_cast<mongo_reply*>(replybuf);

        reply->head.len = len;
        bson_little_endian32(&reply->head.id, &reply_head.id);
        bson_little_endian32(&reply->head.responseTo, &reply_head.responseTo);
        bson_little_endian32(&reply->head.op, &reply_head.op);

        bson_little_endian32(&reply->fields.flag, &reply_fields.flag);
        bson_little_endian64(&reply->fields.cursorID, &reply_fields.cursorID);
        bson_little_endian32(&reply->fields.start, &reply_fields.start);
        bson_little_endian32(&reply->fields.num, &reply_fields.num);

        memcpy(&reply->objs, bufptr, len-headerSize);

        cursor->mm = reply;
        cursor->current.data = NULL;

        for (int i = results->Length(); AdvanceCursor(); i++){
            Local<Value> val = decodeObjectStr(cursor->current.data);
            results->Set(Integer::New(i), val);
        }

        // if this is the last cursor
        if (!cursor->mm || ! reply_fields.cursorID) {
            FreeCursor();
            get_more = false;
            EmitResults();
            results.Dispose();
            results.Clear();
            results = Persistent<Array>::New(Array::New());
        }
    }

    void FreeCursor() {
        free((void*)cursor->ns);
        free(cursor);
        cursor = NULL;
    }

    void EmitResults() {
        Emit(String::New("result"), 1, reinterpret_cast<Handle<Value> *>(&results));
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
            get_more = true;

            // indicate that this is the last result
            return false;
        } else {
            // advance cursor by one object
            bson_init(&cursor->current, bson_addr, 0);

            return true;
        }
        return false;
    }

    void BufferMessageToSend(mongo_message *mm) {
        mongo_header head;
        bson_little_endian32(&head.len, &mm->head.len);
        bson_little_endian32(&head.id, &mm->head.id);
        bson_little_endian32(&head.responseTo, &mm->head.responseTo);
        bson_little_endian32(&head.op, &mm->head.op);

        int size = mm->head.len;
        pdebug("buffering message of size %d\n", size);

        char *tmp = new char[writebuflen+size];

        if (writebuf) {
            memcpy(tmp, writebuf, writebuflen);
        }

        memcpy(tmp+writebuflen, &head, sizeof(head));
        memcpy(tmp+writebuflen+sizeof(head), &mm->data, size-sizeof(head));
        free(mm);

        int ptrdelta = writebufptr - writebuf;

        if (writebuf) {
            delete [] writebuf;
        }

        writebuflen = writebuflen + size;
        writebuf = tmp;
        writebufptr = tmp + ptrdelta;
        pdebug("write buf is of size %d\n", writebuflen);
        pdebug("est lenRem = %d\n", writebuflen-ptrdelta);
        pdebug("wbuf diff = %d\n", ptrdelta);
        StartWriteWatcher();
    }

    void WriteSendBuffer() {
        pdebug("going to write buffer\n");

        int sock = conn->sock;
        int lenRemaining = writebuflen-(writebufptr-writebuf);

        pdebug("remaining: %d\n", lenRemaining);
        while (lenRemaining) {
            pdebug("trying to write %d\n", lenRemaining);
            int sent = write(sock, writebufptr, lenRemaining);
            pdebug("write = %d\n", sent);
            if (sent == -1) {
                if (errno == EAGAIN) {
                    // we need to set the write watcher again and continue
                    // later
                    pdebug("EAGAIN\n");

                    StartWriteWatcher();
                    return;
                }
                else {
                    pdebug("errorno was %d\n", errno);
                }
            }
            writebufptr += sent;
            lenRemaining -= sent;
        }
        if (!lenRemaining) {
            delete [] writebuf;
            writebufptr = writebuf = NULL;
            writebuflen = 0;
        }
        pdebug("done! write buf is of size %d\n", writebuflen);
        pdebug("done! est lenRem = %d\n", writebuflen-(writebufptr-writebuf));
        pdebug("done! wbuf diff = %d\n", (writebufptr-writebuf));
        StopWriteWatcher();
    }

    void ConsumeInput() {
        char *tmp;
        char readbuf[chunk_size];
        int32_t readbuflen;

        for (;;) {
            readbuflen = read(conn->sock, readbuf, chunk_size);

            if (readbuflen == -1 && errno == EAGAIN) {
                // no more input to consume
                pdebug("len == -1 && errno == EAGAIN\n");
                return;
            }
            else if (readbuflen <= 0) {
                // socket problem?
                pdebug("length error on read %d errno = %d\n", readbuflen, errno);
		reallyClose();
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
        data = mongo_data_append64(data, &(cursor->mm->fields.cursorID));

        BufferMessageToSend(mm);
    }


    bool Find(const char *ns, bson *query=0, bson *query_fields=0,
              int nToReturn=0, int nToSkip=0, int options=0) {
        StartReadWatcher();
        assert(!close);
        cursor = static_cast<mongo_cursor*>(
                     bson_malloc(sizeof(mongo_cursor)));

        int sl = strlen(ns)+1;
        cursor->ns = static_cast<char*>(bson_malloc(sl));

        memcpy(static_cast<void*>(const_cast<char*>(cursor->ns)), ns, sl);
        cursor->conn = conn;

        char * data;
        mongo_message * mm = mongo_message_create( 16 + /* header */
                                                   4 + /*  options */
                                                   sl + /* ns */
                                                   4 + 4 + /* skip,return */
                                                   bson_size( query ) +
                                                   bson_size( query_fields ) ,
                                                   0 , 0 , mongo_op_query );

        data = &mm->data;
        data = mongo_data_append32(data, &options);
        data = mongo_data_append(data, ns, strlen(ns)+ 1);
        data = mongo_data_append32(data, &nToSkip);
        data = mongo_data_append32(data, &nToReturn);
        data = mongo_data_append(data, query->data, bson_size(query));
        if (query_fields)
            data = mongo_data_append(data, query_fields->data, bson_size(query_fields));

        bson_fatal_msg((data == ((char*)mm) + mm->head.len), "query building fail!");

        BufferMessageToSend(mm);
    }

    void Insert(const char *ns, bson obj) {
        char * data;
        mongo_message *mm = mongo_message_create( 16 /* header */
                                                + 4 /* ZERO */
                                                + strlen(ns)
                                                + 1 + bson_size(&obj)
                                                , 0, 0, mongo_op_insert);

        data = &mm->data;
        data = mongo_data_append32(data, &zero);
        data = mongo_data_append(data, ns, strlen(ns) + 1);
        data = mongo_data_append(data, obj.data, bson_size(&obj));

        BufferMessageToSend(mm);
    }

    void Remove(const char *ns, bson cond) {
        char * data;
        mongo_message * mm = mongo_message_create( 16 /* header */
                                                 + 4  /* ZERO */
                                                 + strlen(ns) + 1
                                                 + 4  /* ZERO */
                                                 + bson_size(&cond)
                                                 , 0 , 0 , mongo_op_delete );

        data = &mm->data;
        data = mongo_data_append32(data, &zero);
        data = mongo_data_append(data, ns, strlen(ns) + 1);
        data = mongo_data_append32(data, &zero);
        data = mongo_data_append(data, cond.data, bson_size(&cond));
        BufferMessageToSend(mm);
    }

    void Update(const char *ns, bson cond, bson op, int flags=0) {
        char * data;
        mongo_message * mm = mongo_message_create( 16 /* header */
                                                 + 4  /* ZERO */
                                                 + strlen(ns) + 1
                                                 + 4  /* flags */
                                                 + bson_size(&cond)
                                                 + bson_size(&op)
                                                 , 0 , 0 , mongo_op_update );

        data = &mm->data;
        data = mongo_data_append32(data, &zero);
        data = mongo_data_append(data, ns, strlen(ns) + 1);
        data = mongo_data_append32(data, &flags);
        data = mongo_data_append(data, cond.data, bson_size(&cond));
        data = mongo_data_append(data, op.data, bson_size(&op));

        BufferMessageToSend(mm);
    }

    protected:

    static Handle<Value>
    New(const Arguments& args) {
        HandleScope scope;

        // XXX where should this be deleted?
        Connection *connection = new Connection();
        connection->Wrap(args.This());
        return args.This();
    }

    ~Connection() {
    }

    Connection() : node::EventEmitter() {
        HandleScope scope;
        results = Persistent<Array>::New(Array::New());

        close = false;
        cursor = false;
        get_more = false;
        buflen = writebuflen = 0;
        buf = bufptr = writebuf = writebufptr = NULL;

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
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

        // XXX check args.Length
        String::Utf8Value host(args[0]->ToString());
        connection->Connect(*host, args[1]->Int32Value());

        return Undefined();
    }

    static Handle<Value>
    Close(const Arguments &args) {
        HandleScope scope;
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

        connection->Close();

        return Undefined();
    }

    static Handle<Value>
    Find(const Arguments &args) {
        HandleScope scope;
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

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
        HandleScope scope;
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

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
        HandleScope scope;
        Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

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
        if (!args[0]->IsString()) {
            return ThrowException(
                Exception::Error(
                    String::New("ns must be specified")));
        }
        String::Utf8Value ns(args[0]->ToString());


        bson cond;
        if (args.Length() > 1 && args[1]->IsObject()) {
            Local<Object> query(args[1]->ToObject());
            cond = encodeObject(query);
        }
        else if (args.Length() > 1 && args[1]->IsUndefined()) {
            bson_empty(&cond);
        }
        else if (args.Length() > 1 && !args[1]->IsObject()) {
            return ThrowException(
                Exception::Error(
                    String::New("Condition must be an object")));
        }

        connection->Remove(*ns, cond);

        bson_destroy(&cond);
        return Undefined();
    }

    void Event(EV_P_ ev_io *w, int revents) {
        if (!conn->connected) {
            StopReadWatcher();
            StopWriteWatcher();
	    reallyClose();
            return;
        };
        pdebug("event %d %d\n", conn->connected, close ? 1 : 0);
        if (revents & EV_READ) {
            pdebug("!!! got a read event\n");
            StopReadWatcher();
            ConsumeInput();
            CheckBuffer();
        }
        if (revents & EV_WRITE) {
            pdebug("!!! got a write event\n");
            pdebug("!!! writebuflen = %d\n", writebuflen);
            if (writebuflen) {
                pdebug("things to write\n");
                WriteSendBuffer();
            }
            else {
                StopWriteWatcher();
            }

            if (get_more) {
                RequestMore();
            }
            else {
                Emit(String::New("ready"), 0, NULL);
            }
        }
        if (close) {
            pdebug("!!! really closing %d\n", close);
            reallyClose();
            close = false;
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
    bool close;

    mongo_cursor *cursor;

    Persistent<Array> results;

    char *buf;
    char *bufptr;
    int buflen;

    char *writebuf;
    char *writebufptr;
    int writebuflen;

    ev_io read_watcher;
    ev_io write_watcher;
    ev_io connect_watcher;
};

extern "C" void
init (Handle<Object> target) {
    HandleScope scope;

    target->Set(
        String::New("encode"),
        FunctionTemplate::New(encode)->GetFunction());
    target->Set(
        String::New("decode"),
        FunctionTemplate::New(decode)->GetFunction());
    ObjectID::Initialize(target);
    Connection::Initialize(target);
}
