#include <stdio.h>
#include "Connection.h"

extern "C" {
#define MONGO_HAVE_STDINT
#include <bson.h>
#include <mongo.h>
#include <platform_hacks.h>
}


using namespace v8;
using namespace node;

void Connection::Initialize(Handle<Object> target)
{
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
  scope.Close(Undefined());
}

Handle<Value> Connection::New(const Arguments &args)
{
  HandleScope scope;
  Connection *mongo = new Connection();
  mongo->Wrap(args.This());
  mongo->m_results= Persistent<Array>::New(Array::New());
  mongo->m_gettingMore = false;
  return args.This();
}

Handle<Value> Connection::Connect(const Arguments &args)
{
  Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
  HandleScope scope;

  String::Utf8Value conninfo(args[0]->ToString());
  //Number::New(args[1]->ToNumber());
  connection->connect(*conninfo, 27017);
  connection->Ref();
  return scope.Close(Undefined());
}

void Connection::onConnected()
{
  pdebug("got connection!\n");
  Emit(String::New("connection"), 0, NULL);
}

void Connection::onReady()
{
  Emit(String::New("ready"), 0, NULL);
}

void Connection::onClose()
{
  Emit(String::New("close"), 0, NULL);
}

void Connection::onResults(MongoMessage *message)
{
  HandleScope scope;
  mongo_reply *reply = reinterpret_cast<mongo_reply*>(message->messageBuf);
  mongo_cursor cursor;

  cursor.mm = reply;
  cursor.current.data = NULL;

  if(!m_gettingMore)
    {
      m_results.Dispose();
      m_results = Persistent<Array>::New(Array::New());
    }

  int start = m_results->Length();
  pdebug("starting at rec: %d  adding %d for a total of %d\n", m_results->Length(), reply->fields.num, start+reply->fields.num);

  for(int i = m_results->Length(); i < reply->fields.num+start; i++)
    {
      mongo_cursor_next(&cursor);
      Local<Value> val = decodeObjectStr(cursor.current.data);
      m_results->Set(Integer::New(i), val);
      m_recCount = i+1;

      if(m_recLimit > 0 && m_recCount == m_recLimit) // got all we were asked for
	{
	  pdebug("hit limit, bailing %d\n", m_recCount);

	  if(reply->fields.cursorID) // still more data in cursor even though we dont want it, release it
	    {
	      pdebug("killing cursor\n");
	      mongo_message * mm = mongo_message_create(16 /*header*/
							+4 /*ZERO*/
							+4 /*numCursors*/
							+8 /*cursorID*/
							, 0, 0, mongo_op_kill_cursors);
	      char* data = &mm->data;
	      data = mongo_data_append32(data, &zero);
	      data = mongo_data_append32(data, &one);
	      data = mongo_data_append64(data, &reply->fields.cursorID);
	      WriteMessage(mm);
	    }

	  reply->fields.cursorID = 0; // easy way to abort gathering more data
	  break;
	}
    }

  if(reply->fields.cursorID && m_recCount < m_recLimit) // get more data
    {
      pdebug("need to get more data\n");
      m_gettingMore = true;
      unsigned int count = m_recLimit-m_recCount;
      mongo_connection* conn = cursor.conn;
      char* data;
      int sl = strlen(m_cursor->ns)+1;
      mongo_message * mm = mongo_message_create(16 /*header*/
						+4 /*ZERO*/
						+sl
						+4 /*numToReturn*/
						+8 /*cursorID*/
						, 0, 0, mongo_op_get_more);
      data = &mm->data;
      data = mongo_data_append32(data, &zero);
      data = mongo_data_append(data, m_cursor->ns, sl);
      data = mongo_data_append32(data, &count);
      data = mongo_data_append64(data, &reply->fields.cursorID);
      WriteMessage(mm);
      scope.Close(Undefined());
      return;
    }

      
  Emit(String::New("result"), 1, reinterpret_cast<Handle<Value> *>(&m_results));

  // clean up and reset state
  m_results.Clear();
  m_results.Dispose();
  m_results = Persistent<Array>::New(Array::New());
  m_gettingMore = false;
  free((void *)m_cursor->ns);
  free(m_cursor);
  scope.Close(Undefined());
}

Handle<Value> Connection::Insert(const Arguments &args) 
{
  HandleScope scope;
  Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
  
  String::Utf8Value ns(args[0]->ToString());
  // TODO assert ns != undefined (args.Length > 0)
  bson obj;
  
  // XXX check args > 1                                                                                                                                                  
  Local<Object> query(args[1]->ToObject());
  obj = encodeObject(query);

  char * data;
  mongo_message *mm = mongo_message_create( 16 /* header */
					    + 4 /* ZERO */
					    + strlen(*ns)
					    + 1 + bson_size(&obj)
					    , 0, 0, mongo_op_insert);

  data = &mm->data;
  data = mongo_data_append32(data, &zero);
  data = mongo_data_append(data, *ns, strlen(*ns) + 1);
  data = mongo_data_append(data, obj.data, bson_size(&obj));

  connection->WriteMessage(mm);

  bson_destroy(&obj);
  return scope.Close(Undefined());
}

Handle<Value> Connection::Update(const Arguments &args) {
  HandleScope scope;
  Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

  String::Utf8Value ns(args[0]->ToString());

  bson cond;
  bson obj;
  int flags = 0;

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

  if (args.Length() > 3 && !args[3]->IsUndefined()) {
    Local<Integer> jsflags = args[3]->ToInteger();
    flags = jsflags->Value();
  }

  //connection->Update(*ns, cond, obj, flags);
  char * data;
  mongo_message * mm = mongo_message_create( 16 /* header */
					     + 4  /* ZERO */
					     + strlen(*ns) + 1
					     + 4  /* flags */
					     + bson_size(&cond)
					     + bson_size(&obj)
					     , 0 , 0 , mongo_op_update );

  data = &mm->data;
  data = mongo_data_append32(data, &zero);
  data = mongo_data_append(data, *ns, strlen(*ns) + 1);
  data = mongo_data_append32(data, &flags);
  data = mongo_data_append(data, cond.data, bson_size(&cond));
  data = mongo_data_append(data, obj.data, bson_size(&obj));
  connection->WriteMessage(mm);


  bson_destroy(&cond);
  bson_destroy(&obj);
  return scope.Close(Undefined());
}
Handle<Value> Connection::Close(const Arguments &args)
{
  HandleScope scope;
  Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

  connection->disconnect();

  return scope.Close(Undefined());

}
Handle<Value> Connection::Remove(const Arguments &args) {
  HandleScope scope;
  Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
  if (!args[0]->IsString()) {
    return ThrowException(Exception::Error(String::New("ns must be specified")));
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
    return ThrowException(Exception::Error(String::New("Condition must be an object")));
  }

  //connection->Remove(*ns, cond);
  char * data;
  mongo_message * mm = mongo_message_create( 16 /* header */
					     + 4  /* ZERO */
					     + strlen(*ns) + 1
					     + 4  /* ZERO */
					     + bson_size(&cond)
					     , 0 , 0 , mongo_op_delete );

  data = &mm->data;
  data = mongo_data_append32(data, &zero);
  data = mongo_data_append(data, *ns, strlen(*ns) + 1);
  data = mongo_data_append32(data, &zero);
  data = mongo_data_append(data, cond.data, bson_size(&cond));
  connection->WriteMessage(mm);

  bson_destroy(&cond);
  return scope.Close(Undefined());
}



Handle<Value> Connection::Find(const Arguments &args) 
{
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

  pdebug("find on: %s\n", *ns);
  
  mongo_cursor *cursor = static_cast<mongo_cursor*>(
				      bson_malloc(sizeof(mongo_cursor)));

  if(nToReturn > 0) // track limit so cursor can advance until necessary
    connection->m_recLimit = nToReturn;

  int sl = strlen(*ns)+1;
  cursor->ns = static_cast<char*>(bson_malloc(sl));

  memcpy(static_cast<void*>(const_cast<char*>(cursor->ns)), *ns, sl);
  cursor->conn = connection->m_connection;

  char * data;
  mongo_message * mm = mongo_message_create( 16 + /* header */
					     4 + /*  options */
					     sl + /* ns */
					     4 + 4 + /* skip,return */
					     bson_size( &query_bson ) +
					     bson_size( &query_fields_bson ) ,
					     0 , 0 , mongo_op_query );
  int options = 0;
  data = &mm->data;
  data = mongo_data_append32(data, &options);
  data = mongo_data_append(data, *ns, strlen(*ns)+ 1);
  data = mongo_data_append32(data, &nToSkip);
  data = mongo_data_append32(data, &nToReturn);
  data = mongo_data_append(data, query_bson.data, bson_size(&query_bson));
  //if (query_fields_bson)
  data = mongo_data_append(data, query_fields_bson.data, bson_size(&query_fields_bson));

  bson_fatal_msg((data == ((char*)mm) + mm->head.len), "query building fail!");


  connection->WriteMessage(mm);
  connection->m_cursor = cursor;

  bson_destroy(&query_bson);
  bson_destroy(&query_fields_bson);

  return scope.Close(Undefined());
}

extern "C"
void init( Handle<Object> target ) {
  HandleScope scope;
  Connection::Initialize( target );
  ObjectID::Initialize(target);
  scope.Close(Undefined());
}
