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

#include "yateclass.h"
#include "yatexml.h"

using namespace TelEngine;

static const String s_name = "name";
static const String s_flags = "flags";

enum MatchingItemType
{
    MatchingItemTypeUnknown = 0,
    MatchingItemTypeString,
    MatchingItemTypeRegexp,
    MatchingItemTypeRandom,
    MatchingItemTypeList,
    MatchingItemTypeCustom,
};
static const TokenDict64 s_miLoadType[] = {
    {"string", MatchingItemTypeString},
    {"regexp", MatchingItemTypeRegexp},
    {"random", MatchingItemTypeRandom},
    {"list",   MatchingItemTypeList},
    {"custom", MatchingItemTypeCustom},
    {0,0}
};

static inline const char* miTypeName(const MatchingItemBase* item)
{
    if (!item)
	return "";
    if (item->itemList())
	return "list";
    if (item->itemString())
	return "string";
    if (item->itemRegexp())
	return "regexp";
    if (item->itemRandom())
	return "random";
    if (item->itemCustom())
	return item->itemCustom()->type().safe("custom");
    return "unknown";
}

static inline int miType(const MatchingItemBase* item)
{
    if (!item)
	return 0;
    if (item->itemList())
	return MatchingItemTypeList;
    if (item->itemString())
	return MatchingItemTypeString;
    if (item->itemRegexp())
	return MatchingItemTypeRegexp;
    if (item->itemRandom())
	return MatchingItemTypeRandom;
    if (item->itemCustom())
	return MatchingItemTypeCustom;
    return 0;
}

static const TokenDict s_miItemFlags[] = {
    {"negated", MatchingItemLoad::ItemNegated},
    {"caseinsensitive", MatchingItemLoad::ItemCaseInsensitive},
    {"basic", MatchingItemLoad::ItemBasic},
    {"any", MatchingItemLoad::ItemAny},
    {0,0}
};


//
// MatchingItemDump
//
static inline void addFlags(String& buf, const String& flags)
{
    if (flags)
	buf << '[' << flags << "] ";
}

static const TokenDict s_miDumpFlags[] = {
    {"no_initial_list_desc", MatchingItemDump::NoInitialListDesc},
    {"dump_xml", MatchingItemDump::DumpXmlStr},
    {0,0}
};

static inline String& dumpRandom(String& buf, const MatchingItemRandom& mi)
{
    buf << mi.value();
    if (mi.maxValue() == 100)
	return buf << '%';
    return buf << '/' << mi.maxValue();
}

static inline String dumpIndent(const String& buf)
{
    String s;
    for (const char* ss = buf; (ss && *ss); ++ss)
	if (*ss == '\r')
	    s << "\\r";
	else if (*ss == '\n')
	    s << "\\n";
	else
	    s << *ss;
    return s;
}

void MatchingItemDump::init(const NamedList& params)
{
    for (ObjList* o = params.paramList()->skipNull(); o; o = o->skipNext()) {
	NamedString* ns = static_cast<NamedString*>(o->get());
	if (ns->name() == YSTRING("flags"))
	    m_flags = ns->encodeFlags(s_miDumpFlags);
	else if (ns->name() == YSTRING("rex_enclose"))
	    m_rexEnclose = (*ns)[0];
	else if (ns->name() == YSTRING("str_enclose"))
	    m_strEnclose = (*ns)[0];
	else if (ns->name() == YSTRING("name_value_sep"))
	    m_nameValueSep = *ns;
	else if (ns->name() == YSTRING("prop_negated"))
	    m_negated = (*ns)[0];
	else if (ns->name() == YSTRING("prop_caseinsensitive"))
	    m_caseInsentive = (*ns)[0];
	else if (ns->name() == YSTRING("prop_rex_basic"))
	    m_regexpBasic = (*ns)[0];
	else if (ns->name() == YSTRING("prop_rex_extended"))
	    m_regexpExtended = (*ns)[0];
    }
}

String& MatchingItemDump::dumpValue(const MatchingItemBase* item, String& buf,
    const String& indent, const String& origIndent, unsigned int depth) const
{
    if (!item)
	return buf;
    String tmp;
    // Done if already dumped (item implements dumpValue())
    if (item->dumpValue(tmp,this,indent,origIndent,depth))
	return buf << tmp;
    XDebug("MatchingItemDump",DebugAll,
	"dumpValue (%p) %s '%s' depth=%u indent='%s'/'%s' [%p]",
	item,miTypeName(item),item->name().safe(),depth,dumpIndent(indent).safe(),
	dumpIndent(origIndent).safe(),this);
    if (item->itemList()) {
	for (unsigned int i = 0; i < item->itemList()->length(); ++i) {
	    String tmp;
	    buf << dump(item->itemList()->at(i),tmp,indent,origIndent,depth);
	}
    }
    else {
	const MatchingItemString* str = item->itemString();
	const MatchingItemRegexp* rex = str ? 0 : item->itemRegexp();
	String flags;
	if (item->negated())
	    flags << m_negated;
	if (str) {
	    if (str->caseInsensitive())
		flags << m_caseInsentive;
	    addFlags(buf,flags);
	    buf << m_strEnclose << item->itemString()->value() << m_strEnclose;
	}
	else if (rex) {
	    if (rex->value().isCaseInsensitive())
		flags << m_caseInsentive;
	    if (rex->value().isExtended())
		flags << m_regexpExtended;
	    else
		flags << m_regexpBasic;
	    addFlags(buf,flags);
	    buf << m_rexEnclose << item->itemRegexp()->value() << m_rexEnclose;
	}
	else {
	    addFlags(buf,flags);
	    if (item->itemRandom()) {
		buf << "RANDOM ";
		dumpRandom(buf,*(item->itemRandom()));
	    }
	    else if (item->itemCustom())
		buf << "<CUSTOM " << item->itemCustom()->type() << '>';
	    else
		buf << "<UNKNOWN>";
	}
    }
    XDebug("MatchingItemDump",DebugAll,"Dumped value (%p) '%s' [%p]\r\n-----\r\n%s\r\n-----",
	item,item->name().safe(),this,buf.safe());
    return buf;
}

String& MatchingItemDump::dump(const MatchingItemBase* item, String& buf,
    const String& indent, const String& origIndent, unsigned int depth) const
{
    if (!item)
	return buf;

    XDebug("MatchingItemDump",DebugAll,
	"dump (%p) %s '%s' flags=0x%x depth=%u indent='%s'/'%s' [%p]",
	item,miTypeName(item),item->name().safe(),m_flags,depth,
	dumpIndent(indent).safe(),dumpIndent(origIndent).safe(),this);
    while (true) {
	if (!depth && 0 != (m_flags & DumpXmlStr)) {
	    XmlElement* xml = static_cast<XmlElement*>(dumpXml(item,depth));
	    if (xml) {
		xml->toString(buf,false,indent,origIndent);
		TelEngine::destruct(xml);
	    }
	    break;
	}
	unsigned int oLen = buf.length();
	item->dump(buf,this,indent,origIndent,depth);
	// Done if already dumped (item implements dump())
	if (oLen != buf.length())
	    break;

	const MatchingItemList* list = item->itemList();
	if (list) {
	    String tmp;
	    if (depth || 0 == (m_flags & NoInitialListDesc)) {
		String flags;
		if (list->negated())
		    flags.append("negated",",");
		if (!list->matchAll())
		    flags.append("any",",");
		if (flags)
		    flags.printf(" [%s]",flags.safe());
		if (depth || flags || item->name())
		    tmp << item->name().safe("List") << ':' << flags;
	    }
	    // Nothing dumped at first level:
	    // print the rest of the list at same alignment
	    String newIndent = indent;
	    if (tmp) {
		buf << indent << tmp;
		newIndent += origIndent;
	    }
	    for (unsigned int i = 0; i < list->length(); ++i) {
		tmp.clear();
		buf << dump(list->at(i),tmp,newIndent,origIndent,depth + 1);
	    }
	}
	else {
	    String val;
	    dumpValue(item,val);
	    if (item->name() || val) {
		buf << indent;
		if (item->name())
		    buf << item->name() << m_nameValueSep.safe("=");
		buf << val;
	    }
	}
	break;
    }
    XDebug("MatchingItemDump",DebugAll,"Dumped (%p) '%s' [%p]\r\n-----\r\n%s\r\n-----",
	item,item->name().safe(),this,buf.safe());
    return buf;
}

GenObject* MatchingItemDump::dumpXml(const MatchingItemBase* item, unsigned int depth) const
{
    if (!item)
	return 0;

    XDebug("MatchingItemDump",DebugAll,"dumpXml (%p) %s '%s' flags=0x%x depth=%u [%p]",
	item,miTypeName(item),item->name().safe(),m_flags,depth,this);
    XmlElement* xml = 0;
    while (true) {
	xml = static_cast<XmlElement*>(item->dumpXml(this,depth));
	// Done if already dumped (item implements dumpXml())
	if (xml)
	    break;

	xml = new XmlElement(miTypeName(item));
	xml->setAttribute(s_name,item->name());
	unsigned int flags = 0;
	if (item->negated())
	    flags |= MatchingItemLoad::ItemNegated;
	if (item->itemList()) {
	    const MatchingItemList* list = item->itemList();
	    if (!list->matchAll())
		flags |= MatchingItemLoad::ItemAny;
	    for (unsigned int i = 0; i < list->length(); ++i) {
		XmlElement* ch = static_cast<XmlElement*>(dumpXml(list->at(i),depth + 1));
		xml->addChildSafe(ch);
	    }
	}
	else if (item->itemString()) {
	    const MatchingItemString* str = item->itemString();
	    if (str->caseInsensitive())
		flags |= MatchingItemLoad::ItemCaseInsensitive;
	    if (str->value())
		xml->setText(str->value());
	}
	else if (item->itemRegexp()) {
	    const MatchingItemRegexp* rex = item->itemRegexp();
	    if (rex->value().isCaseInsensitive())
		flags |= MatchingItemLoad::ItemCaseInsensitive;
	    if (!rex->value().isExtended())
		flags |= MatchingItemLoad::ItemBasic;
	    if (rex->value().c_str())
		xml->setText(rex->value().c_str());
	}
	String tmp;
	xml->setAttribute(s_flags,tmp.decodeFlags(flags,s_miItemFlags));
	if (item->itemRandom()) {
	    tmp.clear();
	    xml->setText(dumpRandom(tmp,*(item->itemRandom())));
	}
	break;
    }
#ifdef XDEBUG
    String tmp;
    xml->toString(tmp);
    Debug("MatchingItemDump",DebugAll,"dumpXml (%p) %s '%s' flags=0x%x depth=%u [%p]\r\n-----\r\n%s\r\n-----",
	item,miTypeName(item),item->name().safe(),m_flags,depth,this,tmp.safe());
#endif
    return xml;
};


//
// MatchingItemLoad
//
static const TokenDict64 s_miLoadFlags[] = {
    {"regexp_basic", MatchingItemLoad::RexBasic},
    {"regexp_detect", MatchingItemLoad::RexDetect},
    {"regexp_detect_negated", MatchingItemLoad::RexDetectNegated},
    {"regexp_validate", MatchingItemLoad::RexValidate},
    {"name_required_simple", MatchingItemLoad::NameReqSimple},
    {"ignore_failed", MatchingItemLoad::IgnoreFailed},
    {"list_any", MatchingItemLoad::ListAny},
    {0,0}
};

static MatchingItemBase* miLoadRex(const MatchingItemLoad& l, String* error,
    const String& name, const String& val, const String& flagsStr, unsigned int flags,
    const char* loc, bool* fatal = 0)
{
    int rexNeg = 0;
    bool extended = 0 == (l.m_flags & MatchingItemLoad::RexBasic);
    if (flagsStr) {
	rexNeg = (0 != (flags & MatchingItemLoad::ItemNegated)) ? 1 : 0;
	extended = 0 == (flags & MatchingItemLoad::ItemBasic);
    }
    else
	rexNeg = (0 != (l.m_flags & MatchingItemLoad::RexDetectNegated)) ? -1 : 0;
    MatchingItemRegexp* mi = MatchingItemRegexp::build(name,val,rexNeg,
	0 != (flags & MatchingItemLoad::ItemCaseInsensitive),
	extended,
	(l.m_flags & MatchingItemLoad::RexValidate) ? (val ? 1 : -1) : 0);
    if (mi)
	return mi;
    if (0 == (l.m_flags & MatchingItemLoad::IgnoreFailed)) {
	if (error)
	    error->printf("invalid regexp '%s'='%s'",name.safe(),val.safe());
	return 0;
    }
    if (fatal)
	*fatal = true;
    if (l.m_dbg)
	Debug(l.m_dbg,DebugInfo,"Invalid matching regexp '%s'='%s' in '%s'",
	    name.safe(),val.safe(),TelEngine::c_safe(loc));
    return 0;
}

static MatchingItemBase* miLoadRandom(const MatchingItemLoad& l, String* error,
    const String& name, const String& val, unsigned int flags,
    const char* loc, bool* fatal = 0)
{
    int64_t v = 0;
    int64_t maxV = 100;
    if (val) {
	// Value: val[/maxVal] or [0..100]%
	if (val[val.length() - 1] == '%') {
	    v = val.substr(0,val.length() - 1).toInt64(-1);
	    if ( v > 100)
		v = -1;
	}
	else {
	    int pos = val.find('/');
	    if (pos > 0) {
		v = val.substr(0,pos).toInt64(-1);
		maxV = val.substr(pos + 1).toInt64(-1);
	    }
	    else
		v = val.toInt64(-1);
	}
    }
    if (v >= 0 && v <= 0xffffffff && maxV >= 0 && maxV <= 0xffffffff)
	return new MatchingItemRandom(v,maxV,0 != (flags & MatchingItemLoad::ItemNegated),name);
    if (0 == (l.m_flags & MatchingItemLoad::IgnoreFailed)) {
	if (error)
	    error->printf("invalid random '%s'='%s'",name.safe(),val.safe());
	return 0;
    }
    if (fatal)
	*fatal = true;
    if (l.m_dbg)
	Debug(l.m_dbg,DebugInfo,"Invalid matching random '%s'='%s' in '%s'",
	    name.safe(),val.safe(),TelEngine::c_safe(loc));
    return 0;
}

MatchingItemBase* MatchingItemLoad::load(const NamedList& params, String* error,
    const char* prefix, const char* suffix) const
{
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
	    if (ns->name().startsWith(prefXml)) {
		String e;
		mi = loadXml(*ns,&e);
		if (!mi) {
		    if (!e)
			continue;
		    if (error)
			*error = e;
		    return 0;
		}
	    }
	    else
		continue;
	}
	else {
	    if (ns->name().length() == prefMatch.length())
		continue;
	    String name(ns->name().c_str() + prefMatch.length());
	    if (ignore(name))
		continue;
	    int type = 0;
	    const String& tName = params[prefType + name];
	    if (tName)
		type = lookup(tName,s_miLoadType);
	    else if (flagSet(RexDetect) && (*ns)[0] == '^')
		type = MatchingItemTypeRegexp;
	    else
		type = MatchingItemTypeString;
	    const String* flagsStr = params.getParam(prefFlags + name);
	    unsigned int flags = flagsStr ? flagsStr->encodeFlags(s_miItemFlags) : 0;
	    switch (type) {
		case MatchingItemTypeString:
		    mi = new MatchingItemString(name,*ns,0 != (flags & ItemCaseInsensitive),
			0 != (flags & ItemNegated));
		    break;
		case MatchingItemTypeRegexp:
		    fatal = false;
		    mi = miLoadRex(*this,error,name,*ns,flagsStr,flags,params,&fatal);
		    if (!mi && fatal)
			return 0;
		    break;
		case MatchingItemTypeRandom:
		    fatal = false;
		    mi = miLoadRandom(*this,error,name,*ns,flags,params,&fatal);
		    if (!mi && fatal)
			return 0;
		    break;
		default:
		    if (m_dbg)
			Debug(m_dbg,DebugInfo,"Unknown matching type '%s' in '%s'",
			    tName.c_str(),params.safe());
		    continue;
	    }
	}
	if (mi)
	    add = add->append(mi);
    }
    ObjList* first = items.skipNull();
    if (!first)
	return 0;
    if (!first->skipNext())
	return static_cast<MatchingItemBase*>(first->remove(false));
    const String* tmp = params.getParam(listFlags);
    unsigned int flags = tmp ? tmp->encodeFlags(s_miItemFlags) : 0;
    bool any = tmp ? (0 != (flags & ItemAny)) : flagSet(ListAny);
    MatchingItemList* list = new MatchingItemList("",!any,0 != (flags & ItemNegated));
    list->append(items);
    return MatchingItemList::optimize(list);
}

MatchingItemBase* MatchingItemLoad::loadXml(const String& str, String* error) const
{
    XDebug("MatchingItemLoad",DebugAll,"loadXml(%s,%p)",str.safe(),error);
    if (!str)
	return 0;
    XmlDomParser parser("MatchingItemLoad",true);
    parser.parse(str);
    XmlElement* xml = parser.fragment()->popElement();
    if (!xml) {
	if (error && !flagSet(IgnoreFailed))
	    error->printf("invalid xml error='%s'",parser.getError());
	return 0;
    }
    String e;
    ObjList items;
    ObjList* add = &items;
    while (xml) {
	MatchingItemBase* mi = loadXml(xml,&e);
	TelEngine::destruct(xml);
	if (mi)
	    add = add->append(mi);
	else if (e) {
	    if (error)
		*error = e;
	    return 0;
	}
	xml = parser.fragment()->popElement();
    }
    if (!items.skipNull())
	return 0;
    MatchingItemList* list = new MatchingItemList("");
    list->append(items);
    return MatchingItemList::optimize(list);
};

MatchingItemBase* MatchingItemLoad::loadXml(const GenObject* gen, String* error) const
{
    XmlElement* xml = YOBJECT(XmlElement,gen);
    XDebug("MatchingItemLoad",DebugAll,"loadXml(%p,%p) xml=(%p)",gen,error,xml);
    if (!xml)
	return 0;
    const NamedList& attrs = xml->attributes();
    const String& name = attrs[s_name];
    if (name && ignore(name))
	return 0;
    const String* flagsStr = attrs.getParam(s_flags);
    unsigned int flags = flagsStr ? flagsStr->encodeFlags(s_miItemFlags) : 0;
    int type = lookup(xml->getTag(),s_miLoadType);
    switch (type) {
	case MatchingItemTypeString:
	    if (!name && 0 != emptyNameAllow(error))
		return 0;
	    return new MatchingItemString(name,xml->getText(),
		0 != (flags & ItemCaseInsensitive),0 != (flags & ItemNegated));
	case MatchingItemTypeRegexp:
	    if (!name && 0 != emptyNameAllow(error))
		return 0;
	    return miLoadRex(*this,error,name,xml->getText(),flagsStr,flags,"xml");
	case MatchingItemTypeRandom:
	    return miLoadRandom(*this,error,name,xml->getText(),flags,"xml");
	case MatchingItemTypeList:
	    break;
	default:
	    return 0;
    }
    ObjList* chs = xml->getChildren().skipNull();
    String e;
    ObjList items;
    ObjList* add = &items;
    for (XmlElement* x = 0; 0 != (x = XmlFragment::getElement(chs)); ) {
	MatchingItemBase* mi = loadXml(x,&e);
	if (mi)
	    add = add->append(mi);
	else if (e) {
	    if (error)
		*error = e;
	    return 0;
	}
    }
    if (!items.skipNull())
	return 0;
    MatchingItemList* list = new MatchingItemList(name,0 == (flags & ItemAny),
	0 != (flags & ItemNegated));
    list->append(items);
    return MatchingItemList::optimize(list);
}

const TokenDict64* MatchingItemLoad::loadFlags()
{
    return s_miLoadFlags;
}

const TokenDict* itemFlags()
{
    return s_miItemFlags;
}


//
// MatchingItemRegexp
//
MatchingItemRegexp* MatchingItemRegexp::build(const char* name, const String& str,
    int negated, bool insensitive, bool extended, int fail)
{
    Regexp rex(0,extended,insensitive);
    if (str) {
	if (negated >= 0)
	    rex.assign(str);
	else {
	    unsigned int pos = str.length() - 1;
	    negated = (str[pos] == '^') ? 1 : 0;
	    if (negated)
		rex.assign(str.substr(0,pos));
	    else
		rex.assign(str);
	}
    }
    else if (negated < 0)
	negated = 0;
    if (fail > 1) {
	if (!rex.compile())
	    return 0;
    }
    else if (fail < 0 && !rex.c_str())
	return 0;
    return new MatchingItemRegexp(name,rex,negated);
}


//
// MatchingItemList
//
bool MatchingItemList::change(MatchingItemBase* item, int pos, bool ins, unsigned int overAlloc)
{
    if (!item) {
	unsigned int n = count();
	if (ins || pos < 0 || pos >= (int)n)
	    return false;
	// Remove
	GenObject* gen = m_value.take(pos);
	if (gen) {
	    for (; pos < (int)n; ++pos)
		m_value.set(m_value.take(pos + 1),pos);
	    TelEngine::destruct(gen);
	}
	return true;
    }
    // Detect first free position
    unsigned int firstFree = 0;
    while (m_value.at(firstFree))
	firstFree++;
    if (firstFree >= m_value.length()) {
	if (firstFree >= m_value.resize(m_value.length() + 1 + overAlloc,true)) {
	    TelEngine::destruct(item);
	    return false;
	}
    }
    if (pos < 0 || pos >= (int)firstFree)
	pos = firstFree;
    else if (ins) {
	for (; (int)firstFree > pos; --firstFree)
	    m_value.set(m_value.take(firstFree - 1),firstFree);
    }
    m_value.set(item,pos);
    return true;
}

MatchingItemBase* MatchingItemList::copy() const
{
    MatchingItemList* lst = new MatchingItemList(name(),matchAll(),negated());
    if (length()) {
	unsigned int overAlloc = length() - 1;
	for (unsigned int i = 0; i < length(); ++i) {
	    const MatchingItemBase* it = at(i);
	    MatchingItemBase* item = it ? it->copy() : 0;
	    if (item) {
		lst->append(item,overAlloc);
		overAlloc = 0;
	    }
	}
    }
    return lst;
}

static inline bool matchingListRun(const MatchingItemList& mil, MatchingParams* params,
    const NamedList* list, const String& str = String::empty())
{
    bool allMatch = mil.matchAll();
#ifdef XDEBUG
    Debugger dbg(DebugAll,"MatchingItemList MATCHING",
	" name='%s' all=%s negated=%s input='%s' params=(%p) [%p]",
	mil.name().safe(),String::boolText(allMatch),String::boolText(mil.negated()),
	list ? list->safe("list") : "<string>",params,&mil);
#endif
    int pos = -1;
    while (true) {
	const MatchingItemBase* item = const_cast<MatchingItemBase*>(mil.at(++pos));
	if (!item)
	    break;
	bool ok = list ? item->matchListParam(*list,params) : item->matchString(str,params);
	XDebug("MatchingItemList",DebugAll,"matched [%s] idx=%d (%p) '%s' type=%s [%p]",
	    String::boolText(ok),pos,item,item->name().safe(),miTypeName(item),&mil);
	// Matched: done if not all match (any match)
	// Not matched: done if all match is required
	if (ok) {
	    if (!allMatch)
		return true;
	}
	else if (allMatch)
	    return false;
    }
    // End of list reached
    // Empty list or match any: not matched
    // Otherwise: matched
    XDebug("MatchingItemList",DebugAll,"End of matching [%s] pos=%d [%p]",
	String::boolText(pos && allMatch),pos,&mil);
    return pos && allMatch;
}

bool MatchingItemList::runMatchString(const String& str, MatchingParams* params) const
{
    return matchingListRun(*this,params,0,str);
}

bool MatchingItemList::runMatchListParam(const NamedList& list, MatchingParams* params) const
{
    return matchingListRun(*this,params,&list);
}

MatchingItemBase* MatchingItemList::optimize(MatchingItemList* list)
{
    if (!list || list->at(1))
	return list;
    MatchingItemBase* ret = static_cast<MatchingItemBase*>(list->m_value.take(0));
    if (ret) {
	// Reverse item (not)negated flag if list is negated to keep the same matching behaviour
	if (list->negated())
	    ret->m_notNegated = !ret->m_notNegated;
    }
    TelEngine::destruct(list);
    return ret;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
