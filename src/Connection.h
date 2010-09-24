#ifndef _mongo_h_
#define _mongo_h_

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

#include "MongoConnection.h"

extern "C" {
#define MONGO_HAVE_STDINT
#include <bson.h>
#include <mongo.h>
#include <platform_hacks.h>
}
#include "bson.h"

using namespace v8;

class Connection : public node::EventEmitter, MongoConnection
{
 public:
  // v8 interfaces
  static void Initialize(Handle<Object> target);
  static Handle<Value> New(const Arguments &args);
  static Handle<Value> Connect(const Arguments &args);
  static Handle<Value> Close(const Arguments &args);
  static Handle<Value> Find(const Arguments &args);
  static Handle<Value> Insert(const Arguments &args);
  static Handle<Value> Update(const Arguments &args);
  static Handle<Value> Remove(const Arguments &args);

 protected:
 Connection() : EventEmitter() { };

  // inherited events
  void onConnected();
  void onReady();
  void onResults(MongoMessage *message);
  void onClose();

  Persistent<Array> m_results;
  //Handle<Value> results;
  bool m_gettingMore;
  unsigned int m_recCount;
  unsigned int m_recLimit;
  mongo_cursor *m_cursor;
};
#endif
