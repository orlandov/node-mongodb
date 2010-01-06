#ifndef node_connection_h
#define node_connection_h

#include <mongo/client/dbclient.h>
#include <mongo/stdafx.h>
#include <mongo/util/message.h>
#include <mongo/db/jsobj.h>
#include <mongo/db/json.h>
#include <mongo/util/sock.h>

namespace node_mongo {
    using namespace std;

    class NonBlockingPort : public mongo::AbstractMessagingPort {
    public:
        NonBlockingPort(int sock, SockAddr& farEnd);
        NonBlockingPort();
        virtual ~NonBlockingPort();

        void shutdown();
        
        bool connect(SockAddr& farEnd);
        bool recv(mongo::Message& m);
        void reply(mongo::Message& received, mongo::Message& response, MSGID responseTo);
        void reply(mongo::Message& received, mongo::Message& response);
        bool call(mongo::Message& toSend, mongo::Message& response);
        void say(mongo::Message& toSend, int responseTo = -1);

        void piggyBack( mongo::Message& toSend , int responseTo = -1 );

        virtual unsigned remotePort();
    private:
        int sock;
        PiggyBackData * piggyBackData;
    public:
        SockAddr farEnd;

        friend class PiggyBackData;
    };

    class NonBlockingConnection : public mongo::DBClientConnection {
        auto_ptr<mongo::MessagingPort> p;
        auto_ptr<mongo::SockAddr> server;
        bool failed; // true if some sort of fatal error has ever happened
        bool autoReconnect;
        time_t lastReconnectTry;
        string serverAddress; // remember for reconnects
        void _checkConnection();
        void checkConnection() { if( failed ) _checkConnection(); }
		map< string, pair<string,string> > authCache;
    public:

        /**
           @param _autoReconnect if true, automatically reconnect on a connection failure
           @param cp used by DBClientPaired.  You do not need to specify this parameter
         */
        NonBlockingConnection(bool _autoReconnect=false) :
                failed(false), autoReconnect(_autoReconnect), lastReconnectTry(0) { }

        /** Connect to a Mongo database server.

           If autoReconnect is true, you can try to use the NonBlockingConnection even when
           false was returned -- it will try to connect again.

           @param serverHostname host to connect to.  can include port number ( 127.0.0.1 , 127.0.0.1:5555 )
           @param errmsg any relevant error message will appended to the string
           @return false if fails to connect.
        */
        virtual bool connect(const string &serverHostname, string& errmsg);

        /** Connect to a Mongo database server.  Exception throwing version.
            Throws a UserException if cannot connect.

           If autoReconnect is true, you can try to use the NonBlockingConnection even when
           false was returned -- it will try to connect again.

           @param serverHostname host to connect to.  can include port number ( 127.0.0.1 , 127.0.0.1:5555 )
        */
        void connect(string serverHostname) { 
            string errmsg;
            if( !connect(serverHostname.c_str(), errmsg) ) 
                throw ConnectException(string("can't connect ") + errmsg);
        }

        virtual bool auth(const string &dbname, const string &username, const string &pwd, string& errmsg, bool digestPassword = true);

        /* virtual auto_ptr<NodeV> query(const string &ns, Query query, int nToReturn = 0, int nToSkip = 0,
                                               const BSONObj *fieldsToReturn = 0, int queryOptions = 0) {
            checkConnection();
            return DBClientBase::query( ns, query, nToReturn, nToSkip, fieldsToReturn, queryOptions );
        } */

        /**
           @return true if this connection is currently in a failed state.  When autoreconnect is on, 
                   a connection will transition back to an ok state after reconnecting.
         */
        bool isFailed() const {
            return failed;
        }

        MessagingPort& port() {
            return *p.get();
        }

        string toStringLong() const {
            stringstream ss;
            ss << serverAddress;
            if ( failed ) ss << " failed";
            return ss.str();
        }

        /** Returns the address of the server */
        string toString() {
            return serverAddress;
        }
        
        string getServerAddress() const {
            return serverAddress;
        }

    protected:
        virtual bool call( mongo::Message &toSend, mongo::Message &response, bool assertOk = true );
        virtual void say( mongo::Message &toSend );
        virtual void checkResponse( const char *data, int nReturned );
    };
}

#endif
