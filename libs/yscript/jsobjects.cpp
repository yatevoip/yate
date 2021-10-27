/**
 * jsobject.cpp
 * Yet Another (Java)script library
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2014 Null Team
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

#include "yatescript.h"
#include <string.h>

using namespace TelEngine;

namespace { // anonymous

// Object object
class JsObjectObj : public JsObject
{
    YCLASS(JsObjectObj,JsObject)
public:
    inline JsObjectObj(ScriptMutex* mtx)
	: JsObject("Object",mtx,true)
	{
	}
    virtual void initConstructor(JsFunction* construct)
	{
	    construct->params().addParam(new ExpFunction("keys"));
	    construct->params().addParam(new ExpFunction("global"));
	}
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

// Date object
class JsDate : public JsObject
{
    YCLASS(JsDate,JsObject)
public:
    inline JsDate(ScriptMutex* mtx)
	: JsObject("Date",mtx,true),
	  m_time(0), m_msec(0), m_offs(0)
	{
	    params().addParam(new ExpFunction("getDate"));
	    params().addParam(new ExpFunction("getDay"));
	    params().addParam(new ExpFunction("getFullYear"));
	    params().addParam(new ExpFunction("getHours"));
	    params().addParam(new ExpFunction("getMilliseconds"));
	    params().addParam(new ExpFunction("getMinutes"));
	    params().addParam(new ExpFunction("getMonth"));
	    params().addParam(new ExpFunction("getSeconds"));
	    params().addParam(new ExpFunction("getTime"));
	    params().addParam(new ExpFunction("getTimezoneOffset"));

	    params().addParam(new ExpFunction("getUTCDate"));
	    params().addParam(new ExpFunction("getUTCDay"));
	    params().addParam(new ExpFunction("getUTCFullYear"));
	    params().addParam(new ExpFunction("getUTCHours"));
	    params().addParam(new ExpFunction("getUTCMilliseconds"));
	    params().addParam(new ExpFunction("getUTCMinutes"));
	    params().addParam(new ExpFunction("getUTCMonth"));
	    params().addParam(new ExpFunction("getUTCSeconds"));

	    params().addParam(new ExpFunction("toJSON"));
	}
    virtual void initConstructor(JsFunction* construct)
	{
	    construct->params().addParam(new ExpFunction("now"));
	    construct->params().addParam(new ExpFunction("UTC"));
	}
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual const String& toString() const {
	    if (!m_str)
		Time::appendTo(m_str,(uint64_t)m_time * 1000000 + (uint64_t)m_msec * 1000,1);
	    return m_str;
	}

protected:
    inline JsDate(ScriptMutex* mtx, unsigned int lineNo, u_int64_t msecs, bool local = false)
	: JsObject(mtx,"[object Date]",lineNo),
	  m_time((unsigned int)(msecs / 1000)), m_msec((unsigned int)(msecs % 1000)), m_offs(Time::timeZone(m_time))
	{ if (local) m_time -= m_offs; }
    inline JsDate(ScriptMutex* mtx, const char* name, unsigned int line, unsigned int time, unsigned int msec, unsigned int offs)
	: JsObject(mtx,name,line),
	  m_time(time), m_msec(msec), m_offs(offs)
	{ }
    virtual JsObject* clone(const char* name, const ExpOperation& oper) const
	{ return new JsDate(mutex(),name,oper.lineNumber(),m_time,m_msec,m_offs); }
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    unsigned int m_time;
    unsigned int m_msec;
    int m_offs;
    mutable String m_str;
};

// Math class - not really an object, all methods are static
class JsMath : public JsObject
{
    YCLASS(JsMath,JsObject)
public:
    inline JsMath(ScriptMutex* mtx)
	: JsObject("Math",mtx,true)
	{
	    params().addParam(new ExpFunction("abs"));
	    params().addParam(new ExpFunction("max"));
	    params().addParam(new ExpFunction("min"));
	    params().addParam(new ExpFunction("random"));
	}
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

class RecursiveTraceItem : public String
{
public:
    inline RecursiveTraceItem(const GenObject* obj, const char* path = 0)
	: String(path), m_traced((GenObject*)obj), m_jpath(0)
	{}
    ~RecursiveTraceItem()
	{ setJPath(0); }
    inline const GenObject* traced() const
	{ return m_traced; }
    inline GenObject* traced()
	{ return m_traced; }
    inline GenObject* getJPath() const
	{ return m_jpath; }
    inline void setJPath(GenObject* obj) {
	    if (obj == m_jpath)
		return;
	    TelEngine::destruct(m_jpath);
	    m_jpath = obj;
	}

protected:
    GenObject* m_traced;
    GenObject* m_jpath;
};

#ifdef XDEBUG
#define JS_DEBUG_RECURSIVE_TRACE
#else
//#define JS_DEBUG_RECURSIVE_TRACE
#endif

#ifdef XDEBUG
#define JS_DEBUG_REPLACE_REFERENCES
#else
//#define JS_DEBUG_REPLACE_REFERENCES
#endif

class RecursiveTrace
{
public:
    inline RecursiveTrace(bool toJSON, JsObject* rootJS, const GenObject* root)
	: m_json(toJSON), m_root(root), m_rootJS(rootJS) {
	    m_append = &m_trace;
#ifdef JS_DEBUG_RECURSIVE_TRACE
	    Debug(DebugNote,"Start%s tracing root %p (%p)",m_json ? " JSON" : "",root,rootJS);
#endif
	}
    inline bool isRoot(const GenObject* gen) const
	{ return m_root == gen; }
    inline RecursiveTraceItem* find(const GenObject* gen) const {
	    if (gen)
		for (ObjList* o = m_trace.skipNull(); o; o = o->skipNext()) {
		    RecursiveTraceItem* it = static_cast<RecursiveTraceItem*>(o->get());
		    if (it->traced() == gen) {
#ifdef JS_DEBUG_RECURSIVE_TRACE
			Debug(DebugNote,"Found traced %p path='%s'",gen,it->safe());
#endif
			return it;

		    }
		}
	    return 0;
	}
    inline RecursiveTraceItem* findPath(const String& path) const {
	    GenObject* gen = m_trace[path];
	    if (!gen)
		return 0;
	    RecursiveTraceItem* it = static_cast<RecursiveTraceItem*>(gen);
#ifdef JS_DEBUG_RECURSIVE_TRACE
	    Debug(DebugNote,"Found traced %p path='%s'",gen,it->safe());
#endif
	    return it;
	}
    inline void trace(const GenObject* obj, const String& path = String::empty()) {
	    if (!obj)
		return;
	    debugTrace(false,obj,path);
	    addTrace(obj,isRoot(obj),path);
	}
    inline void traceJsObj(const JsObject* obj, const String& path = String::empty()) {
	    if (!obj)
		return;
	    debugTrace(true,obj,path);
	    addTrace(obj,m_rootJS == obj,path);
	}

protected:
    inline void addTrace(const GenObject* obj, bool root, const String& path) {
	    if (root)
		m_append = m_append->append(new RecursiveTraceItem(obj,"#"));
	    else if (path)
		m_append = m_append->append(new RecursiveTraceItem(obj,"#" + path));
	    else
		m_append = m_append->append(new RecursiveTraceItem(obj,"#/"));
	}
    inline void debugTrace(bool js, const void* ptr, const String& path = String::empty()) {
#ifdef JS_DEBUG_RECURSIVE_TRACE
	    if ((js && m_rootJS == ptr) || (!js && ptr == m_root))
		Debug(DebugNote,"Tracing%s root %p",(js ? " JS" : ""),ptr);
	    else
		Debug(DebugNote,"Tracing%s %p path='%s'",(js ? " JS" : ""),ptr,path.safe());
#endif
	}

    bool m_json;
    const GenObject* m_root;
    JsObject* m_rootJS;
    ObjList m_trace;
    ObjList* m_append;
};
#undef JS_DEBUG_RECURSIVE_TRACE

}; // anonymous namespace


// Helper function that does the actual object printing
static void dumpRecursiveObj(const GenObject* obj, String& buf, unsigned int depth, RecursiveTrace& seen,
    unsigned int flags, const String& path)
{
    if (!obj)
	return;
    if (depth > 1 && !(flags & JsObject::DumpRecursive))
	return;
    // Check if we have something to dump
    unsigned int dump = flags & (JsObject::DumpFunc | JsObject::DumpProp);
    if (!dump)
	return;
    String str(' ',2 * depth);
    RecursiveTraceItem* objRecursed = seen.find(obj);
    if (objRecursed) {
	str << "(recursivity encountered)";
	str.append(*objRecursed," ");
	buf.append(str,"\r\n");
	return;
    }
    const NamedString* nstr = YOBJECT(NamedString,obj);
    // Check for prototype dump (always dump the first level if original object is a prototype)
    bool isProto = nstr && nstr->name() == JsObject::protoName();
    if (depth && isProto && !(flags & JsObject::DumpProto))
	return;
    const NamedPointer* nptr = YOBJECT(NamedPointer,nstr);
    const char* type = nstr ? (nptr ? "NamedPointer" : "NamedString") : "???";
    const char* subType = 0;
    const ScriptContext* scr = YOBJECT(ScriptContext,obj);
    const ExpWrapper* wrap = 0;
    bool isFunc = false;
    String nextPath = path;
    if (!seen.isRoot(obj)) {
	if (nstr)
	    JPath::addItem(nextPath,nstr->name().safe());
	else
	    JPath::addItem(nextPath,"");
    }
    if (scr) {
	const JsObject* jso = YOBJECT(JsObject,scr);
	if (jso) {
	    objRecursed = seen.find(jso);
	    if ((jso != obj) && !objRecursed)
		seen.traceJsObj(jso,nextPath);
	    if (YOBJECT(JsArray,scr))
		type = "JsArray";
	    else if (YOBJECT(JsFunction,scr)) {
		type = "JsFunction";
		isFunc = true;
	    }
	    else if (YOBJECT(JsRegExp,scr))
		type = "JsRegExp";
	    else if (YOBJECT(JsDate,scr))
		type = "JsDate";
	    else if (YOBJECT(JsJPath,scr))
		type = "JsJPath";
	    else
		type = "JsObject";
	}
	else
	    type = "ScriptContext";
    }
    seen.trace(obj,nextPath);
    const ExpOperation* exp = YOBJECT(ExpOperation,nstr);
    if (exp && !scr) {
	if ((wrap = YOBJECT(ExpWrapper,exp)))
	    type = wrap->object() ? "ExpWrapper" : "Undefined";
	else if (YOBJECT(ExpFunction,exp)) {
	    type = "ExpFunction";
	    isFunc = true;
	}
	else {
	    type = "ExpOperation";
	    subType = exp->typeOf();
	}
    }
    // Check for func/prop dump (don't do it if we are printing a prototype)
    if (depth && !isProto && ((isFunc && (0 == (flags & JsObject::DumpFunc))) ||
	(!isFunc && (0 == (flags & JsObject::DumpProp)))))
	return;
    bool dumpType = 0 != (flags & JsObject::DumpType);
    if (nstr) {
	str << "'" << nstr->name() << "'";
	// Nicely dump property value if dumping props only and type is not shown 
	if ((dump == JsObject::DumpProp) && !isProto && !dumpType) {
	    if (scr) {
		if (exp && JsParser::isNull(*exp))
		    str << " = null";
		else if (YOBJECT(JsRegExp,scr))
		    str << " = /" << *nstr << "/";
		else if (YOBJECT(JsJPath,scr))
		    str << " = '" << *nstr << "'";
		else if (flags & JsObject::DumpPropObjType) {
		    if (YOBJECT(JsObject,scr))
			str << " = " << *nstr;
		    else
			str << " = [ScriptContext]";
		}
	    }
	    else if (exp) {
		if (JsParser::isUndefined(*exp))
		    str << " = undefined";
		else if (exp->isInteger()) {
		    if (exp->isBoolean())
			str << " = " << exp->valBoolean();
		    else
			str << " = " << exp->number();
		}
		else if (exp->isNumber()) // NaN
		    str << " = " << *nstr;
		else // string
		    str << " = '" << *nstr << "'";
	    }
	    else
		str << " = '" << *nstr << "'";
	}
	else
	    str << " = '" << *nstr << "'";
    }
    else
	str << "'" << obj->toString() << "'";
    if (dumpType)
	str << " (" << type << (subType ? ", " : "") << subType << ")";
    if (objRecursed) {
	str << " (already seen)";
	//String tmpP; str << tmpP.printf(" (%p)",objRecursed->traced());
	str.append(*objRecursed," ");
    }
    buf.append(str,"\r\n");
    if (objRecursed)
	return;
    const JsObject* jso = YOBJECT(JsObject,obj);
    if (!(scr || wrap || nptr || jso))
	return;
    if (scr) {
	NamedIterator iter(scr->params());
	while (const NamedString* p = iter.get())
	    dumpRecursiveObj(p,buf,depth + 1,seen,flags,nextPath);
	if (scr->nativeParams()) {
	    iter = *scr->nativeParams();
	    while (const NamedString* p = iter.get())
		dumpRecursiveObj(p,buf,depth + 1,seen,flags,nextPath);
	}
    }
    else if (wrap)
	dumpRecursiveObj(wrap->object(),buf,depth + 1,seen,flags,nextPath);
    else if (nptr)
	dumpRecursiveObj(nptr->userData(),buf,depth + 1,seen,flags,nextPath);
    if (jso) {
	const HashList* hash = jso->getHashListParams();
	if (hash) {
	    for (unsigned int i = 0; i < hash->length(); i++) {
		ObjList* lst = hash->getList(i);
		for (lst ? lst = lst->skipNull() : 0; lst; lst = lst->skipNext())
		    dumpRecursiveObj(lst->get(),buf,depth + 1,seen,flags,nextPath);
	    }
	}
    }
}


const String JsObject::s_protoName("__proto__");

JsObject::JsObject(const char* name, ScriptMutex* mtx, bool frozen)
    : ScriptContext(String("[object ") + name + "]"),
      m_frozen(frozen), m_mutex(mtx), m_lineNo(0)
{
    XDebug(DebugAll,"JsObject::JsObject('%s',%p,%s) [%p]",
	name,mtx,String::boolText(frozen),this);
    params().addParam(new ExpFunction("freeze"));
    params().addParam(new ExpFunction("isFrozen"));
    params().addParam(new ExpFunction("toString"));
    params().addParam(new ExpFunction("hasOwnProperty"));
}

JsObject::JsObject(ScriptMutex* mtx, const char* name, unsigned int line, bool frozen)
    : ScriptContext(name),
      m_frozen(frozen), m_mutex(mtx), m_lineNo(line)
{
    XDebug(DebugAll,"JsObject::JsObject(%p,'%s',0x%08x,%s) [%p]",
	mtx,name,m_lineNo,String::boolText(frozen),this);
    if (/*m_lineNo && */ m_mutex && m_mutex->objTrack())
	m_mutex->objCreated(this);
}

JsObject::JsObject(GenObject* context, unsigned int line, ScriptMutex* mtx, bool frozen)
    : ScriptContext("[object Object]"),
      m_frozen(frozen), m_mutex(mtx), m_lineNo(line)
{
    XDebug(DebugAll,"JsObject::JsObject(ctxt=%p,l=0x%08x,mtx=%p,f=%s) [%p]",
	context,m_lineNo,mtx,String::boolText(frozen),this);
    setPrototype(context,YSTRING("Object"));
    if (/*m_lineNo && */ m_mutex && m_mutex->objTrack())
	m_mutex->objCreated(this);
}

JsObject::~JsObject()
{
    if (m_mutex && m_mutex->objTrack())
	m_mutex->objDeleted(this);
    XDebug(DebugAll,"JsObject::~JsObject '%s' [%p]",toString().c_str(),this);
}

JsObject* JsObject::copy(ScriptMutex* mtx, const ExpOperation& oper) const
{
    JsObject* jso = new JsObject(mtx,toString(),oper.lineNumber(),frozen());
    deepCopyParams(jso->params(),params(),mtx);
    return jso;
}

void JsObject::dumpRecursive(const GenObject* obj, String& buf, unsigned int flags)
{
    RecursiveTrace seen(false,YOBJECT(JsObject,obj),obj);
    String path;
    dumpRecursiveObj(obj,buf,0,seen,flags,path);
}

void JsObject::printRecursive(const GenObject* obj, unsigned int flags)
{
    String buf;
    dumpRecursive(obj,buf,flags);
    Output("%s",buf.c_str());
}

String JsObject::strEscape(const char* str)
{
    String s("\"");
    char c;
    while (str && (c = *str++)) {
	switch (c) {
	    case '\"':
	    case '\\':
		s += "\\";
		break;
	    case '\b':
		s += "\\b";
		continue;
	    case '\f':
		s += "\\f";
		continue;
	    case '\n':
		s += "\\n";
		continue;
	    case '\r':
		s += "\\r";
		continue;
	    case '\t':
		s += "\\t";
		continue;
	    case '\v':
		s += "\\v";
		continue;
	}
	s += c;
    }
    s += "\"";
    return s;
}

ExpOperation* JsObject::toJSON(const ExpOperation* oper, int spaces)
{
    if (!oper || YOBJECT(JsFunction,oper) || YOBJECT(ExpFunction,oper) || JsParser::isUndefined(*oper))
	return 0;
    if (spaces < 0)
	spaces = 0;
    else if (spaces > 10)
	spaces = 10;
    RecursiveTrace trace(true,YOBJECT(JsObject,oper),0);
    ExpOperation* ret = new ExpOperation("","JSON");
    toJSON(oper,*ret,spaces,0,&trace);
    return ret;
}

static bool internalResolveReferences(ExpOperation* root, ExpWrapper* param, RecursiveTrace& trace);

static bool resolveJsReference(ExpOperation* root, ExpWrapper* crt, RecursiveTrace& trace,
    const String& prop)
{
    if (!(root && crt))
	return true;
    JsObject* jso = YOBJECT(JsObject,crt);
    if (!jso)
	return true;
    String str;
    bool ok = jso->getStringField(YSTRING("$ref"),str);
#ifdef JS_DEBUG_REPLACE_REFERENCES
    Debug(DebugAll,"resolveJsReference%s root=(%p '%s') current=(%p) prop='%s' ref='%s'",
	(ok ? "" : " (no ref)"),root,root->toString().c_str(),crt,prop.safe(),str.safe());
#endif
    if (!ok) {
	if (trace.find(jso))
	    return true;
	trace.trace(jso,"-");
	return internalResolveReferences(root,crt,trace);
    }
    ExpOperation* found = 0;
    if ("#" == str)
	found = root;
    else if ('#' == str[0]) {
	RecursiveTraceItem* it = trace.findPath(str);
	if (it)
	    found = static_cast<ExpOperation*>(it->traced());
	else {
	    JPath path(str.substr(1));
	    found = JsObject::find(root,path);
	    if (!found)
		return false;
	    trace.trace(found,path);
	}
    }
    else {
	Debug(DebugMild,"Invalid JSON path '%s'",str.c_str());
	return false;
    }

    jso = YOBJECT(JsObject,found);
    if (!jso) {
	Debug(DebugMild,"Found non object for JSON path '%s'",str.c_str());
	return false;
    }
#ifdef JS_DEBUG_REPLACE_REFERENCES
    Debug(DebugAll,"resolveJsReference root: %p '%s' found %p '%s'",
	root,root->toString().c_str(),jso,jso->toString().c_str());
#endif
    if (jso == crt->object())
	return true;
    if (jso->ref()) {
	crt->setObject(jso);
	return true;
    }
    return false;
}

static bool internalResolveReferences(ExpOperation* root, ExpWrapper* wrap, RecursiveTrace& trace)
{
    if (!root)
	return true;
    JsObject* jso = wrap ? YOBJECT(JsObject,wrap) : YOBJECT(JsObject,root);
    JsArray* jsa = YOBJECT(JsArray,jso);
#ifdef JS_DEBUG_REPLACE_REFERENCES
    Debugger dbg(DebugInfo,"internalResolveReferences",
	" root=(%p '%s') wrap=(%p) object=(%p '%s') array=%u",
	root,root->toString().c_str(),wrap,jso,(jso ? jso->toString().safe() : ""),jsa);
#endif
    bool rVal = true;
    if (jsa) {
	unsigned int n = jsa->length();
	for (unsigned int i = 0; i < n; i++) {
	    String name(i);
	    wrap = YOBJECT(ExpWrapper,jsa->params().getParam(name));
	    if (wrap)
		rVal = resolveJsReference(root,wrap,trace,name) && rVal;
	}
    }
    else if (jso) {
	for (ObjList* o = jso->params().paramList()->skipNull(); o; o = o->skipNext()) {
	    wrap = YOBJECT(ExpWrapper,o->get());
	    if (!wrap)
		continue;
	    const String& name = wrap->name();
	    if (name != JsObject::protoName())
		rVal = resolveJsReference(root,wrap,trace,name) && rVal;
	}
    }
    return rVal;
}

bool JsObject::resolveReferences(ExpOperation* oper)
{
    if (!oper)
	return true;
#ifdef JS_DEBUG_REPLACE_REFERENCES
    Debugger dbg(DebugInfo,"JsObject::resolveReferences"," %p '%s'",oper,oper->toString().c_str());
#endif
    RecursiveTrace trace(true,YOBJECT(JsObject,oper),0);
    return internalResolveReferences(oper,0,trace);
}

ExpOperation* JsObject::find(ExpOperation* oper, const JPath& path)
{
    if (!path.valid())
	return 0;
    JsObject* obj = YOBJECT(JsObject,oper);
    if (!obj)
	return 0;

    if (!path.count())
	return oper;
    for (unsigned int i = 0; i < path.count(); ) {
	const String& prop = path[i++];
	if (prop == JsObject::protoName())
	    return 0;
	JsArray* jsa = YOBJECT(JsArray,obj);
	if (jsa && (0 > JPath::validArrayIndex(prop)))
	    return 0;
	ExpOperation* found = YOBJECT(ExpOperation,obj->params().getParam(prop));
	if (!found || i == path.count())
	    return found;
	obj = YOBJECT(JsObject,found);
	if (!obj)
	    return 0;
    }
    return 0;
}

// Utility: retrieve a JSON candidate from given list item
// Advance the list
// Return pointer to candidate, NULL if not found
static inline GenObject* nextJSONCandidate(ObjList*& crt, bool isNs = true)
{
    if (!crt)
	return 0;
    if (!crt->get()) {
	crt = crt->skipNull();
	if (!crt)
	    return 0;
    }
    while (crt) {
	GenObject* gen = crt->get();
	crt = crt->skipNext();
	const String& n = isNs ? static_cast<NamedString*>(gen)->name() : gen->toString();
	if (!n || n == JsObject::protoName() || YOBJECT(JsFunction,gen) || YOBJECT(ExpFunction,gen))
	    continue;
	const ExpOperation* op = YOBJECT(ExpOperation,gen);
	if (!(op && JsParser::isUndefined(*op)))
	    return gen;
    }
    return 0;
}

// Utility: retrieve a JSON candidate from given hash list
// Advance the list
// Return pointer to candidate, NULL if not found
static inline GenObject* nextJSONCandidate(const HashList& hash, unsigned int& idx, ObjList*& crt)
{
    GenObject* gen = nextJSONCandidate(crt,false);
    if (gen)
	return gen;
    crt = 0;
    while (++idx < hash.length()) {
	crt = hash.getList(idx);
	if (!crt)
	    continue;
	gen = nextJSONCandidate(crt,false);
	if (gen)
	    return gen;
    }
    return 0;
}

// Utility used in internalToJSON to handle recursivity
// Return true if handled
bool JsObject::recursiveToJSON(String& newPath, JsObject* jso, String& buf, int spaces, int indent,
    void* data, const String& path, const String& crtProp)
{
    if (!(jso && data))
	return false;
    RecursiveTrace* trace = (RecursiveTrace*)data;
    RecursiveTraceItem* it = trace->find(jso);
    if (!it) {
	newPath = path;
	JPath::addItem(newPath,crtProp);
	trace->traceJsObj(jso,newPath);
	return false;
    }
    GenObject* obj = it->getJPath();
    if (!obj) {
	JsObject* jobj = new JsObject;
	jobj->setStringField("$ref",*it);
	obj = new ExpWrapper(jobj);
	it->setJPath(obj);
    }
    internalToJSON(obj,false,buf,spaces,indent);
    return true;
}

void JsObject::internalToJSON(const GenObject* obj, bool isStr, String& buf, int spaces, int indent,
    void* data, const String& path, const String& crtProp)
{
    if (!obj) {
	buf << "null";
	return;
    }
    const ExpOperation* oper = YOBJECT(ExpOperation,obj);
    if (!oper) {
	if (isStr)
	    buf << strEscape(*static_cast<const String*>(obj));
	else
	    buf << "null";
	return;
    }
    if (JsParser::isNull(*oper) || JsParser::isUndefined(*oper) || YOBJECT(JsFunction,oper)
	    || YOBJECT(ExpFunction,oper)) {
	buf << "null";
	return;
    }
    const char* nl = spaces ? "\r\n" : "";
    JsObject* jso = YOBJECT(JsObject,oper);
    JsArray* jsa = YOBJECT(JsArray,jso);
    if (jsa) {
	String newPath;
	if (recursiveToJSON(newPath,jsa,buf,spaces,indent,data,path,crtProp))
	    return;
	if (jsa->length() <= 0) {
	    buf << "[]";
	    return;
	}
	String li(' ',indent);
	String ci(' ',indent + spaces);
	buf << "[" << nl;
	for (int32_t i = 0; ; ) {
	    buf << ci;
	    const NamedString* p = jsa->params().getParam(String(i));
	    if (p)
		toJSON(p,buf,spaces,indent + spaces,data,newPath,p->name());
	    else
		buf << "null";
	    if (++i < jsa->length())
		buf << "," << nl;
	    else {
		buf << nl;
		break;
	    }
	}
	buf << li << "]";
	return;
    }
    if (jso) {
	if (YOBJECT(JsDate,jso) || YOBJECT(JsJPath,jso)) {
	    buf << strEscape(jso->toString());
	    return;
	}
	String newPath;
	if (recursiveToJSON(newPath,jso,buf,spaces,indent,data,path,crtProp))
	    return;
	const HashList* hash = jso->getHashListParams();
	if (hash) {
	    ObjList* crt = hash->getList(0);
	    unsigned int idx = 0;
	    GenObject* gen = nextJSONCandidate(*hash,idx,crt);
	    if (!gen) {
		buf << "{}";
		return;
	    }
	    String li(' ',indent);
	    String ci(' ',indent + spaces);
	    const char* sep = spaces ? ": " : ":";
	    buf << "{" << nl;
	    while (gen) {
		buf << ci << strEscape(gen->toString()) << sep;
		internalToJSON(gen,false,buf,spaces,indent + spaces,data,newPath,gen->toString());
		gen = static_cast<NamedString*>(nextJSONCandidate(*hash,idx,crt));
		if (gen)
		    buf << ",";
		buf << nl;
	    }
	    buf << li << "}";
	    return;
	}
	switch (jso->params().count()) {
	    case 1:
		if (!jso->params().getParam(protoName()))
		    break;
		// fall through
	    case 0:
		buf << "{}";
		return;
	}
	ObjList* l = jso->params().paramList()->skipNull();
	String li(' ',indent);
	String ci(' ',indent + spaces);
	const char* sep = spaces ? ": " : ":";
	buf << "{" << nl;
	const NamedString* p = static_cast<NamedString*>(nextJSONCandidate(l));
	while (p) {
	    buf << ci << strEscape(p->name()) << sep;
	    internalToJSON(p,true,buf,spaces,indent + spaces,data,newPath,p->name());
	    p = static_cast<NamedString*>(nextJSONCandidate(l));
	    if (p)
		buf << ",";
	    buf << nl;
	}
	buf << li << "}";
	return;
    }
    if (oper->isBoolean())
	buf << String::boolText(oper->valBoolean());
    else if (oper->isNumber()) {
	if (oper->isInteger())
	    buf << *oper;
	else
	    buf << "null";
    }
    else
	buf << strEscape(*oper);
}

void JsObject::setPrototype(GenObject* context, const String& objName)
{
    ScriptContext* ctxt = YOBJECT(ScriptContext,context);
    if (!ctxt) {
	ScriptRun* sr = static_cast<ScriptRun*>(context);
	if (!(sr && (ctxt = YOBJECT(ScriptContext,sr->context()))))
	    return;
    }
    JsObject* objCtr = YOBJECT(JsObject,ctxt->params().getParam(objName));
    if (objCtr) {
	JsObject* proto = YOBJECT(JsObject,objCtr->params().getParam(YSTRING("prototype")));
	if (proto && proto->ref())
	    params().addParam(new ExpWrapper(proto,protoName()));
    }
}

JsObject* JsObject::buildCallContext(ScriptMutex* mtx, JsObject* thisObj)
{
    JsObject* ctxt = new JsObject(mtx,"()",0);
    if (thisObj && thisObj->alive()) {
	ctxt->lineNo(thisObj->lineNo());
	ctxt->params().addParam(new ExpWrapper(thisObj,"this"));
    }
    return ctxt;
}

void JsObject::fillFieldNames(ObjList& names)
{
    ScriptContext::fillFieldNames(names,params(),false,"__");
    const NamedList* native = nativeParams();
    if (native)
	ScriptContext::fillFieldNames(names,*native);
#ifdef XDEBUG
    String tmp;
    tmp.append(names,",");
    Debug(DebugInfo,"JsObject::fillFieldNames: %s",tmp.c_str());
#endif
}

const HashList* JsObject::getHashListParams() const
{
    return 0;
}

bool JsObject::hasField(ObjList& stack, const String& name, GenObject* context) const
{
    if (ScriptContext::hasField(stack,name,context))
	return true;
    const ScriptContext* proto = YOBJECT(ScriptContext,params().getParam(protoName()));
    if (proto && proto->hasField(stack,name,context))
	return true;
    NamedList* np = nativeParams();
    return np && np->getParam(name);
}

NamedString* JsObject::getField(ObjList& stack, const String& name, GenObject* context) const
{
    NamedString* fld = ScriptContext::getField(stack,name,context);
    if (fld)
	return fld;
    const ScriptContext* proto = YOBJECT(ScriptContext,params().getParam(protoName()));
    if (proto) {
	fld = proto->getField(stack,name,context);
	if (fld)
	    return fld;
    }
    NamedList* np = nativeParams();
    if (np)
	return np->getParam(name);
    return 0;
}

JsObject* JsObject::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (!ref())
	return 0;
    JsObject* obj = clone("[object " + oper.name() + "]",oper);
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

bool JsObject::runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugInfo,"JsObject::runFunction() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    NamedString* param = getField(stack,oper.name(),context);
    if (!param)
	return false;
    ExpFunction* ef = YOBJECT(ExpFunction,param);
    if (ef)
	return runNative(stack,oper,context);
    JsFunction* jf = YOBJECT(JsFunction,param);
    if (jf) {
	JsObject* objThis = 0;
	if (toString() != YSTRING("()"))
	    objThis = this;
	return jf->runDefined(stack,oper,context,objThis);
    }
    return false;
}

bool JsObject::runField(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsObject::runField() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    const String* param = getField(stack,oper.name(),context);
    if (param) {
	ExpFunction* ef = YOBJECT(ExpFunction,param);
	if (ef)
	    ExpEvaluator::pushOne(stack,ef->ExpOperation::clone());
	else {
	    ExpWrapper* w = YOBJECT(ExpWrapper,param);
	    if (w)
		ExpEvaluator::pushOne(stack,w->clone(oper.name()));
	    else {
		JsObject* jso = YOBJECT(JsObject,param);
		if (jso && jso->ref())
		    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,oper.name()));
		else {
		    ExpOperation* o = YOBJECT(ExpOperation,param);
		    ExpEvaluator::pushOne(stack,o ? new ExpOperation(*o,oper.name(),false) : new ExpOperation(*param,oper.name(),true));
		}
	    }
	}
    }
    else
	ExpEvaluator::pushOne(stack,new ExpWrapper(0,oper.name()));
    return true;
}

bool JsObject::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsObject::runAssign() '%s'='%s' (%s) in '%s' [%p]",
	oper.name().c_str(),oper.c_str(),oper.typeOf(),toString().c_str(),this);
    if (frozen()) {
	Debug(DebugWarn,"Object '%s' is frozen",toString().c_str());
	return false;
    }
    ExpFunction* ef = YOBJECT(ExpFunction,&oper);
    if (ef)
	params().setParam(ef->ExpOperation::clone());
    else {
	ExpWrapper* w = YOBJECT(ExpWrapper,&oper);
	if (w) {
	    JsFunction* jsf = YOBJECT(JsFunction,w->object());
	    if (jsf) {
		ScriptRun* runner = YOBJECT(ScriptRun,context);
		JsFunction* clonedFunc = jsf->cloneFunction(oper,runner && runner->context() ? runner->context()->mutex() : 0);
		if (clonedFunc) {
		    clonedFunc->firstName(oper.name());
		    w = new ExpWrapper(clonedFunc,oper.name(),oper.barrier());
		    w->lineNumber(oper.lineNumber());
		    params().setParam(w);
		    return true;
		}
		else
		    jsf->firstName(oper.name());
	    }
	    params().setParam(w->clone(oper.name()));
	}
	else
	    params().setParam(oper.clone());
    }
    return true;
}

bool JsObject::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsObject::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("freeze"))
	freeze();
    else if (oper.name() == YSTRING("isFrozen"))
	ExpEvaluator::pushOne(stack,new ExpOperation(frozen()));
    else if (oper.name() == YSTRING("toString"))
	ExpEvaluator::pushOne(stack,new ExpOperation(params()));
    else if (oper.name() == YSTRING("hasOwnProperty")) {
	bool ok = true;
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		continue;
	    ok = ok && params().getParam(*op);
	    TelEngine::destruct(op);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else
	return false;
    return true;
}

ExpOperation* JsObject::popValue(ObjList& stack, GenObject* context)
{
    ExpOperation* oper = ExpEvaluator::popOne(stack);
    if (!oper || (oper->opcode() != ExpEvaluator::OpcField))
	return oper;
    XDebug(DebugAll,"JsObject::popValue() field '%s' in '%s' [%p]",
	oper->name().c_str(),toString().c_str(),this);
    bool ok = runMatchingField(stack,*oper,context);
    TelEngine::destruct(oper);
    return ok ? ExpEvaluator::popOne(stack) : 0;
}

// Static method that adds an object to a parent
void JsObject::addObject(NamedList& params, const char* name, JsObject* obj)
{
    params.addParam(new NamedPointer(name,obj,obj->toString()));
}

// Static method that adds a constructor to a parent
void JsObject::addConstructor(NamedList& params, const char* name, JsObject* obj)
{
    JsFunction* ctr = new JsFunction(obj->mutex(),name,0);
    ctr->params().addParam(new NamedPointer("prototype",obj,obj->toString()));
    obj->initConstructor(ctr);
    params.addParam(new NamedPointer(name,ctr,ctr->toString()));
}

// Static method that pops arguments off a stack to a list in proper order
int JsObject::extractArgs(JsObject* obj, ObjList& stack, const ExpOperation& oper,
    GenObject* context, ObjList& arguments)
{
    if (!obj || !oper.number())
	return 0;
    for (int i = (int)oper.number(); i;  i--) {
	ExpOperation* op = obj->popValue(stack,context);
	JsFunction* jsf = YOBJECT(JsFunction,op);
	if (jsf)
	    jsf->firstName(op->name());
	arguments.insert(op);
    }
    return (int)oper.number();
}

// Static helper method that deep copies all parameters
void JsObject::deepCopyParams(NamedList& dst, const NamedList& src, ScriptMutex* mtx)
{
    NamedIterator iter(src);
    while (const NamedString* p = iter.get()) {
	ExpOperation* oper = YOBJECT(ExpOperation,p);
	if (oper)
	    dst.addParam(oper->copy(mtx));
	else
	    dst.addParam(p->name(),*p);
    }
}

// Initialize standard globals in the execution context
void JsObject::initialize(ScriptContext* context)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& p = context->params();
    static_cast<String&>(p) = "[object Global]";
    if (!p.getParam(YSTRING("Object")))
	addConstructor(p,"Object",new JsObjectObj(mtx));
    if (!p.getParam(YSTRING("Function")))
	addConstructor(p,"Function",new JsFunction(mtx));
    if (!p.getParam(YSTRING("Array")))
	addConstructor(p,"Array",new JsArray(mtx));
    if (!p.getParam(YSTRING("RegExp")))
	addConstructor(p,"RegExp",new JsRegExp(mtx));
    if (!p.getParam(YSTRING("Date")))
	addConstructor(p,"Date",new JsDate(mtx));
    if (!p.getParam(YSTRING("Math")))
	addObject(p,"Math",new JsMath(mtx));
    if (!p.getParam(YSTRING("JPath")))
	addConstructor(p,"JPath",new JsJPath(mtx));
}

void JsObject::setLineForObj(JsObject* obj,unsigned int lineNo, bool recursive)
{
    if (!obj)
	return;
    DDebug(DebugAll,"JsObject::setLineForObj(%p,%u,%s)",obj,lineNo,String::boolText(recursive));
    obj->lineNo(lineNo);
    if (!recursive)
	return;
    for (unsigned int i = 0;i < obj->params().length();i++) {
	String* param = obj->params().getParam(i);
	JsObject* tmpObj = YOBJECT(JsObject,param);
	if (!tmpObj)
	    continue;
	JsObject::setLineForObj(tmpObj,lineNo,recursive);
	tmpObj->lineNo(lineNo);
    }
}

bool JsObject::getIntField(const String& name, int64_t& val)
{
    NamedString* ns = params().getParam(name);
    ExpOperation* op = YOBJECT(ExpOperation,ns);
    if (!(op && op->isInteger()))
	return false;
    val = op->number();
    return true;
}

bool JsObject::getBoolField(const String& name, bool& val)
{
    NamedString* ns = params().getParam(name);
    ExpOperation* op = YOBJECT(ExpOperation,ns);
    if (!(op && op->isBoolean()))
	return false;
    val = op->valBoolean();
    return true;
}

bool JsObject::getStringField(const String& name, String& val)
{
    NamedString* ns = params().getParam(name);
    if (!(ns && *ns))
	return false;
    val = ns;
    return true;
}

bool JsObject::getObjField(const String& name, JsObject*& obj)
{
    if (!name)
	return false;
    String* n = params().getParam(name);
    JsObject* jso = YOBJECT(JsObject,n);
    if (jso && jso ->ref()) {
        obj = jso;
        return true;
    }
    return false;
}

bool JsObjectObj::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("constructor"))
	ExpEvaluator::pushOne(stack,new ExpWrapper(new JsObject("Object",mutex())));
    else if (oper.name() == YSTRING("keys")) {
	ExpOperation* op = 0;
	GenObject* obj = 0;
	if (oper.number() == 0) {
	    ScriptRun* run = YOBJECT(ScriptRun,context);
	    if (run)
		obj = run->context();
	    else
		obj = context;
	}
	else if (oper.number() == 1) {
	    op = popValue(stack,context);
	    if (!op)
		return false;
	    obj = op;
	}
	else
	    return false;
	ScriptContext* scr = YOBJECT(ScriptContext,obj);
	if (scr) {
	    ObjList names;
	    scr->fillFieldNames(names);
	    JsArray* jsa = new JsArray(context,oper.lineNumber(),mutex());
	    for (ObjList* o = names.skipNull(); o; o = o->skipNext()) {
		String* name = static_cast<String*>(o->get());
		jsa->push(new ExpOperation(*name,0,true));
	    }
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"keys"));
	}
	else if (const NamedList* lst = YOBJECT(NamedList,obj)) {
	    NamedIterator iter(*lst);
	    JsArray* jsa = new JsArray(context,oper.lineNumber(),mutex());
	    while (const NamedString* ns = iter.get())
		if (ns->name() != protoName())
		    jsa->push(new ExpOperation(ns->name(),0,true));
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"keys"));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("global")) {
	if (oper.number() != 0)
	    return false;
	ScriptRun* run = YOBJECT(ScriptRun,context);
	JsObject* obj = YOBJECT(JsObject,run ? run->context() : context);
	if (obj && obj->ref())
	    ExpEvaluator::pushOne(stack,new ExpWrapper(obj,"global"));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}


JsArray::JsArray(ScriptMutex* mtx)
    : JsObject("Array",mtx), m_length(0)
{
    params().addParam(new ExpFunction("push"));
    params().addParam(new ExpFunction("pop"));
    params().addParam(new ExpFunction("concat"));
    params().addParam(new ExpFunction("join"));
    params().addParam(new ExpFunction("reverse"));
    params().addParam(new ExpFunction("shift"));
    params().addParam(new ExpFunction("unshift"));
    params().addParam(new ExpFunction("slice"));
    params().addParam(new ExpFunction("splice"));
    params().addParam(new ExpFunction("sort"));
    params().addParam(new ExpFunction("includes"));
    params().addParam(new ExpFunction("indexOf"));
    params().addParam(new ExpFunction("lastIndexOf"));
    params().addParam("length","0");
}

JsArray::JsArray(GenObject* context, unsigned int line, ScriptMutex* mtx)
    : JsObject(mtx,"[object Array]",line), m_length(0)
{
    setPrototype(context,YSTRING("Array"));
}

JsObject* JsArray::copy(ScriptMutex* mtx, const ExpOperation& oper) const
{
    JsArray* jsa = new JsArray(mtx,toString(),oper.lineNumber(),frozen());
    deepCopyParams(jsa->params(),params(),mtx);
    jsa->setLength(length());
    return jsa;
}

void JsArray::push(ExpOperation* item)
{
    if (!item)
	return;
    unsigned int pos = m_length;
    while (params().getParam(String(pos)))
	pos++;
    const_cast<String&>(item->name()) = pos;
    params().addParam(item);
    setLength(pos + 1);
}

bool JsArray::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsArray::runAssign() '%s'='%s' (%s) in '%s' [%p]",
	oper.name().c_str(),oper.c_str(),oper.typeOf(),toString().c_str(),this);
    if (oper.name() == YSTRING("length")) {
	int newLen = oper.toInteger(-1);
	if (newLen < 0)
	    return false;
	for (int i = newLen; i < length(); i++)
	    params().clearParam(String(i));
	setLength(newLen);
	return true;
    }
    else if (!JsObject::runAssign(stack,oper,context))
	return false;
    int idx = oper.toString().toInteger(-1) + 1;
    if (idx && idx > m_length)
	setLength(idx);
    return true;
}

bool JsArray::runField(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsArray::runField() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("length")) {
	// Reflects the number of elements in an array.
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)length()));
	return true;
    }
    return JsObject::runField(stack,oper,context);
}

void JsArray::initConstructor(JsFunction* construct)
{
    construct->params().addParam(new ExpFunction("isArray"));
}

JsObject* JsArray::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (!ref())
	return 0;
    JsArray* obj = static_cast<JsArray*>(clone("[object " + oper.name() + "]",oper));
    unsigned int len = (unsigned int)oper.number();
    for (unsigned int i = len; i;  i--) {
	ExpOperation* op = obj->popValue(stack,context);
	if ((len == 1) && op->isInteger() && (op->number() >= 0) && (op->number() <= 0xffffffff)) {
	    len = (unsigned int)op->number();
	    TelEngine::destruct(op);
	    break;
	}
	const_cast<String&>(op->name()) = i - 1;
	obj->params().paramList()->insert(op);
    }
    obj->setLength(len);
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

bool JsArray::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsArray::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("isArray")) {
	// Static function that checks if the argument is an Array
	ObjList args;
	extractArgs(this,stack,oper,context,args);
	ExpEvaluator::pushOne(stack,new ExpOperation(!!YOBJECT(JsArray,args[0])));
    }
    else if (oper.name() == YSTRING("push")) {
	// Adds one or more elements to the end of an array and returns the new length of the array.
	ObjList args;
	if (!extractArgs(this,stack,oper,context,args))
	    return false;
	while (ExpOperation* op = static_cast<ExpOperation*>(args.remove(false))) {
	    const_cast<String&>(op->name()) = (unsigned int)m_length++;
	    params().addParam(op);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)length()));
    }
    else if (oper.name() == YSTRING("pop")) {
	// Removes the last element from an array and returns that element
	if (oper.number())
	    return false;
	NamedString* last = 0;
	while ((m_length > 0) && !last)
	    last = params().getParam(String(--m_length));
	if (!last)
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0,0));
	else {
	    params().paramList()->remove(last,false);
	    ExpOperation* op = YOBJECT(ExpOperation,last);
	    if (!op) {
		op = new ExpOperation(*last,0,true);
		TelEngine::destruct(last);
	    }
	    ExpEvaluator::pushOne(stack,op);
	}
    }
    else if (oper.name() == YSTRING("concat")) {
	// Returns a new array comprised of this array joined with other array(s) and/or value(s).
	// var num1 = [1, 2, 3];
	// var num2 = [4, 5, 6];
	// var num3 = [7, 8, 9];
	//
	// creates array [1, 2, 3, 4, 5, 6, 7, 8, 9]; num1, num2, num3 are unchanged
	// var nums = num1.concat(num2, num3);

	// var alpha = ['a', 'b', 'c'];
	// creates array ["a", "b", "c", 1, 2, 3], leaving alpha unchanged
	// var alphaNumeric = alpha.concat(1, [2, 3]);

	ObjList args;
	extractArgs(this,stack,oper,context,args);

	JsArray* array = new JsArray(context,oper.lineNumber(),mutex());
	// copy this array - only numerically indexed elements!
	for (int i = 0; i < m_length; i++) {
	    NamedString* ns = params().getParam(String(i));
	    if (!ns)
		continue;
	    ExpOperation* op = YOBJECT(ExpOperation,ns);
	    op = op ? op->clone() : new ExpOperation(*ns,ns->name(),true);
	    array->params().addParam(op);
	}
	array->setLength(length());
	// add parameters - either basic types or elements of Array
	while (ExpOperation* op = static_cast<ExpOperation*>(args.remove(false))) {
	    JsArray* ja = YOBJECT(JsArray,op);
	    if (ja) {
		int len = ja->length();
		for (int i = 0; i < len; i++) {
		    NamedString* ns = ja->params().getParam(String(i));
		    if (ns) {
			ExpOperation* arg = YOBJECT(ExpOperation,ns);
			arg = arg ? arg->clone() : new ExpOperation(*ns,0,true);
			const_cast<String&>(arg->name()) = (unsigned int)array->m_length++;
			array->params().addParam(arg);
		    }
		    else
			array->m_length++;
		}
		TelEngine::destruct(op);
	    }
	    else {
		const_cast<String&>(op->name()) = (unsigned int)array->m_length++;
		array->params().addParam(op);
	    }
	}
	ExpEvaluator::pushOne(stack,new ExpWrapper(array));
    }
    else if (oper.name() == YSTRING("join")) {
	// Joins all elements of an array into a string
	// var a = new Array("Wind","Rain","Fire");
	// var myVar1 = a.join();      // assigns "Wind,Rain,Fire" to myVar1
	// var myVar2 = a.join(", ");  // assigns "Wind, Rain, Fire" to myVar2
	// var myVar3 = a.join(" + "); // assigns "Wind + Rain + Fire" to myVar3
	String separator = ",";
	if (oper.number()) {
	    ExpOperation* op = popValue(stack,context);
	    separator = *op;
	    TelEngine::destruct(op);
	}
	String result;
	for (int32_t i = 0; i < length(); i++)
	    result.append(params()[String(i)],separator);
	ExpEvaluator::pushOne(stack,new ExpOperation(result));
    }
    else if (oper.name() == YSTRING("reverse")) {
	// Reverses the order of the elements of an array -- the first becomes the last, and the last becomes the first.
	// var myArray = ["one", "two", "three"];
	// myArray.reverse(); => three, two, one
	if (oper.number())
	    return false;
	int i1 = 0;
	int i2 = length() - 1;
	for (; i1 < i2; i1++, i2--) {
	    String s1(i1);
	    String s2(i2);
	    NamedString* n1 = params().getParam(s1);
	    NamedString* n2 = params().getParam(s2);
	    if (n1)
		const_cast<String&>(n1->name()) = s2;
	    if (n2)
		const_cast<String&>(n2->name()) = s1;
	}
	ref();
	ExpEvaluator::pushOne(stack,new ExpWrapper(this));
    }
    else if (oper.name() == YSTRING("shift")) {
	// Removes the first element from an array and returns that element
	// var myFish = ["angel", "clown", "mandarin", "surgeon"];
	// println("myFish before: " + myFish);
	// var shifted = myFish.shift();
	// println("myFish after: " + myFish);
	// println("Removed this element: " + shifted);
	// This example displays the following:

	// myFish before: angel,clown,mandarin,surgeon
	// myFish after: clown,mandarin,surgeon
	// Removed this element: angel
	if (oper.number())
	    return false;
	ObjList* l = params().paramList()->find("0");
	if (l) {
	    NamedString* ns = static_cast<NamedString*>(l->get());
	    params().paramList()->remove(ns,false);
	    ExpOperation* op = YOBJECT(ExpOperation,ns);
	    if (!op) {
		op = new ExpOperation(*ns,0,true);
		TelEngine::destruct(ns);
	    }
	    ExpEvaluator::pushOne(stack,op);
	    // shift : value n+1 becomes value n
	    for (int32_t i = 0; ; i++) {
		ns = static_cast<NamedString*>((*params().paramList())[String(i + 1)]);
		if (!ns) {
		    setLength(i);
		    break;
		}
		const_cast<String&>(ns->name()) = i;
	    }
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0,0));
    }
    else if (oper.name() == YSTRING("unshift")) {
	// Adds one or more elements to the front of an array and returns the new length of the array
	// myFish = ["angel", "clown"];
	// println("myFish before: " + myFish);
	// unshifted = myFish.unshift("drum", "lion");
	// println("myFish after: " + myFish);
	// println("New length: " + unshifted);
	// This example displays the following:
	// myFish before: ["angel", "clown"]
	// myFish after: ["drum", "lion", "angel", "clown"]
	// New length: 4
	// shift array
	int32_t shift = (int32_t)oper.number();
	if (shift >= 1) {
	    for (int32_t i = length() + shift - 1; i >= shift; i--) {
		NamedString* ns = static_cast<NamedString*>((*params().paramList())[String(i - shift)]);
		if (ns) {
		    String index(i);
		    params().clearParam(index);
		    const_cast<String&>(ns->name()) = index;
		}
	    }
	    for (int32_t i = shift - 1; i >= 0; i--) {
		ExpOperation* op = popValue(stack,context);
		if (!op)
		    continue;
	        const_cast<String&>(op->name()) = i;
		params().paramList()->insert(op);
	    }
	    setLength(length() + shift);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)length()));
    }
    else if (oper.name() == YSTRING("slice"))
	return runNativeSlice(stack,oper,context);
    else if (oper.name() == YSTRING("splice"))
	return runNativeSplice(stack,oper,context);
    else if (oper.name() == YSTRING("sort")) {
	return runNativeSort(stack,oper,context);
    }
    else if (oper.name() == YSTRING("toString")) {
	// Override the JsObject toString method
	// var monthNames = ['Jan', 'Feb', 'Mar', 'Apr'];
	// var myVar = monthNames.toString(); // assigns "Jan,Feb,Mar,Apr" to myVar.
	String separator = ",";
	String result;
	for (int32_t i = 0; i < length(); i++)
	    result.append(params()[String(i)],separator);
	ExpEvaluator::pushOne(stack,new ExpOperation(result));
    } else if (oper.name() == YSTRING("includes") || oper.name() == YSTRING("indexOf")
	    || oper.name() == YSTRING("lastIndexOf")) {
	// arr.includes(searchElement[,startIndex = 0[,"fieldName"]])
	// arr.indexOf(searchElement[,startIndex = 0[,"fieldName"]])
	// arr.lastIndexOf(searchElement[,startIndex = arr.length-1[,"fieldName"]])
	ObjList args;
	if (!extractArgs(this,stack,oper,context,args)) {
	    Debug(DebugWarn,"Failed to extract arguments!");
	    return false;
	}
	ExpOperation* op1 = static_cast<ExpOperation*>(args.remove(false));
	if (!op1)
	    return false;
	ExpWrapper* w1 = YOBJECT(ExpWrapper,op1);
	ExpOperation* fld = 0;
	int dir = 1;
	int pos = 0;
	if (oper.name().at(0) == 'l') {
	    dir = -1;
	    pos = length() - 1;
	}
	if (args.skipNull()) {
	    String* spos = static_cast<String*>(args.remove(false));
	    if (spos) {
		pos = spos->toInteger(pos);
		if (pos < 0)
		    pos += length();
		if (dir > 0) {
		    if (pos < 0)
			pos = 0;
		}
		else if (pos >= length())
		    pos = length() - 1;
	    }
	    TelEngine::destruct(spos);
	    fld = static_cast<ExpOperation*>(args.remove(false));
	}
	int index = -1;
	for (int i = pos; ; i += dir) {
	    if (dir > 0) {
		if (i >= length())
		    break;
	    }
	    else if (i < 0)
		break;
	    ExpOperation* op2 = static_cast<ExpOperation*>(params().getParam(String(i)));
	    if (op2 && !TelEngine::null(fld)) {
		const ExpExtender* ext = YOBJECT(ExpExtender,op2);
		if (!ext)
		    continue;
		op2 = YOBJECT(ExpOperation,ext->getField(stack,*fld,context));
	    }
	    if (!op2 || op2->opcode() != op1->opcode())
		continue;
	    ExpWrapper* w2 = YOBJECT(ExpWrapper,op2);
	    if (w1 || w2) {
		if (w1 && w2 && w1->object() == w2->object()) {
		    index = i;
		    break;
		}
	    } else if ((op1->number() == op2->number()) && (*op1 == *op2)) {
		index = i;
		break;
	    }
	}
	TelEngine::destruct(op1);
	TelEngine::destruct(fld);
	if (oper.name().length() == 8)
	    ExpEvaluator::pushOne(stack,new ExpOperation(index >= 0));
	else
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)index));
	return true;
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

bool JsArray::runNativeSlice(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    // Extracts a section of an array and returns a new array.
    // var myHonda = { color: "red", wheels: 4, engine: { cylinders: 4, size: 2.2 } };
    // var myCar = [myHonda, 2, "cherry condition", "purchased 1997"];
    // var newCar = myCar.slice(0, 2);
    int32_t begin = 0, end = length();
    switch (oper.number()) {
	case 2:
	    {   // get end of interval
		ExpOperation* op = popValue(stack,context);
		if (op && op->isInteger())
		    end = op->number();
		TelEngine::destruct(op);
	    }
	// intentional fallthrough
	case 1:
	    {
		ExpOperation* op = popValue(stack,context);
		if (op && op->isInteger())
		    begin = op->number();
		TelEngine::destruct(op);
	    }
	    break;
	case 0:
	    break;
	default:
	    // maybe we should ignore the rest of the given parameters?
	    return false;
    }
    if (begin < 0) {
	begin = length() + begin;
	if (begin < 0)
	    begin = 0;
    }
    if (end < 0)
	end = length() + end;

    JsArray* array = new JsArray(context,oper.lineNumber(),mutex());
    for (int32_t i = begin; i < end; i++) {
	NamedString* ns = params().getParam(String(i));
	if (!ns) {
	    // if missing, insert undefined element in array also
	    array->m_length++;
	    continue;
	}
	ExpOperation* arg = YOBJECT(ExpOperation,ns);
	arg = arg ? arg->clone() : new ExpOperation(*ns,0,true);
	const_cast<String&>(arg->name()) = (unsigned int)array->m_length++;
	array->params().addParam(arg);
    }
    ExpEvaluator::pushOne(stack,new ExpWrapper(array));
    return true;
}

bool JsArray::runNativeSplice(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    // Changes the content of an array, adding new elements while removing old elements.
    // Returns an array containing the removed elements
    // array.splice(index , howMany[, element1[, ...[, elementN]]])
    // array.splice(index ,[ howMany[, element1[, ...[, elementN]]]])
    ObjList args;
    int argc = extractArgs(this,stack,oper,context,args);
    if (!argc)
	return false;
    // get start index
    int32_t len = length();
    ExpOperation* op = static_cast<ExpOperation*>(args.remove(false));
    int32_t begin = (int)(op->number() > len ? len : op->number());
    if (begin < 0)
	begin = len + begin > 0 ? len + begin : 0;
    TelEngine::destruct(op);
    argc--;
    // get count of objects to delete
    int32_t delCount = len - begin;
    if (argc) {
	op = static_cast<ExpOperation*>(args.remove(false));
	// howMany is negative, set it to 0
	if (op->number() < 0)
	    delCount = 0;
	// if howMany is greater than the length of remaining elements from start index, do not set it
	else if (op->number() < delCount)
	    delCount = op->number();
	TelEngine::destruct(op);
	argc--;
    }

    // remove elements
    JsArray* removed = new JsArray(context,oper.lineNumber(),mutex());
    for (int32_t i = begin; i < begin + delCount; i++) {
	NamedString* ns = params().getParam(String(i));
	if (!ns) {
	    // if missing, insert undefined element in array also
	    removed->m_length++;
	    continue;
	}
	params().paramList()->remove(ns,false);
	ExpOperation* op = YOBJECT(ExpOperation,ns);
	if (!op) {
	    op = new ExpOperation(*ns,0,true);
	    TelEngine::destruct(ns);
	}
	const_cast<String&>(op->name()) = (unsigned int)removed->m_length++;
	removed->params().addParam(op);
    }

    int32_t shiftIdx = argc - delCount;
    // shift elements to make room for those that are to be inserted or move the ones that remained
    // after delete
    if (shiftIdx > 0) {
	for (int32_t i = m_length - 1; i >= begin + delCount; i--) {
	    NamedString* ns = static_cast<NamedString*>((*params().paramList())[String(i)]);
	    if (ns)
		const_cast<String&>(ns->name()) = i + shiftIdx;
	}
    }
    else if (shiftIdx < 0) {
	for (int32_t i = begin + delCount; i < m_length; i++) {
	    NamedString* ns = static_cast<NamedString*>((*params().paramList())[String(i)]);
	    if (ns)
		const_cast<String&>(ns->name()) = i + shiftIdx;
	}
    }
    setLength(length() + shiftIdx);
    // insert the new elements
    for (int i = 0; i < argc; i++) {
	ExpOperation* arg = static_cast<ExpOperation*>(args.remove(false));
	const_cast<String&>(arg->name()) = (unsigned int)(begin + i);
	params().addParam(arg);
    }
    ExpEvaluator::pushOne(stack,new ExpWrapper(removed));
    return true;
}

class JsComparator
{
public:
    JsComparator(const char* funcName, ScriptRun* runner)
	: m_name(funcName), m_runner(runner), m_failed(false)
	{ }
    const char* m_name;
    ScriptRun* m_runner;
    bool m_failed;
};

int compare(GenObject* op1, GenObject* op2, void* data)
{
    JsComparator* cmp = static_cast<JsComparator*>(data);
    if (cmp && cmp->m_failed)
	return 0;
    if (!(cmp && cmp->m_runner))
	return ::strcmp(*(static_cast<String*>(op1)),*(static_cast<String*>(op2)));
    ScriptRun* runner = cmp->m_runner->code()->createRunner(cmp->m_runner->context());
    if (!runner)
	return 0;
    ObjList stack;
    stack.append((static_cast<ExpOperation*>(op1))->clone());
    stack.append((static_cast<ExpOperation*>(op2))->clone());
    ScriptRun::Status rval = runner->call(cmp->m_name,stack);
    int ret = 0;
    if (ScriptRun::Succeeded == rval) {
	ExpOperation* sret = static_cast<ExpOperation*>(ExpEvaluator::popOne(runner->stack()));
	if (sret) {
	    ret = sret->toInteger();
	    TelEngine::destruct(sret);
	}
	else
	    cmp->m_failed = true;
    }
    else
	cmp->m_failed = true;
    TelEngine::destruct(runner);
    return ret;
}

bool JsArray::runNativeSort(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList arguments;
    ExpOperation* op = 0;
    if (extractArgs(this,stack,oper,context,arguments))
	op = static_cast<ExpOperation*>(arguments[0]);
    ScriptRun* runner = YOBJECT(ScriptRun,context);
    if (op && !runner)
	return false;
    ObjList sorted;
    ObjList* last = &sorted;
    // Copy the arguments in a ObjList for sorting
    for (ObjList* o = params().paramList()->skipNull(); o; o = o->skipNext()) {
	NamedString* str = static_cast<NamedString*>(o->get());
	if (str->name().toInteger(-1) > -1)
	    (last = last->append(str))->setDelete(false);
    }
    JsComparator* comp = op ? new JsComparator(op->name() ,runner) : 0;
    sorted.sort(&compare,comp);
    bool ok = comp ? !comp->m_failed : true;
    delete comp;
    if (ok) {
	for (ObjList* o = params().paramList()->skipNull(); o;) {
	    NamedString* str = static_cast<NamedString*>(o->get());
	    if (str && str->name().toInteger(-1) > -1)
		o->remove(false);
	    else
		o = o->skipNext();
	}
	int i = 0;
	last = params().paramList()->last();
	for (ObjList* o = sorted.skipNull();o; o = o->skipNull()) {
	    ExpOperation* slice = static_cast<ExpOperation*>(o->remove(false));
	    const_cast<String&>(slice->name()) = i++;
	    last = last->append(slice);
	}
    }
    return ok;
}


JsRegExp::JsRegExp(ScriptMutex* mtx)
    : JsObject("RegExp",mtx)
{
    params().addParam(new ExpFunction("test"));
    params().addParam(new ExpFunction("valid"));
}

JsRegExp::JsRegExp(ScriptMutex* mtx, const char* name, unsigned int line, const char* rexp, bool insensitive, bool extended, bool frozen)
    : JsObject(mtx,name,line,frozen),
      m_regexp(rexp,extended,insensitive)
{
    XDebug(DebugAll,"JsRegExp::JsRegExp('%s',%p,%s) [%p]",
	name,mtx,String::boolText(frozen),this);
    params().addParam("ignoreCase",String::boolText(insensitive));
    params().addParam("basicPosix",String::boolText(!extended));
}

JsRegExp::JsRegExp(ScriptMutex* mtx, unsigned int line, const Regexp& rexp, bool frozen)
    : JsObject(mtx,rexp.c_str(),line),
      m_regexp(rexp)
{
    XDebug(DebugAll,"JsRegExp::JsRegExp('%s',%p,%s) [%p]",
	toString().c_str(),mtx,String::boolText(frozen),this);
}

JsObject* JsRegExp::copy(ScriptMutex* mtx, const ExpOperation& oper) const
{
    JsRegExp* reg = new JsRegExp(mtx,oper.lineNumber(),m_regexp,frozen());
    deepCopyParams(reg->params(),params(),mtx);
    return reg;
}

bool JsRegExp::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsRegExp::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("test")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	bool ok = op && regexp().matches(*op);
	TelEngine::destruct(op);
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("valid")) {
	if (oper.number())
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(regexp().compile()));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

bool JsRegExp::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsRegExp::runAssign() '%s'='%s' (%s) in '%s' [%p]",
	oper.name().c_str(),oper.c_str(),oper.typeOf(),toString().c_str(),this);
    if (!JsObject::runAssign(stack,oper,context))
	return false;
    if (oper.name() == YSTRING("ignoreCase"))
	regexp().setFlags(regexp().isExtended(),oper.toBoolean());
    else if (oper.name() == YSTRING("basicPosix"))
	regexp().setFlags(!oper.toBoolean(),regexp().isCaseInsensitive());
    return true;
}

JsObject* JsRegExp::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList args;
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
	case 2:
	    break;
	default:
	    return 0;
    }
    ExpOperation* pattern = static_cast<ExpOperation*>(args[0]);
    ExpOperation* flags = static_cast<ExpOperation*>(args[1]);
    if (!pattern)
	return 0;
    bool insensitive = false;
    bool extended = true;
    if (flags && *flags)  {
	const char* f = *flags;
	char c = *f++;
	while (c) {
	    switch (c) {
		case 'i':
		    c = *f++;
		    insensitive = true;
		    break;
		case 'b':
		    c = *f++;
		    extended = false;
		    break;
		default:
		    c = 0;
	    }
	}
    }
    if (!ref())
	return 0;
    JsRegExp* obj = new JsRegExp(mutex(),*pattern,oper.lineNumber(),*pattern,insensitive,extended);
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}


bool JsMath::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsMath::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("abs")) {
	int64_t n = ExpOperation::nonInteger();
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (op->isInteger())
		n = op->number();
	    else if (JsParser::isEmpty(*op))
		n = 0;
	    TelEngine::destruct(op);
	}
	if ((ExpOperation::nonInteger() != n) && (n < 0))
	    n = -n;
	ExpEvaluator::pushOne(stack,new ExpOperation(n));
    }
    else if (oper.name() == YSTRING("max")) {
	int64_t n = LLONG_MIN;
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (op->isInteger() && op->number() > n)
		n = op->number();
	    TelEngine::destruct(op);
	}
	if (LLONG_MIN == n)
	    n = ExpOperation::nonInteger();
	ExpEvaluator::pushOne(stack,new ExpOperation(n));
    }
    else if (oper.name() == YSTRING("min")) {
	int64_t n = LLONG_MAX;
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (op->isInteger() && op->number() < n)
		n = op->number();
	    TelEngine::destruct(op);
	}
	if (LLONG_MAX == n)
	    n = ExpOperation::nonInteger();
	ExpEvaluator::pushOne(stack,new ExpOperation(n));
    }
    else if (oper.name() == YSTRING("random")) {
	long min = 0;
	long max = LONG_MAX;
	ObjList args;
	if (extractArgs(stack,oper,context,args)) {
	    if (args.skipNull()) {
		const String* mins = static_cast<String*>(args[0]);
		if (mins)
		    min = mins->toLong(0);
	    }
	    if (args.count() >= 2) {
		const String* maxs = static_cast<String*>(args[1]);
		if (maxs)
		    max = maxs->toLong(max);
	    }
	}
	if (min < 0 || max < 0 || min >= max)
	    return false;
	int64_t rand = (max > (min + 1)) ? (Random::random() % (max - min)) : 0;
	ExpEvaluator::pushOne(stack,new ExpOperation(rand + min));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}


JsObject* JsDate::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsDate::runConstructor '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    JsObject* obj = 0;
    switch (extractArgs(stack,oper,context,args)) {
	case 0:
	    obj = new JsDate(mutex(),oper.lineNumber(),Time::msecNow());
	    break;
	case 1:
	    {
		ExpOperation* val = static_cast<ExpOperation*>(args[0]);
		if (val) {
		    if (val->isInteger())
			obj = new JsDate(mutex(),oper.lineNumber(),val->number());
		    else {
			// Check string
			uint64_t n = Time::toEpoch(*val,val->length(),1);
			if (n == (uint64_t)-1)
			    return JsParser::nullObject();
			obj = new JsDate(mutex(),oper.lineNumber(),n);
		    }
		}
	    }
	    break;
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	    {
		unsigned int parts[7];
		for (int i = 0; i < 7; i++) {
		    parts[i] = (2 == i) ? 1 : 0;
		    ExpOperation* val = static_cast<ExpOperation*>(args[i]);
		    if (val) {
			if (val->isInteger())
			    parts[i] = (int)val->number();
			else
			    return 0;
		    }
		}
		// Date components use local time, year can be 0-99, month starts from 0
		if (parts[0] < 100)
		    parts[0] += 1900;
		parts[1]++;
		u_int64_t time = Time::toEpoch(parts[0],parts[1],parts[2],parts[3],parts[4],parts[5]);
		obj = new JsDate(mutex(),oper.lineNumber(),1000 * time + parts[6],true);
	    }
	    break;
	default:
	    return 0;
    }
    if (obj && ref())
	obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

bool JsDate::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsDate::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("now")) {
	// Returns the number of milliseconds elapsed since 1 January 1970 00:00:00 UTC.
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)Time::msecNow()));
    }
    else if (oper.name() == YSTRING("UTC")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 2:
	    case 3:
	    case 4:
	    case 5:
	    case 6:
	    case 7:
		{
		    unsigned int parts[7];
		    for (int i = 0; i < 7; i++) {
			parts[i] = (2 == i) ? 1 : 0;
			ExpOperation* val = static_cast<ExpOperation*>(args[i]);
			if (val) {
			    if (val->isInteger())
				parts[i] = (int)val->number();
			    else
				return 0;
			}
		    }
		    // Year can be 0-99, month starts from 0
		    if (parts[0] < 100)
			parts[0] += 1900;
		    parts[1]++;
		    unsigned int time = Time::toEpoch(parts[0],parts[1],parts[2],parts[3],parts[4],parts[5]);
		    if (time != (unsigned int)-1) {
			ExpEvaluator::pushOne(stack,new ExpOperation(1000 * (int64_t)time + parts[6]));
			break;
		    }
		}
		// fall through
	    case 0:
	    case 1:
		ExpEvaluator::pushOne(stack,new ExpOperation(ExpOperation::nonInteger(),"NaN"));
		break;
	    default:
		return false;
	}
    }
    else if (oper.name() == YSTRING("getDate")) {
	// Returns the day of the month for the specified date according to local time.
	// The value returned by getDate is an integer between 1 and 31.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)day));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getDay")) {
	// Get the day of the week for the date (0 is Sunday and returns values 0-6)
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0, wday = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec,&wday))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)wday));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getFullYear")) {
	// Returns the year of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)year));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getHours")) {
	// Returns the hour ( 0 - 23) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)hour));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getMilliseconds")) {
	// Returns just the milliseconds part ( 0 - 999 )
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_msec));
    }
    else if (oper.name() == YSTRING("getMinutes")) {
	// Returns the minute ( 0 - 59 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)minute));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getMonth")) {
	// Returns the month ( 0 - 11 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)month - 1));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getSeconds")) {
	// Returns the second ( 0 - 59 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time + m_offs,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)sec));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getTime")) {
	// Returns the time in milliseconds since UNIX Epoch
	ExpEvaluator::pushOne(stack,new ExpOperation(1000 * ((int64_t)m_time) + (int64_t)m_msec));
    }
    else if (oper.name() == YSTRING("getTimezoneOffset")) {
	// Returns the UTC to local difference in minutes, positive goes west
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)(m_offs / -60)));
    }
    else if (oper.name() == YSTRING("getUTCDate")) {
	// Returns the day of the month for the specified date according to local time.
	// The value returned by getDate is an integer between 1 and 31.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)day));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCDay")) {
	// Get the day of the week for the date (0 is Sunday and returns values 0-6)
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0, wday = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec,&wday))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)wday));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCFullYear")) {
	// Returns the year of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)year));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCHours")) {
	// Returns the hour ( 0 - 23) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)hour));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCMilliseconds")) {
	// Returns just the milliseconds part ( 0 - 999 )
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_msec));
    }
    else if (oper.name() == YSTRING("getUTCMinutes")) {
	// Returns the minute ( 0 - 59 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)minute));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCMonth")) {
	// Returns the month ( 0 - 11 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)month - 1));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("getUTCSeconds")) {
	// Returns the second ( 0 - 59 ) of the specified date according to local time.
	int year = 0;
	unsigned int month = 0, day = 0, hour = 0, minute = 0, sec = 0;
	if (Time::toDateTime(m_time,year,month,day,hour,minute,sec))
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)sec));
	else
	    return false;
    }
    else if (oper.name() == YSTRING("toJSON")) {
	if (toString().null())
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(toString()));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}


//
// JPath
//
JPath::JPath(const char* value)
    : String(value),
    m_data(0), m_count(0)
{
    parse();
}

JPath::JPath(const JPath& other)
    : String(other),
    m_data(0), m_count(0)
{
    if (!(other.m_data && other.m_count))
	return;
    m_data = new String[other.m_count];
    m_count = other.m_count;
    for (unsigned int i = 0; i < m_count; i++)
	m_data[i] = other.m_data[i];
}

JPath::~JPath()
{
    reset();
}

void JPath::changed()
{
    parse();
}

void JPath::parse()
{
    reset();
    if (!c_str())
	return;
    if ('/' != *c_str()) {
	Debug(DebugWarn,"JPath(%s): invalid path - not starting with '/'",c_str());
	return;
    }

    ObjList* lst = split('/');
    ObjList* o = lst->skipNull();
    m_count = lst->count();
    if (o)
	o = o->skipNext();
    if (m_count)
	m_count--;
    bool ok = true;
    if (m_count) {
	m_data = new String[m_count];
	unsigned int itemIdx = 0;
	for (; o && ok && itemIdx < m_count; o = o->skipNext(), itemIdx++) {
	    String& str = *static_cast<String*>(o->get());
	    if (!str)
		continue;
	    char* start = (char*)(str.c_str());
	    for (char* s = start; *s; s++) {
		if ('~' != *s)
		    continue;
		char c = unescapeChar(s[1]);
		if (!c) {
		    Debug(DebugWarn,"JPath(%s): invalid item %u - %s",
			c_str(),itemIdx,(s[1] ? "unknown escape char" : "unexpected end after escape"));
		    ok = false;
		    break;
		}
		*s = 0;
		m_data[itemIdx] << start << c;
		start = s + 2;
		*s++ = '~';
	    }
	    if (*start)
		m_data[itemIdx] << start;
	}
    }
    TelEngine::destruct(lst);
    if (!ok)
	reset();
}


//
// JsJPath
//
JsJPath::JsJPath(ScriptMutex* mtx)
    : JsObject("JPath",mtx,true)
{
    params().addParam(new ExpFunction("getItems"));
    params().addParam(new ExpFunction("at"));
    params().addParam(new ExpFunction("count"));
    params().addParam(new ExpFunction("valid"));
}

JsObject* JsJPath::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsJPath::runConstructor '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    JsObject* obj = 0;
    int n = extractArgs(stack,oper,context,args);
    if (1 == n) {
	ExpOperation* val = static_cast<ExpOperation*>(args[0]);
	if (!val)
	    return 0;
	obj = new JsJPath(mutex(),oper.lineNumber(),val->c_str());
    }
    else
	return 0;
    if (obj) {
	if (ref())
	    obj->params().addParam(new ExpWrapper(this,protoName()));
	else
	    TelEngine::destruct(obj);
    }
    return obj;
}

bool JsJPath::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsJPath::runNative() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    if (oper.name() == YSTRING("getItems")) {
	// Returns array of items
	JsArray* jsa = new JsArray(context,oper.lineNumber(),mutex());
	for (unsigned int i = 0; i < m_path.count(); i++)
	    jsa->push(new ExpOperation(m_path[i]));
	ExpEvaluator::pushOne(stack,new ExpWrapper(jsa));
    }
    else if (oper.name() == YSTRING("at")) {
	// Retrieve path item at given index
	ObjList args;
	if (!extractArgs(this,stack,oper,context,args))
	    return false;
	unsigned int idx = m_path.count();
	if (m_path.count()) {
	    ExpOperation* op = static_cast<ExpOperation*>(args[0]);
	    if (op && op->isInteger())
		idx = (unsigned int)op->number();
	}
	if (idx < m_path.count())
	    ExpEvaluator::pushOne(stack,new ExpOperation(m_path[idx]));
	else
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0,"undefined"));
    }
    else if (oper.name() == YSTRING("count"))
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_path.count()));
    else if (oper.name() == YSTRING("valid"))
	ExpEvaluator::pushOne(stack,new ExpOperation(m_path.valid()));
    else
	return JsObject::runNative(stack,oper,context);

    return true;
}

void* JsJPath::getObject(const String& name) const
{
    void* obj = (name == YATOM("JsJPath")) ? const_cast<JsJPath*>(this)
	: JsObject::getObject(name);
    return obj ? obj : m_path.getObject(name);
}


/* vi: set ts=8 sw=4 sts=4 noet: */
