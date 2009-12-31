#ifndef node_cursor_h
#define node_cursor_h
#include <mongo/client/dbclient.h>
#include <mongo/stdafx.h>
#include <mongo/util/message.h>
#include <mongo/db/jsobj.h>
#include <mongo/db/json.h>

namespace node_mongo {
using namespace std;

void assembleRequest( const string &ns, mongo::BSONObj query, int nToReturn, int nToSkip, const mongo::BSONObj *fieldsToReturn, int queryOptions, mongo::Message &toSend );

/** Queries return a cursor object */
class NodeMongoCursor {
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
    mongo::BSONObj next();

    /** throws AssertionException if get back { $err : ... } */
    mongo::BSONObj nextSafe() {
        mongo::BSONObj o = next();
        mongo::BSONElement e = o.firstElement();
        assert( strcmp(e.fieldName(), "$err") != 0 );
        return o;
    }

    void setData(mongo::MsgData *d) {
        m->reset();
        m->setData(d, false);
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
        return (opts & mongo::Option_CursorTailable) != 0;
    }

    bool hasResultFlag( int flag ){
        return (resultFlags & flag) != 0;
    }


    public:
    NodeMongoCursor(
            mongo::DBConnector *_connector,
            const string &_ns,
            mongo::BSONObj _query,
            int _nToReturn,
            int _nToSkip,
            const mongo::BSONObj *_fieldsToReturn,
            int queryOptions ) :
        connector(_connector),
        ns(_ns),
        query(_query),
        nToReturn(_nToReturn),
        nToSkip(_nToSkip),
        fieldsToReturn(_fieldsToReturn),
        opts(queryOptions),
        m(new mongo::Message()),
        cursorId(0),
        nReturned(),
        pos(),
        data(),
        toSend(new mongo::Message()),
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
    void dataReceived();
    void requestMore();

    private:
    auto_ptr<mongo::Message> toSend;
    mongo::DBConnector *connector;
    string ns;
    mongo::BSONObj query;
    int nToReturn;
    int nToSkip;
    const mongo::BSONObj *fieldsToReturn;
    int opts;
    auto_ptr<mongo::Message> m;

    int resultFlags;
    long long cursorId;
    int nReturned;
    int pos;
    const char *data;
    bool ownCursor_;
};

}
#endif
