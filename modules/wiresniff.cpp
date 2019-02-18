/**
 * wiresniff.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Capture interface for YATE messages.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2018 Null Team
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
#include <stdio.h>
#include <string.h>

#define MAX_BUFFER_SIZE 65507

using namespace TelEngine;
namespace { //anonymous

static Configuration s_cfg;
static Socket s_socket;
static SocketAddr addr;
static SocketAddr s_addr;
static Regexp s_filter;
static bool s_timer = false;
static Mutex s_mutex(false,"WireSniff");
    
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
    WireSniffHandler() : MessageHandler(0,0) { }
    virtual bool received(Message &msg);
};
    
class WireSniffHook : public MessagePostHook
{
public:
    virtual void dispatched(const Message& msg, bool handled);
};    

static void addTag(DataBlock& data, uint8_t tag, uint8_t length = 0, const void* value = 0)
{
    unsigned char buf[2];
    buf[0] = tag;
    buf[1] = length;
    data.append(buf,2);
    if (length)
	 data.append(const_cast<void*>(value),length);
}
    
static void addTag(DataBlock& data, uint8_t tag, const char* text)
{
    if (text && *text)
	addTag(data,tag,::strlen(text),text);
}
    
static void addTag(DataBlock& data, uint8_t tag, void* ptr)
{
    if (!ptr)
	return;
    intptr_t val = reinterpret_cast<intptr_t>(ptr);
    addTag(data,tag,sizeof(val),ptr);
}
    
    
static bool sendMessage(const Message& msg, bool result, bool handled)
{
    DataBlock buf;
    String signature("yate-msg");
    buf.append(signature);

    uint8_t i = result ? 1: 0; 
    addTag(buf,YSNIFF_RESULT,1,&i);

    Thread* t = Thread::current();
    addTag(buf,YSNIFF_THREAD_ADDR,t);
	
    const char* name = Thread::currentName();
    addTag(buf,YSNIFF_THREAD_NAME,name);
      
    RefObject* data = msg.userData();
    addTag(buf, YSNIFF_DATA, data);
	
    uint8_t broadcast = msg.broadcast() ? 1 : 0;
    addTag(buf,YSNIFF_BROADCAST,1,&broadcast);
	
    uint8_t finaltag = 0;
    addTag(buf,YSNIFF_FINAL_TAG,1,&finaltag);
	
	
    String id;
    id.printf("%p",&msg);
    if (result)
	buf.append(msg.encode(handled,id));
    else
	buf.append(msg.encode(id));
	
    if (!(buf.length() < MAX_BUFFER_SIZE))
	DDebug(DebugWarn,"Buffer Overrun");
	
    s_mutex.lock();
    unsigned int len = s_socket.sendTo(buf.data(),buf.length(),addr);
    s_mutex.unlock();
    if (!len)
	DDebug(DebugWarn,"Unable to send package");
	
    return(len == buf.length());
}
    
    
bool WireSniffHandler::received(Message &msg)
{
    if (!s_timer && (msg == YSTRING("engine.timer")))
	return false;
    if (!s_socket.valid()) {
	Debug(DebugWarn,"Socket %s is not valid", strerror(s_socket.error()));
	return false;
    }
    if (!s_addr.valid()){
	Debug(DebugWarn,"SocketAddr is not valid");
	return false;
    }

    if (s_filter && !s_filter.matches(msg))
	return false;
	
    sendMessage(msg,false,false);
	
	
    return false;
}
    
void WireSniffHook::dispatched(const Message& msg, bool handled)
{
    if ((!s_timer && (msg == YSTRING("engine.timer"))))
	return;
	
    if (s_filter && !s_filter.matches(msg))
        return;
	
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
    s_mutex.lock();
    s_cfg = Engine::configFile("wiresniff");
    s_cfg.load();
    s_mutex.unlock();

    String remoteName = s_cfg.getValue("general","remote_host");
    addr.host(remoteName);
    addr.port(s_cfg.getIntValue("general","remote_port"));
	
    if(addr.valid() && addr.port()) {
        DDebug(this,DebugNote,"Remote address '%s' and remote port '%u' are set", remoteName.c_str(), addr.port());
    }
	
    if(!(addr.host() && addr.port())) {
	s_socket.terminate();
	if(remoteName)
	    Debug(this,DebugWarn,"Remote address is invalid");
	return;
    }
	
    if (addr.host() && addr.port() && !(addr.valid())) {
	DDebug(this,DebugWarn,"Invalid address '%s'",remoteName.c_str());
	return;
    }

    if(addr.family() != SocketAddr::IPv6)
	s_addr.host(s_cfg.getValue("general","local_host","0.0.0.0"));
    else
	s_addr.host(s_cfg.getValue("general","local_host","::"));
	
    s_addr.port(s_cfg.getIntValue("general","local_port",0));
	
    if (!(s_addr.valid() && s_addr.host())) {
	DDebug(this,DebugWarn,"Invalid address '%s'", s_addr.host().c_str());
	return;
	    
    }

    if (addr.host()) {
    if (s_addr.family() != addr.family()) {
	Debug(this,DebugWarn,"Socket Addresses are not compatible");
	return;
	}
    }

    if (!s_socket.create(s_addr.family(),SOCK_DGRAM)) {
	Debug(this,DebugWarn,"Could not create socket %s", strerror(s_socket.error()));
	s_socket.terminate();
    }

    if (!s_socket.bind(s_addr)) {
	Debug(this,DebugWarn,"Unable to bind to %s:%u : %s",s_addr.host().c_str(),s_addr.port(),strerror(s_socket.error()));
        return;
    }
	
    int val = 1;
    if (!s_socket.setOption(IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val))) {
	Debug(this,DebugWarn,"Socket %s option is not set!", strerror(s_socket.error()));
        return;
    }

    if (!s_socket.setBlocking(false)) {
	s_socket.terminate();
	Debug(DebugWarn,"Unable to set socket %s to nonblocking mode", strerror(s_socket.error()));
        return;
    }
    if (!s_socket.getSockName(s_addr)) {
	Debug(this,DebugWarn,"Error getting address: %s",strerror(s_socket.error()));
        return;
    }
	
    DDebug(this,DebugNote,"Socket bound to: %s:%u",s_addr.host().c_str(),s_addr.port());
    Debug(this,DebugNote,"Sending from %s to %s:%u",s_addr.addr().c_str(),addr.host().c_str(),addr.port());
	
    if (m_first) {
	m_first = false;
	Engine::install(new WireSniffHandler);
	Engine::self()->setHook(new WireSniffHook);
    } 
	
    s_mutex.lock();
    s_filter = s_cfg.getValue("general", "filter");
    s_mutex.unlock();
	
    Output("WireSniff was initialized");
}
    
    INIT_PLUGIN(WireSniffPlugin);
}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
