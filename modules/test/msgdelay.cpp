/**
 * msgdelay.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * An arbitrary message delayer
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2023 Null Team
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

class DelayHandler : public MessageHandler
{
public:
    DelayHandler(int prio, const char* trackName) : MessageHandler(0,prio,trackName) { }
    virtual bool received(Message &msg);
};

class MsgDelay : public Module
{
public:
    MsgDelay();
    virtual ~MsgDelay();
    virtual void initialize();
    bool unload();
private:
    DelayHandler* m_handler;
};

INIT_PLUGIN(MsgDelay);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
        return __plugin.unload();
    return true;
}


bool DelayHandler::received(Message &msg)
{
    NamedString* p = msg.getParam(YSTRING("message_delay"));
    if (!p)
	return false;
    int ms = p->toInteger();
    bool force = false;
    NamedString* fp = msg.getParam(YSTRING("message_delay_always"));
    if (fp) {
	force = fp->toBoolean();
	msg.clearParam(fp);
    }
    // make sure we don't get here again
    msg.clearParam(p);
    if (ms > 0 && (force || !Engine::exiting())) {
	// delay maximum 10s
	if (ms > 10000)
	    ms = 10000;
	Debug(DebugAll,"Delaying '%s' by %d ms in thread '%s'",msg.safe(),ms,Thread::currentName());
	unsigned int n = (ms + Thread::idleMsec() - 1) / Thread::idleMsec();
	if (force)
	    while (n-- && !Thread::check(false))
		Thread::idle();
	else
	    while (n-- && !Engine::exiting())
		Thread::idle();
    }
    return false;
};


MsgDelay::MsgDelay()
    : Module("msgdelay","misc"),
      m_handler(0)
{
    Output("Loaded module MsgDelay");
}

MsgDelay::~MsgDelay()
{
    Output("Unloading module MsgDelay");
}

bool MsgDelay::unload()
{
    if (m_handler) {
	Engine::uninstall(m_handler);
	TelEngine::destruct(m_handler);
    }
    return true;
}

void MsgDelay::initialize()
{
    if (!m_handler) {
	int prio = Engine::config().getIntValue("general","msgdelay",50);
	if (prio > 0) {
	    Output("Initializing module MsgDelay priority %d",prio);
	    m_handler = new DelayHandler(prio,"msgdelay");
	    m_handler->setFilter(new NamedPointer("message_delay",new Regexp("^[1-9]")));
	    Engine::install(m_handler);
	    installRelay(Level);
	    installRelay(Command);
	}
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
