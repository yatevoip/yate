/*
 * msgsniff.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A sample message sniffer that inserts a wildcard message handler
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

#include <yatengine.h>

#ifdef XDEBUG
#define SNIFF_DEBUG_CHANGE_LIST 9
#define SNIFF_DEBUG_BUILD 10
#else
//#define SNIFF_DEBUG_CHANGE_LIST 9
//#define SNIFF_DEBUG_BUILD 1
#endif

using namespace TelEngine;
namespace { // anonymous

class MatchingItemMessage : public MatchingItemCustom, public MessageFilter
{
    YCLASS(MatchingItemMessage,MatchingItemCustom)
public:
    inline MatchingItemMessage(const char* name, MatchingItemBase* msg, MatchingItemBase* params = 0,
	uint64_t age = 0)
	: MatchingItemCustom("message",name,"Message"), MessageFilter(msg,params), m_minAge(age)
	{}
    inline bool empty() const
	{ return !(m_minAge || getMsgFilter() || getFilter()); }
    inline uint64_t minAge() const
	{ return m_minAge; }
    virtual const String& dumpValue(const MatchingItemDump& dump, String& buf) const {
	    String val;
	    dump.dumpValue(getMsgFilter(),val);
	    if (m_minAge)
		val.printfAppend("%sage %u.%06u",val ? " " : "",
		    (unsigned int)(m_minAge / 1000000),(unsigned int)(m_minAge % 1000000));
	    return buf << val;
	}
    virtual bool runMatchListParam(const NamedList& list, MatchingParams* params = 0) const {
	    const Message* msg = m_minAge ? YOBJECT(Message,&list) : 0;
	    if (msg) {
		if (params) {
		    if (!params->m_now)
			params->m_now = Time::now();
		    if (m_minAge > params->m_now - msg->msgTime().usec())
			return false;
		}
		else if (m_minAge > Time::now() - msg->msgTime().usec())
		    return false;
	    }
	    return matchesList(list,params);
	}
    virtual MatchingItemBase* customCopyItem() const {
	    return new MatchingItemMessage(name(),getMsgFilter() ? getMsgFilter()->copy() : 0,
		getFilter() ? getFilter()->copy() : 0,m_minAge);
	}
    virtual String& dump(const MatchingItemDump& dump, String& buf,
	const String& indent, const String& addIndent, unsigned int depth) const {
	    String params;
	    return buf << dump.dump(getFilter(),params,indent + addIndent,addIndent);
	}
    virtual String& dumpFull(const MatchingItemDump& dump, String& buf,
	const String& indent, const String& addIndent, unsigned int depth) const {
	    String msg, params;
	    dumpValue(dump,msg);
	    dump.dump(getFilter(),params,indent,addIndent);
	    if (!(msg || params))
		return buf;
	    if (name())
		buf << indent << "Name: " << name();
	    if (msg)
		buf << indent << "Message: " << msg;
	    buf << params;
	    return buf;
	}

private:
    uint64_t m_minAge;                   // Minimum age. No match if message age is less than this value
    mutable String m_value;
};

class SniffMatch : public RefObject
{
public:
    inline SniffMatch(MatchingItemList* lst)
	: m_match(lst)
	{}
    ~SniffMatch()
	{ TelEngine::destruct(m_match); }
    inline const MatchingItemList* matching() const
	{ return m_match; }
    inline bool matches(const Message& msg, MatchingParams& params) const
	{ return m_match->matchListParam(msg,&params); }
    inline void dump(String& buf) {
	    unsigned int n = m_match->length();
	    if (!n)
		return;
	    String s, indent("\r\n"), addIndent("  ");
	    MatchingItemDump d;
	    d.m_flags = MatchingItemDump::DumpCustomFull;
	    // We are always building regexp, no enclose needed
	    d.m_rexEnclose = 0;
	    for (unsigned int i = 0; i < n; i++) {
		String tmp;
		s.append(d.dump(m_match->at(i),tmp,indent,addIndent),"\r\n-----");
	    }
	    const char* sep = (s && n > 1) ? "\r\n-----" : "";
	    buf << sep << s << sep;
	}

private:
    MatchingItemList* m_match;
};

class MsgSniff : public Plugin
{
public:
    enum FilterParams {
	FilterName = 0,
	FilterFilter,
	FilterParams,
	FilterAge,
	FilterParamsCount
    };
    MsgSniff();
    virtual void initialize();
    inline bool itemComplete(String& itemList, const String& item, const String& partWord) {
	    if (!partWord || item.startsWith(partWord)) {
		itemList.append(item,"\t");
		return true;
	    }
	    return false;
	}
    inline bool listComplete(String& itemList, const String* items, const String& partWord) {
	    bool ok = false;
	    while (!TelEngine::null(items))
		ok = itemComplete(itemList,*items++,partWord);
	    return ok;
	}
    inline void setFilter(MatchingItemList* lst = 0) {
	    SniffMatch* sm = lst ? new SniffMatch(lst) : 0;
	    WLock lck(m_lock);
	    m_filter = (sm && sm->matching() && sm->matching()->length()) ? sm : 0;
	    TelEngine::destruct(sm);
	}
    inline bool getFilter(RefPointer<SniffMatch>& flt) {
	    RLock lck(m_lock);
	    flt = m_filter;
	    return (0 != flt);
	}
    void handleMsg(const Message& msg, bool* handled = 0);
    void handleCommand(String& line);
    void commandComplete(Message& msg);
    String& dumpSnifferState(String& buf);
    MatchingItemMessage* splitFilter(const String& line, String* params = 0);
    MatchingItemMessage* buildFilter(String* filterParams, String* filter = 0,
	String* params = 0, const String* age = 0, const MatchingItemMessage* old = 0);
    MatchingItemList* changeListItem(MatchingItemList* lst, MatchingItemMessage* item,
	const MatchingItemMessage* old = 0);

private:
    bool m_first;
    RWLock m_lock;
    RefPointer<SniffMatch> m_filter;
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

static bool s_active = false;
static bool s_timer = false;
static bool s_dumpMsgData = false;
static bool s_dumpMsgDataShort = true;
static bool s_dumpParamPtrData = false;
static bool s_dumpParamPtrDataShort = true;
static const String s_command = "sniffer";
static const String s_onOff[] = {"on", "off", ""};
static const String s_commands[] = {"set", "reset", "filter", "params", "age", ""};
static const Regexp s_completeOnOff("^sniffer( ((on|off) )?timer)?$",true);
static const Regexp s_completeAllCmds("^sniffer( (on|off))?$",true);
static const Regexp s_completeCmds("^sniffer( (on|off))?( timer( (on|off))?)?$",true);
static const char* s_help =
    "  sniffer [on|off] [timer [on|off]] [{set|reset|filter|params|age} ...]\r\n"
    "Change sniffer rules, enable or disable sniffer and/or timer, display status\r\n"
    "Multiple rules with filter/params/age may be configured. A rule may be named\r\n"
    "Rule parameters:\r\n"
    "filter: message name filter (regexp)\r\n"
    "params: message parameters filter. Format: [any] [negated] name=regexp ...\r\n"
    "age: message minimum age filter (seconds, 1.5=1500ms)\r\n"
    "regexp: may end with ^ for negated match\r\n"
    "  sniffer [on|off] set [name=[value]] [filter=[value]] [age=[value]] [params [any negated] [name=value]]\r\n"
    "Add, replace or remove a sniffer rule\r\n"
    "  sniffer [on|off] reset [name=[value]] [filter=[value]] [age=[value]] [params [any negated] [name=value]]\r\n"
    "Reset all sniffer rules (except timer). Optional set a new rule\r\n"
    "  sniffer [on|off] [{filter|params|age} ...]\r\n"
    "Partial (re)set of unnamed rule data\r\n"
    "  sniffer [on|off] timer [on|off]\r\n"
    "Enable or disable engine.timer message handling\r\n"
;

INIT_PLUGIN(MsgSniff);

static inline String& extractUntilSpace(String& dest, String& line)
{
    int pos = line.find(' ');
    if (pos >= 0) {
	dest = line.substr(0,pos);
	line = line.substr(pos + 1);
    }
    else {
	dest = line;
	line = "";
    }
    return dest;
}

bool SniffHandler::received(Message &msg)
{
    if (msg == YSTRING("engine.command")) {
	const NamedString* line = msg.getParam(YSTRING("line"));
	if (!line)
	    __plugin.commandComplete(msg);
	else if (line->startsWith(s_command)) {
	    String l = *line;
	    if (l.startSkip(s_command)) {
		__plugin.handleCommand(l);
		__plugin.dumpSnifferState(msg.retValue());
		return true;
	    }
	}
    }
    else if (msg == YSTRING("engine.help")) {
	const String& line = msg[YSTRING("line")];
	if (line == s_command) {
	    msg.retValue() = s_help;
	    return true;
	}
    }
    __plugin.handleMsg(msg);
    return false;
};


void HookHandler::dispatched(const Message& msg, bool handled)
{
    __plugin.handleMsg(msg,&handled);
}


MsgSniff::MsgSniff()
    : Plugin("msgsniff"),
    m_first(true), m_lock("FilterSniff")
{
    Output("Loaded module MsgSniffer");
}

void MsgSniff::initialize()
{
    Output("Initializing module MsgSniffer");
    const NamedList* genSect = Engine::config().getSection(YSTRING("general"));
    const NamedList& gen = genSect ? *genSect : NamedList::empty();
    if (m_first) {
	m_first = false;
	String trackName;
	if (genSect) {
	    s_active = gen.getBoolValue(YSTRING("msgsniff"));
	    MatchingItemList* lst = 0;
	    String filter = gen[YSTRING("filtersniff")];
	    String params = gen[YSTRING("filtersniffparams")];
	    String age = gen[YSTRING("agesniff")];
	    if (filter || params || age)
		lst = changeListItem(lst,buildFilter(0,&filter,&params,&age));
	    for (ObjList* o = gen.paramList()->skipNull(); o; o = o->skipNext()) {
		NamedString* ns = static_cast<NamedString*>(o->get());
		if (ns->name() == YSTRING("msgsniff") || ns->name().startsWith("msgsniff:")) {
		    String fp[FilterParamsCount];
		    fp[FilterName] = ns->name().substr(9);
		    lst = changeListItem(lst,splitFilter(*ns,fp));
		}
	    }
	    setFilter(lst);
#ifdef SNIFF_DEBUG_CHANGE_LIST
	    if (m_filter) {
		String tmp;
		Debug(this,SNIFF_DEBUG_CHANGE_LIST,"Loaded\r\n%s",dumpSnifferState(tmp).c_str());
	    }
#endif
	    trackName = gen[YSTRING("msgsniff_trackname")];
	    if (trackName && trackName.isBoolean()) {
		if (trackName.toBoolean())
		    trackName = "msgsniff";
		else
		    trackName = "";
	    }
	}
	Engine::install(new SniffHandler(trackName));
	Engine::self()->setHook(new HookHandler);
    }
    s_dumpMsgData = gen.getBoolValue(YSTRING("dump_msg_data"));
    s_dumpMsgDataShort = gen.getBoolValue(YSTRING("dump_msg_data_short"),true);
    s_dumpParamPtrData = gen.getBoolValue(YSTRING("dump_msg_param_ptr_data"));
    s_dumpParamPtrDataShort = gen.getBoolValue(YSTRING("dump_msg_param_ptr_data_short"),true);
}

static inline String& dumpPtr(bool dump, bool shortInfo, String& buf, const GenObject* gen, const String* paramVal = 0)
{
    if (paramVal) {
	buf.printfAppend(" [%p]",gen);
	if (*paramVal) {
	    if (!gen)
		return buf;
	    return buf << " '" << gen->toString() << '\'';
	}
    }
    else
	buf.printfAppend("%p",gen);
    if (!gen)
	return buf;
    if (dump) {
	String content;
	XmlElement* xml = YOBJECT(XmlElement,gen);
	if (xml) {
	    if (shortInfo)
		return buf << " XML '" << gen->toString() << '\'';
	    return buf << " XML '" << xml->toString(content) << '\'';
	}
	DataBlock* db = YOBJECT(DataBlock,gen);
	if (db) {
	    buf.printfAppend(" DATA[%u]",db->length());
	    if (shortInfo)
		return buf;
	    return buf << " '" << content.hexify(db->data(),db->length()) << '\'';
	}
	Array* arr = YOBJECT(Array,gen);
	if (arr)
	    return buf.printfAppend(" Array[%d,%d]",arr->getRows(),arr->getColumns());
    }
    if (paramVal)
	return buf << " '" << gen->toString() << '\'';
    return buf;
}

void MsgSniff::handleMsg(const Message& msg, bool* handled)
{
    if (!s_active || (!s_timer && msg == YSTRING("engine.timer")))
	return;
    RefPointer<SniffMatch> filter;
    uint64_t age = 0;
    if (getFilter(filter)) {
	MatchingParams params;
	if (!filter->matches(msg,params))
	    return;
	age = params.m_now ? params.m_now : Time::now();
    }
    else
	age = Time::now();

    bool dumpParamPtrData = s_dumpParamPtrData;
    bool dumpParamPtrDataShort = s_dumpParamPtrDataShort;
    String par;
    for (ObjList* o = msg.paramList()->skipNull(); o; o = o->skipNext()) {
	const NamedString* s = static_cast<NamedString*>(o->get());
	String tmp;
	tmp << "\r\n  param['" << s->name() << "'] = ";
	if (s->name() == YSTRING("password"))
	    tmp << "(hidden)";
	else
	    tmp << "'" << *s << "'";
	if (const NamedPointer* p = YOBJECT(NamedPointer,s))
	    dumpPtr(dumpParamPtrData,dumpParamPtrDataShort,tmp,p->userData(),s);
	par << tmp;
    }

    String data;
    uint64_t mt = msg.msgTime().usec();
    age -= mt;
    if (!handled) {
	String extra;
	if (msg.msgTimeEnqueue() && msg.msgTimeDispatch() > msg.msgTimeEnqueue()) {
	    uint64_t dur = msg.msgTimeDispatch() - msg.msgTimeEnqueue();
	    extra.printf(" queued=%u.%06u",(unsigned int)(dur / 1000000),
		(unsigned int)(dur % 1000000));
	}
	Output("Sniffed '%s' time=%u.%06u age=%u.%06u%s%s\r\n  thread=%p '%s'\r\n  data=%s\r\n  retval='%s'%s",
	    msg.c_str(),
	    (unsigned int)(mt / 1000000),
	    (unsigned int)(mt % 1000000),
	    (unsigned int)(age / 1000000),
	    (unsigned int)(age % 1000000),
	    extra.safe(),
	    (msg.broadcast() ? " (broadcast)" : ""),
	    Thread::current(),
	    Thread::currentName(),
	    dumpPtr(s_dumpMsgData,s_dumpMsgDataShort,data,msg.userData()).safe(),
	    msg.retValue().c_str(),
	    par.safe());
    }
    else {
	const char* rval = msg.retValue().c_str();
	const char* rsep = "'";
	if (*handled && rval && (rval[0] != '-' || rval[1]) && (msg == YSTRING("user.auth"))) {
	    rval = "(hidden)";
	    rsep = "";
	}
	Output("Returned %s '%s' delay=%u.%06u%s\r\n  thread=%p '%s'\r\n  data=%s\r\n  retval=%s%s%s%s",
	    String::boolText(*handled),
	    msg.c_str(),
	    (unsigned int)(age / 1000000),
	    (unsigned int)(age % 1000000),
	    (msg.broadcast() ? " (broadcast)" : ""),
	    Thread::current(),
	    Thread::currentName(),
	    dumpPtr(s_dumpMsgData,s_dumpMsgDataShort,data,msg.userData()).safe(),
	    rsep,rval,rsep,
	    par.safe());
    }
}

void MsgSniff::handleCommand(String& line)
{
    line >> s_active;
    line.trimSpaces();
    if (line.startSkip("timer"))
	(line >> s_timer).trimSpaces();
    if (!line)
	return;
    const MatchingItemList* crtList = 0;
    const MatchingItemMessage* old = 0;
    RefPointer<SniffMatch> crtFlt;
    if (getFilter(crtFlt)) {
	crtList = crtFlt->matching();
	old = YOBJECT(MatchingItemMessage,crtList ? crtList->find(String::empty()) : 0);
    }
    String cmd;
    extractUntilSpace(cmd,line);
    MatchingItemMessage* newItem = 0;
    if (cmd == YSTRING("filter"))
	newItem = buildFilter(0,&line,0,0,old);
    else if (cmd == YSTRING("params"))
	newItem = buildFilter(0,0,&line,0,old);
    else if (cmd == YSTRING("age"))
	newItem = buildFilter(0,0,0,&line,old);
    else if (cmd == YSTRING("set")) {
	if (line) {
	    String fp[FilterParamsCount];
	    newItem = splitFilter(line,fp);
	    const String& n = crtList ? (const String&)fp[FilterName] : String::empty();
	    old = n ? YOBJECT(MatchingItemMessage,crtList->find(n)) : 0;
	}
    }
    else if (cmd == YSTRING("reset")) {
	crtList = 0;
	setFilter(0);
	if (line)
	    newItem = splitFilter(line);
    }
    else
	return;
    if (newItem || old) {
	MatchingItemBase* mib = crtList ? crtList->copy() : 0;
	MatchingItemList* lst = YOBJECT(MatchingItemList,mib);
	if (!lst)
	    TelEngine::destruct(mib);
	setFilter(changeListItem(lst,newItem,old));
    }
}

void MsgSniff::commandComplete(Message& msg)
{
    const String& partLine = msg[YSTRING("partline")];
    const String& partWord = msg[YSTRING("partword")];
    if (!partLine || partLine == YSTRING("help"))
	itemComplete(msg.retValue(),s_command,partWord);
    else if (s_command == partLine) {
	listComplete(msg.retValue(),s_onOff,partWord);
	listComplete(msg.retValue(),s_commands,partWord);
	itemComplete(msg.retValue(),YSTRING("timer"),partWord);
    }
    else if (partLine.startsWith(s_command,true)) {
	String line = partLine;
	if (line.matches(s_completeAllCmds)) {
	    listComplete(msg.retValue(),s_commands,partWord);
	    itemComplete(msg.retValue(),YSTRING("timer"),partWord);
	}
	else if (line.matches(s_completeOnOff))
	    listComplete(msg.retValue(),s_onOff,partWord);
	else if (line.matches(s_completeCmds))
	    listComplete(msg.retValue(),s_commands,partWord);
    }
}

String& MsgSniff::dumpSnifferState(String& buf)
{
    buf << "Message sniffer: " << (s_active ? "on" : "off");
    if (s_active || s_timer)
	buf << ", timer: " << (s_timer ? "on" : "off");
    RefPointer<SniffMatch> flt;
    if (getFilter(flt))
	flt->dump(buf);
    buf << "\r\n";
    return buf;
}

MatchingItemMessage* MsgSniff::splitFilter(const String& line, String* fp)
{
    if (!line)
	return 0;
    String fpTmp[FilterParamsCount];
    if (!fp)
	fp = fpTmp;
    const char* s = line;
    unsigned int len = line.length();
    unsigned int skipped = 0;
    while (*s) {
	len -= String::c_skip_chars(s," ");
	if (!*s)
	    break;
	int set = -1;
	if (0 != (skipped = String::c_skip(s,"name=",len,5)))
	    set = FilterName;
	else if (0 != (skipped = String::c_skip(s,"filter=",len,7)))
	    set = FilterFilter;
	else if (0 != (skipped = String::c_skip(s,"age=",len,4)))
	    set = FilterAge;
	else if (String::c_skip(s,"params ",len,7)) {
	    fp[FilterParams] = s;
	    break;
	}
	const char* orig = s;
	unsigned int n = String::c_skip_chars(s," ",-1,false);
	len -= skipped + n;
	if (set >= 0)
	    fp[set].assign(orig,n);
    }
#ifdef SNIFF_DEBUG_BUILD
    Debug(this,DebugAll,"splitFilter '%s' -> name='%s' filter='%s' age='%s' params='%s'",line.safe(),
	fp[FilterName].safe(),fp[FilterFilter].safe(),fp[FilterAge].safe(),fp[FilterParams].safe());
#endif
    return buildFilter(fp);
}

MatchingItemMessage* MsgSniff::buildFilter(String* filterParams,
    String* filter, String* params, const String* age, const MatchingItemMessage* old)
{
    const String* name = &String::empty();
    if (filterParams) {
	name = filterParams + FilterName;
	filter = filterParams + FilterFilter;
	params = filterParams + FilterParams;
	age = filterParams + FilterAge;
    }
    else if (!(filter || params || age))
	return 0;
#ifdef SNIFF_DEBUG_BUILD
    Debug(this,DebugAll,"buildFilter '%s' '%s' '%s' '%s' (%p)",TelEngine::c_safe(name),
	TelEngine::c_safe(filter),TelEngine::c_safe(params),TelEngine::c_safe(age),old);
#endif
    MatchingItemBase* matchName = 0;
    if (!TelEngine::null(filter))
	matchName = MatchingItemRegexp::build("",*filter,0,false,-1);
    MatchingItemBase* matchParams = 0;
    if (!TelEngine::null(params)) {
	static const Regexp s_matchParamListParam("^( *)?(any|negated)( .*)?$",true);
	static const Regexp s_matchParam("^(.* )?([^= ]+)=([^=]*)$",true);
	bool matchAll = true;
	bool negated = false;
	while (params->matches(s_matchParamListParam)) {
	    String tmp = params->matchString(2);
	    *params = params->matchString(3);
	    if (tmp == YSTRING("any"))
		matchAll = false;
	    else if (tmp == YSTRING("negated"))
		negated = true;
	}
	ObjList items;
	while (params->matches(s_matchParam)) {
	    MatchingItemBase* p = MatchingItemRegexp::build(params->matchString(2),
		params->matchString(3).trimSpaces(),0,false,-1);
	    if (p)
		items.insert(p);
	    *params = params->matchString(1);
	}
	if (items.skipNull()) {
	    MatchingItemList* l = new MatchingItemList("Params",matchAll,negated);
	    l->append(items);
	    if (l->length())
		matchParams = l;
	    else
		TelEngine::destruct(matchParams);
	}
    }
    uint64_t minAge = 0;
    if (age) {
	double d = age->toDouble();
	minAge = (uint64_t)(1000000.0 * (d >= 0 ? d : -d));
    }
    if (old) {
	if (!filter && old->getMsgFilter())
	    matchName = old->getMsgFilter()->copy();
	if (!params && old->getFilter())
	    matchParams = old->getFilter()->copy();
	if (!age)
	    minAge = old->minAge();
    }
    if (!(matchName || matchParams || minAge))
	return 0;
    MatchingItemMessage* m = new MatchingItemMessage(*name,matchName,matchParams,minAge);
#ifdef SNIFF_DEBUG_BUILD
    String tmp;
    Debug(this,SNIFF_DEBUG_BUILD,"Built item\r\n-----%s\r\n-----",
	MatchingItemDump::dumpItem(m,tmp,"\r\n","  ").safe());
#endif
    return m;
}

MatchingItemList* MsgSniff::changeListItem(MatchingItemList* lst, MatchingItemMessage* item,
    const MatchingItemMessage* old)
{
    if (item == old)
	return lst;
    String oldName;
    if (!item || item->empty()) {
	if (!lst)
	    return 0;
	if (!old)
	    old = item;
	int idx = lst->indexOf(old->name());
#ifdef SNIFF_DEBUG_CHANGE_LIST
	Debug(this,SNIFF_DEBUG_CHANGE_LIST,"Removing item '%s' index=%d list=(%p,%u)",
	    old->name().safe(),idx,lst,lst->length());
#endif
	if (idx >= 0)
	    lst->set(0,lst->indexOf(old->name()));
	TelEngine::destruct(item);
	return lst;
    }
#ifdef SNIFF_DEBUG_CHANGE_LIST
    String tmp;
    MatchingItemDump::dumpItem(item,tmp,"\r\n","  ");
    if (tmp)
	tmp = "\r\n-----" + tmp + "\r\n-----";
    int idx = lst ? lst->indexOf(item->name()) : -1;
    Debug(this,SNIFF_DEBUG_CHANGE_LIST,"%s item=(%p) list=(%p,%u) index=%d%s",
	idx >= 0 ? "Replacing" : "Adding",item,lst,lst ? lst->length() : 0,
	idx,tmp.safe());
#endif
    if (!lst) {
	lst = new MatchingItemList("",false);
	lst->append(item);
    }
    else
	lst->set(item,lst->indexOf(item->name()));
    return lst;
}

}; // anonymous namespace
