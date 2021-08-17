/*
 * msgsniff.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A sample message sniffer that inserts a wildcard message handler
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

#include <yatengine.h>

#include <stdio.h>
#include <math.h>

using namespace TelEngine;
namespace { // anonymous

static const char* s_debugs[] =
{
    "on",
    "off",
    "enable",
    "disable",
    "true",
    "false",
    "yes",
    "no",
    "filter",
    "timer",
    "age",
    "params",
    0
};


class SniffParamMatch : public String
{
public:
    inline SniffParamMatch(const char* name, const String& rex)
	: String(name), m_match(rex.length() < 2 || '^' != rex[rex.length() - 1])
	{
	    m_filter = m_match ? rex : rex.substr(0,rex.length() - 1);
	    m_filter.compile();
	}

    inline SniffParamMatch(const SniffParamMatch& other)
	: String(other), m_filter(other.m_filter), m_match(other.m_match)
	{
	    m_filter.compile();
	}

    Regexp m_filter;                     // Regexp to match parameter name
    bool m_match;                        // Match value, false to revert match (matches if value don't matches)
};

class SniffMatch : public RefObject
{
public:
    inline SniffMatch()
	: m_allParams(true)
	{}

    inline SniffMatch(const SniffMatch& other)
	: m_params(other.m_params.length()), m_allParams(other.m_allParams)
	{
	    setFilter(other.m_filter);
	    for (unsigned int i = 0; i < other.m_params.length(); i++) {
		const SniffParamMatch* m = static_cast<const SniffParamMatch*>(other.m_params[i]);
		if (m)
		    m_params.set(new SniffParamMatch(*m),i);
	    }
	}

    inline bool valid() const
	{ return m_filter || m_params.length(); };

    inline bool matches(const Message& msg) const {
	    if (m_filter && !m_filter.matches(msg))
		return false;
	    if (m_params.length()) {
		bool matched = false;
		for (unsigned int i = 0; i < m_params.length(); i++) {
		    SniffParamMatch* m = static_cast<SniffParamMatch*>(m_params[i]);
		    if (!m)
			continue;
		    const NamedString* ns = msg.getParam(*m);
		    bool ok = false;
		    // Match if:
		    // - filter set: regexp matches
		    // - filter not set: match if parameter is missing or empty
		    if (m->m_filter)
			ok = (m->m_match == m->m_filter.matches(TelEngine::c_safe(ns)));
		    else
			ok = TelEngine::null(ns);
		    if (ok) {
			matched = true;
			if (m_allParams)
			    continue;
			break;
		    }
		    if (m_allParams)
			return false;
		}
		if (!matched)
		    return false;
	    }
	    return true;
	}

    inline void setFilter(const String& value) {
	    m_filter = value;
	    m_filter.compile();
	}

    inline void setParams(const String& line) {
	    String s = line;
	    m_allParams = !s.startSkip("any");
	    ObjList list;
	    static const Regexp s_matchParam("^\\(.* \\)\\?\\([^= ]\\+\\)=\\([^=]*\\)$");
	    while (s.matches(s_matchParam)) {
		list.insert(new SniffParamMatch(s.matchString(2),s.matchString(3).trimSpaces()));
		s = s.matchString(1);
	    }
	    m_params.assign(list);
	}

    Regexp m_filter;                     // Filter for message name
    ObjVector m_params;                  // Filter(s) for message parameters
    bool m_allParams;                    // Match all parameters or at least one
};

class MsgSniff : public Plugin
{
public:
    MsgSniff();
    virtual void initialize();
    inline void setFilter(SniffMatch* flt = 0) {
	    Lock lck(m_mutex);
	    if (flt == m_filter)
		return;
	    m_filter = flt && flt->valid() ? flt : 0;
	    TelEngine::destruct(flt);
	}
    inline bool getFilter(RefPointer<SniffMatch>& flt) {
	    Lock lck(m_mutex);
	    flt = m_filter;
	    return (0 != flt);
	}
    inline bool filterMatches(const Message& msg) {
	    RefPointer<SniffMatch> flt;
	    return !getFilter(flt) || flt->matches(msg);
	}

private:
    bool m_first;
    RefPointer<SniffMatch> m_filter;
    Mutex m_mutex;
};

class SniffHandler : public MessageHandler
{
public:
    SniffHandler(const char* trackName = 0) : MessageHandler(0,0,trackName) { }
    virtual bool received(Message &msg);
};

class HookHandler : public MessagePostHook
{
public:
    virtual void dispatched(const Message& msg, bool handled);
};

static bool s_active = true;
static bool s_timer = false;
static u_int64_t s_minAge = 0;

INIT_PLUGIN(MsgSniff);


static void dumpParams(const Message &msg, String& par)
{
    unsigned n = msg.length();
    for (unsigned i = 0; i < n; i++) {
	const NamedString *s = msg.getParam(i);
	if (s) {
	    par << "\r\n  param['" << s->name() << "'] = ";
	    if (s->name() == YSTRING("password"))
		par << "(hidden)";
	    else
		par << "'" << *s << "'";
	    if (const NamedPointer* p = YOBJECT(NamedPointer,s)) {
		char buf[64];
		GenObject* obj = p->userData();
		::sprintf(buf," [%p]",obj);
		par << buf;
		if (obj)
		    par << " '" << obj->toString() << "'";
	    }
	}
    }
}

bool SniffHandler::received(Message &msg)
{
    if (!s_timer && (msg == YSTRING("engine.timer")))
	return false;
    if (msg == YSTRING("engine.command")) {
	static const String name("sniffer");
	String line(msg.getValue(YSTRING("line")));
	if (line.startSkip(name)) {
	    line >> s_active;
	    line.trimSpaces();
	    if (line.startSkip("timer"))
		(line >> s_timer).trimSpaces();
	    RefPointer<SniffMatch> crtFlt;
	    __plugin.getFilter(crtFlt);
	    SniffMatch* newFlt = 0;
	    if (line.startSkip("filter")) {
		if (crtFlt)
		    newFlt = new SniffMatch(*crtFlt);
		else
		    newFlt = new SniffMatch;
		newFlt->setFilter(line);
		line = "";
	    }
	    if (line.startSkip("params")) {
		if (!newFlt) {
		    if (crtFlt)
			newFlt = new SniffMatch(*crtFlt);
		    else
			newFlt = new SniffMatch;
		}
		newFlt->setParams(line);
		line = "";
	    }
	    if (newFlt) {
		__plugin.setFilter(newFlt);
		__plugin.getFilter(crtFlt);
	    }
	    if (line.startSkip("age")) {
		s_minAge = (u_int64_t)(1000000.0 * fabs(line.toDouble()));
		line = "";
	    }
	    msg.retValue() << "Message sniffer: " << (s_active ? "on" : "off");
	    if (s_active)
		msg.retValue() << ", timer: " << (s_timer ? "on" : "off");
	    if (s_active && crtFlt) {
		if (crtFlt->m_filter)
		    msg.retValue() << ", filter: " << crtFlt->m_filter;
		if (crtFlt->m_params.length()) {
		    msg.retValue() << ", params:";
		    if (!crtFlt->m_allParams)
			msg.retValue() << " any";
		    for (unsigned int i = 0; i < crtFlt->m_params.length(); i++) {
			SniffParamMatch* m = static_cast<SniffParamMatch*>(crtFlt->m_params[i]);
			msg.retValue() << " " << *m << "=" << m->m_filter << (m->m_match ? "" : "^");
		    }
		}
	    }
	    if (s_active && s_minAge)
		msg.retValue() << ", age: " << String().printf("%u.%06u",
		    (unsigned int)(s_minAge / 1000000),(unsigned int)(s_minAge % 1000000));
	    msg.retValue() << "\r\n";
	    return true;
	}
	line = msg.getParam(YSTRING("partline"));
	if (line.null()) {
	    if (name.startsWith(msg.getValue(YSTRING("partword"))))
		msg.retValue().append(name,"\t");
	}
	else if (name == line) {
	    line = msg.getValue(YSTRING("partword"));
	    for (const char** b = s_debugs; *b; b++)
		if (line.null() || String(*b).startsWith(line))
		    msg.retValue().append(*b,"\t");
	}
    }
    if (!s_active)
	return false;
    if (!__plugin.filterMatches(msg))
	return false;
    u_int64_t mt = msg.msgTime().usec();
    u_int64_t dt = Time::now() - mt;
    if (s_minAge && (dt < s_minAge))
	return false;
    String extra;
    if (msg.msgTimeEnqueue() && msg.msgTimeDispatch() > msg.msgTimeEnqueue()) {
	uint64_t dur = msg.msgTimeDispatch() - msg.msgTimeEnqueue();
	extra.printf(" queued=%u.%06u",(unsigned int)(dur / 1000000),
	    (unsigned int)(dur % 1000000));
    }
    String par;
    dumpParams(msg,par);
    Output("Sniffed '%s' time=%u.%06u age=%u.%06u%s%s\r\n  thread=%p '%s'\r\n  data=%p\r\n  retval='%s'%s",
	msg.c_str(),
	(unsigned int)(mt / 1000000),
	(unsigned int)(mt % 1000000),
	(unsigned int)(dt / 1000000),
	(unsigned int)(dt % 1000000),
	extra.safe(),
	(msg.broadcast() ? " (broadcast)" : ""),
	Thread::current(),
	Thread::currentName(),
	msg.userData(),
	msg.retValue().c_str(),
	par.safe());
    return false;
};


void HookHandler::dispatched(const Message& msg, bool handled)
{
    if (!s_active || (!s_timer && (msg == YSTRING("engine.timer"))))
	return;
    if (!__plugin.filterMatches(msg))
	return;
    u_int64_t dt = Time::now() - msg.msgTime().usec();
    if (s_minAge && (dt < s_minAge))
	return;
    String par;
    dumpParams(msg,par);
    const char* rval = msg.retValue().c_str();
    const char* rsep = "'";
    if (handled && rval && (rval[0] != '-' || rval[1]) && (msg == YSTRING("user.auth"))) {
	rval = "(hidden)";
	rsep = "";
    }
    Output("Returned %s '%s' delay=%u.%06u%s\r\n  thread=%p '%s'\r\n  data=%p\r\n  retval=%s%s%s%s",
	String::boolText(handled),
	msg.c_str(),
	(unsigned int)(dt / 1000000),
	(unsigned int)(dt % 1000000),
	(msg.broadcast() ? " (broadcast)" : ""),
	Thread::current(),
	Thread::currentName(),
	msg.userData(),
	rsep,rval,rsep,
	par.safe());
}


MsgSniff::MsgSniff()
    : Plugin("msgsniff"),
      m_first(true), m_mutex(false,"FilterSniff")
{
    Output("Loaded module MsgSniffer");
}

void MsgSniff::initialize()
{
    Output("Initializing module MsgSniffer");
    if (m_first) {
	m_first = false;
	s_active = Engine::config().getBoolValue("general","msgsniff",false);
	SniffMatch* m = new SniffMatch;
	m->setFilter(Engine::config().getValue("general","filtersniff"));
	m->setParams(Engine::config().getValue("general","filtersniffparams"));
	setFilter(m);
	s_minAge = (u_int64_t)(1000000.0 * fabs(Engine::config().getDoubleValue("general","agesniff")));
	String trackName = Engine::config().getValue("general","msgsniff_trackname");
	if (trackName) {
	    if (trackName.isBoolean()) {
		if (trackName.toBoolean())
		    trackName = "msgsniff";
		else
		    trackName = "";
	    }
	}
	Engine::install(new SniffHandler(trackName));
	Engine::self()->setHook(new HookHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
