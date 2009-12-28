#ifndef node_cursor_h
#define node_cursor_h
#include <mongo/client/dbclient.h>
#include <mongo/stdafx.h>
#include <mongo/util/message.h>
#include <mongo/db/jsobj.h>
#include <mongo/db/json.h>

#include "mongo.h"

namespace mongo {

extern void assembleRequest( const string &ns, BSONObj query, int nToReturn, int nToSkip, const BSONObj *fieldsToReturn, int queryOptions, Message &toSend );

/** Queries return a cursor object */
class NodeMongoCursor : boost::noncopyable {
    public:
    bool init();
    bool reallySend();
    /** if true, safe to call next() */
    bool more();

    /** next
      @return next object in the result cursor.
      on an error at the remote server, you will get back:
      { $err: <string> }
      if you do not want to handle that yourself, call nextSafe().
      */
    BSONObj next();

    /** throws AssertionException if get back { $err : ... } */
    BSONObj nextSafe() {
        BSONObj o = next();
        BSONElement e = o.firstElement();
        assert( strcmp(e.fieldName(), "$err") != 0 );
        return o;
    }

    /**
      iterate the rest of the cursor and return the number if items
      */
    int itcount(){
        int c = 0;
        while ( more() ){
            next();
            c++;
        }
        return c;
    }

    /** cursor no longer valid -- use with tailable cursors.
      note you should only rely on this once more() returns false;
      'dead' may be preset yet some data still queued and locally
      available from the dbclientcursor.
      */
    bool isDead() const {
        return cursorId == 0;
    }

    bool tailable() const {
        return (opts & Option_CursorTailable) != 0;
    }

    bool hasResultFlag( int flag ){
        return (resultFlags & flag) != 0;
    }


    public:
    NodeMongoCursor( DBConnector *_connector, const string &_ns, BSONObj _query, int _nToReturn,
            int _nToSkip, const BSONObj *_fieldsToReturn, int queryOptions ) :
        connector(_connector),
        ns(_ns),
        query(_query),
        nToReturn(_nToReturn),
        nToSkip(_nToSkip),
        fieldsToReturn(_fieldsToReturn),
        opts(queryOptions),
        m(new Message()),
        cursorId(0),
        nReturned(),
        pos(),
        data(),
        ownCursor_( true ) {
        }

    /* NodeMongoCursor( DBConnector *_connector, const string &_ns, long long _cursorId, int _nToReturn, int options ) :
        connector(_connector),
        ns(_ns),
        nToReturn( _nToReturn ),
        opts( options ),
        m(new Message()),
        cursorId( _cursorId ),
        nReturned(),
        pos(),
        data(),
        ownCursor_( true ) {
        }             */

    virtual ~NodeMongoCursor();

    long long getCursorId() const { return cursorId; }
    void decouple() { ownCursor_ = false; }

    private:
    Message toSend;
    DBConnector *connector;
    string ns;
    BSONObj query;
    int nToReturn;
    int nToSkip;
    const BSONObj *fieldsToReturn;
    int opts;
    auto_ptr<Message> m;

    int resultFlags;
    long long cursorId;
    int nReturned;
    int pos;
    const char *data;
    void dataReceived();
    void requestMore();
    bool ownCursor_;
};

// class NodeMongoCursor : public mongo::DBClientCursor {
//     public:
//         bool init() {
//             Message toSend;
//             if (! cursorId) {
//                 printf("assembling request\n");
//                 assembleRequest(ns, query, nToReturn, nToSkip, fieldsToReturn, opts, toSend);
//             }
//             else {
//                 printf("building getmore buffer\n");
//                 BufBuilder b;
//                 b.append(opts);
//                 b.append(ns.c_str());
//                 b.append(nToReturn);
//                 b.append(cursorId);
//                 toSend.setData(dbGetMore, b.buf(), b.len());
//             }
//         }
//         bool reallySend() {
//             if (!connector->call(toSend, *m, false)) {
//                 return false;
//             }
//         }
// }

// class NodeMongoCursor : public mongo::DBClientCursor {
//     public:
//         bool init() {
//             Message toSend;
//             if (! cursorId) {
//                 printf("assembling request\n");
//                 assembleRequest(ns, query, nToReturn, nToSkip, fieldsToReturn, opts, toSend);
//             }
//             else {
//                 printf("building getmore buffer\n");
//                 BufBuilder b;
//                 b.append(opts);
//                 b.append(ns.c_str());
//                 b.append(nToReturn);
//                 b.append(cursorId);
//                 toSend.setData(dbGetMore, b.buf(), b.len());
//             }
//         }
//         bool reallySend() {
//             if (!connector->call(toSend, *m, false)) {
//                 return false;
//             }
//         }
// }
    
}
#endif
