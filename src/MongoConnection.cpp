#include "MongoConnection.h"

#define chunksize 8092

MongoConnection::MongoConnection()
{
  m_connected = false;

  ev_init(&read_watcher, IOEventProxy);
  read_watcher.data = this; // stash this reference for use in static callback
  ev_init(&write_watcher, IOEventProxy);
  write_watcher.data = this;
  ev_init(&connect_watcher, ConnectEventProxy);
  connect_watcher.data = this;

  memset(&m_inboundBuffer, 0, sizeof(MongoMessage));
  memset(&m_outboundBuffer, 0, sizeof(MongoMessage));
  pdebug("created\n");
}

MongoConnection::~MongoConnection()
{
  disconnect();
  // clean up m_connection
  // clean up m_currentMessage
}

void MongoConnection::connect(const char *host, const int32_t port)
{
  pdebug("connecting\n");
  mongo_connection_options opts;
  memcpy(opts.host, host, strlen(host)+1);
  opts.host[strlen(host)] = '\0';
  opts.port = port;

  m_connection->left_opts = (mongo_connection_options *)bson_malloc(sizeof(mongo_connection_options));
  m_connection->right_opts = NULL;

  if ( strlen(opts.host) > 0 ) // ghetto
    {
      memcpy( m_connection->left_opts , &opts , sizeof( mongo_connection_options ) );
    }
  else 
    {
      strcpy( m_connection->left_opts->host , "127.0.0.1" );
      m_connection->left_opts->port = 27017;
    }

  m_connection->sock = 0;
  m_connection->connected = 0;

  memset(m_connection->sa.sin_zero, 0, sizeof(m_connection->sa.sin_zero));
  m_connection->sa.sin_family = AF_INET;
  m_connection->sa.sin_port = htons(m_connection->left_opts->port);
  m_connection->sa.sin_addr.s_addr = inet_addr(m_connection->left_opts->host);
  m_connection->addressSize = sizeof(m_connection->sa);

  m_connection->sock = socket( AF_INET, SOCK_STREAM, 0 );

  if(m_connection->sock <= 0)
    {
      // onerror
    }

  int sockflags = fcntl(m_connection->sock, F_GETFL, 0);
  fcntl(m_connection->sock, F_SETFL, sockflags | O_NONBLOCK);
  int res = ::connect(m_connection->sock, (struct sockaddr*) &m_connection->sa, m_connection->addressSize);

  ev_io_set(&connect_watcher,  m_connection->sock, EV_WRITE);
  ConnectWatcher(true);

  ev_io_set(&read_watcher,  m_connection->sock, EV_READ);
  ev_io_set(&write_watcher, m_connection->sock, EV_WRITE);
  ReadWatcher(true);
  //WriteWatcher(true);
}

void MongoConnection::disconnect()
{
  ReadWatcher(false);
  WriteWatcher(false);
  free(m_outboundBuffer.messageBuf);
  free(m_inboundBuffer.messageBuf);
  memset(&m_outboundBuffer, 0, sizeof(MongoMessage));
  memset(&m_inboundBuffer, 0, sizeof(MongoMessage));
  close(m_connection->sock);
  m_connected = false;
  mongo_destroy(m_connection);
  onClose();
}

bool MongoConnection::isConnected()
{
  return m_connected;
}

void MongoConnection::onConnected()
{
  pdebug("!!!got my connection!\n");
}

void MongoConnection::onClose()
{
  pdebug("!!! closing connection\n");
}

void MongoConnection::onResults(MongoMessage *message)
{
  pdebug("!!!results\n");
}

void MongoConnection::onReady()
{
  pdebug("!!!ready!\n");
}
void MongoConnection::ReadWatcher(bool state)
{
  if(state)
    ev_io_start(EV_DEFAULT_ &read_watcher);
  else
    ev_io_stop(EV_DEFAULT_ &read_watcher);
}

void MongoConnection::WriteWatcher(bool state)
{
  if(state)
    ev_io_start(EV_DEFAULT_ &write_watcher);
  else
    ev_io_stop(EV_DEFAULT_ &write_watcher);
}


void MongoConnection::ConnectWatcher(bool state)
{
  if(state)
    ev_io_start(EV_DEFAULT_ &connect_watcher);
  else
    ev_io_stop(EV_DEFAULT_ &connect_watcher);
}

void MongoConnection::ConnectEvent(int revents)
{
  ConnectWatcher(false);
  int flag = 1;
  setsockopt( m_connection->sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag) ); // disable nagle, not sure if needed

  m_connection->connected = 1;
  onConnected();
  onReady();
  ReadWatcher(true);
  //  WriteWatcher(true);
}

void MongoConnection::ReadData()
{
  char readbuf[chunksize];
  
  while(true)
    {
      // read the largest chunk we can
      int len = read(m_connection->sock, readbuf, chunksize);
      if(len == -1)
	{
	  if(errno == EAGAIN)
	    {
	      //pdebug("EGAIN on read, try later\n");
	      return;
	    }
	  else
	    {
	      disconnect();
	      return;
	    }
	}

      //pdebug("realloc: %p\t%d bytes -- %d + %d\n", m_inboundBuffer.messageBuf, m_inboundBuffer.messageLen+len, m_inboundBuffer.messageLen, len);
      m_inboundBuffer.messageBuf = (char *) realloc(m_inboundBuffer.messageBuf, m_inboundBuffer.bufferLen+len);
      m_inboundBuffer.index = m_inboundBuffer.messageBuf + m_inboundBuffer.bufferLen;
      memcpy(m_inboundBuffer.index, readbuf, len);

      if(!m_inboundBuffer.messageLen && len > sizeof(mongo_header)+sizeof(mongo_reply_fields)) // set up header struct so we can watch for completed messages
	{
	  memcpy(&m_inboundBuffer.messageHeader, m_inboundBuffer.index, sizeof(mongo_header));
	  m_inboundBuffer.index += sizeof(mongo_header);
	  memcpy(&m_inboundBuffer.messageReply, m_inboundBuffer.index, sizeof(mongo_reply_fields));
	  m_inboundBuffer.index += sizeof(mongo_reply_fields);
	  bson_little_endian32(&m_inboundBuffer.messageLen, &m_inboundBuffer.messageHeader.len);
	  pdebug("initial read, expecting %d bytes\n", m_inboundBuffer.messageLen);
	}

      //pdebug("read: %d (%d)  expected: %d   left: %d\n", len, m_inboundBuffer.bufferLen, m_inboundBuffer.messageLen, m_inboundBuffer.messageLen - m_inboundBuffer.bufferLen);
      m_inboundBuffer.bufferLen += len;
      m_inboundBuffer.index += len; // hmm.

      if(m_inboundBuffer.bufferLen == m_inboundBuffer.messageLen) // complete message!
	{
	  // get appropriate data structures
	  mongo_reply *reply = reinterpret_cast<mongo_reply *>(m_inboundBuffer.messageBuf);


	  onResults(&m_inboundBuffer);
	  free(m_inboundBuffer.messageBuf);
	  memset(&m_inboundBuffer, 0, sizeof(MongoMessage));
	}
    }
  
}

void MongoConnection::WriteMessage(mongo_message *message)
{
  mongo_header head;
  bson_little_endian32(&head.len, &message->head.len);
  bson_little_endian32(&head.id, &message->head.id);
  bson_little_endian32(&head.responseTo, &message->head.responseTo);
  bson_little_endian32(&head.op, &message->head.op);

  int len = message->head.len;
  pdebug("adding %d bytes to outbound buffer (%d)\n", len, m_outboundBuffer.messageLen);
  
  int currentOffset = m_outboundBuffer.index - m_outboundBuffer.messageBuf;
  //pdebug("realloc: %p\t%d bytes\n", m_outboundBuffer.messageBuf, m_outboundBuffer.messageLen+len);
  m_outboundBuffer.messageBuf = (char *) realloc(m_outboundBuffer.messageBuf, m_outboundBuffer.messageLen+len);

  m_outboundBuffer.index = m_outboundBuffer.messageBuf + currentOffset; // adjust write ptr in case of relocation

  // copy new data to the end of the buffer
  //memcpy(m_outboundBuffer.index, &head, sizeof(head));
  //memcpy(m_outboundBuffer.index+sizeof(head), &message->data, len-sizeof(head));
  memcpy(m_outboundBuffer.messageBuf+m_outboundBuffer.messageLen, &head, sizeof(head));
  memcpy(m_outboundBuffer.messageBuf+m_outboundBuffer.messageLen+sizeof(head), &message->data, len-sizeof(head));

  m_outboundBuffer.messageLen += len;

  free(message); // clean up message since it's been queued
  WriteWatcher(true);
}

void MongoConnection::WriteData()
{
  WriteWatcher(false);
  pdebug("%d bytes to left write\n", m_outboundBuffer.messageLen);

  int bytes = write(m_connection->sock, m_outboundBuffer.index, m_outboundBuffer.messageLen);
  pdebug("wrote: %d bytes  [%d - %d]\n", bytes, m_outboundBuffer.index-m_outboundBuffer.messageBuf, m_outboundBuffer.index-m_outboundBuffer.messageBuf+bytes);
  if(bytes == -1)
    {
      if(errno == EAGAIN)
	{
	  WriteWatcher(true);
	  return;
	}
      disconnect();
    }

  m_outboundBuffer.index += bytes;
  m_outboundBuffer.messageLen -= bytes;

  if(m_outboundBuffer.messageLen == 0) // done sending, clean up
    {
      pdebug("done!\n");
      free(m_outboundBuffer.messageBuf);
      memset(&m_outboundBuffer, 0 , sizeof(MongoMessage));
      WriteWatcher(false);
    }
  else
    WriteWatcher(true); // not done writing yet
}

void MongoConnection::IOEvent(int revents)
{
  if(!m_connection->connected)
    {
      ReadWatcher(false);
      WriteWatcher(false);
      disconnect();
    }
  //pdebug("event %d %d\n", m_connection->connected, revents);

  if(revents & EV_READ)
    {
      //pdebug("read event\n");
      ReadData();
    }

  if(revents & EV_WRITE)
    {
      pdebug("write event\n");
      if(m_outboundBuffer.messageBuf)
	WriteData();
      else
	{
	  WriteWatcher(false);
	  ReadWatcher(true);
	  onReady();
	}
    }

  if(revents & EV_ERROR)
    {
      pdebug("error event\n");
      disconnect();
    }
}

void MongoConnection::ConnectEventProxy(EV_P_ ev_io *w, int revents)
{
  pdebug("got a connect event\n");
  MongoConnection *connection = static_cast<MongoConnection *>(w->data);
  connection->ConnectEvent(revents);
}

void MongoConnection::IOEventProxy(EV_P_ ev_io *w, int revents)
{
  MongoConnection *connection = static_cast<MongoConnection *>(w->data);
  connection->IOEvent(revents);
}
