#ifndef node_mongo_h
#define node_mongo_h

#include <v8.h>
#include <node.h>
#include <node_events.h>

#include <mongo/client/dbclient.h>
#include <mongo/stdafx.h>
#include <mongo/util/message.h>
#include <mongo/db/jsobj.h>
#include <mongo/db/json.h>

#include "cursor.h"

extern "C" {
    #define MONGO_HAVE_STDINT
    #include <bson.h>
    #include <mongo.h>
    #include <platform_hacks.h>
}

namespace node_mongo {

using namespace std;
using namespace v8;

enum ReadState {
    STATE_READ_HEAD,
    STATE_READ_MESSAGE,
    STATE_PARSE_MESSAGE,
};

const char HEADER_SIZE = mongo::MsgDataHeaderSize;
const int chunk_size = 4094;
#define NS "test.widgets"

class Connection : public node::EventEmitter {
    public:

    static void
    Initialize (Handle<Object> target);

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

    bool Connect(const char *host, const int port);
    void CheckBufferContents(void);
    void ParseMessage(void);
    void GetResults();
    bool ConsumeInput(void);
    bool SendGetMore(void);
    bool Find(void);

    protected:

    Connection();
    static Handle<Value> New(const Arguments& args);
    static Handle<Value> Connect(const Arguments &args);
    static Handle<Value> Find(const Arguments &args);
    void Event(int revents);

    private:

    static void
    io_event (EV_P_ ev_io *w, int revents);

    // states
    bool get_more;
    ReadState state;
    auto_ptr<mongo::DBClientConnection> conn;

    auto_ptr<NodeMongoCursor> node_cursor;

    Persistent<Array> results;

    char *buf;
    char *bufptr;
    int32_t buflen;

    ev_io read_watcher;
    ev_io write_watcher;
};

}
#endif
