#include <mongo/stdafx.h>
#include <mongo/client/dbclient.h>
#include <mongo/db/dbmessage.h>
#include <mongo/util/message.h>
#include <mongo/db/jsobj.h>
#include <mongo/db/json.h>

#include "cursor.h"

namespace node_mongo {

void assembleRequest( const string &ns, mongo::BSONObj query, int nToReturn, int nToSkip, const mongo::BSONObj *fieldsToReturn, int queryOptions, mongo::Message &toSend ) {
    CHECK_OBJECT( query , "assembleRequest query" );
    // see query.h for the protocol we are using here.
    mongo::BufBuilder b;
    int opts = queryOptions;
    b.append(opts);
    b.append(ns.c_str());
    b.append(nToSkip);
    b.append(nToReturn);
    query.appendSelfToBufBuilder(b);
    if ( fieldsToReturn )
        fieldsToReturn->appendSelfToBufBuilder(b);
    toSend.setData(mongo::dbQuery, b.buf(), b.len());
}

bool NodeMongoCursor::init() {
    printf("cursorID was %llu", cursorId);
    if (! cursorId) {
        printf("assembling request\n");
        assembleRequest(ns, query, nToReturn, nToSkip, fieldsToReturn, opts, *toSend);
    }
    else {
        printf("building getmore buffer\n");
        mongo::BufBuilder b;
        b.append(opts);
        b.append(ns.c_str());
        b.append(nToReturn);
        b.append(cursorId);
        toSend->setData(mongo::dbGetMore, b.buf(), b.len());
    }

    return true;
}
bool NodeMongoCursor::reallySend() {
    connector->say(*toSend);
//     if (!connector->call(toSend, *m, false)) {
//         return false;
//     }
}
/* ***************** */
void NodeMongoCursor::requestMore() {
    assert( cursorId && pos == nReturned );

    mongo::BufBuilder b;
    b.append(opts);
    b.append(ns.c_str());
    b.append(nToReturn);
    b.append(cursorId);

    mongo::Message toSend;
    toSend.setData(mongo::dbGetMore, b.buf(), b.len());
    auto_ptr<mongo::Message> response(new mongo::Message());
    connector->call( toSend, *response );

    m = response;
    dataReceived();
}

void NodeMongoCursor::dataReceived() {
    mongo::QueryResult *qr = (mongo::QueryResult *) m->data;
    resultFlags = qr->resultFlags();
    if ( qr->resultFlags() & mongo::QueryResult::ResultFlag_CursorNotFound ) {
        // cursor id no longer valid at the server.
        assert( qr->cursorId == 0 );
        cursorId = 0; // 0 indicates no longer valid (dead)
        // TODO: should we throw a UserException here???
    }
    if ( cursorId == 0 || ! ( opts & mongo::Option_CursorTailable ) ) {
        // only set initially: we don't want to kill it on end of data
        // if it's a tailable cursor
        cursorId = qr->cursorId;
    }
    nReturned = qr->nReturned;
    pos = 0;
    data = qr->data();

    connector->checkResponse( data, nReturned );
    /* this assert would fire the way we currently work:
       assert( nReturned || cursorId == 0 );
       */
}

    bool NodeMongoCursor::more() {
        if ( pos < nReturned )
            return true;

        if ( cursorId == 0 )
            return false;

        requestMore();
        return pos < nReturned;
    }

mongo::BSONObj NodeMongoCursor::next() {
    assert( more() );
    pos++;
    mongo::BSONObj o(data);
    data += o.objsize();
    return o;
}

NodeMongoCursor::~NodeMongoCursor() {
    if ( cursorId && ownCursor_ ) {
        mongo::BufBuilder b;
        b.append( (int)0 ); // reserved
        b.append( (int)1 ); // number
        b.append( cursorId );

        mongo::Message m;
        m.setData( mongo::dbKillCursors , b.buf() , b.len() );

        connector->sayPiggyBack( m );
    }
}

}
