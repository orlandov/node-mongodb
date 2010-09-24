#ifndef _mongoconnection_h_
#define _mongoconnection_h_

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


typedef struct {
  char *messageBuf;
  mongo_header messageHeader;
  mongo_reply_fields messageReply;
  unsigned int messageLen;
  unsigned int bufferLen;
  char *index;
} MongoMessage;

class MongoConnection
{
public:
  MongoConnection();
  ~MongoConnection();

  void connect(const char *host, const int32_t port);
  void disconnect();

  bool isConnected();

  // (TODO: soon to be) virtual functions for inheriting classes to implement
  virtual void onConnected();
  virtual void onResults(MongoMessage *message);
  virtual void onReady();
  virtual void onClose();

  // IO
  void WriteMessage(mongo_message *message);

  mongo_connection m_connection[1];
 private:
  bool m_connected;

  // ev watchers
  ev_io read_watcher;
  ev_io write_watcher;
  ev_io connect_watcher;

  void ReadWatcher(bool state);
  void WriteWatcher(bool state);
  void ConnectWatcher(bool state);

  // ev interfaces
  static void ConnectEventProxy(EV_P_ ev_io *w, int revents);
  static void IOEventProxy(EV_P_ ev_io *w, int revents);

  // instance event handlers
  void IOEvent(int revents);
  void ConnectEvent(int revents);


  // current buffers
  MongoMessage m_inboundBuffer;
  MongoMessage m_outboundBuffer;

  // internal IO
  void WriteData();
  void ReadData();
};

#endif
