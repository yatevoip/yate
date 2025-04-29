/**
 * MatchingItem.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2023-2024 Null Team
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

//#include "yatengine.h"
//#include "yatexml.h"

#include "yatematchingitem.h"

using namespace TelEngine;

#ifdef XDEBUG
    #define TRACK_MI_MATCH
    #define TRACK_MI_LOAD
    #define TRACK_MI_DUMP
    #define TRACK_MI_OBJ_LIFE
#else
//    #define TRACK_MI_MATCH
//    #define TRACK_MI_LOAD
//    #define TRACK_MI_DUMP
//    #define TRACK_MI_OBJ_LIFE
#endif
#if defined(TRACK_MI_MATCH) || defined(TRACK_MI_LOAD) || defined(TRACK_MI_DUMP)
    #define TRACK_MI
#else
    #undef TRACK_MI
#endif

#define MI_LIST_C(item) (static_cast<const MatchingItemList*>(item))
#define MI_STR_C(item) (static_cast<const MatchingItemString*>(item))
#define MI_REX_C(item) (static_cast<const MatchingItemRegexp*>(item))
#define MI_XPATH_C(item) (static_cast<const MatchingItemXPath*>(item))
#define MI_RAND_C(item) (static_cast<const MatchingItemRandom*>(item))
#define MI_CUSTOM_C(item) (static_cast<const MatchingItemCustom*>(item))

static inline bool flagSet(uint64_t flags, uint64_t mask)
{
    return 0 != (flags & mask);
}

static inline bool flagSet(unsigned int flags, unsigned int mask)
{
    return 0 != (flags & mask);
}

static inline String& dumpItemInfo(const MatchingItemBase& mi, String& buf)
{
    return buf.printf("(%p,%s,%s)",&mi,mi.typeName(),mi.name().safe());
}

static inline String dumpItemInfo(const MatchingItemBase& mi)
{
    String tmp;
    return dumpItemInfo(mi,tmp);
}

static bool s_dumpItemFlagName = false;

class MiDebugEnabler : public DebugEnabler
{
public:
    inline MiDebugEnabler(const char* name = 0)
	: m_name(TelEngine::null(name) ? "MatchingItem" : name)
	{ debugName(m_name); debugLevel(100); }
    inline MiDebugEnabler(const MatchingItemBase& mi, MatchingParams* params) {
	    if (params && params->m_dbg && params->m_dbg->debugName())
#ifdef TRACK_MI_MATCH
		m_name.printf("%s/Match/%s",params->m_dbg->debugName(),dumpItemInfo(mi).safe());
#else
		m_name.printf("%s/Match",params->m_dbg->debugName());
#endif
	    else
		m_name.printf("MatchingItemMatch/%s",dumpItemInfo(mi).safe());
	    debugName(m_name);
	    debugChain(params && params->m_dbg ? params->m_dbg : &s_debug);
	}
    static MiDebugEnabler s_debug;
protected:
    String m_name;
};

MiDebugEnabler MiDebugEnabler::s_debug;
MiDebugEnabler* s_debug = &MiDebugEnabler::s_debug;

void MatchingItemBase::setup(const NamedList& params)
{
    s_dumpItemFlagName = params.getBoolValue(YSTRING("matchingitem_dump_item_flag_name"));
#ifdef TRACK_MI
    s_debug->debugLevel(params.getIntValue("matchingitem_debug",DebugAll,1));
#endif
}

#ifdef TRACK_MI
class MiDebugger
{
public:
    inline MiDebugger(DebugEnabler* dbg = 0)
	: m_dbg(dbg), m_debugger(0)
	{}
    inline ~MiDebugger() {
	    if (m_debugger)
		delete m_debugger;
	}
    inline void set(int level, const char* name, const String& info) {
	    if (m_debugger)
		return;
	    if (m_dbg)
		m_name.printf("<%s>",m_dbg->debugName());
	    m_name.append(name," ");
	    m_debugger = new Debugger(m_dbg,level,m_name,info.safe());
	}
    inline void appendName(const char* str) {
	    if (!m_debugger || TelEngine::null(str))
		return;
	    m_name.printfAppend(" %s",str);
	    m_debugger->setName(m_name);
	}
    DebugEnabler* m_dbg;
    Debugger* m_debugger;
private:
    String m_name;
};
#else
class MiDebugger
{
public:
    inline MiDebugger(DebugEnabler* dbg = 0)
	{}
    inline void set(int level, const char* name, const String& info)
	{}
    inline void appendName(const char* str)
	{}
};
#endif


static const String s_name = "name";
static const String s_value = "value";
static const String s_id = "id";
static const String s_flags = "flags";
static const String s_match = "match";
static const String s_item = "item:";
static const String s_xml = "xml";

static const TokenDict s_miType[] = {
    {"string", MatchingItemBase::TypeString},
    {"regexp", MatchingItemBase::TypeRegexp},
    {"xpath",  MatchingItemBase::TypeXPath},
    {"random", MatchingItemBase::TypeRandom},
    {"list",   MatchingItemBase::TypeList},
    {"custom", MatchingItemBase::TypeCustom},
    {0,0}
};

static const TokenDict s_miMissingMatch[] = {
    {"match",    MatchingItemBase::MissingParamMatch},
    {"no_match", MatchingItemBase::MissingParamNoMatch},
    {0,0}
};


//
// MatchingParams
//
class MatchingParamsData : public String
{
public:
    inline MatchingParamsData(MatchingParams* p)
	: level(p->m_level > 0 ? p->m_level : DebugCall)
	{
	    String::printf("[%p]",p);
	}
    int level;
};

bool MatchingParams::trackMatchDbg()
{
#ifdef TRACK_MI_MATCH
    return true;
#else
    return 0 != m_dbg;
#endif
}

const MatchingItemBase* MatchingParams::matches(const MatchingItemBase& mi,
    const NamedList* list, const String* str)
{
#define MI_P_CALL (mi.type() == MatchingItemBase::TypeList) ? \
    MI_LIST_C(&mi)->runMatch(this,list,str) : \
    (list ? mi.matchListParam(*list,this) : mi.matchStringOpt(str,this)) ? &mi : 0

#ifndef TRACK_MI_MATCH
    if (!m_dbg)
	return MI_P_CALL;
#endif
    bool first = !m_private;
    if (first)
	m_private = new MatchingParamsData(this);
    MatchingParamsData* data = static_cast<MatchingParamsData*>(m_private);
    String miInfo;
    dumpItemInfo(mi,miInfo);
    MiDebugEnabler dbg(mi,this);
    MiDebugger debugger(&dbg);
    if (dbg.debugAt(data->level)) {
	String info, extra;
	if (list)
	    info.printf(" list='%s'",list->safe());
	else if (str)
	    info.printf(" str='%s'",str->safe());
	else
	    info << " str=<missing>";
	if (mi.type() == MatchingItemBase::TypeList) {
	    const MatchingItemList* lst = MI_LIST_C(&mi);
	    info.printfAppend(" count=%u match_all=%s",
		lst->length(),String::boolText(lst->matchAll()));
	}
	MatchingItemDump d;
	d.dumpValue(&mi,extra);
	if (extra)
	    info << " matching: " << extra;
#ifdef TRACK_MI_MATCH
	debugger.set(data->level,data->safe(),info.safe());
#else
	Debug(&dbg,data->level,">>> %s %s%s",data->safe(),miInfo.safe(),info.safe());
#endif
    }
    const MatchingItemBase* matched = MI_P_CALL;
    const char* res = matched ? "MATCHED" : "not matched";
#ifdef TRACK_MI_MATCH
    debugger.appendName(res);
#else
    Debug(&dbg,matched && first ? data->level : DebugAll,"<<< %s %s %s",
	data->safe(),miInfo.safe(),res);
#endif
    return matched;

#undef MI_P_CALL
}


//
// MatchingItemBase
//
MatchingItemBase::MatchingItemBase(int type, const char* name, bool negated,
    int missingMatch, const char* id)
    : m_type(type), m_name(name), m_notNegated(!negated),
    m_missingMatch(missingMatch), m_id(id)
{
#ifdef TRACK_MI_OBJ_LIFE
    if (type != TypeCustom)
	Debug(s_debug,DebugTest,">>> MatchingItemBase %s neg=%u missing_match=%d id=%s",
	    dumpItemInfo(*this).safe(),negated,missingMatch,id);
#endif
}

MatchingItemBase::~MatchingItemBase()
{
#ifdef TRACK_MI_OBJ_LIFE
    if (type() != TypeCustom)
	Debug(s_debug,DebugTest,"<<< MatchingItemBase %s",dumpItemInfo(*this).safe());
#endif
}

bool MatchingItemBase::runMatchListParam(const NamedList& list, MatchingParams* params) const
{
    return runMatchStringOpt(list.getParam(name()),params);
}

bool MatchingItemBase::runMatchStringOpt(const String* str, MatchingParams* params) const
{
    return str ? runMatchString(*str,params) :
	missingMatch() ? missingMatch() == MissingParamMatch :
	runMatchString(String::empty(),params);
}

const TokenDict* MatchingItemBase::typeDict()
{
    return s_miType;
}

const TokenDict* MatchingItemBase::missingMatchDict()
{
    return s_miMissingMatch;
}

const char* MatchingItemBase::typeName() const
{
    return lookup(type(),typeDict(),"unknown");
}

bool MatchingItemBase::runMatchString(const String& str, MatchingParams* params) const
{
    return false;
}

const String& MatchingItemBase::toString() const
{
    return name();
}


//
// MatchingItemString
//
bool MatchingItemString::runMatchString(const String& str, MatchingParams* params) const
{
#ifdef TRACK_MI_MATCH
    if (!params) {
	MatchingParams mp;
	return mp.matches(*this,0,&str);
    }
#endif
    return m_caseMatch ? (str == m_value) : (str &= m_value);
}

MatchingItemBase* MatchingItemString::copyItem() const
{
    return new MatchingItemString(name(),value(),caseInsensitive(),negated(),missingMatch(),id());
}


//
// MatchingItemRegexp
//
bool MatchingItemRegexp::runMatchString(const String& str, MatchingParams* params) const
{
#ifdef TRACK_MI_MATCH
    if (!params) {
	MatchingParams mp;
	return mp.matches(*this,0,&str);
    }
#endif
    return m_value.matches(str);
}

MatchingItemBase* MatchingItemRegexp::copyItem() const
{
    return new MatchingItemRegexp(name(),value(),negated(),missingMatch(),id());
}

MatchingItemRegexp* MatchingItemRegexp::build(const char* name, const String& str, bool* valid,
    bool validate, int negated, bool insensitive, bool extended, int missingMatch, const char* id)
{
    Regexp rex(0,extended,insensitive);
    if (str) {
	if (negated >= 0)
	    rex.assign(str);
	else {
	    unsigned int pos = str.length() - 1;
	    negated = (str[pos] == '^') ? 1 : 0;
	    if (negated)
		rex.assign(str.c_str(),pos);
	    else
		rex.assign(str);
	}
    }
    else if (negated < 0)
	negated = 0;
    bool ok = true;
    if (valid || validate) {
	if (rex.c_str())
	    ok = rex.compile();
	else
	    ok = false;
	if (valid)
	    *valid = ok;
    }
    return (ok || valid) ? new MatchingItemRegexp(name,rex,negated,missingMatch,id) : 0;
}


//
// MatchingItemXPath
//
bool MatchingItemXPath::runMatch(MatchingParams* params, const NamedList* list,
    const String& str) const
{
#ifdef TRACK_MI_MATCH
    if (!params) {
	MatchingParams mp;
	return mp.matches(*this,list,&str);
    }
#endif
    AutoGenObject autoDel;
    bool foundParam = false;
    String pName;
    XmlElement* xml = 0;
    if (name() && params) {
	pName << name() << "__xml__";
	ObjList* o = params->m_params.find(pName);
	if (o) {
	    xml = YOBJECT(XmlElement,o->get());
	    foundParam = true;
	}
    }
    if (!(xml || foundParam)) {
	if (!list) {
	    xml = YOBJECT(XmlElement,&str);
	    if (!xml) {
		xml = XmlDomParser::parseXml(str,0,"MatchingItemXPath");
		autoDel.set(xml);
	    }
	    foundParam = true;
	}
	else if (name()) {
	    NamedPointer* npOwner = 0;
	    int error = 0;
	    xml = XmlDomParser::getXml(*list,name(),&npOwner,&error,"MatchingItemXPath");
	    foundParam = error != XmlDomParser::GetXmlMissing;
	    // Store in params for later use. Set owned (autodelete)
	    if (pName)
		params->m_params.insert(new AutoGenObject(xml,pName,!npOwner));
	    else if (!npOwner)
		autoDel.set(xml);
	}
    }
    if (params && params->trackMatchDbg()) {
	MiDebugEnabler dbg(*this,params);
	String info;
	if (xml)
	    info.printf("match=(%p)",m_match);
	else
	    info.printf("found=%s",String::boolText(foundParam));
	Debug(&dbg,DebugAll,"[%p] %s xml=(%p) %s",params,dumpItemInfo(*this).safe(),xml,info.safe());
    }
    bool ok = false;
    if (xml) {
	if (!m_match)
	    ok = m_value.find(*xml,XPath::FindAny);
	else if (params)
	    ok = params->matches(*m_match,0,m_value.findText(*xml));
	else
	    ok = m_match->matchStringOpt(m_value.findText(*xml),params);
    }
    else if (!foundParam)
	ok = missingMatch() == MissingParamMatch;
    return ok;
}

bool MatchingItemXPath::runMatchString(const String& str, MatchingParams* params) const
{
    return runMatch(params,0,str);
}

bool MatchingItemXPath::runMatchListParam(const NamedList& list, MatchingParams* params) const
{
    return runMatch(params,&list);
}

MatchingItemBase* MatchingItemXPath::copyItem() const
{
    MatchingItemBase* m = m_match ? m_match->copy() : 0;
    return new MatchingItemXPath(name(),value(),m,negated(),missingMatch(),id());
}

MatchingItemXPath* MatchingItemXPath::build(const char* name, const String& str, String* error,
    bool validate, MatchingItemBase* match, bool negated, int missingMatch, const char* id)
{
    if (!(error || validate))
	return new MatchingItemXPath(name,str,match,negated,missingMatch,id);
    XPath path(str);
    unsigned int res = path.parse();
    if (0 == res || error) {
	if (error)
	    path.describeError(*error);
	return new MatchingItemXPath(name,path,match,negated,missingMatch,id);
    }
    TelEngine::destruct(match);
    return 0;
}

MatchingItemXPath::~MatchingItemXPath()
{
    TelEngine::destruct(m_match);
}


//
// MatchingItemRandom
//
bool MatchingItemRandom::runMatchString(const String& str, MatchingParams* params) const
{
#ifdef TRACK_MI_MATCH
    if (!params) {
	MatchingParams mp;
	return mp.matches(*this,0,&str);
    }
#endif
    return value() > (Random::random() % (maxValue() - 1));
}

MatchingItemBase* MatchingItemRandom::copyItem() const
{
    return new MatchingItemRandom(value(),maxValue(),negated(),name(),missingMatch(),id());
}

MatchingItemRandom* MatchingItemRandom::build(const String& str, bool* valid,
    bool validate, bool negated, const char* name, int missingMatch, const char* id)
{
    int64_t v = 0;
    int64_t maxV = 100;
    if (str) {
	// Value: val[/maxVal] or [0..100]%
	if (str[str.length() - 1] == '%') {
	    v = str.substr(0,str.length() - 1).toInt64(-1);
	    if (v > 100)
		v = -1;
	}
	else {
	    int pos = str.find('/');
	    if (pos > 0) {
		v = str.substr(0,pos).toInt64(-1);
		maxV = str.substr(pos + 1).toInt64(-1);
	    }
	    else
		v = str.toInt64(-1);
	}
    }
    bool ok = v >= 0 && v <= 0xffffffff && maxV >= 0 && maxV <= 0xffffffff;
    if (ok && (valid || validate) && checkMatchValues(v,maxV))
	ok = false;
    if (ok || valid) {
	if (valid)
	    *valid = ok;
	return new MatchingItemRandom(v,maxV,negated,name,missingMatch,id);
    }
    return 0;
}


//
// MatchingItemList
//
MatchingItemList::MatchingItemList(const char* name, bool matchAll, bool negated,
    int missingMatch, const char* id)
    : MatchingItemBase(TypeList,name,negated,missingMatch,id),
    m_value(true,3), m_matchAll(matchAll)
{
}

const MatchingItemBase* MatchingItemList::runMatch(MatchingParams* params,
    const NamedList* list, const String& str) const
{
#ifdef TRACK_MI_MATCH
    if (!params) {
	MatchingParams mp;
	return mp.matches(*this,list,&str);
    }
#endif
    const GenObject** d = m_value.data();
    // Empty list: no match
    if (!(d && *d))
	return 0;
    unsigned int n = m_value.length();
    while (n--) {
	const MatchingItemBase* item = static_cast<const MatchingItemBase*>(*d++);
	if (!item)
	    break;
	bool ok = params ? (0 != params->matches(*item,list,&str)) :
	    list ? item->matchListParam(*list) : item->matchString(str);
	if (ok) {
	    // Item matched: done if not all match (any match)
	    if (!matchAll())
		return item;
	    continue;
	}
	// Not matched: done if all match is required
	if (matchAll())
	    return 0;
    }
    // End of list reached
    // Match any: not matched (no item matched), match all: matched
    return matchAll() ? this : 0;
}

MatchingItemBase* MatchingItemList::copyItem() const
{
    MatchingItemList* lst = new MatchingItemList(name(),matchAll(),negated(),missingMatch(),id());
    lst->m_value.resize(length());
    GenObject** dst = lst->m_value.data();
    if (dst) {
	const GenObject** src = m_value.data();
	for (unsigned int n = m_value.length(); n--; ++src)
	    *dst++ = *src ? static_cast<const MatchingItemBase*>(*src)->copy() : 0;
	lst->m_value.compact(true);
    }
    return lst;
}

bool MatchingItemList::runMatchString(const String& str, MatchingParams* params) const
{
    return runMatch(params,0,str);
}

bool MatchingItemList::runMatchListParam(const NamedList& list, MatchingParams* params) const
{
    return runMatch(params,&list);
}

MatchingItemBase* MatchingItemList::doOptimize(MatchingItemList* list, uint64_t flags,
    unsigned int depth, const MatchingItemLoad* loader)
{
    if (!list)
	return 0;
    if (list->m_value.at(1)) {
	GenObject** d = list->m_value.data();
	for (unsigned int i = 0; i < list->m_value.length(); ++i) {
	    if (!d[i] || static_cast<MatchingItemBase*>(d[i])->type() != TypeList)
		continue;
	    d[i] = doOptimize(static_cast<MatchingItemList*>(d[i]),flags,depth + 1,loader);
	}
	list->m_value.compact(true);
	if (list->m_value.at(1))
	    return list;
	// fall through
    }
    MatchingItemBase* ret = static_cast<MatchingItemBase*>(list->m_value.take(0));
    if (ret) {
	// Reverse item (not)negated flag if list is negated to keep the same matching behaviour
	if (list->negated())
	    ret->m_notNegated = !ret->m_notNegated;
    }
    TelEngine::destruct(list);
    return ret;
}

bool MatchingItemList::append(ObjList& items)
{
    ObjList* first = items.skipNull();
    if (!first)
	return false;
    unsigned int pos = m_value.length();
    m_value.resize(m_value.length() + first->count(),true,false);
    GenObject** dst = m_value.data(pos);
    if (dst) {
	do
	    *dst++ = static_cast<MatchingItemBase*>(first->remove(false));
	while (0 != (first = first->skipNull()));
    }
    m_value.compact(true);
    return !!dst;
}

bool MatchingItemList::change(MatchingItemBase* item, int pos, bool ins)
{
    if (!item) {
	if (ins || pos < 0 || pos >= (int)m_value.length())
	    return false;
	// Remove
	item = static_cast<MatchingItemBase*>(m_value.take(pos));
	if (item) {
	    m_value.compact(true);
	    TelEngine::destruct(item);
	}
	return true;
    }
    bool ok = false;
    if (ins)
	ok = m_value.insertObj(item,pos);
    else if (pos >= 0 && (unsigned int)pos < m_value.length())
	ok = m_value.set(item,pos);
    else
	ok = m_value.appendObj(item);
    if (!ok)
	TelEngine::destruct(item);
    m_value.compact(true);
    return ok;
}


//
// MatchingItemCustom
//
MatchingItemCustom::MatchingItemCustom(const char* type, const char* name, const char* typeDisplay)
    : MatchingItemBase(TypeCustom,name), m_type(type), m_typeDisplay(typeDisplay)
{
    if (!m_type)
	m_type = "custom";
    if (!m_typeDisplay) {
	m_typeDisplay = m_type;
	m_typeDisplay.toUpper();
    }
#ifdef TRACK_MI_OBJ_LIFE
    Debug(s_debug,DebugTest,">>> MatchingItemCustom %s",dumpItemInfo(*this).safe());
#endif
}

MatchingItemCustom::~MatchingItemCustom()
{
#ifdef TRACK_MI_OBJ_LIFE
    Debug(s_debug,DebugTest,"<<< MatchingItemCustom %s",dumpItemInfo(*this).safe());
#endif
}

MatchingItemBase* MatchingItemCustom::copyItem() const
{
    MatchingItemBase* mi = customCopyItem();
    if (!mi)
	return 0;
    mi->m_notNegated = m_notNegated;
    mi->m_missingMatch = m_missingMatch;
    mi->m_id = m_id;
    return mi;
}

const char* MatchingItemCustom::typeName() const
{
    return m_type.c_str();
}

const String* MatchingItemCustom::valueStr() const
{
    return 0;
}

const String& MatchingItemCustom::dumpValue(const MatchingItemDump& dump, String& buf) const
{
    return buf;
}

String& MatchingItemCustom::dump(const MatchingItemDump& dump, String& buf,
    const String& indent, const String& addIndent, unsigned int depth) const
{
    return buf;
}

String& MatchingItemCustom::dumpFull(const MatchingItemDump& dump, String& buf,
    const String& indent, const String& addIndent, unsigned int depth) const
{
    return buf;
}

void MatchingItemCustom::dumpXml(const MatchingItemDump& dump, XmlElement* xml,
    unsigned int depth) const
{
}

unsigned int MatchingItemCustom::dumpList(const MatchingItemDump& dump, NamedList& list,
    const char* prefix, unsigned int depth, const char* id) const
{
    return 0;
}

bool MatchingItemCustom::loadItem(const MatchingItemLoad& load, uint64_t flags,
    const NamedList& params, String* error, const char* prefix)
{
    return false;
}

bool MatchingItemCustom::loadXml(const MatchingItemLoad& load, uint64_t flags,
    const XmlElement& xml, String* error)
{
    return false;
}


//
// MatchingItemCustomFactory
//
ObjVector s_customFactory(false,5);
RWLock s_customFactLock("MiCustomFactory");

MatchingItemCustom* MatchingItemCustomFactory::build(const String& type, const char* name,
    bool* known)
{
    Lock lck(s_customFactLock,-1,true);
    MatchingItemCustomFactory* f = static_cast<MatchingItemCustomFactory*>(s_customFactory[type]);
    if (known)
	*known = 0 != f;
    return f ? f->customBuild(name) : 0;
}

MatchingItemCustomFactory::MatchingItemCustomFactory(const char* name)
    : m_name(name)
{
    if (!m_name)
	return;
    Lock lck(s_customFactLock);
    if (!s_customFactory[m_name])
	s_customFactory.appendObj(this);
    else {
	Debug(DebugGoOn,"Trying to add duplicate MatchingItemCustomFactory '%s'",name);
	m_name = "";
    }
}

MatchingItemCustomFactory::~MatchingItemCustomFactory()
{
    if (!m_name)
	return;
    Lock lck(s_customFactLock);
    int idx = s_customFactory.index(this);
    if (idx < 0)
	return;
    s_customFactory.take(idx);
    s_customFactory.compact();
}

const String& MatchingItemCustomFactory::toString() const
{
    return name();
}


//
// MatchingItemLoad
//
static const TokenDict64 s_miLoadFlags[] = {
    {"ignore_failed", MatchingItemLoad::IgnoreFailed},
    {"load_invalid", MatchingItemLoad::LoadInvalid},
    {"load_item_id", MatchingItemLoad::LoadItemId},
    {"validate", MatchingItemLoad::Validate},
    {"regexp_validate", MatchingItemLoad::RexValidate},
    {"xpath_validate", MatchingItemLoad::XPathValidate},
    {"random_validate", MatchingItemLoad::RandomValidate},
    {"nooptimize", MatchingItemLoad::NoOptimize},
    {"name_required_simple", MatchingItemLoad::NameReqSimple},
    {"name_required_list", MatchingItemLoad::NameReqList},
    {"name_required_xpath_match", MatchingItemLoad::NameReqXPathMatch},
    {"regexp_basic", MatchingItemLoad::RexBasic},
    {"regexp_detect", MatchingItemLoad::RexDetect},
    {"regexp_detect_negated", MatchingItemLoad::RexDetectNegated},
    {"list_any", MatchingItemLoad::ListAny},
    {0,0}
};

static const TokenDict s_miFlags[] = {
    {"negated",          MatchingItemLoad::ItemNegated},
    {"caseinsensitive",  MatchingItemLoad::ItemCaseInsensitive},
    {"basic",            MatchingItemLoad::ItemBasic},
    {"any",              MatchingItemLoad::ItemAny},
    {"missing_match",    MatchingItemLoad::ItemMissingMatch},
    {"missing_no_match", MatchingItemLoad::ItemMissingNoMatch},
    {0,0}
};

static inline bool miNegated(unsigned int flags)
{
    return flagSet(flags,MatchingItemLoad::ItemNegated);
}

static inline bool miMatchAll(unsigned int flags)
{
    return !flagSet(flags,MatchingItemLoad::ItemAny);
}

static inline int miMissingMatch(unsigned int flags)
{
    if (flagSet(flags,MatchingItemLoad::ItemMissingMatch))
	return MatchingItemBase::MissingParamMatch;
    if (flagSet(flags,MatchingItemLoad::ItemMissingNoMatch))
	return MatchingItemBase::MissingParamNoMatch;
    return MatchingItemBase::MissingParamRunMatch;
}

static inline bool miCaseInsensitive(unsigned int flags)
{
    return flagSet(flags,MatchingItemLoad::ItemCaseInsensitive);
}

static inline unsigned int buildFlags(const MatchingItemBase* mi)
{
    if (!mi)
	return 0;
    unsigned int flags = 0;
    if (mi->negated())
	flags |= MatchingItemLoad::ItemNegated;
    if (mi->missingMatch()) {
	if (mi->missingMatch() == MatchingItemBase::MissingParamMatch)
	    flags |= MatchingItemLoad::ItemMissingMatch;
	else if (mi->missingMatch() == MatchingItemBase::MissingParamNoMatch)
	    flags |= MatchingItemLoad::ItemMissingNoMatch;
    }
    switch (mi->type()) {
	case MatchingItemBase::TypeList:
	    if (!MI_LIST_C(mi)->matchAll())
		flags |= MatchingItemLoad::ItemAny;
	    break;
	case MatchingItemBase::TypeString:
	    if (MI_STR_C(mi)->caseInsensitive())
		flags |= MatchingItemLoad::ItemCaseInsensitive;
	    break;
	case MatchingItemBase::TypeRegexp:
	    if (MI_REX_C(mi)->value().isCaseInsensitive())
		flags |= MatchingItemLoad::ItemCaseInsensitive;
	    if (!MI_REX_C(mi)->value().isExtended())
		flags |= MatchingItemLoad::ItemBasic;
	    break;
    }
    return flags;
}

class MiLoad : public GenObject
{
public:
    inline MiLoad()
	: m_type(MatchingItemBase::TypeList),
	m_value(0), m_name(0), m_flagsStr(0), m_flags(0), m_id(0), m_typeName(0)
	{}
    inline MiLoad(const NamedList& params, const char* prefix, const String* tName = 0)
	: m_type(0), m_value(0), m_name(0), m_flagsStr(0), m_flags(0), m_id(0), m_typeName(tName)
	{
	    String pref(prefix);
	    m_type = m_typeName ? lookup(*m_typeName,s_miType) : MatchingItemBase::TypeList;
	    if (m_type != MatchingItemBase::TypeList) {
		if (!pref)
		    m_value = params.getParam(s_value);
		else
		    m_value = params.getParam(pref + s_value);
	    }
	    if (!pref)
		fillCommon(params);
	    else {
		m_name = params.getParam(pref + s_name);
		m_id = params.getParam(pref + s_id);
		setFlags(params.getParam(pref + s_flags));
	    }
	}
    inline MiLoad(const XmlElement& xml)
	: m_type(lookup(xml.getTag(),s_miType)),
	m_value(0), m_name(0), m_flagsStr(0), m_flags(0), m_id(0),
	m_typeName(&(xml.getTag())) {
	    m_value = &(xml.getText());
	    fillCommon(xml.attributes());
	}
    inline MiLoad(const String* tName, const String* name, const String* val, const String* flags)
	: m_type(lookup(validStr(tName),s_miType)),
	m_value(val), m_name(name), m_flagsStr(0), m_flags(0), m_id(0), m_typeName(tName)
	{ setFlags(flags); }
    inline const String& name() const
	{ return validStr(m_name); }
    inline const String& value() const
	{ return validStr(m_value); }
    inline const String& id() const
	{ return validStr(m_id); }
    inline bool haveFlags() const
	{ return m_flagsStr; }
    inline unsigned int flags() const
	{ return m_flags; }
    inline const String& typeName() const
	{ return validStr(m_typeName); }
    inline void setFlags(const String* str) {
	    m_flagsStr = str;
	    m_flags = m_flagsStr ? m_flagsStr->encodeFlags(s_miFlags) : 0;
	}
    inline void fillCommon(const NamedList& list) {
	    m_name = list.getParam(s_name);
	    m_id = list.getParam(s_id);
	    setFlags(list.getParam(s_flags));
	}
    static inline const String& validStr(const String* str)
	{ return str ? *str : String::empty(); }

    int m_type;
    const String* m_value;
    const String* m_name;
    const String* m_flagsStr;
    unsigned int m_flags;
    const String* m_id;
    const String* m_typeName;
};

class MiLoadDebug : public MiDebugger
{
public:
    inline MiLoadDebug(const MatchingItemLoad& mil, uint64_t flags, const char* loc = 0,
	const char* prefix = 0)
#ifdef TRACK_MI_LOAD
	: MiDebugger(mil.m_dbg ? mil.m_dbg : s_debug) {
	    if (loc) {
		String info;
		info.decodeFlags(flags,MatchingItemLoad::loadFlags());
		info.printf(" [%s] flags 0x" FMT64x " (%s)",loc,flags,info.safe());
		if (!TelEngine::null(prefix))
		    info.printfAppend(" prefix='%s'",prefix);
		set(DebugCall,"MatchingItem LOAD",info);
	    }
	}
#else
	{}
#endif
    inline void setMiLoad(MiLoad& d, const char* loc, const String& pName,
	const NamedList* params, const char* prefix,
	const XmlElement* xml, const String* xmlFrag) {
#ifdef TRACK_MI_LOAD
	    if (m_debugger)
		return;
	    String info;
	    info.printf(" [%s] type=(%d,%s)",TelEngine::c_safe(loc),d.m_type,lookup(d.m_type,s_miType,""));
	    if (d.haveFlags()) {
		String s;
		info.printfAppend(" flags=(0x%x,%s)",d.m_flags,s.decodeFlags(d.m_flags,s_miFlags).safe());
	    }
	    info.printfAppend(" parameter=(%s,%s)",pName.safe(),d.name().safe());
	    if (params)
		info.printfAppend(" params=(%p,%s) prefix='%s'",
		    params,params->safe(),TelEngine::c_safe(prefix));
	    else if (xml)
		info.printfAppend(" xml=(%p,%s)",xml,xml->tag());
	    else if (xmlFrag)
		info.printfAppend(" fragment=(%p,%s)",xmlFrag,xmlFrag->safe());
	    set(DebugAll,"MatchingItem LOAD ITEM",info);
#endif
	}
};

static inline bool miLoadAdd(ObjList*& add, MatchingItemBase* mi, bool fatal)
{
    if (mi)
	add = add->append(mi);
    else if (fatal)
	return false;
    return true;
}

static inline void warnLoadInvalid(DebugEnabler* dbg, const char* what,
    const char* param, const String& value, const char* loc, const char* error = 0)
{
    String e;
    if (!TelEngine::null(error))
	e.printf(" (%s)",error);
    Debug(dbg,DebugConf,"Loaded invalid matching %s '%s'='%s'%s in '%s'",
	what,param,value.safe(),e.safe(),TelEngine::c_safe(loc));
}

static inline bool milIgnore(const MatchingItemLoad& mil, const String& val, bool name,
    const char* param, const char* loc)
{
    if (!val)
	return false;
    int i = name ?
	((mil.m_ignoreName && mil.m_ignoreName->find(val)) ? 1 :
	(mil.m_allowName && mil.m_allowName->find(val)) ? -1 : 0)
	:
	((mil.m_ignoreType && mil.m_ignoreType->find(val)) ? 1 :
	(mil.m_allowType && mil.m_allowType->find(val)) ? -1 : 0);
    if (!i)
	return false;
    if (mil.m_dbg && mil.m_warnLevel > 0)
	Debug(mil.m_dbg,mil.m_warnLevel,
	    "Item '%s' (%s) %s %s in '%s'",param,val.c_str(),(name ? "name" : "type"),
	    (i > 0 ? "ignored" : "not allowed"),TelEngine::c_safe(loc));
    return 0;
}

MatchingItemBase* MatchingItemLoad::miLoadItem(uint64_t flags, bool& fatal, String* error, void* data,
    const char* loc, const String& pName, const NamedList* params, const char* prefix,
    const XmlElement* xml, const String* xmlFrag, bool forceFail) const
{
    if (!data)
	return 0;
    MiLoad& d = *(MiLoad*)data;
    MiLoadDebug dbg(*this,flags);
    const String& name = d.name();
    MatchingItemBase* ret = 0;
    String loadError;
    while (true) {
	const String& id = flagSet(flags,LoadItemId) ? d.id() : String::empty();
	bool negated = miNegated(d.flags());
	int missingMatch = miMissingMatch(d.flags());
	if (MatchingItemBase::TypeList == d.m_type) {
	    ObjList items;
	    ObjList* add = &items;
	    if (params) {
		if (!prefix)
		    prefix = "";
		String prefItem(prefix,-1,s_item);
		String itemXml(prefix,-1,s_xml);
		for (ObjList* o = params->paramList()->skipNull(); o; o = o->skipNext()) {
		    MatchingItemBase* mi = 0;
		    NamedString* ns = static_cast<NamedString*>(o->get());
		    if (ns->name().startsWith(prefItem)) {
			if (ns->name().length() == prefItem.length())
			    continue;
			dbg.setMiLoad(d,loc,pName,params,prefix,xml,xmlFrag);
			String tmpPref;
			tmpPref.printf("%s%s:",prefix,ns->name().c_str() + prefItem.length());
			MiLoad tmpData(*params,tmpPref,ns);
			mi = miLoadItem(flags,fatal,error,&tmpData,loc,ns->name(),params,tmpPref,
			    0,0,forceFail);
		    }
		    else if (ns->name() == itemXml) {
			dbg.setMiLoad(d,loc,pName,params,prefix,xml,xmlFrag);
			MiLoad tmpData;
			mi = miLoadItem(flags,fatal,error,&tmpData,loc,ns->name(),
			    0,0,0,ns,forceFail);
		    }
		    if (!miLoadAdd(add,mi,fatal))
			return 0;
		}
	    }
	    else {
		dbg.setMiLoad(d,loc,pName,params,prefix,xml,xmlFrag);
		XmlDomParser parser("MatchingItemLoad",true);
		if (!xml && xmlFrag && *xmlFrag && !parser.parse(*xmlFrag)) {
		    loadError.printf("invalid '%s' xml (%s)",pName.safe(name),parser.getError());
		    break;
		}
		ObjList* children = xml ? xml->getChildren().skipNull() :
		    parser.fragment()->getChildren().skipNull();
		for (XmlElement* x = 0; 0 != (x = XmlFragment::getElement(children)); ) {
		    MiLoad tmpData(*x);
		    MatchingItemBase* mi = miLoadItem(flags,fatal,error,&tmpData,loc,
			pName ? pName : x->getTag(),0,0,x,0,forceFail);
		    if (!miLoadAdd(add,mi,fatal))
			return 0;
		}
	    }
	    ret = miLoadRetList(flags,items,name,miMatchAll(d.flags()),negated,missingMatch,id);
	    break;
	}

	const char* pn = pName.safe(name.safe());
	if (milIgnore(*this,name,true,pn,loc))
	    return 0;
	const String& val = d.value();
	const String& tName = d.typeName();
	if (!(d.m_type || tName)) {
	    if (flagSet(flags,RexDetect) && val[0] == '^')
		d.m_type = MatchingItemBase::TypeRegexp;
	    else
		d.m_type = MatchingItemBase::TypeString;
	}
	if ((m_ignoreType || m_allowType) && (tName || d.m_type)) {
	    String tmp(tName ? "" : lookup(d.m_type,s_miType));
	    if (milIgnore(*this,tName ? tName : tmp,false,pn,loc))
		return 0;
	}
	dbg.setMiLoad(d,loc,pName,params,prefix,xml,xmlFrag);
	if (MatchingItemBase::TypeString == d.m_type)
	    ret = new MatchingItemString(name,val,miCaseInsensitive(d.flags()),negated,
		missingMatch,id);
	else if (MatchingItemBase::TypeRegexp == d.m_type) {
	    bool ok = true;
	    unsigned int useF = xml || d.haveFlags();
	    int neg = (useF || !flagSet(flags,RexDetectNegated)) ? (negated ? 1 : 0) : -1;
	    bool ci = useF ? miCaseInsensitive(d.flags()) : false;
	    bool extended = useF ? !flagSet(d.flags(),ItemBasic) : !flagSet(flags,RexBasic);
	    ret = MatchingItemRegexp::build(name,val,flagSet(flags,LoadInvalid) ? &ok : 0,
		flagSet(flags,RexValidate),neg,ci,extended,missingMatch,id);
	    if (!ret)
		loadError.printf("invalid regexp '%s'='%s'",pn,val.safe());
	    else if (!ok && m_dbg)
		warnLoadInvalid(m_dbg,"regexp",pn,val.safe(),loc);
	}
	else if (MatchingItemBase::TypeXPath == d.m_type) {
	    MatchingItemBase* match = miLoadItemParam(flags | InternalInXPathMatch,s_match,fatal,
		error,loc,pName,params,prefix,xml);
	    if (!match && fatal)
		return 0;
	    String e;
	    ret = MatchingItemXPath::build(name,val,flagSet(flags,XPathValidate) ? &e : 0,
		!flagSet(flags,LoadInvalid),match,negated,missingMatch,id);
	    if (!ret)
		loadError.printf("invalid xpath '%s'='%s' (%s)",pn,val.safe(),e.safe());
	    else if (e && m_dbg)
		warnLoadInvalid(m_dbg,"xpath",pn,val,loc,e);
	}
	else if (MatchingItemBase::TypeRandom == d.m_type) {
	    bool ok = true;
	    ret = MatchingItemRandom::build(val,flagSet(flags,LoadInvalid) ? &ok : 0,
		flagSet(flags,RandomValidate),negated,name,missingMatch,id);
	    if (!ret)
		loadError.printf("invalid random '%s'='%s'",pn,val.safe());
	    else if (!ok && m_dbg)
		warnLoadInvalid(m_dbg,"random",pn,val,loc);
	}
	else if (tName) {
	    if (!d.m_type) {
		bool known = false;
		ret = MatchingItemCustomFactory::build(tName,name,&known);
		if (ret) {
		    ret->m_notNegated = negated;
		    ret->m_missingMatch = missingMatch;
		    ret->m_id = id;
		    MatchingItemCustom* c = static_cast<MatchingItemCustom*>(ret);
		    bool ok = params ? c->loadItem(*this,flags,*params,error,prefix) :
			c->loadXml(*this,flags,*xml,error);
		    if (!ok)
			TelEngine::destruct(ret);
		    break;
		}
		if (known)
		    break;
	    }
#ifdef TRACK_MI_LOAD
	    Debug(dbg.m_dbg,DebugStub,"Unhandled load for type %d '%s'",d.m_type,tName.safe());
#else
	    if (m_dbg)
		Debug(m_dbg,m_warnLevel > 0 ? m_warnLevel : DebugAll,
		    "Unknown matching type '%s' in '%s'",tName.safe(),TelEngine::c_safe(loc));
#endif
	    return 0;
	}
	else
	    return 0;
	break;
    }

    if (!loadError && ret) {
	if (!ret->name() && nameRequired(ret->type(),flags)) {
	    if (pName)
		loadError.printf("invalid '%s' name (empty)",pName.safe());
	    else
		loadError = "invalid name (empty)";
	}
    }
    if (loadError) {
	if (forceFail || !flagSet(flags,IgnoreFailed)) {
	    fatal = true;
	    if (error)
		*error = loadError;
	}
	else if (m_dbg)
	    Debug(m_dbg,m_warnLevel > 0 ? m_warnLevel : DebugConf,
		"Failed to load matching in '%s': %s",TelEngine::c_safe(loc),loadError.safe());
	TelEngine::destruct(ret);
    }
#ifdef TRACK_MI_LOAD
    if (ret)
	Debug(dbg.m_dbg,DebugAll,"Built item %s",dumpItemInfo(*ret).safe());
    else if (loadError && !m_dbg)
	Debug(dbg.m_dbg,DebugNote,"Load error: %s",loadError.safe());
#endif
    return ret;
}

MatchingItemBase* MatchingItemLoad::miLoadItemParam(uint64_t flags, const String& name, bool& fatal,
    String* error, const char* loc, const String& pName,
    const NamedList* params, const char* prefix, const XmlElement* xml) const
{
    fatal = false;
    if (params) {
	String pref;
	pref << prefix << name << ":";
	MiLoad data(*params,pref);
	return miLoadItem(flags,fatal,error,&data,loc,pName,params,pref,0,0,true);
    }
    if (xml) {
	xml = xml->findFirstChild(name);
	if (!xml)
	    return 0;
	MiLoad data(*xml);
	return miLoadItem(flags,fatal,error,&data,loc,pName ? pName : xml->getTag(),
	    0,0,xml,0,true);
    }
    return 0;
}

MatchingItemBase* MatchingItemLoad::miLoadRetList(uint64_t flags, ObjList& items, const char* name,
    bool matchAll, bool negated, int missingMatch, const char* id) const
{
    ObjList* first = items.skipNull();
    if (!first)
	return 0;
    if (!first->skipNext()) {
	MatchingItemBase* mi = static_cast<MatchingItemBase*>(first->remove(false));
	// Reverse item (not)negated flag if list is negated to keep the same matching behaviour
	if (negated)
	    mi->m_notNegated = !mi->m_notNegated;
	return mi;
    }
    MatchingItemList* l = new MatchingItemList(name,matchAll,negated,missingMatch,id);
    l->append(items);
    return flagSet(flags,NoOptimize) ? l : MatchingItemList::doOptimize(l,flags,0,this);
}

MatchingItemBase* MatchingItemLoad::loadItem(const NamedList& params, String* error,
    const char* prefix, uint64_t* flags) const
{
    uint64_t f = flags ? *flags : m_flags;
    MiLoadDebug dbg(*this,f,"loadItem",prefix);
    bool fatal = false;
    MiLoad data(params,prefix);
    return miLoadItem(f,fatal,error,&data,params,String::empty(),&params,prefix);
}

MatchingItemBase* MatchingItemLoad::loadXml(const String& str, String* error,
    uint64_t* flags) const
{
    if (!str)
	return 0;
    uint64_t f = flags ? *flags : m_flags;
    MiLoadDebug dbg(*this,f,"loadXmlStr");
    bool fatal = false;
    MiLoad data;
    return miLoadItem(f,fatal,error,&data,"loadXml",String::empty(),0,0,0,&str);
};

MatchingItemBase* MatchingItemLoad::loadXml(const XmlElement* xml, String* error,
    uint64_t* flags) const
{
    if (!xml)
	return 0;
    uint64_t f = flags ? *flags : m_flags;
    MiLoadDebug dbg(*this,f,"loadXml");
    bool fatal = false;
    MiLoad data(*xml);
    return miLoadItem(f,fatal,error,&data,"loadXml",String::empty(),0,0,xml);
}

MatchingItemBase* MatchingItemLoad::load(const NamedList& params, String* error,
    const char* prefix, const char* suffix, uint64_t* flags) const
{
    uint64_t f = flags ? *flags : m_flags;
    MiLoadDebug dbg(*this,f,"load");
    String prefMatch(prefix), suff(suffix);
    String prefXml(prefMatch + "xml:"), prefFlags(prefMatch + "flags:"),
	prefType(prefMatch + "type:"), listFlags((prefMatch + "listflags"));
    if (prefMatch)
	prefMatch << ":";
    if (suff) {
	suff << ':';
	prefMatch << suff;
	prefXml << suff;
	prefFlags << suff;
	prefType << suff;
	listFlags << ":" << suff;
    }
    XDebug("MatchingItemLoad",DebugAll,"load(%s,%p,%s,%s) prefMatch='%s'",
	params.safe(),error,TelEngine::c_safe(prefix),TelEngine::c_safe(suffix),
	prefMatch.safe());
    ObjList items;
    ObjList* add = &items;
    bool fatal = false;
    for (ObjList* o = params.paramList()->skipNull(); o; o = o->skipNext()) {
	MatchingItemBase* mi = 0;
	NamedString* ns = static_cast<NamedString*>(o->get());
	if (!ns->name().startsWith(prefMatch)) {
	    if (!ns->name().startsWith(prefXml))
		continue;
	    MiLoad tmpData;
	    mi = miLoadItem(f,fatal,error,&tmpData,"load",ns->name(),0,0,0,ns);
	}
	else {
	    if (ns->name().length() == prefMatch.length())
		continue;
	    String name(ns->name().c_str() + prefMatch.length());
	    const String& tName = params[prefType + name];
	    MiLoad miLoad(&tName,&name,ns,params.getParam(prefFlags + name));
	    mi = miLoadItem(f,fatal,error,&miLoad,"load",ns->name());
	}
	if (mi)
	    add = add->append(mi);
	else if (fatal)
	    return 0;
    }
    const String* tmp = params.getParam(listFlags);
    if (!tmp)
	return miLoadRetList(f,items,"",!flagSet(f,ListAny));
    unsigned int mif = tmp->encodeFlags(s_miFlags);
    return miLoadRetList(f,items,"",miMatchAll(mif),miNegated(mif));
}

const TokenDict64* MatchingItemLoad::loadFlags()
{
    return s_miLoadFlags;
}

const TokenDict* itemFlags()
{
    return s_miFlags;
}


//
// MatchingItemDump
//
static const TokenDict s_miDumpFlags[] = {
    {"force_initial_list_desc", MatchingItemDump::ForceInitialListDesc},
    {"dump_xml", MatchingItemDump::DumpXmlStr},
    {"ignore_name", MatchingItemDump::IgnoreName},
    {"dump_ignore_empty", MatchingItemDump::DumpIgnoreEmpty},
    {"dump_item_flag_name", MatchingItemDump::DumpItemFlagsName},
    {"dump_item_id", MatchingItemDump::DumpItemId},
    {"dump_custom_full", MatchingItemDump::DumpCustomFull},
    {0,0}
};

MatchingItemDump::MatchingItemDump(const NamedList* params, const char* name)
    : String(name), m_flags(0), m_rexEnclose('/'), m_strEnclose('\''),
    m_nameValueSep(": "), m_negated('!'), m_missingMatch(true), m_caseInsentive('i'),
    m_regexpBasic(0), m_regexpExtended(0)
{
    if (params)
	init(*params);
}

void MatchingItemDump::init(const NamedList& params)
{
    for (ObjList* o = params.paramList()->skipNull(); o; o = o->skipNext()) {
	NamedString* ns = static_cast<NamedString*>(o->get());
	if (ns->name() == s_flags)
	    m_flags = ns->encodeFlags(s_miDumpFlags);
	else if (ns->name() == YSTRING("rex_enclose"))
	    m_rexEnclose = (*ns)[0];
	else if (ns->name() == YSTRING("str_enclose"))
	    m_strEnclose = (*ns)[0];
	else if (ns->name() == YSTRING("name_value_sep"))
	    m_nameValueSep = *ns;
	else if (ns->name() == YSTRING("prop_negated"))
	    m_negated = (*ns)[0];
	else if (ns->name() == YSTRING("missing_match_dump"))
	    m_missingMatch = ns->toBoolean();
	else if (ns->name() == YSTRING("prop_caseinsensitive"))
	    m_caseInsentive = (*ns)[0];
	else if (ns->name() == YSTRING("prop_rex_basic"))
	    m_regexpBasic = (*ns)[0];
	else if (ns->name() == YSTRING("prop_rex_extended"))
	    m_regexpExtended = (*ns)[0];
    }
}

String& MatchingItemDump::dumpValue(const MatchingItemBase* item, String& buf) const
{
    if (!item)
	return buf;
    if (item->type() == MatchingItemBase::TypeList)
	return buf;
    String flags;
    if (s_dumpItemFlagName || 0 != (m_flags & DumpItemFlagsName))
	flags.decodeFlags(buildFlags(item),s_miFlags);
    else {
	if (item->negated())
	    flags << m_negated;
	switch (item->type()) {
	    case MatchingItemBase::TypeString:
		if (MI_STR_C(item)->caseInsensitive())
		    flags << m_caseInsentive;
		break;
	    case MatchingItemBase::TypeRegexp:
		if (MI_REX_C(item)->value().isCaseInsensitive())
		    flags << m_caseInsentive;
		if (MI_REX_C(item)->value().isExtended())
		    flags << m_regexpExtended;
		else
		    flags << m_regexpBasic;
		break;
	}
	if (m_missingMatch && item->missingMatch()) {
	    const char* s = lookup(item->missingMatch(),s_miMissingMatch);
	    if (s) {
		String tmp;
		flags.append(tmp.printf("-%s",s)," ");
	    }
	}
    }
    if (flags)
	buf << "[" << flags << "] ";
    return dumpValueStr(item,buf,true);
}

String& MatchingItemDump::dumpValueStr(const MatchingItemBase* item, String& buf, bool typeInfo) const
{
    if (!item)
	return buf;
    switch (item->type()) {
	case MatchingItemBase::TypeString:
	    if (typeInfo)
		buf << m_strEnclose << MI_STR_C(item)->value() << m_strEnclose;
	    else
		buf << MI_STR_C(item)->value();
	    break;
	case MatchingItemBase::TypeRegexp:
	    if (typeInfo)
		buf << m_rexEnclose << MI_REX_C(item)->value() << m_rexEnclose;
	    else
		buf << MI_REX_C(item)->value();
	    break;
	case MatchingItemBase::TypeXPath:
	    if (typeInfo)
		buf << "XPATH: ";
	    buf << MI_XPATH_C(item)->value();
	    break;
	case MatchingItemBase::TypeRandom:
	    if (typeInfo)
		buf << "RANDOM: ";
	    MI_RAND_C(item)->dumpValue(buf);
	    break;
	case MatchingItemBase::TypeCustom:
	    if (typeInfo) {
		String val;
		MI_CUSTOM_C(item)->dumpValue(*this,val);
		buf << MI_CUSTOM_C(item)->displayType();
		buf.append(val,": ");
	    }
	    else
		MI_CUSTOM_C(item)->dumpValue(*this,buf);
	    break;
	default:
	    if (typeInfo)
		buf << "<UNKNOWN " << item->typeName() << '>';
    }
#ifdef TRACK_MI_DUMP
    Debug(s_debug,DebugAll,"dumpValueStr %s '%s'",dumpItemInfo(*item).safe(),buf.safe());
#endif
    return buf;
}

String& MatchingItemDump::dump(const MatchingItemBase* item, String& buf,
    const String& indent, const String& addIndent, unsigned int depth) const
{
    if (!item)
	return buf;
    if (!depth && 0 != (m_flags & DumpXmlStr)) {
	XmlElement* xml = static_cast<XmlElement*>(dumpXml(item,depth));
	if (!xml)
	    return buf;
	String str;
	xml->toString(str,false,indent,addIndent);
	return buf << str;
    }
#ifdef TRACK_MI_DUMP
    String tmpDbg;
    if (item->type() == MatchingItemBase::TypeList)
	tmpDbg.printf(" len=%u",MI_LIST_C(item)->length());
    Debugger dbg(s_debug,depth ? DebugInfo : DebugCall,"DUMP"," %s flags=0x%x depth=%u %s",
	dumpItemInfo(*item).safe(),m_flags,depth,tmpDbg.safe());
#endif
    bool dumpName = item->name() && 0 == (m_flags & IgnoreName);
    bool dumpId = item->id() && 0 != (m_flags & DumpItemId);
    String tmpBuf;
    String* b = buf ? &tmpBuf : &buf;
    if (item->type() == MatchingItemBase::TypeList) {
#define INSERT_STR(str) if (TelEngine::null(str)) TelEngine::destruct(str); else strs.insert(str)
	ObjList strs;
	const MatchingItemList* list = static_cast<const MatchingItemList*>(item);
	bool haveList = 0 != depth;
	for (unsigned int i = 0; !haveList && i < list->length() && !haveList; ++i)
	    haveList = static_cast<const MatchingItemList*>(list->at(i))->type() ==
		MatchingItemBase::TypeList;
	String flags;
	if (list->negated())
	    flags.append("negated",",");
	if (!list->matchAll())
	    flags.append("any",",");
	if (m_missingMatch) {
	    const char* s = lookup(item->missingMatch(),s_miMissingMatch);
	    if (s) {
		if (flags)
		    flags << ',';
		flags << '-' << + s;
	    }
	}
	String* pre = 0;
	if (haveList || flags || dumpName || dumpId ||
	    (0 == depth && 0 != (m_flags & ForceInitialListDesc))) {
	    pre = new String(indent);
	    *pre << (dumpName ? item->name().c_str() : "LIST");
	    if (flags)
		pre->printfAppend(" [%s]",flags.safe());
	    if (dumpId)
		*pre << indent << addIndent << s_id << ": " << item->id();
	}
	if (list->length()) {
	    String newIndent;
	    newIndent.assign(indent,indent.length(),pre ? addIndent.c_str() : 0);
	    for (int i = list->length() - 1; i >= 0; --i) {
		String* tmp = new String;
		dump(list->at(i),*tmp,newIndent,addIndent,depth + 1);
		INSERT_STR(tmp);
	    }
	}
	INSERT_STR(pre);
	b->append(strs);
#undef INSERT_STR
    }
    else if (item->type() == MatchingItemBase::TypeCustom && 0 != (m_flags & DumpCustomFull))
	MI_CUSTOM_C(item)->dumpFull(*this,*b,indent,addIndent,depth);
    else {
	dumpValue(item,*b);
	if (dumpName || *b) {
	    if (dumpName) {
		b->insert(0,m_nameValueSep.safe("="));
		b->insert(0,item->name(),item->name().length());
	    }
	    b->insert(0,indent,indent.length());
	}
	if (dumpId)
	    *b << indent << addIndent << s_id << ": " << item->id();
	if (item->type() == MatchingItemBase::TypeXPath) {
	    if (MI_XPATH_C(item)->match()) {
		String extra;
		String i2(indent + addIndent);
		dump(MI_XPATH_C(item)->match(),extra,i2,addIndent,depth);
		if (extra)
		    *b << i2 << "Match:" << extra;
	    }
	}
	else if (item->type() == MatchingItemBase::TypeCustom)
	    MI_CUSTOM_C(item)->dump(*this,*b,indent,addIndent,depth);
    }
#ifdef TRACK_MI_DUMP
    String tmp;
    Debug(s_debug,DebugAll,"Dumped %s [%p]\r\n-----\r\n%s\r\n-----",
	dumpItemInfo(*item).safe(),this,b->safe());
#endif
    return (b != &buf) ? (buf << *b) : buf;
}

XmlElement* MatchingItemDump::dumpXml(const MatchingItemBase* item, unsigned int depth) const
{
    if (!item)
	return 0;
#ifdef TRACK_MI_DUMP
    Debugger dbg(s_debug,depth ? DebugInfo : DebugCall,"DUMP XML"," %s flags=0x%x depth=%u",
	dumpItemInfo(*item).safe(),m_flags,depth);
#endif
    const char* tName = item->typeName();
    if (TelEngine::null(tName))
	return 0;
    XmlElement* xml = new XmlElement(tName);
    bool dumpEmpty = 0 != (m_flags & DumpIgnoreEmpty);
    unsigned int flags = buildFlags(item);
    if (dumpEmpty) {
	xml->setAttribute(s_name,item->name());
	xml->setAttribute(s_flags,flags,s_miFlags);
	xml->setAttribute(s_id,item->id());
    }
    else {
	xml->setAttributeValid(s_name,item->name());
	if (flags)
	    xml->setAttribute(s_flags,flags,s_miFlags);
	xml->setAttributeValid(s_id,item->id());
    }
    if (!dumpValueStr(item,xml->setText().text()))
	xml->clearText(true);
    switch (item->type()) {
	case MatchingItemBase::TypeList:
	    for (unsigned int i = 0; i < MI_LIST_C(item)->length(); ++i) {
		XmlElement* x = dumpXml(MI_LIST_C(item)->at(i),depth + 1);
		if (x)
		    xml->addChildSafe(x);
	    }
	    break;
	case MatchingItemBase::TypeXPath:
	    dumpXmlChild(xml,MI_XPATH_C(item)->match(),s_match,depth);
	    break;
	case MatchingItemBase::TypeCustom:
	    MI_CUSTOM_C(item)->dumpXml(*this,xml,depth);
	    break;
    }
#ifdef TRACK_MI_DUMP
    String tmp;
    Debug(s_debug,DebugAll,"dumpXml %s flags=0x%x depth=%u [%p]\r\n-----\r\n%s\r\n-----",
	dumpItemInfo(*item).safe(),m_flags,depth,this,xml->toString(tmp).safe());
#endif
    return xml;
};

XmlElement* MatchingItemDump::dumpXmlChild(XmlElement* parent, const MatchingItemBase* mi,
    const char* childTag, unsigned int depth) const
{
    XmlElement* xm = parent ? dumpXml(mi,depth) : 0;
    if (!xm)
	return 0;
    if (TelEngine::null(childTag))
	return static_cast<XmlElement*>(parent->addChildSafe(xm));
    XmlElement* x = new XmlElement(childTag);
    if (x->addChildSafe(xm))
	return parent->addChildSafe(x) ? xm : 0;
    TelEngine::destruct(x);
    return 0;
}

static inline void appendNs(ObjList*& add, ObjList*& first, NamedString* ns)
{
    add = add->append(ns);
    if (!first)
	first = add;
}

static inline void addNs(ObjList*& add, ObjList*& first, bool emptyOk, const String& val,
    const String& name, const String& prefix)
{
    if (emptyOk || val)
	appendNs(add,first,new NamedString(name,val,val.length(),prefix,name.length()));
}


unsigned int MatchingItemDump::dumpList(const MatchingItemBase* mi, NamedList& list,
    const char* prefix, unsigned int depth, const char* id) const
{
    if (!mi)
	return 0;

#if 0
    Debugger dbg(s_debug,DebugCall,"dumpList"," %s depth=%u prefix=%s id=%s",
	dumpItemInfo(*mi).safe(),depth,TelEngine::c_safe(prefix),TelEngine::c_safe(id));
#endif
    String pref(prefix);
    ObjList* add = list.paramList();
    ObjList* first = 0;
    const MatchingItemList* ml = mi->type() == MatchingItemBase::TypeList ? MI_LIST_C(mi) : 0;
    if (depth || !ml) {
	const char* type = mi->typeName();
	if (TelEngine::null(type))
	    return 0;
	String tmp(TelEngine::null(id) ? "0" : id);
	appendNs(add,first,new NamedString(prefix + s_item + tmp,type));
	pref << tmp << ":";
    }
    bool emptyOk = 0 == (m_flags & DumpIgnoreEmpty);
    addNs(add,first,emptyOk,mi->name(),s_name,pref);
    String val;
    addNs(add,first,emptyOk,dumpValueStr(mi,val),s_value,pref);
    unsigned int flags = buildFlags(mi);
    if (flags || emptyOk) {
	NamedString* ns = new NamedString(s_flags,0,-1,pref);
	ns->decodeFlags(flags,s_miFlags);
	appendNs(add,first,ns);
    }
    addNs(add,first,emptyOk,mi->id(),s_id,pref);
    if (ml) {
	unsigned int n = 0;
	for (unsigned int i = 0; i < ml->length(); ++i)
	    n += dumpList(ml->at(i),list,pref,depth + 1,String(i));
	if (!n)
	    first->clear();
    }
    else if (mi->type() == MatchingItemBase::TypeXPath) {
	if (MI_XPATH_C(mi)->match())
	    dumpList(MI_XPATH_C(mi)->match(),list,pref + s_match + ":",depth);
    }
    else if (mi->type() == MatchingItemBase::TypeCustom)
	MI_CUSTOM_C(mi)->dumpList(*this,list,pref,depth,id);
    return (first && first->skipNull()) ? 1 : 0;
}

const TokenDict* MatchingItemDump::flagsDict()
{
    return s_miDumpFlags;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
