/**
 * regexroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Regular expressions based routing
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2023 Null Team
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

#include <stdlib.h>
#include <string.h>

using namespace TelEngine;
namespace { // anonymous

#define DEFAULT_RULE "^\\(false\\|no\\|off\\|disable\\|f\\|0*\\)$^"
#define BLOCK_STACK 10
#define MAX_VAR_LEN 8100

class RegexConfig;
class GenericHandler;
class RouteHandler;
class PrerouteHandler;


static RegexConfig* s_cfg = 0;
static bool s_prerouteall;
static Mutex s_mutex(true,"RegexRoute");
static ObjList s_extra;

static NamedList s_vars("");
static Mutex s_varsMtx(true,"RegexRouteVars");

static NamedCounter s_dispatching("dispatching");
static NamedCounter s_processing("processing");
static NamedCounter s_serial("serial_number");

PrerouteHandler* s_preroute = 0;
RouteHandler* s_route = 0;


class GenericHandler : public MessageHandler
{
public:
    GenericHandler(const char* name, int prio, const char* context, const char* match,
	const char* trackName, const char* filterKey, const char* filterVal, bool addToExtra = true);
    ~GenericHandler();
    virtual bool received(Message &msg);
    inline bool sameHash(unsigned int hash) const
	{ return m_hash == hash; }
    inline unsigned int serial() const
	{ return m_serial; }
    inline void updateSerial()
	{ m_serial = s_serial.count(); }
    static inline unsigned int getHash(const char* name, int prio, const char* context,
	const char* match, const char* trackName,const char* filterKey, const char* filterVal)
	{  return String::hash(String(name) << prio << context << match
		<< trackName << filterKey << filterVal); }

private:
    String m_context;
    String m_match;
    unsigned int m_hash;
    unsigned int m_serial;
    bool m_inExtra;
};

class RouteHandler : public GenericHandler
{
public:
    RouteHandler(int prio, const char* trackName)
	: GenericHandler("call.route",prio,0,0,trackName,0,0,false)
	{ }
    virtual bool received(Message &msg);
};

class PrerouteHandler : public GenericHandler
{
public:
    PrerouteHandler(int prio, const char* trackName)
	: GenericHandler("call.preroute",prio,0,0,trackName,0,0,false)
	{ }
    virtual bool received(Message &msg);
};

class RegexConfig: public RefObject
{
public:
    enum BlockState {
	BlockRun  = 0,
	BlockSkip = 1,
	BlockDone = 2
    };
    RegexConfig(const String& confName);
    void initialize(bool first);
    void setDefault(Regexp& reg);
    bool oneMatch(Message& msg, Regexp& reg, String& match, const String& context,
        unsigned int rule, const String& trace = String::empty(), ObjList* traceLst = 0);
    bool oneContext(Message &msg, String &str, const String &context, String &ret,
	const String& trace = String::empty(), int traceLevel = DebugNote, ObjList* traceLst = 0,
	bool warn = false, int depth = 0);
    inline unsigned int sectCount() const
	{ return m_cfg.count(); }

private:
    Configuration m_cfg;
    bool m_extended;
    bool m_insensitive;
    int m_maxDepth;
    String m_defRule;
};

class RegexRoutePlugin : public Module
{
public:
    RegexRoutePlugin();
    virtual ~RegexRoutePlugin()
	{ TelEngine::destruct(s_cfg); }
    virtual void initialize();
    void initVars(NamedList* sect, bool replace = true);
    virtual void statusParams(String& str);

private:
    bool m_first;
};

class RegexRouteDebug : public Module
{
public:
    RegexRouteDebug();
    inline bool enabled() const
	{ return m_enabled; }
    virtual void initialize();

private:
    bool m_enabled;
};

INIT_PLUGIN(RegexRoutePlugin);
static RegexRouteDebug __plugin_debug;

static inline String& vars(String& s, String* vName = 0)
{
    if (s.startSkip("$",false)) {
	s.trimBlanks();
	if (vName)
	    *vName = s;
	s_varsMtx.lock();
	s = s_vars.getValue(s);
	s_varsMtx.unlock();
    }
    return s;
}

enum {
    OPER_ADD,
    OPER_SUB,
    OPER_MUL,
    OPER_DIV,
    OPER_MOD,
    OPER_EQ,
    OPER_NE,
    OPER_GT,
    OPER_LT,
    OPER_GE,
    OPER_LE,
};

static void mathOper(String& str, String& par, int sep, int oper)
{
    str = par.substr(0,sep);
    par = par.substr(sep+1);
    int len = str.length();
    sep = par.find(',');
    s_varsMtx.lock();
    if (sep >= 0) {
	String tmp = par.substr(sep+1);
	len = vars(tmp).toInteger();
	par = par.substr(0,sep);
    }
    int p1 = vars(str).toInteger(0,10);
    int p2 = vars(par).toInteger(0,10);
    s_varsMtx.unlock();
    switch (oper) {
	case OPER_ADD:
	    str = p1+p2;
	    break;
	case OPER_SUB:
	    str = p1-p2;
	    break;
	case OPER_MUL:
	    str = p1*p2;
	    break;
	case OPER_DIV:
	    str = p2 ? p1/p2 : 0;
	    break;
	case OPER_MOD:
	    str = p2 ? p1%p2 : 0;
	    break;
	case OPER_EQ:
	    str = (p1 == p2);
	    len = 0;
	    return;
	case OPER_NE:
	    str = (p1 != p2);
	    len = 0;
	    return;
	case OPER_GT:
	    str = (p1 > p2);
	    len = 0;
	    return;
	case OPER_LT:
	    str = (p1 < p2);
	    len = 0;
	    return;
	case OPER_GE:
	    str = (p1 >= p2);
	    len = 0;
	    return;
	case OPER_LE:
	    str = (p1 <= p2);
	    len = 0;
	    return;
    }
    len -= (int)str.length();
    if (len > 0) {
	// left pad the result to the desired length
	String tmp('0',len);
	if (str[0] == '-')
	    str = "-" + tmp + str.substr(1);
	else
	    str = tmp + str;
    }
}

static void evalFunc(String& str, Message& msg)
{
    if (str.null())
	str = ";";
    else if (str == "$")
	;
    else if (str.startSkip("++",false)) {
	String tmp;
	Lock lck(s_varsMtx);
	str = vars(str,&tmp).toInteger(0,10) + 1;
	if (tmp)
	    s_vars.setParam(tmp,str);
    }
    else if (str.startSkip("--",false)) {
	String tmp;
	Lock lck(s_varsMtx);
	str = vars(str,&tmp).toInteger(0,10) - 1;
	if (tmp)
	    s_vars.setParam(tmp,str);
    }
    else {
	bool bare = true;
	int sep = str.find(',');
	String par;
	if (sep > 0) {
	    bare = false;
	    par = str.substr(sep+1);
	    str = str.substr(0,sep);
	    sep = par.find(',');
	}
	if (str == YSTRING("length"))
	    str = vars(par).length();
	else if (str == YSTRING("upper"))
	    str = vars(par).toUpper();
	else if (str == YSTRING("lower"))
	    str = vars(par).toLower();
	else if (str == YSTRING("chr"))
	    str = static_cast<char>(0xff & vars(par).toInteger());
	else if ((sep >= 0) && ((str == YSTRING("streq")) || (str == YSTRING("strne")))) {
	    bool ret = (str == YSTRING("strne"));
	    str = par.substr(sep+1);
	    par = par.substr(0,sep);
	    s_varsMtx.lock();
	    vars(str);
	    vars(par);
	    s_varsMtx.unlock();
	    ret ^= (str == par);
	    str = ret;
	}
	else if ((sep >= 0) && (str == YSTRING("strpos"))) {
	    str = par.substr(sep+1);
	    par = par.substr(0,sep);
	    s_varsMtx.lock();
	    vars(str);
	    vars(par);
	    s_varsMtx.unlock();
	    str = str.find(par);
	}
	else if ((sep >= 0) && ((str == "add") || (str == "+")))
	    mathOper(str,par,sep,OPER_ADD);
	else if ((sep >= 0) && ((str == "sub") || (str == "-")))
	    mathOper(str,par,sep,OPER_SUB);
	else if ((sep >= 0) && ((str == "mul") || (str == "*")))
	    mathOper(str,par,sep,OPER_MUL);
	else if ((sep >= 0) && ((str == "div") || (str == "/")))
	    mathOper(str,par,sep,OPER_DIV);
	else if ((sep >= 0) && ((str == "mod") || (str == "%")))
	    mathOper(str,par,sep,OPER_MOD);
	else if ((sep >= 0) && (str == "eq"))
	    mathOper(str,par,sep,OPER_EQ);
	else if ((sep >= 0) && (str == "ne"))
	    mathOper(str,par,sep,OPER_NE);
	else if ((sep >= 0) && ((str == "gt") || (str == ">")))
	    mathOper(str,par,sep,OPER_GT);
	else if ((sep >= 0) && ((str == "lt") || (str == "<")))
	    mathOper(str,par,sep,OPER_LT);
	else if ((sep >= 0) && (str == "ge"))
	    mathOper(str,par,sep,OPER_GE);
	else if ((sep >= 0) && (str == "le"))
	    mathOper(str,par,sep,OPER_LE);
	else if (str == YSTRING("random")) {
	    str.clear();
	    vars(par);
	    for (unsigned int i = 0; i < par.length(); i++) {
		if (par.at(i) == '?')
		    str << (int)(Random::random() % 10);
		else
		    str << par.at(i);
	    }
	}
	else if (str == YSTRING("hex")) {
	    char hsep = ' ';
	    int len = 0;
	    if (sep >= 0) {
		str = par.substr(sep+1);
		par = par.substr(0,sep);
		sep = str.find(',');
		if (sep >= 0) {
		    hsep = str.at(sep+1);
		    str = str.substr(0,sep);
		}
		len = str.toInteger();
	    }
	    int val = par.toInteger();
	    unsigned char buf[4];
	    buf[0] = (unsigned char)val;
	    buf[1] = (unsigned char)(val >> 8);
	    buf[2] = (unsigned char)(val >> 16);
	    buf[3] = (unsigned char)(val >> 24);
	    if (len > 4)
		len = 4;
	    else if (len <= 0) {
		if (buf[3])
		    len = 4;
		else if (buf[2])
		    len = 3;
		else if (buf[1])
		    len = 2;
		else
		    len = 1;
	    }
	    str.hexify(&buf,len,hsep);
	}
	else if ((sep > 0) && ((str == YSTRING("index")) || (str == YSTRING("rotate")))) {
	    bool rotate = (str == YSTRING("rotate"));
	    String vname;
	    str = par.substr(0,sep);
	    par = par.substr(sep+1).trimBlanks();
	    Lock lck(s_varsMtx);
	    int idx = vars(str,&vname).toInteger(0,10);
	    ObjList* lst = par.split(',');
	    str.clear();
	    par.clear();
	    unsigned int n = lst->count();
	    if (n) {
		int i = idx % n;
		for (ObjList* l = lst->skipNull(); l; l = l->skipNext()) {
		    String* s = static_cast<String*>(l->get());
		    vars(*s);
		    if (rotate) {
			if (i > 0)
			    par.append(*s," ");
			else
			    str.append(*s," ");
		    }
		    else if (0 == i) {
			str = *s;
			break;
		    }
		    i--;
		}
		str.append(par," ");
		// auto increment the index variable if any
		if (vname) {
		    par = (idx + 1) % n;
		    s_vars.setParam(vname,par);
		}
	    }
	    lst->destruct();
	}
	else if ((sep >= 0) && (str == YSTRING("config"))) {
	    str = par.substr(0,sep).trimBlanks();
	    par = par.substr(sep+1).trimBlanks();
	    str = Engine::config().getValue(str,par);
	}
	else if (str == YSTRING("engine"))
	    str = Engine::runParams().getValue(vars(par));
	else if (str == YSTRING("loaded"))
	    str = Engine::self()->pluginLoaded(par);
	else if (str == YSTRING("message")) {
	    if (sep >= 0) {
		str = par.substr(sep+1).trimBlanks();
		par = par.substr(0,sep).trimBlanks();
	    }
	    else
		str.clear();
	    if (par.null() || par == YSTRING("name"))
		str = msg;
	    else if (par == YSTRING("time"))
		str = msg.msgTime().sec();
	    else if (par == YSTRING("broadcast"))
		str = msg.broadcast();
	    else if (par == YSTRING("retval"))
		str = msg.retValue();
	    else if (par == YSTRING("count"))
		str = msg.count();
	    else if (par == YSTRING("parameters")) {
		par = str;
		if (par.null())
		    par = ",";
		str.clear();
		for (const ObjList* l = msg.paramList()->skipNull(); l; l = l->skipNext())
		    str.append(static_cast<const NamedString*>(l->get())->name(),par);
	    }
	    else
		str.clear();
	}
	else if (str == YSTRING("variables")) {
	    if (sep >= 0) {
		str = par.substr(sep+1).trimBlanks();
		par = par.substr(0,sep).trimBlanks();
	    }
	    else
		str.clear();
	    if (par.null() || par == YSTRING("count")) {
		Lock l(s_varsMtx);
		str = s_vars.count();
	    }
	    else if (par == YSTRING("list")) {
		par = str;
		if (par.null())
		    par = ",";
		str.clear();
		Lock l(s_varsMtx);
		for (const ObjList* l = s_vars.paramList()->skipNull(); l; l = l->skipNext()) {
		    if (str.length() > MAX_VAR_LEN) {
			Debug(&__plugin,DebugWarn,"Truncating output of $(variables,list)");
			str.append("...",par);
			break;
		    }
		    str.append(static_cast<const NamedString*>(l->get())->name(),par);
		}
	    }
	    else {
		Lock l(s_varsMtx);
		str = !!s_vars.getParam(par);
	    }
	}
	else if (str == YSTRING("runid")) {
	    str.clear();
	    str << Engine::runId();
	}
	else if (str == YSTRING("nodename"))
	    str = Engine::nodeName();
	else if (str == YSTRING("threadname"))
	    str = Thread::currentName();
	else if (str == YSTRING("accepting"))
	    str = lookup(Engine::accept(),Engine::getCallAcceptStates());
	else if ((sep >= 0) && (str == YSTRING("transcode"))) {
	    str = par.substr(0,sep);
	    par = par.substr(sep+1).trimBlanks();
	    ObjList* fmts = DataTranslator::allFormats(par,
		(str.find('e') < 0),
		(str.find('r') < 0),
		(str.find('c') < 0));
	    str.clear();
	    str.append(fmts,",");
	    TelEngine::destruct(fmts);
	}
	else if (str == YSTRING("dispatching"))
	    str = s_dispatching.count();
	else if (str == YSTRING("timestamp")) {
	    char buf[32];
	    Debugger::formatTime(buf);
	    str = buf;
	}
	else if (bare && str.trimBlanks()) {
	    Lock l(s_varsMtx);
	    str = s_vars.getValue(str);
	}
	else {
	    Debug(&__plugin,DebugWarn,"Invalid function '%s'",str.c_str());
	    str.clear();
	}
    }
}

// handle $(function) replacements
static void replaceFuncs(String &str, Message& msg)
{
    int p1;
    while ((p1 = str.find("$(")) >= 0) {
	int p2 = str.find(')',p1+2);
	if (p2 > 0) {
	    String v = str.substr(p1+2,p2-p1-2);
	    v.trimBlanks();
	    DDebug(&__plugin,DebugAll,"Replacing function '%s'",
		v.c_str());
	    evalFunc(v,msg);
	    str = str.substr(0,p1) + v + str.substr(p2+1);
	}
	else {
	    Debug(&__plugin,DebugWarn,"Unmatched function end: '%s'",str.c_str()+p1);
	    break;
	}
    }
}

// handle ;paramname[=value] assignments
static void setMessage(const String& match, Message& msg, String& line, Message* target = 0)
{
    if (!target)
	target = &msg;
    ObjList *strs = line.split(';');
    bool first = true;
    for (ObjList *p = strs; p; p=p->next()) {
	String *s = static_cast<String*>(p->get());
	Lock l(s_varsMtx);
	if (s) {
	    *s = match.replaceMatches(*s);
	    msg.replaceParams(*s);
	    replaceFuncs(*s,msg);
	}
	if (first) {
	    first = false;
	    line = s ? *s : String::empty();
	    continue;
	}
	if (s && !s->trimBlanks().null()) {
	    int q = s->find('=');
	    if (q > 0) {
		String n = s->substr(0,q);
		String v = s->substr(q+1);
		n.trimBlanks();
		v.trimBlanks();
		DDebug(&__plugin,DebugAll,"Setting '%s' to '%s'",n.c_str(),v.c_str());
		if (n.startSkip("$",false))
		    s_vars.setParam(n,v);
		else
		    target->setParam(n,v);
	    }
	    else {
		DDebug(&__plugin,DebugAll,"Clearing parameter '%s'",s->c_str());
		if (s->startSkip("$",false))
		    s_vars.clearParam(*s);
		else
		    target->clearParam(*s);
	    }
	}
    }
    strs->destruct();
}

#define CHECK_HANDLER(handler,classType,name,priority,trackName) \
do { \
    if (!handler) \
	Engine::install(handler = new classType(priority,trackName)); \
    else { \
	unsigned int hash = GenericHandler::getHash(name,priority,0,0,trackName,0,0); \
	if (!handler->sameHash(hash)) { \
	    classType* tmp = handler; \
	    Engine::install(handler = new classType(priority,trackName)); \
	    TelEngine::destruct(tmp); \
	} \
    } \
} while(false);

static GenericHandler* findHandler(unsigned int hash)
{
    Lock l(s_mutex);
    for (ObjList* o = s_extra.skipNull(); o; o = o->skipNext()) {
	GenericHandler* h = static_cast<GenericHandler*>(o->get());
	if (h->sameHash(hash))
	    return h;
    }
    return 0;
}

RegexConfig::RegexConfig(const String& confName)
    : m_extended(false), m_insensitive(false),
    m_maxDepth(5)
{
    Debug(&__plugin,DebugAll,"Creating new RegexConfig for configuration name '%s' [%p]",
	confName.c_str(),this);
    m_cfg = confName;
}

void RegexConfig::initialize(bool first)
{
    m_cfg.load();
    NamedList* once = m_cfg.getSection("$once");
    if (once && (first || m_cfg.getBoolValue("priorities","add_once",true)))
	__plugin.initVars(once,first);
    __plugin.initVars(m_cfg.getSection("$init"));
    s_prerouteall = m_cfg.getBoolValue("priorities","prerouteall",false);
    m_extended = m_cfg.getBoolValue("priorities","extended",false);
    m_insensitive = m_cfg.getBoolValue("priorities","insensitive",false);
    int depth = m_cfg.getIntValue("priorities","maxdepth",5);
    if (depth < 5)
	depth = 5;
    else if (depth > 100)
	depth = 100;
    m_maxDepth = depth;
    m_defRule = m_cfg.getValue("priorities","defaultrule",DEFAULT_RULE);

    const char* trackName = m_cfg.getBoolValue("priorities","trackparam",true) ?
	__plugin.name().c_str() : (const char*)0;
    unsigned priority = m_cfg.getIntValue("priorities","preroute",100);
    if (priority) {
	CHECK_HANDLER(s_preroute,PrerouteHandler,"call.preroute",priority,trackName);
    }
    else
	TelEngine::destruct(s_preroute);
    priority = m_cfg.getIntValue("priorities","route",100);
    if (priority) {
	CHECK_HANDLER(s_route,RouteHandler,"call.route",priority,trackName);
    }
    else
	TelEngine::destruct(s_route);

    NamedList* l = m_cfg.getSection("extra");
    if (l) {
	unsigned int len = l->length();
	for (unsigned int i=0; i<len; i++) {
	    NamedString* n = l->getParam(i);
	    if (n) {
		// message=priority[,[parameter][,context],filter_param,filter_match]
		ObjList* o = n->split(',');
		const String* s = static_cast<const String*>(o->at(0));
		int prio = s ? s->toInteger(100) : 100;
		const char* match = TelEngine::c_str(static_cast<const String*>(o->at(1)));
		const char* context = TelEngine::c_str(static_cast<const String*>(o->at(2)));
		if (TelEngine::null(context))
		    context = n->name().c_str();
		const char* key = TelEngine::c_str(static_cast<const String*>(o->at(3)));
		const char* val = TelEngine::c_str(static_cast<const String*>(o->at(4)));
		// check if we have the same handler already installed
		GenericHandler* old = findHandler(GenericHandler::getHash(n->name(),prio,context,match,trackName,key,val));
		if (m_cfg.getSection(context)) {
		    if (!old)
			Engine::install(new GenericHandler(n->name(),prio,context,match,trackName,key,val));
		    else
			old->updateSerial();
		}
		else {
		    Debug(DebugWarn,"Missing context [%s] for handling %s",context,n->name().c_str());
		    TelEngine::destruct(old);
		}
		TelEngine::destruct(o);
	    }
	}
    }
    // Remove non-updated handlers
    Lock lck(s_mutex);
    for (ObjList* o = s_extra.skipNull(); o; ) {
	GenericHandler* h = static_cast<GenericHandler*>(o->get());
	if (h->serial() < (unsigned int)s_serial.count()) {
	    lck.drop();
	    Debug(DebugAll,"Removing handler '%s' (%p) on prio '%u' with serial number '%u', current serial number '%u'",
		    h->toString().c_str(),h,h->priority(),h->serial(),s_serial.count());
	    TelEngine::destruct(h);
	    lck.acquire(s_mutex);
	    o = o->skipNull();
	    continue;
	}
	lck.acquire(s_mutex);
	o = o->skipNext();
    }
}

#undef CHECK_HANDLER

// helper function to set the default regexp
void RegexConfig::setDefault(Regexp& reg)
{
    if (m_defRule.null())
	return;
    if (reg.null())
	reg = m_defRule;
    else if (reg == "^") {
	// deal with double '^' at end
	if (m_defRule.endsWith("^"))
	    reg.assign(m_defRule,m_defRule.length()-1);
	else
	    reg = m_defRule + reg;
    }
}


#define TRACE_RULE(dbgLevel,traceId,lst,args,...) \
do { \
    Trace(traceId,&__plugin,dbgLevel,args,##__VA_ARGS__); \
    if (lst) { \
	String* tmp = new String(); \
	tmp->printf(args,##__VA_ARGS__); \
	lst->append(tmp); \
    } \
} while (false)

#define TRACE_DBG(dbgLevel,traceId,lst,args,...) \
do { \
    TraceDebug(traceId,&__plugin,dbgLevel,args,##__VA_ARGS__); \
    if (lst) { \
	String* tmp = new String(); \
	tmp->printf(args,##__VA_ARGS__); \
	lst->append(tmp); \
    } \
} while (false)

#define TRACE_DBG_ONLY(dbgLevel,traceId,lst,args,...) \
do { \
    TraceDebug(traceId,dbgLevel,args,##__VA_ARGS__); \
    if (lst) { \
	String* tmp = new String(); \
	tmp->printf(args,##__VA_ARGS__); \
	lst->append(tmp); \
    } \
} while (false)

// helper function to process one match attempt
bool RegexConfig::oneMatch(Message& msg, Regexp& reg, String& match, const String& context,
        unsigned int rule, const String& trace, ObjList* traceLst)
{
    if (reg.startsWith("${")) {
	// handle special matching by param ${paramname}regexp
	int p = reg.find('}');
	if (p < 3) {
	    TRACE_DBG(DebugWarn,trace,traceLst,"Invalid parameter match '%s' in rule #%u in context '%s'",
		reg.c_str(),rule,context.c_str());
	    return false;
	}
	match = reg.substr(2,p-2);
	reg = reg.substr(p+1);
	match.trimBlanks();
	reg.trimBlanks();
	String def;
	p = match.find('$');
	if (p >= 0) {
	    // param is in ${<name>$<default>} format
	    def = match.substr(p+1);
	    match = match.substr(0,p);
	    match.trimBlanks();
	}
	setDefault(reg);
	if (match.null() || reg.null()) {
	    TRACE_DBG(DebugWarn,trace,traceLst,"Missing parameter or rule in rule #%u in context '%s'",
		rule,context.c_str());
	    return false;
	}
	DDebug(&__plugin,DebugAll,"Using message parameter '%s' default '%s'",
	    match.c_str(),def.c_str());
	match = msg.getValue(match,def);
    }
    else if (reg.startsWith("$(")) {
	// handle special matching by param $(function)regexp
	int p = reg.find(')');
	if (p < 3) {
	    TRACE_DBG(DebugWarn,trace,traceLst,"Invalid function match '%s' in rule #%u in context '%s'",
		reg.c_str(),rule,context.c_str());
	    return false;
	}
	match = reg.substr(0,p+1);
	reg = reg.substr(p+1);
	reg.trimBlanks();
	setDefault(reg);
	if (reg.null()) {
	    TRACE_DBG(DebugWarn,trace,traceLst,"Missing rule in rule #%u in context '%s'",
		rule,context.c_str());
	    return false;
	}
	DDebug(&__plugin,DebugAll,"Using function '%s'",match.c_str());
	msg.replaceParams(match);
	replaceFuncs(match,msg);
    }
    match.trimBlanks();

    bool doMatch = true;
    if (reg.endsWith("^")) {
	// reverse match on final ^ (makes no sense in a regexp)
	doMatch = false;
	reg = reg.substr(0,reg.length()-1);
    }
    return (match.matches(reg) == doMatch);
}

// process one context, can call itself recursively
bool RegexConfig::oneContext(Message &msg, String &str, const String &context, String &ret,
    const String& trace, int traceLevel, ObjList* traceLst, bool warn, int depth)
{
    if (context.null())
	return false;
    if (depth > m_maxDepth) {
	TRACE_DBG(DebugWarn,trace,traceLst,"Possible loop detected, current context '%s'",context.c_str());
	return false;
    }

    TRACE_RULE(traceLevel,trace,traceLst,"Searching match for %s",str.c_str());
    NamedList *l = m_cfg.getSection(context);
    if (l) {
	unsigned int blockDepth = 0;
	BlockState blockStack[BLOCK_STACK];
	unsigned int len = l->length();
	for (unsigned int i = 0; i < len; i++) {
	    const NamedString* n = l->getParam(i);
	    if (!n)
		continue;
	    BlockState blockThis = (blockDepth > 0) ? blockStack[blockDepth-1] : BlockRun;
	    BlockState blockLast = BlockSkip;
	    Regexp reg(n->name(),m_extended,m_insensitive);
	    if (reg.startSkip("}",false)) {
		if (!blockDepth) {
		    TRACE_DBG(DebugWarn,trace,traceLst,"Got '}' outside block in line #%u in context '%s'",
			i+1,context.c_str());
		    continue;
		}
		if (reg.trimBlanks().null())
		    reg = ".*";
		blockDepth--;
		blockLast = blockThis;
		blockThis = (blockDepth > 0) ? blockStack[blockDepth-1] : BlockRun;
	    }
	    static Regexp s_blockStart("^\\(.*=[[:space:]]*\\)\\?{$");
	    if (s_blockStart.matches(*n)) {
		// start of a new block
		if (blockDepth >= BLOCK_STACK) {
		    TRACE_DBG(DebugWarn,trace,traceLst,"Block stack overflow in line #%u in context '%s'",
			i+1,context.c_str());
		    return false;
		}
		// assume block is done
		BlockState blockEnter = BlockDone;
		if (BlockRun == blockThis) {
		    // if we just returned from a false inner block to a true outer block
		    if (BlockSkip == blockLast)
			blockEnter = BlockSkip;
		    else
			blockThis = BlockDone;
		}
		blockStack[blockDepth++] = blockEnter;
	    }
	    else if (BlockSkip != blockLast)
		blockThis = BlockDone;
	    XDebug(&__plugin,DebugAll,"%s:%d(%u:%s) %s=%s",context.c_str(),i+1,
		blockDepth,String::boolText(BlockRun == blockThis),
		n->name().c_str(),n->c_str());
	    if (BlockRun != blockThis)
		continue;

	    String val(*n);
	    String match;
	    bool ok;
	    do {
		match = str;
		ok = oneMatch(msg,reg,match,context,i+1,trace,traceLst);
		if (ok) {
		    if (val.startSkip("or")) {
			do {
			    int p = val.find('=');
			    if (p < 0) {
				TRACE_DBG(DebugWarn,trace,traceLst,"Malformed 'or' rule #%u in context '%s'",
				    i+1,context.c_str());
				ok = false;
				break;
			    }
			    val = val.substr(p+1);
			    val.trimBlanks();
			} while (ok && (val.startSkip("or") || val.startSkip("if") || val.startSkip("and")));
			break;
		    }
		    if (!(val.startSkip("if") || val.startSkip("and")))
			break;
		}
		else if (val.startSkip("or"))
		    ok = true;
		if (ok) {
		    int p = val.find('=');
		    if (p >= 1) {
			reg = val.substr(0,p);
			val = val.substr(p+1);
			reg.trimBlanks();
			val.trimBlanks();
			if (!reg.null()) {
			    NDebug(&__plugin,DebugAll,"Secondary match rule '%s' by rule #%u in context '%s'",
				reg.c_str(),i+1,context.c_str());
			    continue;
			}
		    }
		    TRACE_DBG(DebugWarn,trace,traceLst,"Missing 'if' in rule #%u in context '%s'",
			i+1,context.c_str());
		    ok = false;
		}
	    } while (ok);
	    TRACE_RULE(traceLevel,trace,traceLst,"Matched:%s %s:%d - %s=%s",
		     String::boolText(ok),context.c_str(),i,n->name().c_str(),n->safe());
	    if (!ok)
		continue;

	    int level = 0;
	    if (val.startSkip("echo") || val.startSkip("output")
		    || (val.startSkip("debug") && ((level = DebugAll)))) {
		if (level) {
		    val >> level;
		    val.trimBlanks();
		    if (level < DebugTest)
			level = DebugTest;
		    else if (level > DebugAll)
			level = DebugAll;
		}
		// special case: display the line but don't set params
		val = match.replaceMatches(val);
		msg.replaceParams(val);
		replaceFuncs(val,msg);
		if (!level)
		    Output("%s",val.safe());
		else if (!__plugin_debug.enabled())
		    Debug(level,"%s",val.safe());
		else if (__plugin_debug.filterDebug(val))
		    Debug(&__plugin_debug,level,"%s",val.safe());
		continue;
	    }
	    else if (val == "{") {
		// mark block as being processed now
		if (blockDepth)
		    blockStack[blockDepth-1] = BlockRun;
		else
		    TRACE_DBG(DebugWarn,trace,traceLst,"Got '{' outside block in line #%u in context '%s'",
			i+1,context.c_str());
		continue;
	    }
	    bool disp = val.startSkip("dispatch");
	    if (disp || val.startSkip("enqueue")) {
		// special case: enqueue or dispatch a new message
		if (val && (val[0] != ';')) {
		    Message* m = new Message("");
		    // parameters are set in the new message
		    setMessage(match,msg,val,m);
		    val.trimBlanks();
		    if (val) {
			*m = val;
			m->userData(msg.userData());
			NDebug(&__plugin,DebugAll,"%s new message '%s' by rule #%u '%s' in context '%s'",
			    (disp ? "Dispatching" : "Enqueueing"),
			    val.c_str(),i+1,n->name().c_str(),context.c_str());
			if (disp) {
			    s_dispatching.inc();
			    Engine::dispatch(m);
			    s_dispatching.dec();
			}
			else if (Engine::enqueue(m))
			    m = 0;
		    }
		    TelEngine::destruct(m);
		}
		continue;
	    }
	    setMessage(match,msg,val);
	    warn = true;
	    val.trimBlanks();
	    if (val.null() || val.startSkip("noop")) {
		// special case: do nothing on empty target
		continue;
	    }
	    else if (val.startSkip("return")) {
		bool ok = val.toBoolean();
		NDebug(&__plugin,DebugAll,"Returning %s from context '%s'",
		    String::boolText(ok),context.c_str());
		return ok;
	    }
	    else if (val.startSkip("goto") || val.startSkip("jump") ||
		((val.startSkip("@goto") || val.startSkip("@jump")) && !(warn = false))) {
		NDebug(&__plugin,DebugAll,"Jumping to context '%s' by rule #%u '%s'",
		    val.c_str(),i+1,n->name().c_str());
		return oneContext(msg,str,val,ret,trace,traceLevel,traceLst,warn,depth+1);
	    }
	    else if (val.startSkip("include") || val.startSkip("call") ||
		((val.startSkip("@include") || val.startSkip("@call")) && !(warn = false))) {
		NDebug(&__plugin,DebugAll,"Including context '%s' by rule #%u '%s'",
		    val.c_str(),i+1,n->name().c_str());
		if (oneContext(msg,str,val,ret,trace,traceLevel,traceLst,warn,depth+1)) {
		    DDebug(&__plugin,DebugAll,"Returning true from context '%s'", context.c_str());
		    return true;
		}
	    }
	    else if (val.startSkip("match") || val.startSkip("newmatch")) {
		if (!val.null()) {
		    NDebug(&__plugin,DebugAll,"Setting match string '%s' by rule #%u '%s' in context '%s'",
			val.c_str(),i+1,n->name().c_str(),context.c_str());
		    str = val;
		}
	    }
	    else if (val.startSkip("rename")) {
		if (!val.null()) {
		    NDebug(&__plugin,DebugAll,"Renaming message '%s' to '%s' by rule #%u '%s' in context '%s'",
			msg.c_str(),val.c_str(),i+1,n->name().c_str(),context.c_str());
		    msg = val;
		}
	    }
	    else if (val.startSkip("retval")) {
		NDebug(&__plugin,DebugAll,"Setting retValue length %u by rule #%u '%s' in context '%s'",
			val.length(),i+1,n->name().c_str(),context.c_str());
		ret = val;
	    }
	    else if (val.startSkip("msleep")) {
		val.trimBlanks();
		if (!val.null()) {
		    NDebug(&__plugin,DebugAll,"Sleeping for %s milliseconds by rule #%u '%s' in context '%s'",
			val.c_str(),i+1,n->name().c_str(),context.c_str());
		    uint64_t t = val.toInt64(0,0,0);
		    uint64_t count = t / Thread::idleMsec();
		    uint64_t rest = t % Thread::idleMsec();
		    for (uint64_t i = 0; i < count; i++) {
			Thread::idle();
			if (Thread::check(false))
			    break;
		    }
		    if (rest && !Thread::check(false))
			Thread::msleep(rest);
		}
	    }
	    else {
		DDebug(&__plugin,DebugAll,"Returning '%s' for '%s' in context '%s' by rule #%u '%s'",
		    val.c_str(),str.c_str(),context.c_str(),i+1,n->name().c_str());
		ret = val;
		return true;
	    }
	}
	if (blockDepth)
	    TRACE_DBG(DebugWarn,trace,traceLst,"There are %u blocks still open at end of context '%s'",
		blockDepth,context.c_str());
	DDebug(&__plugin,DebugAll,"Returning false at end of context '%s'", context.c_str());
    }
    else if (warn)
	TRACE_DBG(DebugWarn,trace,traceLst,"Missing target context '%s'",context.c_str());
    return false;
}

static inline void dumpTraceToMsg(Message& msg, ObjList*& lst)
{
    if (!lst)
	return;
    unsigned int count = msg.getIntValue(YSTRING("trace_msg_count"),0);
    static String s_tracePref = "trace_msg_";
    for (ObjList* o = lst->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (TelEngine::null(s))
	    continue;
	msg.setParam(s_tracePref + String(count++),*s);
    }
    msg.setParam(YSTRING("trace_msg_count"),String(count));
    TelEngine::destruct(lst);
}

bool RouteHandler::received(Message &msg)
{
    u_int64_t tmr = Time::now();
    String called(msg.getValue(YSTRING("called")));
    if (called.null())
	return false;
    s_processing.inc();
    const char *context = msg.getValue(YSTRING("context"),"default");
    const String& traceID = msg[YSTRING("trace_id")];
    int traceLvl = msg.getIntValue(YSTRING("trace_lvl"),DebugNote,DebugGoOn,DebugAll);
    ObjList* traceLst = msg.getBoolValue(YSTRING("trace_to_msg"),false) ? new ObjList() : 0;
    Lock lock(s_mutex);
    RefPointer<RegexConfig> cfg = s_cfg;
    lock.drop();
    if (cfg && cfg->oneContext(msg,called,context,msg.retValue(),traceID,traceLvl,traceLst)) {
	TRACE_DBG_ONLY(DebugInfo,traceID,traceLst,"Routing %s to '%s' in context '%s' via '%s' in " FMT64U " usec",
	    msg.getValue(YSTRING("route_type"),"call"),called.c_str(),context,
	    msg.retValue().c_str(),Time::now()-tmr);
	dumpTraceToMsg(msg,traceLst);
	s_processing.dec();
	return true;
    }
    TRACE_DBG_ONLY(DebugInfo,traceID,traceLst,"Could not route %s to '%s' in context '%s', wasted " FMT64U " usec",
	msg.getValue(YSTRING("route_type"),"call"),called.c_str(),context,Time::now()-tmr);
    dumpTraceToMsg(msg,traceLst);
    s_processing.dec();
    return false;
};


bool PrerouteHandler::received(Message &msg)
{
    u_int64_t tmr = Time::now();
    // return immediately if there is already a context
    if (!s_prerouteall && msg.getValue(YSTRING("context")))
	return false;

    String caller(msg.getValue(YSTRING("caller")));
    if (!s_prerouteall && caller.null())
	return false;
    s_processing.inc();

    String ret;
    const String& traceID = msg[YSTRING("trace_id")];
    int traceLvl = msg.getIntValue(YSTRING("trace_lvl"),DebugNote,DebugGoOn,DebugAll);
    ObjList* traceLst = msg.getBoolValue(YSTRING("trace_to_msg"),false) ? new ObjList() : 0;
    Lock lock(s_mutex);
    RefPointer<RegexConfig> cfg = s_cfg;
    lock.drop();
    if (cfg && cfg->oneContext(msg,caller,"contexts",ret,traceID,traceLvl,traceLst)) {
	TRACE_DBG_ONLY(DebugInfo,traceID,traceLst,"Classifying caller '%s' in context '%s' in " FMT64 " usec",
	    caller.c_str(),ret.c_str(),Time::now()-tmr);
	if (ret == YSTRING("-") || ret == YSTRING("error"))
	    msg.retValue() = ret;
	else
	    msg.setParam("context",ret);
	dumpTraceToMsg(msg,traceLst);
	s_processing.dec();
	return true;
    }
    TRACE_DBG_ONLY(DebugInfo,traceID,traceLst,"Could not classify call from '%s', wasted " FMT64 " usec",
	caller.c_str(),Time::now()-tmr);
    dumpTraceToMsg(msg,traceLst);
    s_processing.dec();
    return false;
};


GenericHandler::GenericHandler(const char* name, int prio, const char* context, const char* match,
    const char* trackName, const char* filterKey, const char* filterVal, bool addToExtra)
    : MessageHandler(name,prio,trackName),
    m_context(context), m_match(match), m_serial(0), m_inExtra(addToExtra)
{
    DDebug(&__plugin,DebugAll,
	"Creating generic handler for '%s' prio %d to [%s] match '%s%s%s', track name '%s', filter '%s=%s' [%p]",
	toString().c_str(),prio,TelEngine::c_safe(context),
	(match ? "${" : ""),(match ? match : toString().c_str()),(match ? "}" : ""),
	TelEngine::c_safe(trackName),TelEngine::c_safe(filterKey),TelEngine::c_safe(filterVal),this);
    if (filterKey && filterVal) {
	if (filterVal[0] == '^')
	    setFilter(new NamedPointer(filterKey,new Regexp(filterVal)));
	else
	    setFilter(filterKey,filterVal);
    }
    m_hash = getHash(name,prio,context,match,trackName,filterKey,filterVal);
    if (m_inExtra) {
	Lock l(s_mutex);
	s_extra.append(this);
    }
    updateSerial();
}

GenericHandler::~GenericHandler()
{
    DDebug(&__plugin,DebugAll,
	"Destroying generic handler for '%s' prio %d to [%s] match '%s', track name '%s' [%p]",
	toString().c_str(),priority(),m_context.c_str(),m_match.c_str(),
	trackName().c_str(),this);
    if (m_inExtra) {
	Lock l(s_mutex);
	s_extra.remove(this,false);
    }
}

bool GenericHandler::received(Message &msg)
{
    DDebug(DebugAll,"Handling message '%s' [%p]",c_str(),this);
    s_processing.inc();

    String what(m_match);
    if (what)
	what = msg.getValue(what);
    else
	what = *this;
    const String& traceID = msg[YSTRING("trace_id")];
    int traceLvl = msg.getIntValue(YSTRING("trace_lvl"),DebugNote,DebugGoOn,DebugAll);
    ObjList* traceLst = msg.getBoolValue(YSTRING("trace_to_msg"),false) ? new ObjList() : 0;
    Lock lock(s_mutex);
    RefPointer<RegexConfig> cfg = s_cfg;
    lock.drop();
    bool ok = cfg && cfg->oneContext(msg,what,m_context,msg.retValue(),traceID,traceLvl,traceLst);
    dumpTraceToMsg(msg,traceLst);
    s_processing.dec();
    return ok;
}

#undef TRACE_DBG_ONLY
#undef TRACE_DBG
#undef TRACE


RegexRoutePlugin::RegexRoutePlugin()
    : Module("regexroute","route"),
      m_first(true)
{
    debugName("RegexRoute");
    Output("Loaded module RegexRoute");
}

void RegexRoutePlugin::initVars(NamedList* sect, bool replace)
{
    if (!sect)
	return;
    Lock l(s_varsMtx); // we want all set at the same time
    for (ObjList* o = sect->paramList()->skipNull(); o; o = o->skipNext()) {
	NamedString* n = static_cast<NamedString*>(o->get());
	if (replace)
	    s_vars.setParam(n->name(),*n);
	else if (!s_vars.getParam(n->name()))
	    s_vars.addParam(n->name(),*n);
    }
}

void RegexRoutePlugin::initialize()
{
    static int s_priority = 0;

    Output("Initializing module RegexRoute");

    Configuration cfg(Engine::configFile(__plugin.name()),false);
    int prio = cfg.getIntValue(YSTRING("priorities"),YSTRING("status"),110);
    if (prio != s_priority) {
	s_priority = prio;
	if (prio) {
	    installRelay(Status,prio);
	    installRelay(Command,prio);
	    installRelay(Level,prio);
	}
	else {
	    uninstallRelay(Status);
	    uninstallRelay(Command);
	    uninstallRelay(Level);
	}
    }

    s_serial.inc();
    RegexConfig* rCfg = new RegexConfig(Engine::configFile(name()));
    rCfg->initialize(m_first);
    if (m_first)
	m_first = false;
    Lock lock(s_mutex);
    RegexConfig* tmp = s_cfg;
    s_cfg = rCfg;
    lock.drop();
    TelEngine::destruct(tmp);
}

void RegexRoutePlugin::statusParams(String& str)
{
    Lock lock(s_mutex);
    str.append("sections=",";");
    str << s_cfg->sectCount() << ",extra=" << s_extra.count();
    lock.acquire(s_varsMtx);
    str << ",variables=" << s_vars.count();
    lock.drop();
    str << ",processing=" << s_processing.count();
}


RegexRouteDebug::RegexRouteDebug()
    : Module("rex_debug","misc"),
    m_enabled(false)
{
    debugName("RegexRoute");
    debugChain(&__plugin);
}

void RegexRouteDebug::initialize()
{
    Configuration cfg(Engine::configFile(__plugin.name()),false);
    m_enabled = cfg.getBoolValue(YSTRING("priorities"),YSTRING("rex_debug"),true);
    if (m_enabled) {
	installRelay(Status);
	installRelay(Command);
	installRelay(Level);
    }
    else {
	uninstallRelay(Status);
	uninstallRelay(Command);
	uninstallRelay(Level);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
