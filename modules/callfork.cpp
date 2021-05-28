/**
 * callfork.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Call Forker
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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

#define MOD_PREFIX "fork"

static ObjList s_calls;
static int s_current = 0;

class ForkSlave;

class ForkMaster : public CallEndpoint, public DebugEnabler
{
    friend class ForkSlave;
public:
    ForkMaster(ObjList* targets, int lvl);
    virtual ~ForkMaster();
    virtual void disconnected(bool final, const char* reason);
    bool startCalling(Message& msg);
    void checkTimer(const Time& tmr);
    void lostSlave(ForkSlave* slave, const char* reason);
    bool msgAnswered(Message& msg, const String& dest);
    bool msgProgress(Message& msg, const String& dest);
    const ObjList& slaves() const
	{ return m_slaves; }
    bool msgToSlaves(const Message& msg, const String& match);
protected:
    bool clearRinging(const String& id);
    void clear(bool softly);
    String* getNextDest();
    bool forkSlave(String* dest);
    bool callContinue();
    RefPointer<CallEndpoint> m_discPeer;
    ObjList m_slaves;
    String m_ringing;
    Regexp m_failures;
    int m_index;
    bool m_answered;
    bool m_rtpForward;
    bool m_rtpStrict;
    bool m_fake;
    ObjList* m_targets;
    Message* m_exec;
    u_int64_t m_timer;
    bool m_timerDrop;
    bool m_execNext;
    bool m_chanMsgs;
    bool m_failuresRev;
    bool m_setId;
    String m_reason;
    String m_media;
    unsigned int m_targetIdx;
    int m_level;
};

class ForkSlave : public CallEndpoint, public DebugEnabler
{
public:
    enum Type {
	Unknown = 0,
	Regular,
	Auxiliar,
	Persistent
    };
    ForkSlave(ForkMaster* master, const char* id);
    virtual ~ForkSlave();
    virtual void destroyed();
    virtual void disconnected(bool final, const char* reason);
    void clearMaster(RefPointer<ForkMaster>* master = 0);
    inline void lostMaster(const char* reason)
	{ clearMaster(); disconnect(reason); }
    inline bool isMaster(ForkMaster* master)
	{ return master && m_master == master; }
    inline Type type() const
	{ return m_type; }
    inline void setType(Type type)
	{ m_type = type; }
protected:
    ForkMaster* m_master;
    Type m_type;
    int m_level;
};

class ForkModule : public Module
{
public:
    ForkModule();
    virtual ~ForkModule();
    bool unload();
    bool msgToSlaves(const Message& msg, const String& match);
protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    bool msgExecute(Message& msg);
    bool msgLocate(Message& msg, bool masquerade);
    bool msgToMaster(Message& msg, bool answer);
    bool m_hasRelays;
};


static TokenDict s_calltypes[] = {
    { "regular",    ForkSlave::Regular    },
    { "auxiliar",   ForkSlave::Auxiliar   },
    { "persistent", ForkSlave::Persistent },
    { 0, 0 }
};

INIT_PLUGIN(ForkModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return __plugin.unload();
    return true;
}


class ForkRelay : public MessageHandler
{
public:
    inline ForkRelay(const char* name, const char* match, int priority)
	: MessageHandler(name,priority,__plugin.name()),
	  m_match(match)
	{ }
    virtual bool received(Message& msg)
	{ return __plugin.msgToSlaves(msg,m_match); }
private:
    String m_match;
};


ForkMaster::ForkMaster(ObjList* targets, int lvl)
    : m_index(0), m_answered(false), m_rtpForward(false), m_rtpStrict(false),
      m_fake(false), m_targets(targets), m_exec(0),
      m_timer(0), m_timerDrop(false), m_execNext(false), m_chanMsgs(false),
      m_failuresRev(false), m_setId(false), m_reason("hangup"),
      m_targetIdx(0), m_level(lvl)
{
    String tmp(MOD_PREFIX "/");
    tmp << ++s_current;
    setId(tmp);
    debugName(id());
    debugChain(&__plugin);
    if (m_level > 0)
	debugLevel(m_level);
    s_calls.append(this);
    DDebug(this,DebugAll,"ForkMaster::ForkMaster(%p) [%p]",targets,this);
}

ForkMaster::~ForkMaster()
{
    DDebug(this,DebugAll,"ForkMaster::~ForkMaster() [%p]",this);
    m_timer = 0;
    CallEndpoint::commonMutex().lock();
    s_calls.remove(this,false);
    CallEndpoint::commonMutex().unlock();
    clear(false);
    if (m_discPeer && !m_answered) {
	RefPointer<CallEndpoint> call = m_discPeer->getPeer();
	if (call) {
	    Message* r = new Message("chan.replaced",0,true);
	    r->addParam("id",id());
	    r->addParam("newid",call->id());
	    r->addParam("peerid",m_discPeer->id());
	    r->userData(this);
	    Engine::enqueue(r);
	    call = 0;
	}
    }
    m_discPeer = 0;
    if (m_chanMsgs) {
	Message* msg = new Message("chan.hangup");
	msg->addParam("id",id());
	msg->addParam("cdrtrack",String::boolText(false));
	Engine::enqueue(msg);
    }
}

void ForkMaster::disconnected(bool final, const char* reason)
{
    CallEndpoint::disconnected(final,reason);
    if (m_chanMsgs && !(final || m_answered || m_discPeer)) {
	Message* msg = new Message("chan.disconnected");
	msg->addParam("id",id());
	if (m_exec)
	    msg->copyParams(*m_exec,"error,reason");
	msg->userData(this);
	Engine::enqueue(msg);
    }
}

String* ForkMaster::getNextDest()
{
    String* ret = 0;
    while (!ret && m_targets && m_targets->count())
	ret = static_cast<String*>(m_targets->remove(false));
    return ret;
}

bool ForkMaster::forkSlave(String* dest)
{
    if (null(dest))
	return false;
#ifdef XDEBUG
    Debugger dbg(debugAt(DebugAll) ? DebugAll : 50,
	"ForkMaster forkSlave"," '%s' dest='%s' [%p]",id().c_str(),dest->c_str(),this);
#endif
    bool ok = false;
    m_exec->clearParam("error");
    m_exec->clearParam("reason");
    Message msgCopy(*m_exec);
    msgCopy.setParam("callto",*dest);
    msgCopy.setParam("rtp_forward",String::boolText(m_rtpForward));
    msgCopy.setParam("cdrtrack",String::boolText(false));
    NamedList* params = YOBJECT(NamedList,dest);
    if (params)
	msgCopy.copyParams(*params);
    const char* error = "failure";
    if (m_execNext) {
	RefPointer<CallEndpoint> peer = getPeer();
	if (!peer) {
	    clear(false);
	    return false;
	}
	Debug(this,DebugCall,"Call '%s' directly to target '%s' [%p]",
	    peer->id().c_str(),dest->c_str(),this);
	m_discPeer = peer;
	msgCopy.userData(peer);
	msgCopy.setParam("id",peer->id());
	msgCopy.clearParam("cdrtrack");
	if (!Engine::dispatch(msgCopy)) {
	    error = msgCopy.getValue("error",error);
	    Debug(this,DebugNote,"Call '%s' failed non-fork to target '%s', error '%s' [%p]",
		getPeerId().c_str(),dest->c_str(),error,this);
	    return false;
	}
	clear(false);
	return true;
    }
    String tmp(id());
    tmp << "/" << ++m_index;
    ForkSlave* slave = new ForkSlave(this,tmp);
    msgCopy.setParam("id",tmp);
    msgCopy.userData(slave);
    bool autoring = false;
    if (Engine::dispatch(msgCopy)) {
	ok = true;
	autoring = msgCopy.getBoolValue("fork.autoring");
	if (m_ringing.null() && (autoring || msgCopy.getBoolValue("fork.ringer")))
	    m_ringing = tmp;
	else
	    autoring = false;
	if (m_rtpForward) {
	    String rtp(msgCopy.getValue("rtp_forward"));
	    if (rtp != "accepted") {
		error = "nomedia";
		int level = DebugWarn;
		if (m_rtpStrict) {
		    ok = false;
		    level = DebugCall;
		}
		Debug(this,level,"Call '%s' did not get RTP forward from '%s' target '%s' [%p]",
		    getPeerId().c_str(),slave->getPeerId().c_str(),dest->c_str(),this);
	    }
	}
	m_exec->copyParams(msgCopy,"error,reason,rtp_forward");
    }
    else
	error = msgCopy.getValue("error",error);
    XDebug(this,DebugAll,"Executed slave (%p) '%s' refs=%u ok=%u [%p]",
	slave,slave->id().c_str(),slave->refcount(),ok,this);
    msgCopy.userData(0);
    // Avoid adding slave to list if already terminated (master reset)
    // Avoid adding a slave with refcount=1:this will trigger re-enter in callContinue() on slave destroy
    bool master = slave->isMaster(this);
    if (ok && master && slave->refcount() > 1) {
	ForkSlave::Type type = static_cast<ForkSlave::Type>(msgCopy.getIntValue("fork.calltype",s_calltypes,ForkSlave::Regular));
	Debug(this,DebugCall,"Call '%s' calling on %s '%s' target '%s' [%p]",
	    getPeerId().c_str(),lookup(type,s_calltypes),tmp.c_str(),dest->c_str(),this);
	slave->setType(type);
	m_slaves.append(slave);
	XDebug(this,DebugInfo,"Added slave (%p) '%s' refs=%u [%p]",
	    slave,slave->id().c_str(),slave->refcount(),this);
	if (autoring) {
	    Message* ring = new Message(msgCopy.getValue("fork.automessage","call.ringing"));
	    ring->addParam("id",slave->getPeerId());
	    ring->addParam("peerid",tmp);
	    ring->addParam("targetid",tmp);
	    Engine::enqueue(ring);
	}
    }
    else {
	if (!ok)
	    Debug(this,DebugNote,"Call '%s' failed on '%s' target '%s', error '%s' [%p]",
		getPeerId().c_str(),tmp.c_str(),dest->c_str(),error,this);
	else if (!master)
	    Debug(this,DebugAll,"Call '%s' target '%s' slave '%s' lost master during execute [%p]",
		getPeerId().c_str(),dest->c_str(),tmp.c_str(),this);
	else
	    Debug(this,DebugAll,"Call '%s' target '%s' slave '%s' execute succeeded with no peer [%p]",
		getPeerId().c_str(),dest->c_str(),tmp.c_str(),this);
	ok = false;
	slave->lostMaster(error);
    }
    slave->deref();
    return ok;
}

bool ForkMaster::startCalling(Message& msg)
{
#ifdef XDEBUG
    Debugger dbg(debugAt(DebugInfo) ? DebugInfo : 50,
	"ForkMaster startCalling"," '%s' [%p]",id().c_str(),this);
#endif
    m_exec = new Message(msg);
    m_chanMsgs = msg.getBoolValue("fork.chanmsgs",(msg.getParam("pbxoper") != 0));
    if (m_chanMsgs) {
	Message* m = new Message("chan.startup");
	m->addParam("id",id());
	m->addParam("module",__plugin.name());
	m->addParam("status","outgoing");
	m->addParam("cdrtrack",String::boolText(false));
	m->addParam("pbxguest",String::boolText(true));
	m->addParam("fork.origid",getPeerId());
	m->copyParams(msg,"caller,callername,called,billid,username");
	Engine::enqueue(m);
    }
    // stoperror is OBSOLETE
    m_failures = msg.getValue("fork.stop",msg.getValue("stoperror"));
    if (m_failures.endsWith("^")) {
	m_failuresRev = true;
	m_failures = m_failures.substr(0,m_failures.length()-1);
    }
    m_setId = msg.getBoolValue("fork.setid");
    m_exec->clearParam("stoperror");
    m_exec->clearParam("fork.stop");
    m_exec->clearParam("fork.setid");
    m_exec->clearParam("peerid");
    m_exec->setParam("fork.master",id());
    m_exec->setParam("fork.origid",getPeerId());
    m_rtpForward = msg.getBoolValue("rtp_forward");
    m_rtpStrict = msg.getBoolValue("rtpstrict");
    if (!callContinue()) {
	const char* err = m_exec->getValue("reason");
	if (err)
	    msg.setParam("reason",err);
	err = m_exec->getValue("error");
	msg.setParam("error",err);
	XDebug(this,DebugAll,"startCalling failed refs=%u [%p]",refcount(),this);
	disconnect(err);
	return false;
    }
    if (m_rtpForward) {
	String tmp(m_exec->getValue("rtp_forward"));
	if (tmp != "accepted") {
	    // no RTP forwarding from now on
	    m_rtpForward = false;
	    tmp = String::boolText(false);
	}
	msg.setParam("rtp_forward",tmp);
    }
    msg.setParam("peerid",id());
    msg.setParam("targetid",id());
    return true;
}

bool ForkMaster::callContinue()
{
#ifdef XDEBUG
    Debugger dbg(debugAt(DebugInfo) ? DebugInfo : 50,
	"ForkMaster callContinue"," '%s' [%p]",id().c_str(),this);
#endif
    m_timer = 0;
    m_timerDrop = false;
    int forks = 0;
    while (m_exec && !m_answered) {
	// get the fake media source at start of each group
	m_media = m_exec->getValue("fork.fake");
	String* dest = getNextDest();
	if (!dest)
	    break;
	XDebug(this,DebugAll,"Handling target #%u '%s' [%p]",++m_targetIdx,dest->c_str(),this);
	if (dest->startSkip("|",false)) {
	    m_execNext = false;
	    if (*dest) {
		String tmp(*dest);
		int tout = 0;
		if (tmp.startSkip("next=",false) && ((tout = tmp.toInteger()) > 0)) {
		    m_timer = 1000 * tout + Time::now();
		    m_timerDrop = false;
		}
		else if (tmp.startSkip("drop=",false) && ((tout = tmp.toInteger()) > 0)) {
		    m_timer = 1000 * tout + Time::now();
		    m_timerDrop = true;
		}
		else if (tmp.startSkip("exec=",false) && ((tout = tmp.toInteger()) > 0)) {
		    m_timer = 1000 * tout + Time::now();
		    m_timerDrop = true;
		    m_execNext = true;
		}
		else if (tmp == "exec")
		    m_execNext = true;
		else
		    Debug(this,DebugMild,"Call '%s' ignoring modifier '%s' [%p]",
			getPeerId().c_str(),dest->c_str(),this);
	    }
	    dest->destruct();
	    if (forks)
		break;
	    m_timer = 0;
	    m_timerDrop = false;
	    continue;
	}
	if (forkSlave(dest))
	    ++forks;
	dest->destruct();
    }
    XDebug(this,DebugAll,"Exiting callContinue forks=%u [%p]",forks,this);
    return (forks > 0);
}

void ForkMaster::checkTimer(const Time& tmr)
{
    if (!m_timer || m_timer > tmr)
	return;
    m_timer = 0;
    if (m_timerDrop) {
	m_timerDrop = false;
	Debug(this,DebugNote,"Call '%s' dropping slaves on timer [%p]",
	    getPeerId().c_str(),this);
	clear(true);
    }
    else
	Debug(this,DebugNote,"Call '%s' calling more on timer [%p]",
	    getPeerId().c_str(),this);
    callContinue();
}

void ForkMaster::lostSlave(ForkSlave* slave, const char* reason)
{
    Lock lock(CallEndpoint::commonMutex());
    bool ringing = clearRinging(slave->id());
#ifdef XDEBUG
    GenObject* gen = 
#endif
    m_slaves.remove(slave,false);
#ifdef XDEBUG
    XDebug(this,gen ? DebugInfo : DebugMild,"Removed%s slave (%p) '%s' refs=%u [%p]",
	(gen ? "":" MISSING"),slave,slave->id().c_str(),slave->refcount(),this);
#endif
    if (m_answered)
	return;
    if (reason)
	m_exec->setParam("fork.reason",reason);
    if (reason && m_failures && (m_failures.matches(reason) != m_failuresRev)) {
	Debug(this,DebugCall,"Call '%s' terminating early on reason '%s' [%p]",
	    getPeerId().c_str(),reason,this);
    }
    else {
	// Slave have no type: we are still processing it, continue from processing point
	if (!slave->type())
	    return;
	unsigned int regulars = 0;
	unsigned int auxiliars = 0;
	unsigned int persistents = 0;
	for (ObjList* l = m_slaves.skipNull(); l; l = l->skipNext()) {
	    switch (static_cast<const ForkSlave*>(l->get())->type()) {
		case ForkSlave::Auxiliar:
		    auxiliars++;
		    break;
		case ForkSlave::Persistent:
		    persistents++;
		    break;
		default:
		    regulars++;
		    break;
	    }
	}
	Debug(this,DebugNote,
	    "Call '%s' lost%s slave '%s' reason '%s' remaining %u regulars, %u auxiliars, %u persistent [%p]",
	    getPeerId().c_str(),ringing ? " ringing" : "",
	    slave->id().c_str(),reason,
	    regulars,auxiliars,persistents,this);
	if (auxiliars && !regulars) {
	    Debug(this,DebugNote,"Dropping remaining %u auxiliars [%p]",auxiliars,this);
	    clear(true);
	}
	if (regulars || callContinue())
	    return;
	Debug(this,DebugCall,"Call '%s' failed after %d attempts with reason '%s' [%p]",
	    getPeerId().c_str(),m_index,reason,this);
    }
    m_timer = 0;
    lock.drop();
    disconnect(reason);
}

bool ForkMaster::msgAnswered(Message& msg, const String& dest)
{
    Lock lock(CallEndpoint::commonMutex());
    m_timer = 0;
    // make sure only the first succeeds
    if (m_answered)
	return false;
    RefPointer<CallEndpoint> peer = getPeer();
    if (!peer)
	return false;
    ForkSlave* slave = static_cast<ForkSlave*>(m_slaves[dest]);
    if (!slave)
	return false;
    RefPointer<CallEndpoint> call = slave->getPeer();
    if (!call)
	return false;
    m_media.clear();
    m_fake = false;
    m_answered = true;
    m_reason = msg.getValue("reason","pickup");
    Debug(this,DebugCall,"Call '%s' answered on '%s' by '%s' [%p]",
	peer->id().c_str(),dest.c_str(),call->id().c_str(),this);
    if (m_setId) {
	msg.setParam("fork.origid",msg.getValue("id"));
	msg.setParam("id",id());
    }
    else
	msg.setParam("fork.master",id());
    msg.setParam("peerid",peer->id());
    msg.setParam("targetid",peer->id());
    Message* r = new Message("chan.replaced",0,true);
    r->addParam("id",id());
    r->addParam("newid",call->id());
    r->addParam("peerid",peer->id());
    r->addParam("id.1",dest);
    r->addParam("newid.1",peer->id());
    r->addParam("peerid.1",call->id());
    lock.drop();
    clearEndpoint();
    call->connect(peer);
    Engine::enqueue(r);
    return true;
}

bool ForkMaster::msgProgress(Message& msg, const String& dest)
{
    Lock lock(CallEndpoint::commonMutex());
    if (m_answered)
	return false;
    if (m_ringing && (m_ringing != dest))
	return false;

    ForkSlave* slave = static_cast<ForkSlave*>(m_slaves[dest]);
    if (!slave)
	return false;
    RefPointer<CallEndpoint> peer = getPeer();
    if (!peer)
	return false;
    RefPointer<DataEndpoint> dataEp = getEndpoint();
    if (m_ringing.null())
	m_ringing = dest;
    if (m_fake || !dataEp) {
	const CallEndpoint* call = slave->getPeer();
	if (!call)
	    call = static_cast<const CallEndpoint*>(msg.userObject(YATOM("CallEndpoint")));
	if (call) {
	    dataEp = call->getEndpoint();
	    if (dataEp) {
		// don't use the media if it has no format and fake is possible
		if ((m_fake || m_media) &&
		    !(dataEp->getSource() && dataEp->getSource()->getFormat()))
		    dataEp = 0;
		else {
		    m_fake = false;
		    setEndpoint(dataEp);
		    m_media.clear();
		}
	    }
	}
    }
    if (m_setId) {
	msg.setParam("fork.origid",msg.getValue("id"));
	msg.setParam("id",id());
    }
    else
	msg.setParam("fork.master",id());
    msg.setParam("peerid",peer->id());
    msg.setParam("targetid",peer->id());
    if (m_media) {
	Debug(this,DebugInfo,"Call '%s' faking media '%s'",
	    peer->id().c_str(),m_media.c_str());
	String newMsg;
	if (m_exec)
	    newMsg = m_exec->getValue("fork.fakemessage");
	Message m("chan.attach");
	m.userData(this);
	m.addParam("id",id());
	m.addParam("source",m_media);
	m.addParam("single",String::boolText(true));
	if (m_exec)
	    m.copyParam(*m_exec,"autorepeat");
	m_media.clear();
	lock.drop();
	if (Engine::dispatch(m)) {
	    m_fake = true;
	    if (newMsg)
		msg = newMsg;
	}
    }
    Debug(this,DebugNote,"Call '%s' going on '%s' to '%s'%s%s [%p]",
	peer->id().c_str(),dest.c_str(),msg.getValue("id"),
	(dataEp || m_fake) ? " with audio data" : "",
	m_fake ? " (fake)" : "",this);
    return true;
}

bool ForkMaster::msgToSlaves(const Message& msg, const String& match)
{
    bool ok = false;
    for (ObjList* l = m_slaves.skipNull(); l; l = l->skipNext()) {
	ForkSlave* slave = static_cast<ForkSlave*>(l->get());
	if (slave->type() == ForkSlave::Auxiliar)
	    continue;
	Message* m = new Message(msg);
	m->setParam(match,slave->getPeerId());
	m->userData(msg.userData());
	ok = Engine::enqueue(m) || ok;
    }
    return ok;
}

bool ForkMaster::clearRinging(const String& id)
{
    if (m_ringing != id)
	return false;
    m_fake = false;
    m_ringing.clear();
    clearEndpoint();
    return true;
}

void ForkMaster::clear(bool softly)
{
    XDebug(this,DebugAll,"Clearing [%p]",this);
    RefPointer<ForkSlave> slave;
    CallEndpoint::commonMutex().lock();
    ListIterator iter(m_slaves);
    while ((slave = static_cast<ForkSlave*>(iter.get()))) {
	if (softly && (slave->type() == ForkSlave::Persistent))
	    continue;
	clearRinging(slave->id());
	m_slaves.remove(slave,false);
	slave->clearMaster();
	CallEndpoint::commonMutex().unlock();
	slave->lostMaster(m_reason);
	CallEndpoint::commonMutex().lock();
	slave = 0;
    }
    if (softly) {
	CallEndpoint::commonMutex().unlock();
	return;
    }
    TelEngine::destruct(m_exec);
    TelEngine::destruct(m_targets);
    CallEndpoint::commonMutex().unlock();
}


ForkSlave::ForkSlave(ForkMaster* master, const char* id)
    : CallEndpoint(id), m_master(master), m_type(Unknown), m_level(0)
{
    debugName(CallEndpoint::id());
    if (master) {
	debugChain(master);
	m_level = master->m_level;
	if (m_level > 0)
	    debugLevel(m_level);
    }
    DDebug(this,DebugAll,"ForkSlave::ForkSlave(%s) [%p]",master->id().c_str(),this);
}

ForkSlave::~ForkSlave()
{
    DDebug(this,DebugAll,"ForkSlave::~ForkSlave() [%p]",this);
}

void ForkSlave::destroyed()
{
    XDebug(this,DebugAll,"Destroying [%p]",this);
    CallEndpoint::commonMutex().lock();
    RefPointer<ForkMaster> master;
    clearMaster(&master);
    CallEndpoint::commonMutex().unlock();
    if (master)
	master->lostSlave(this,0);
    XDebug(this,DebugAll,"Destroyed [%p]",this);
    CallEndpoint::destroyed();
}

void ForkSlave::disconnected(bool final, const char* reason)
{
    XDebug(this,DebugAll,"Disconnected refs=%u [%p]",refcount(),this);
    CallEndpoint::commonMutex().lock();
    RefPointer<ForkMaster> master;
    clearMaster(&master);
    CallEndpoint::commonMutex().unlock();
    CallEndpoint::disconnected(final,reason);
    if (master)
	master->lostSlave(this,reason);
}

void ForkSlave::clearMaster(RefPointer<ForkMaster>* master)
{
    if (master)
	*master = m_master;
    m_master = 0;
    debugChain(&__plugin);
    if (m_level > 0)
	debugLevel(m_level);
}


ForkModule::ForkModule()
    : Module("callfork","misc"),
      m_hasRelays(false)
{
    Output("Loaded module Call Forker");
}

ForkModule::~ForkModule()
{
    Output("Unloading module Call Forker");
}

void ForkModule::initialize()
{
    Output("Initializing module Call Forker");
    setup();
    if (!m_hasRelays) {
	static const String s_prio("priorities");
	Configuration cfg(Engine::configFile("callfork"));
	installRelay(Execute,cfg.getIntValue(s_prio,messageName(Execute),100));
	installRelay(Masquerade,cfg.getIntValue(s_prio,messageName(Masquerade),10));
	installRelay(Locate,cfg.getIntValue(s_prio,messageName(Locate),40));
	installRelay(Answered,cfg.getIntValue(s_prio,messageName(Answered),20));
	installRelay(Ringing,cfg.getIntValue(s_prio,messageName(Ringing),20));
	installRelay(Progress,cfg.getIntValue(s_prio,messageName(Progress),20));
	int prio = cfg.getIntValue(s_prio,"generic",100);
	const NamedList* generic = cfg.getSection("messages");
	if (generic) {
	    NamedIterator iter(*generic);
	    while (!iter.eof()) {
		const NamedString* item = iter.get();
		if (TelEngine::null(item))
		    continue;
		switch (relayId(item->name())) {
		    case 0:
		    case Tone:
		    case Text:
		    case Update:
		    case Control:
		    case MsgExecute:
			break;
		    default:
			Debug(this,DebugWarn,"Refusing to fork message '%s'",item->name().c_str());
			continue;
		}
		int p = cfg.getIntValue(s_prio,item->name(),prio);
		ForkRelay* r = new ForkRelay(item->name(),*item,p);
		Debug(this,DebugInfo,"Will fork messages '%s' matching '%s' priority %d",
		    item->name().c_str(),item->c_str(),p);
		Engine::install(r);
		m_hasRelays = true;
	    }
	}
	else {
	    int p = cfg.getIntValue(s_prio,"chan.dtmf",prio);
	    Debug(this,DebugInfo,"Default fork for 'chan.dtmf' matching 'peerid' priority %d",p);
	    Engine::install(new ForkRelay("chan.dtmf","peerid",p));
	    m_hasRelays = true;
	}
    }
}

bool ForkModule::unload()
{
    if (m_hasRelays)
	return false;
    Lock lock(CallEndpoint::commonMutex(),500000);
    if (!lock.locked())
	return false;
    if (s_calls.count())
	return false;
    uninstallRelays();
    return true;
}

void ForkModule::statusParams(String& str)
{
    CallEndpoint::commonMutex().lock();
    str.append("total=",",") << s_current << ",forks=" << s_calls.count();
    CallEndpoint::commonMutex().unlock();
}

bool ForkModule::msgExecute(Message& msg)
{
    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    if (!ch)
	return false;
    String dest(msg.getParam("callto"));
    if (!dest.startSkip(MOD_PREFIX))
	return false;
    ObjList* targets = 0;
    if (dest)
	targets = dest.split(' ',false);
    else {
	for (int n = 1; true; n++) {
	    String prefix;
	    prefix << "callto." << n;
	    NamedString* ns = msg.getParam(prefix);
	    if (!ns)
		break;
	    if (TelEngine::null(ns))
		continue;
	    // Set target parameters from enclosed list
	    // Override/add new params from message sub-params
	    NamedPointer* np = YOBJECT(NamedPointer,ns);
	    NamedList* target = YOBJECT(NamedList,np);
	    if (target) {
		np->takeData();
		target->assign(*ns);
	    }
	    else
		target = new NamedList(*ns);
	    target->copySubParams(msg,prefix + ".");
	    if (!targets)
		targets = new ObjList;
	    targets->append(target);
	    // Clear from initial message
	    msg.clearParam(prefix,'.');
	}
    }
    if (!(targets && targets->skipNull())) {
	msg.setParam("error","failure");
	TelEngine::destruct(targets);
	return false;
    }
    CallEndpoint::commonMutex().lock();
    ForkMaster* master = new ForkMaster(targets,msg.getIntValue(YSTRING("fork.debug_level")));
    bool ok = master->connect(ch,msg.getValue("reason")) && master->startCalling(msg);
    CallEndpoint::commonMutex().unlock();
    master->deref();
    return ok;
}

bool ForkModule::msgLocate(Message& msg, bool masquerade)
{
    String tmp(msg.getParam("id"));
    if (!tmp.startsWith(MOD_PREFIX "/"))
	return false;
    Lock lock(CallEndpoint::commonMutex());
    CallEndpoint* c = static_cast<CallEndpoint*>(s_calls[tmp]);
    if (!c) {
	ForkMaster* m = static_cast<ForkMaster*>(s_calls[tmp.substr(0,tmp.rfind('/'))]);
	if (m)
	    c = static_cast<CallEndpoint*>(m->slaves()[tmp]);
    }
    if (!c)
	return false;
    if (masquerade) {
	tmp = msg.getValue("message");
	if (tmp.null())
	    return false;
	msg.clearParam("message");
	msg = tmp;
	if (tmp == "call.answered")
	    msg.setParam("cdrcreate",String::boolText(false));
	else if (tmp == "call.execute")
	    msg.setParam("cdrtrack",String::boolText(false));
	if (c->getPeer())
	    msg.setParam("peerid",c->getPeerId());
    }
    msg.userData(c);
    return !masquerade;
}

bool ForkModule::msgToMaster(Message& msg, bool answer)
{
    String dest(msg.getParam("peerid"));
    if (dest.null())
	dest = msg.getParam("targetid");
    if (!dest.startsWith(MOD_PREFIX "/"))
	return false;
    int slash = dest.rfind('/');
    CallEndpoint::commonMutex().lock();
    // the fork master will be kept referenced until we finish the work
    RefPointer<ForkMaster> m = static_cast<ForkMaster*>(s_calls[dest.substr(0,slash)]);
    CallEndpoint::commonMutex().unlock();
    if (m)
	return answer ? m->msgAnswered(msg,dest) : m->msgProgress(msg,dest);
    return false;
}

bool ForkModule::msgToSlaves(const Message& msg, const String& match)
{
    if (match.null())
	return false;
    const String* param = msg.getParam(match);
    if (TelEngine::null(param))
	return false;
    if (!param->startsWith(MOD_PREFIX "/"))
	return false;
    Lock lock(CallEndpoint::commonMutex());
    ForkMaster* m = static_cast<ForkMaster*>(s_calls[*param]);
    return m && m->msgToSlaves(msg,match);
}

bool ForkModule::received(Message& msg, int id)
{
    switch (id) {
	case Execute:
	    return msgExecute(msg);
	case Locate:
	    return msgLocate(msg,false);
	case Masquerade:
	    return msgLocate(msg,true);
	case Answered:
	    while (msgToMaster(msg,true))
		;
	    return false;
	case Progress:
	case Ringing:
	    while (msgToMaster(msg,false))
		;
	    return false;
	case Timer:
	    CallEndpoint::commonMutex().lock();
	    for (ObjList* l = s_calls.skipNull(); l; l = l->skipNext()) {
		RefPointer<ForkMaster> m = static_cast<ForkMaster*>(l->get());
		if (m)
		    m->checkTimer(msg.msgTime());
	    }
	    CallEndpoint::commonMutex().unlock();
	    // fall through
	default:
	    return Module::received(msg,id);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
