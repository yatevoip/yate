/**
 * wiresniff.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Capture interface for YATE messages.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2018-2023 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */


#include <yatengine.h>
#include <yatewiresniff.h>
#include <string.h>

#define MIN_BUFF_SIZE 2048
#define MAX_BUFF_SIZE 65507

using namespace TelEngine;

namespace { //anonymous

class WireSniffPlugin : public Plugin
{
public:
    WireSniffPlugin();
    virtual void initialize();
private:
    bool m_first;
};

class WireSniffHandler : public MessageHandler
{
public:
    WireSniffHandler()
      : MessageHandler(0,0)
      { }
    virtual bool received(Message &msg);
};

class WireSniffHook : public MessagePostHook
{
public:
    virtual void dispatched(const Message& msg, bool handled);
};

static Socket s_socket;
static SocketAddr s_remAddr;
static SocketAddr s_localAddr;
static Regexp s_filter;
static bool s_timer = false;
static RWLock s_lock("WireSniff");
// max buffer size, max jumbo frame size - IPv4 header and UDP header
// can be rewritten by configuration
static unsigned int s_maxBuffSize = 65507;


INIT_PLUGIN(WireSniffPlugin);

// append data, value must be given in network order
static void addTag(DataBlock& data, uint8_t tag, const uint8_t* value = 0, uint8_t length = 0)
{
    unsigned char buf[2];
    buf[0] = tag;
    buf[1] = length;
    data.append(buf,2);
    if (length)
	 data.append(value,length);
}

// append a string
static inline void addTag(DataBlock& data, uint8_t tag, const char* text)
{
    if (text && *text)
	addTag(data,tag,(const uint8_t*)text,::strlen(text));
}

// append a pointer
static inline void addTag(DataBlock& data, uint8_t tag, void* ptr)
{
    if (!ptr)
	return;
    String id;
    id.printf("%p",ptr);
    addTag(data,tag,id.c_str());
}

static inline void addTag(DataBlock& data, uint8_t tag, uint8_t val)
{
    addTag(data,tag,&val,1);
}

static bool sendMessage(const Message& msg, bool result, bool handled)
{
    DataBlock buf;
    static const String signature("yate-msg");
    buf.append(signature);

    addTag(buf,YSNIFF_RESULT,result ? 1  : 0);
    addTag(buf,YSNIFF_THREAD_ADDR,Thread::current());
    addTag(buf,YSNIFF_THREAD_NAME,Thread::currentName());
    addTag(buf,YSNIFF_DATA,msg.userData());
    addTag(buf,YSNIFF_BROADCAST,msg.broadcast() ? 1 : 0);
    addTag(buf,YSNIFF_FINAL_TAG,(uint8_t)0); // cast to uint8_t for resolving ambiguousness

    String id;
    id.printf("%p",&msg);
    if (result)
	buf.append(msg.encode(handled,id));
    else
	buf.append(msg.encode(id));

    RLock l(s_lock);
    if (buf.length() > s_maxBuffSize) {
	Debug(&__plugin,DebugWarn,"Encoded '%s'(%p) is too long, encoded length=%u, max allowed length=%u",
		msg.toString().c_str(),&msg,buf.length(),s_maxBuffSize);
	return false;
    }

    if (!s_socket.valid())
	return false;
    int len = s_socket.sendTo(buf.data(),buf.length(),s_remAddr);
    if (len == (int)buf.length())
	return true;
    if (len != Socket::socketError()) {
	Debug(&__plugin,DebugMild,"Incomplete write of '%s'(%p) message, written %u of %u octets",
		msg.toString().c_str(),&msg,len,buf.length());
	return false;
    }
    if (!s_socket.canRetry())
	Debug(&__plugin,DebugWarn,"Socket write error: %d: %s",
		s_socket.error(),::strerror(s_socket.error()));
    else
	DDebug(&__plugin,DebugMild,"Socket temporary unavailable: %d: %s",
		s_socket.error(),::strerror(s_socket.error()));
    return false;
}

bool WireSniffHandler::received(Message &msg)
{
    if (!s_timer && (msg == YSTRING("engine.timer")))
	return false;
    s_lock.readLock();
    if (s_filter && !s_filter.matches(msg))
	return false;
    s_lock.unlock();
    sendMessage(msg,false,false);
    return false;
}

void WireSniffHook::dispatched(const Message& msg, bool handled)
{
    if ((!s_timer && (msg == YSTRING("engine.timer"))))
	return;
    s_lock.readLock();
    if (s_filter && !s_filter.matches(msg))
        return;
    s_lock.unlock();
    sendMessage(msg,true,handled);
}

WireSniffPlugin::WireSniffPlugin()
    : Plugin("wiresniff"),
    m_first(true)
{
    Output("Loaded module WireSniff");
}

void WireSniffPlugin::initialize()
{
    Output("Initializing module WireSniff");
    Configuration cfg(Engine::configFile("wiresniff"));
    cfg.load();

    SocketAddr rAddr;
    rAddr.host(cfg.getValue("general","remote_host"));
    rAddr.port(cfg.getIntValue("general","remote_port"));
    if(!(rAddr.host() && rAddr.port() && rAddr.valid())) {
	Debug(this,DebugConf,"Failed to initialize: invalid remote address '%s:%u' [%p]",
		cfg.getValue("general","remote_host"),cfg.getIntValue("general","remote_port"),this);
	return;
    }

    SocketAddr lAddr;
    lAddr.host(cfg.getValue("general","local_host"));
    lAddr.port(cfg.getIntValue("general","local_port"));
    if (!(lAddr.host() && lAddr.port() && lAddr.valid())) {
	Debug(this,DebugConf,"Failed to initialize: invalid local address '%s:%u' [%p]",
		cfg.getValue("general","local_host"),cfg.getIntValue("general","local_port"),this);
	return;
    }

    if (lAddr.family() != rAddr.family()) {
	Debug(this,DebugConf,"Failed to initialize: mismatched socket families for local (%s) and remote (%s) socket addresses [%p]",
		lAddr.familyName(),rAddr.familyName(),this);
	return;
    }

    WLock l(s_lock);
    s_remAddr = rAddr;

    if (lAddr != s_localAddr) {
	if (s_socket.valid()) {
	    Debug(this,DebugInfo,"Stopping socket bound on local address '%s' [%p]",s_localAddr.addr().c_str(),this);
	    s_socket.terminate();
	}

	s_localAddr = lAddr;
	if (!s_socket.create(s_localAddr.family(),SOCK_DGRAM)) {
	    Debug(this,DebugWarn,"Failed to create socket for local address '%s', error=%s(%u) [%p]",
		    s_localAddr.addr().c_str(),::strerror(s_socket.error()),s_socket.error(),this);
	    return;
	}

	if (!s_socket.bind(s_localAddr)) {
	    Debug(this,DebugWarn,"Failed to bind socket on local address '%s', error=%s(%u) [%p]",
		    s_localAddr.addr().c_str(),::strerror(s_socket.error()),s_socket.error(),this);
	    return;
	}

	if (!s_socket.setBlocking(false)) {
	    s_socket.terminate();
	    Debug(DebugWarn,"Failed to set socket bound on local address '%s' in non-blocking mode, error=%s(%u) [%p]",
		    s_localAddr.addr().c_str(),::strerror(s_socket.error()),s_socket.error(),this);
	    return;
	}
    }

    Regexp filter(cfg.getValue("general","filter"));
    if (filter && !filter.compile())
	Debug(this,DebugConf,"Failed to set message filter '%s', does not compile [%p]",
		cfg.getValue("general","filter"),this);
    else
	s_filter = filter;

    Debug(this,DebugAll,"Sending Yate messages from '%s' to '%s' with filter '%s' [%p]",
	s_localAddr.addr().c_str(),s_remAddr.addr().c_str(),s_filter.c_str(),this);

    s_timer = cfg.getBoolValue("general","timer",false);
    s_maxBuffSize = cfg.getIntValue("general","max_buf_size",
	    s_maxBuffSize,MIN_BUFF_SIZE,MAX_BUFF_SIZE);
    l.drop();

    if (m_first) {
	m_first = false;
	Engine::install(new WireSniffHandler);
	Engine::self()->setHook(new WireSniffHook);
    }

}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
