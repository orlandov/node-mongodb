#include <mongo/stdafx.h>
#include <mongo/client/dbclient.h>
#include <mongo/db/dbmessage.h>
#include <mongo/util/message.h>
#include <mongo/db/jsobj.h>
#include <mongo/db/json.h>
#include "cursor.h"

namespace mongo {

// bool NodeMongoCursor::init() {;
//     Message toSend;
//     if ( !cursorId ) {
//         assembleRequest( ns, query, nToReturn, nToSkip, fieldsToReturn, opts, toSend );
//     } else {
//         BufBuilder b;
//         b.append( opts );
//         b.append( ns.c_str() );
//         b.append( nToReturn );
//         b.append( cursorId );
//         toSend.setData( dbGetMore, b.buf(), b.len() );
//     }
//     if ( !connector->call( toSend, *m, false ) )
//         return false;
//     dataReceived();
//     return true;
// }
/* ***************** */
bool NodeMongoCursor::init() {
    printf("cursorID was %llu", cursorId);
    if (! cursorId) {
        printf("assembling request\n");
        assembleRequest(ns, query, nToReturn, nToSkip, fieldsToReturn, opts, toSend);
    }
    else {
        printf("building getmore buffer\n");
        BufBuilder b;
        b.append(opts);
        b.append(ns.c_str());
        b.append(nToReturn);
        b.append(cursorId);
        toSend.setData(dbGetMore, b.buf(), b.len());
    }

    return true;
}
bool NodeMongoCursor::reallySend() {
    if (!connector->call(toSend, *m, false)) {
        return false;
    }
}
/* ***************** */
void NodeMongoCursor::requestMore() {
    assert( cursorId && pos == nReturned );

    BufBuilder b;
    b.append(opts);
    b.append(ns.c_str());
    b.append(nToReturn);
    b.append(cursorId);

    Message toSend;
    toSend.setData(dbGetMore, b.buf(), b.len());
    auto_ptr<Message> response(new Message());
    connector->call( toSend, *response );

    m = response;
    dataReceived();
}

void NodeMongoCursor::dataReceived() {
    QueryResult *qr = (QueryResult *) m->data;
    resultFlags = qr->resultFlags();
    if ( qr->resultFlags() & QueryResult::ResultFlag_CursorNotFound ) {
        // cursor id no longer valid at the server.
        assert( qr->cursorId == 0 );
        cursorId = 0; // 0 indicates no longer valid (dead)
        // TODO: should we throw a UserException here???
    }
    if ( cursorId == 0 || ! ( opts & Option_CursorTailable ) ) {
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

BSONObj NodeMongoCursor::next() {
    assert( more() );
    pos++;
    BSONObj o(data);
    data += o.objsize();
    return o;
}

NodeMongoCursor::~NodeMongoCursor() {
    if ( cursorId && ownCursor_ ) {
        BufBuilder b;
        b.append( (int)0 ); // reserved
        b.append( (int)1 ); // number
        b.append( cursorId );

        Message m;
        m.setData( dbKillCursors , b.buf() , b.len() );

        connector->sayPiggyBack( m );
    }

}

}
