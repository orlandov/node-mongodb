#include <mongo/stdafx.h>
#include <mongo/db/pdfile.h>
#include <mongo/client/dbclient.h>
#include <mongo/util/builder.h>
#include <mongo/db/jsobj.h>
#include <mongo/db/json.h>
#include <mongo/db/instance.h>
#include <mongo/util/md5.h>
#include <mongo/db/dbmessage.h>
#include <mongo/db/cmdline.h>

#include "connection.h"

namespace node_mongo {
    using namespace mongo;

    NonBlockingPort::NonBlockingPort(int _sock, SockAddr& _far) : sock(_sock), piggyBackData(0), farEnd(_far) {
        ports.insert(this);
    }

    NonBlockingPort::NonBlockingPort() {
        ports.insert(this);
        sock = -1;
        piggyBackData = 0;
    }

    void NonBlockingPort::shutdown() {
        if ( sock >= 0 ) {
            closesocket(sock);
            sock = -1;
        }
    }

    NonBlockingPort::~NonBlockingPort() {
        if ( piggyBackData )
            delete( piggyBackData );
        shutdown();
        ports.erase(this);
    }

    bool NonBlockingPort::connect(SockAddr& _far)
    {
        farEnd = _far;

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if ( sock == INVALID_SOCKET ) {
            log() << "ERROR: connect(): invalid socket? " << OUTPUT_ERRNO << endl;
            return false;
        }

#if 0
        long fl = fcntl(sock, F_GETFL, 0);
        assert( fl >= 0 );
        fl |= O_NONBLOCK;
        fcntl(sock, F_SETFL, fl);

        int res = ::connect(sock, (sockaddr *) &farEnd.sa, farEnd.addressSize);
        if ( res ) {
            if ( errno == EINPROGRESS )
                closesocket(sock);
            sock = -1;
            return false;
        }

#endif

        ConnectBG bg;
        bg.sock = sock;
        bg.farEnd = farEnd;
        bg.go();

        // int res = ::connect(sock, (sockaddr *) &farEnd.sa, farEnd.addressSize);
        if ( bg.wait(5000) ) {
            if ( bg.res ) {
                closesocket(sock);
                sock = -1;
                return false;
            }
        }
        else {
            // time out the connect
            closesocket(sock);
            sock = -1;
            bg.wait(); // so bg stays in scope until bg thread terminates
            return false;
        }

        disableNagle(sock);

#ifdef SO_NOSIGPIPE
        // osx
        const int one = 1;
        setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(int));
#endif

        return true;
    }

    void NonBlockingPort::reply(mongo::Message& received, mongo::Message& response) {
        say(/*received.from, */response, received.data->id);
    }

    void NonBlockingPort::reply(mongo::Message& received, mongo::Message& response, MSGID responseTo) {
        say(/*received.from, */response, responseTo);
    }

    void NonBlockingPort::say(mongo::Message& toSend, int responseTo) {
        mmm( out() << "*  say() sock:" << this->sock << " thr:" << GetCurrentThreadId() << endl; )
        toSend.data->id = nextMessageId();
        toSend.data->responseTo = responseTo;

        int x = -100;

        if ( piggyBackData && piggyBackData->len() ) {
            mmm( out() << "*     have piggy back" << endl; )
            if ( ( piggyBackData->len() + toSend.data->len ) > 1300 ) {
                // won't fit in a packet - so just send it off
                piggyBackData->flush();
            }
            else {
                piggyBackData->append( toSend );
                x = piggyBackData->flush();
            }
        }

        if ( x == -100 )
            x = ::send(sock, (char*)toSend.data, toSend.data->len , portSendFlags );
        
        if ( x <= 0 ) {
            log() << "NonBlockingPort say send() " << OUTPUT_ERRNO << ' ' << farEnd.toString() << endl;
            throw SocketException();
        }

    }

    void NonBlockingPort::piggyBack( mongo::Message& toSend , int responseTo ) {

        if ( toSend.data->len > 1300 ) {
            // not worth saving because its almost an entire packet
            say( toSend );
            return;
        }

        // we're going to be storing this, so need to set it up
        toSend.data->id = nextMessageId();
        toSend.data->responseTo = responseTo;

        if ( ! piggyBackData )
            piggyBackData = new PiggyBackData( this );

        piggyBackData->append( toSend );
    }

    bool NonBlockingConnection::auth(const string &dbname, const string &username, const string &password_text, string& errmsg, bool digestPassword) {
        string password = password_text;
        if( digestPassword ) 
            password = createPasswordDigest( username , password_text );

        if( autoReconnect ) {
            /* note we remember the auth info before we attempt to auth -- if the connection is broken, we will 
               then have it for the next autoreconnect attempt. 
               */
            pair<string,string> p = pair<string,string>(username, password);
            authCache[dbname] = p;
        }

        return DBClientBase::auth(dbname, username, password.c_str(), errmsg, false);
    }

    bool NonBlockingConnection::connect(const string &_serverAddress, string& errmsg) {
        serverAddress = _serverAddress;

        string ip;
        int port;
        size_t idx = serverAddress.find( ":" );
        if ( idx != string::npos ) {
            port = strtol( serverAddress.substr( idx + 1 ).c_str(), 0, 10 );
            ip = serverAddress.substr( 0 , idx );
            ip = hostbyname(ip.c_str());
        } else {
            port = CmdLine::DefaultDBPort;
            ip = hostbyname( serverAddress.c_str() );
        }
        massert( "Unable to parse hostname", !ip.empty() );

        // we keep around SockAddr for connection life -- maybe
        // NonBlockingPort

        // requires that?
        server = auto_ptr<SockAddr>(new SockAddr(ip.c_str(), port));
        p = auto_ptr<NonBlockingPort
>(new NonBlockingPort());

        if ( !p->connect(*server) ) {
            stringstream ss;
            ss << "couldn't connect to server " << serverAddress << " " << ip << ":" << port;
            errmsg = ss.str();
            failed = true;
            return false;
        }
        return true;
    }

    bool NonBlockingConnection::call( mongo::Message &toSend, mongo::Message &response, bool assertOk ) {
        /* todo: this is very ugly messagingport::call returns an error code AND can throw 
           an exception.  we should make it return void and just throw an exception anytime 
           it fails
           */
        try { 
            if ( !port().call(toSend, response) ) {
                failed = true;
                if ( assertOk )
                    massert("dbclient error communicating with server", false);
                return false;
            }
        }
        catch( SocketException & ) { 
            failed = true;
            throw;
        }
        return true;
    }

    void NonBlockingConnection::say( mongo::Message &toSend ) {
        checkConnection();
        try { 
            port().say( toSend );
        } catch( SocketException & ) { 
            failed = true;
            throw;
        }
    }
}
