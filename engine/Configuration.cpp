/**
 * Configuration.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yatengine.h"

#include <stdio.h>
#include <string.h>

using namespace TelEngine;

#ifdef XDEBUG
    #define CONFIGURATION_DBG_IO
#endif
#define CONFIGURATION_IO_BUF 1024

class ConfigPrivFile;
class ConfigPriv;

class ConfigPrivBool
{
public:
    inline ConfigPrivBool(bool defVal = false, bool init = true)
	: m_value(defVal), m_init(init)
	{};
    inline operator bool() const
	{ return m_value; }
    inline bool set(bool on) {
	    if (m_init) {
		m_init = false;
		if (on != m_value) {
		    m_value = on;
		    return true;
		}
	    }
	    return false;
	}

private:
    bool m_value;
    bool m_init;
};

bool s_maxDepthInit = true;
static unsigned int s_maxDepth = 10;
#ifdef XDEBUG
static ConfigPrivBool s_warnings(true,true);
#else
static ConfigPrivBool s_warnings(false);
#endif
// Disable [$includesilent/$includesectionsilent]. Handle as 'include'
static ConfigPrivBool s_disableIncludeSilent;
// Include empty conf value (handled in [$include/$require/$includesilent <something>])
// NOTE: If <something> is empty this leads to recursive include of directory
//       containing the current file
static ConfigPrivBool s_includeEmpty;
// Disable conf include recursive check
static ConfigPrivBool s_checkRecursiveInclude(true);

class ConfigPriv : public DebugEnabler
{
public:
    enum Include {
	IncludeNone = 0,
	Include = 1,
	IncludeSilent = 2,
	IncludeRequire = 3,
    };
    inline ConfigPriv(Configuration& cfg, bool isMain, bool warn)
	: m_cfg(cfg), m_main(isMain), m_warn(warn), m_warnings(s_warnings)
	{ debugName("Configuration"); }
    inline Configuration& cfg()
	{ return m_cfg; }
    bool load(const char* file, ConfigPrivFile* parent = 0, NamedList* sect = 0,
	bool silent = false);
    inline bool warn(bool silent = false) const
	{ return (m_warn && silent) ? (bool)s_disableIncludeSilent : m_warn; }

    static const TokenDict s_includeSect[];
    static const TokenDict s_include[];

private:
    NamedList* addSection(ConfigPrivFile& f, String& name, NamedList* crt, const String& line, bool& ok);
    void addParam(ConfigPrivFile& f, NamedList* sect, String& line, bool& ok,
	bool enabled, bool warnNoSect);
    bool handleEnable(String& line, bool& enabled);
    bool include(ConfigPrivFile& f, NamedList* sect, String& line, bool& ok);
    bool includeSection(ConfigPrivFile& f, NamedList* sect, String& line, bool& ok, bool warnNoSect);
    int getInclude(String& buf, String& dest, const TokenDict* dict, bool matchOnly = false);
    inline void processIncludeSections(bool& ok) {
	    for (ObjList* o = m_includeSections.skipNull(); o; o = o->skipNext()) {
		ObjList stack;
		processInclude(static_cast<NamedList*>(o->get()),stack,ok);
	    }
	}
    void processInclude(NamedList* sect, ObjList& stack, bool& ok);
    inline void replaceParams(String& str)
	{ Engine::runParams().replaceParams(str); }

    Configuration& m_cfg;
    bool m_main;
    bool m_warn;
    bool m_warnings;
    ObjList m_includeSections;
    ObjList m_includeSectProcessed;
    ObjList m_includeFiles;
};

const TokenDict ConfigPriv::s_includeSect[] = {
    {"$includesection",       Include},
    {"$includesectionsilent", IncludeSilent},
    {"$requiresection",       IncludeRequire},
    {0,0}
};
const TokenDict ConfigPriv::s_include[] = {
    {"$include",       Include},
    {"$includesilent", IncludeSilent},
    {"$require",       IncludeRequire},
    {0,0}
};

class ConfigPrivFile : public String
{
public:
    inline ConfigPrivFile(ConfigPriv& cfg, const char* fn, ConfigPrivFile* parent = 0)
	: String(fn),
	m_offset(0), m_length(0), m_line(0), m_state(1),
	m_depth(parent ? parent->depth() + 1 : 0), m_warnNul(true),
	m_cfg(cfg), m_track(0)
	{}
    inline ~ConfigPrivFile()
	{ close(); }
    inline unsigned int line() const
	{ return m_line; }
    inline const char* cfg() const
	{ return m_cfg.cfg().safe(); }
    inline const char* fileName() const
	{ return safe(); }
    inline unsigned int depth() const
	{ return m_depth; }
    inline bool warn(bool silent = false)
	{ return m_cfg.warn(silent); }
    inline bool included() const
	{ return 0 != m_depth; }
    inline const char* descFull() const {
	    m_desc.clear();
	    m_desc << "'" << cfg() << "'";
	    if (included())
		m_desc << " [" << *this << "]";
	    m_desc << " line=" << m_line;
	    return m_desc;
	}
    inline const char* desc() const {
	    m_desc.clear();
	    m_desc << "'" << *this << "' line=" << m_line;
	    return m_desc;
	}

    bool open(bool silent, ObjList* track = 0);
    // Read a full line from buffer.
    // Return true if any data is still available (even on file error), false when no data is available
    // Set 'ok' to false as soon as file read error occurs
    bool readLine(String& line, bool& ok);
    inline void close() {
	    m_file.terminate();
	    setTrack();
	}
    static String& dumpStack(String& buf, ObjList* lst);

private:
    void setTrack(ObjList* track = 0);
    bool fileError(const String& oper);

    unsigned int m_offset;               // Data buffer offset
    unsigned int m_length;               // Length of accumulated data
    unsigned int m_line;
    int m_state;                         // 1:start, 0:processing, -1:EOF, -2:error
    File m_file;
    char m_buffer[CONFIGURATION_IO_BUF]; // File read buffer
    unsigned int m_depth;
    bool m_warnNul;
    ConfigPriv& m_cfg;
    ObjList* m_track;
    mutable String m_desc;
};

class ConfigurationPrivate
{
public:
    enum Include {
	IncludeNone = 0,
	Include = 1,
	IncludeSilent = 2,
	IncludeRequire = 3,
    };
    inline ConfigurationPrivate(Configuration& cfg, bool isMain)
	: m_cfg(cfg), m_main(isMain)
	{}
    inline void addingParam(const String& sect, const String& name, const String& value) {
	    if (!m_main || sect != YSTRING("configuration"))
		return;
	    if (s_maxDepthInit && name == YSTRING("max_depth")) {
		s_maxDepthInit = false;
		s_maxDepth = value.toInteger(3,0,3,10);
	    }
	    else if (s_disableIncludeSilent < 0 && name == YSTRING("disable_include_silent"))
		s_disableIncludeSilent.set(value.toBoolean());
	}
    inline bool prepareIncludeSection(const String& sect, String& s, const char* file, bool warn,
	bool& ok) {
	    int inc = getIncludeSect(s);
	    if (!inc)
		return false;
	    NamedList* nl = sect ? m_cfg.getSection(sect) : 0;
	    if (nl) {
		nl->addParam("[]",s);
		if (!m_includeSections.find(nl))
		    m_includeSections.append(nl)->setDelete(false);
		XDebug(DebugAll,"Config '%s' prepared section '%s' include '%s' file='%s'",
		    m_cfg.safe(),sect.safe(),s.safe(),(file == m_cfg.c_str() ? "<same>" : file));
	    }
	    else {
		if (inc == IncludeRequire)
		    ok = false;
		if (getWarn(warn,inc == IncludeSilent)) {
		    String tmp;
		    if (file != m_cfg.c_str())
			tmp.printf(" in included file '%s'",file);
		    Debug(DebugNote,"Config '%s' found '%s' outside any section%s",
			m_cfg.safe(),s.safe(),tmp.safe());
		}
	    }
	    return true;
	}
    inline void processIncludeSections(bool warn, bool& ok) {
	    for (ObjList* o = m_includeSections.skipNull(); o; o = o->skipNext()) {
		ObjList stack;
		processInclude(static_cast<NamedList*>(o->get()),stack,warn,ok);
	    }
	}
    inline bool getWarn(bool warn, bool silent)
	{ return (warn && silent) ? (bool)s_disableIncludeSilent : warn; }

    static inline int getIncludeSect(String& buf, bool setName = false) {
	    if (buf.startsWith("$includesection",true))
		{ if (setName) buf = buf.substr(16,buf.length() - 16); return Include; }
	    if (buf.startsWith("$includesectionsilent",true))
		{ if (setName) buf = buf.substr(22,buf.length() - 22); return IncludeSilent; }
	    if (buf.startsWith("$requiresection",true))
		{ if (setName) buf = buf.substr(16,buf.length() - 16); return IncludeRequire; }
	    return 0;
	}

private:
    void processInclude(NamedList* sect, ObjList& stack, bool warn, bool& ok);

    Configuration& m_cfg;
    bool m_main;
    ObjList m_includeSections;
    ObjList m_includeSectProcessed;
};

void ConfigurationPrivate::processInclude(NamedList* sect, ObjList& stack, bool warn, bool& ok)
{
    if (!sect || m_includeSectProcessed.find(sect))
	return;
    stack.append(sect)->setDelete(false);
#ifdef XDEBUG
    String tmp;
    tmp.append(stack," -> ");
    Debug(DebugInfo,"Config '%s' processing include section stack: %s",
	m_cfg.safe(),tmp.safe());
#endif
    for (ObjList* o = sect->paramList()->skipNull(); o;) {
	NamedString* s = static_cast<NamedString*>(o->get());
	int inc = 0;
	if ('[' == s->name()[0] && ']' == s->name()[1])
	    inc = getIncludeSect(*s,true);
	if (!inc) {
	    o = o->skipNext();
	    continue;
	}
	Engine::runParams().replaceParams(*s);
	if (*s) {
	    String error;
	    if (!stack[*s]) {
		// NOTE: We are adding current section to processed after processing it
		//       Handle already processed sections whithout checking for recursive include
		NamedList* incSect = static_cast<NamedList*>(m_includeSectProcessed[*s]);
		if (!incSect) {
		    incSect = m_cfg.getSection(*s);
		    if (incSect && incSect != sect)
			processInclude(incSect,stack,warn,ok);
		    else
			error = incSect ? "recursive include" : "not found";
		}
		if (!error) {
		    XDebug(DebugAll,"Config '%s' including section '%s' in '%s'",
			m_cfg.safe(),incSect->safe(),sect->safe());
		    for (ObjList* p = incSect->paramList()->skipNull(); p; p = p->skipNext()) {
			NamedString* ns = static_cast<NamedString*>(p->get());
			o->insert(new NamedString(ns->name(),*ns));
			// Update current element (replaced by insert)
			o = o->next();
		    }
		}
	    }
	    else {
		error.append(stack," -> ");
		error = "recursive include stack=" + error;
	    }
	    if (error) {
		if (inc == IncludeRequire)
		    ok = false;
		if (getWarn(warn,inc == IncludeSilent))
		    Debug(DebugNote,"Config '%s' not including section '%s' in '%s': %s",
			m_cfg.safe(),s->safe(),sect->safe(),error.c_str());
	    }
	}
	o->remove();
	o = o->skipNull();
	if (o)
	    continue;
	sect->paramList()->compact();
	break;
    }
    stack.remove(sect,false);
    m_includeSectProcessed.insert(sect)->setDelete(false);
}


// Text sort callback
static int textSort(GenObject* obj1, GenObject* obj2, void* context)
{
    const String* s1 = static_cast<const String*>(obj1);
    const String* s2 = static_cast<const String*>(obj2);
    if (TelEngine::null(s1))
	return TelEngine::null(s2) ? 0 : -1;
    if (TelEngine::null(s2))
	return 1;
    return ::strcmp(s1->c_str(),s2->c_str());
}


Configuration::Configuration()
    : m_main(false)
{
}

Configuration::Configuration(const char* filename, bool warn)
    : String(filename), m_main(false)
{
    load(warn);
}

ObjList* Configuration::getSectHolder(const String& sect) const
{
    if (sect.null())
	return 0;
    return const_cast<ObjList*>(m_sections.find(sect));
}

ObjList* Configuration::makeSectHolder(const String& sect)
{
    if (sect.null())
	return 0;
    ObjList *l = getSectHolder(sect);
    if (!l)
	l = m_sections.append(new NamedList(sect));
    return l;
}

NamedList* Configuration::getSection(unsigned int index) const
{
    return static_cast<NamedList *>(m_sections[index]);
}

NamedList* Configuration::getSection(const String& sect) const
{
    ObjList *l = getSectHolder(sect);
    return l ? static_cast<NamedList *>(l->get()) : 0;
}

NamedString* Configuration::getKey(const String& sect, const String& key) const
{
    NamedList *l = getSection(sect);
    return l ? l->getParam(key) : 0;
}

const char* Configuration::getValue(const String& sect, const String& key, const char* defvalue) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->c_str() : defvalue;
}

int Configuration::getIntValue(const String& sect, const String& key, int defvalue,
    int minvalue, int maxvalue, bool clamp) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->toInteger(defvalue,0,minvalue,maxvalue,clamp) : defvalue;
}

int Configuration::getIntValue(const String& sect, const String& key, const TokenDict* tokens, int defvalue) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->toInteger(tokens,defvalue) : defvalue;
}

int64_t Configuration::getInt64Value(const String& sect, const String& key, int64_t defvalue,
    int64_t minvalue, int64_t maxvalue, bool clamp) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->toInt64(defvalue,0,minvalue,maxvalue,clamp) : defvalue;
}

double Configuration::getDoubleValue(const String& sect, const String& key, double defvalue) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->toDouble(defvalue) : defvalue;
}

bool Configuration::getBoolValue(const String& sect, const String& key, bool defvalue) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->toBoolean(defvalue) : defvalue;
}

void Configuration::clearSection(const char* sect)
{
    if (sect) {
	ObjList *l = getSectHolder(sect);
	if (l)
	    l->remove();
    }
    else
	m_sections.clear();
}

// Make sure a section with a given name exists, create it if required
NamedList* Configuration::createSection(const String& sect)
{
    ObjList* o = makeSectHolder(sect);
    return o ? static_cast<NamedList*>(o->get()) : 0;
}

void Configuration::clearKey(const String& sect, const String& key)
{
    NamedList *l = getSection(sect);
    if (l)
	l->clearParam(key);
}

void Configuration::addValue(const String& sect, const char* key, const char* value)
{
    DDebug(DebugAll,"Configuration::addValue(\"%s\",\"%s\",\"%s\")",sect.c_str(),key,value);
    ObjList *l = makeSectHolder(sect);
    if (!l)
	return;
    NamedList *n = static_cast<NamedList *>(l->get());
    if (n)
	n->addParam(key,value);
}

void Configuration::setValue(const String& sect, const char* key, const char* value)
{
    DDebug(DebugAll,"Configuration::setValue(\"%s\",\"%s\",\"%s\")",sect.c_str(),key,value);
    ObjList *l = makeSectHolder(sect);
    if (!l)
	return;
    NamedList *n = static_cast<NamedList *>(l->get());
    if (n)
	n->setParam(key,value);
}

void Configuration::setValue(const String& sect, const char* key, int value)
{
    char buf[32];
    ::sprintf(buf,"%d",value);
    setValue(sect,key,buf);
}

void Configuration::setValue(const String& sect, const char* key, bool value)
{
    setValue(sect,key,String::boolText(value));
}

bool Configuration::load(bool warn)
{
    static AtomicInt s_useOld(-1);
    if (s_useOld < 0) {
	String path = Engine::configPath();
	if (!path.endsWith(Engine::pathSeparator()))
	    path << Engine::pathSeparator();
	s_useOld = File::exists(path + "Configuration_use_old") ? 1 : 0;
	if (s_useOld)
	    Debug(DebugNote,"Configuration is using old logic");
    }
    if (s_useOld) {
	m_sections.clear();
	if (null())
	    return false;
	ConfigurationPrivate priv(*this,m_main);
	return loadFile(c_str(),"",0,warn,&priv);
    }

    m_sections.clear();
    if (null())
	return false;
    ConfigPriv priv(*this,m_main,warn);
    return priv.load(c_str());
}

static inline char* cfgReadLine(FILE* f, char* buf, int rd,
    char& rest, bool& warn, const char* file, const String& sect, bool* start = 0)
{
    if (rest) {
	buf[0] = rest;
	rest = 0;
	buf[1] = 0;
	fgets(buf + 1,rd - 1,f);
    }
    else if (!::fgets(buf,rd,f))
	return 0;

    int check = warn ? 1 : 0;
    char* pc = ::strchr(buf,'\r');
    if (pc) {
	*pc = 0;
	check = 0;
    }
    pc = ::strchr(buf,'\n');
    if (pc) {
	*pc = 0;
	check = 0;
    }
    pc = buf;
    if (check)
	check = ::strlen(pc);
    // skip over an initial UTF-8 BOM
    if (start && *start) {
	String::stripBOM(pc);
	*start = false;
    }
    if (check == rd - 1) {
	char extra[2] = {0,0};
	::fgets(extra,2,f);
	rest = extra[0];
	if (rest) {
	    warn = false;
	    String tmp(pc);
	    if (sect)
		tmp.printf("section='%s' line %s...",sect.c_str(),tmp.substr(0,30).c_str());
	    else
		tmp.printf("line %s...",tmp.substr(0,30).c_str());
	    Debug(DebugWarn,
		"Configuration '%s' %s too long: subsequent read may lead to wrong parameter set",
		file,tmp.safe());
	}
    }
    while (*pc == ' ' || *pc == '\t')
	pc++;
    return pc;
}

bool Configuration::loadFile(const char* file, String sect, unsigned int depth, bool warn, void* priv)
{
    ConfigurationPrivate& cfg = *(ConfigurationPrivate*)priv;
    DDebug(DebugInfo,"Configuration::loadFile(\"%s\",[%s],%u,%s)",
	file,sect.c_str(),depth,String::boolText(warn));
    if (depth > s_maxDepth) {
	Debug(DebugWarn,"Config '%s' refusing to load config file '%s' at include depth %u",
	    c_str(),file,depth);
	return false;
    }
    FILE *f = ::fopen(file,"r");
    if (f) {
	bool ok = true;
	bool start = true;
	bool enabled = true;
	char rest = 0;
	bool warnLine = true;
	for (;;) {
	    char buf[1024];
	    char* pc = cfgReadLine(f,buf,sizeof(buf),rest,warnLine,file,sect,&start);
	    if (!pc)
		break;
	    switch (*pc) {
		case 0:
		case ';':
		    continue;
	    }
	    String s(pc);
	    if (s[0] == '[') {
		int r = s.find(']');
		if (r > 0) {
		    s = s.substr(1,r-1);
		    s.trimBlanks();
		    if (s.null())
			continue;
		    if (s.startSkip("$enabled")) {
			if ((s == YSTRING("else")) || (s == YSTRING("toggle")))
			    enabled = !enabled;
			else {
			    if (s.startSkip("elseif") && enabled) {
				enabled = false;
				continue;
			    }
			    Engine::runParams().replaceParams(s);
			    bool rev = s.startSkip("$not");
			    if (s.startSkip("$loaded"))
				enabled = Engine::self() && Engine::self()->pluginLoaded(s);
			    else if (s.startSkip("$unloaded"))
				enabled = !(Engine::self() && Engine::self()->pluginLoaded(s));
			    else if (s.startSkip("$filled"))
				enabled = !s.null();
			    else if (s.startSkip("$empty"))
				enabled = s.null();
			    else
				enabled = s.toBoolean(!s.startSkip("$bool"));
			    if (rev)
				enabled = !enabled;
			}
			continue;
		    }
		    if (!enabled)
			continue;
		    if (cfg.prepareIncludeSection(sect,s,file,warn,ok))
			continue;
		    bool noerr = false;
		    bool silent = false;
		    if (s.startSkip("$require") || (noerr = s.startSkip("$include"))
			|| (silent = noerr = s.startSkip("$includesilent"))) {
			Engine::runParams().replaceParams(s);
			String path;
			if (!s.startsWith(Engine::pathSeparator())) {
			    path = file;
			    int sep = path.rfind(Engine::pathSeparator());
			    if ('/' != *Engine::pathSeparator()) {
				int s2 = path.rfind('/');
				if (sep < s2)
				    sep = s2;
			    }
			    switch (sep) {
				case -1:
				    path.clear();
				    break;
				case 0:
				    path = Engine::pathSeparator();
				    break;
				default:
				    path = path.substr(0,sep);
				    path << Engine::pathSeparator();
			    }
			}
			path << s;
			ObjList files;
			bool doWarn = cfg.getWarn(warn,silent);
			if (File::listDirectory(path,0,&files)) {
			    path << Engine::pathSeparator();
			    DDebug(DebugAll,"Configuration loading up to %u files from '%s'",
				files.count(),path.c_str());
			    files.sort(textSort);
			    while (String* it = static_cast<String*>(files.remove(false))) {
				if (!(it->startsWith(".") || it->endsWith("~")
					|| it->endsWith(".bak") || it->endsWith(".tmp")))
				    ok = (loadFile(path + *it,sect,depth+1,doWarn,priv) || noerr) && ok;
#ifdef DEBUG
				else
				    Debug(DebugAll,"Configuration skipping over file '%s'",it->c_str());
#endif
				TelEngine::destruct(it);
			    }
			}
			else
			    ok = (loadFile(path,sect,depth+1,doWarn,priv) || noerr) && ok;
			continue;
		    }
		    Engine::runParams().replaceParams(s);
		    sect = s;
		    createSection(sect);
		}
		continue;
	    }
	    if (!enabled)
		continue;
	    int q = s.find('=');
	    if (q == 0)
		continue;
	    if (q < 0)
		q = s.length();
	    String key = s.substr(0,q).trimBlanks();
	    if (key.null())
		continue;
	    s = s.substr(q+1);
	    while (s.endsWith("\\",false)) {
		// line continues onto next
		s.assign(s,s.length()-1);
		char* pc = cfgReadLine(f,buf,sizeof(buf),rest,warnLine,file,sect);
		if (!pc)
		    break;
		s += pc;
	    }
	    s.trimBlanks();
	    cfg.addingParam(sect,key,s);
	    addValue(sect,key,s);
	}
	::fclose(f);
	if (!depth)
	    cfg.processIncludeSections(warn,ok);
	return ok;
    }
    if (warn) {
	int err = errno;
	if (depth)
	    Debug(DebugNote,"Config '%s' failed to open included config file '%s' (%d: %s)",
		c_str(),file,err,strerror(err));
	else
	    Debug(DebugNote,"Failed to open config file '%s', using defaults (%d: %s)",
		file,err,strerror(err));
    }
    return false;
}

bool Configuration::save() const
{
    if (null())
	return false;
    FILE *f = ::fopen(c_str(),"w");
    if (f) {
	bool separ = false;
	ObjList *ol = m_sections.skipNull();
	for (;ol;ol=ol->skipNext()) {
	    NamedList *nl = static_cast<NamedList *>(ol->get());
	    if (separ)
		::fprintf(f,"\n");
	    else
		separ = true;
	    ::fprintf(f,"[%s]\n",nl->c_str());
	    unsigned int n = nl->length();
	    for (unsigned int i = 0; i < n; i++) {
		NamedString *ns = nl->getParam(i);
		if (ns) {
		    // add a space after a line that ends with backslash
		    const char* bk = ns->endsWith("\\",false) ? " " : "";
		    ::fprintf(f,"%s=%s%s\n",ns->name().safe(),ns->safe(),bk);
		}
	    }
	}
	::fclose(f);
	return true;
    }
    int err = errno;
    Debug(DebugWarn,"Failed to save config file '%s' (%d: %s)",
	c_str(),err,strerror(err));
    return false;
}


//
// ConfigPriv
//
static inline bool cfgBlank(char c)
{
    return c == ' ' || c == '\t';
}

static inline void cfgTrim(String& buf, const char* str, unsigned int len)
{
    if (!(str && len)) {
	buf.clear();
	return;
    }
    const char* tmp = str + len - 1;
    while (len && cfgBlank(*tmp--))
	len--;
    while (len && cfgBlank(*str)) {
	str++;
	len--;
    }
    if (str != buf.c_str() || len != buf.length())
	buf.assign(str,len);
}


// Utility: check for section or directive line
static inline int sectionLength(const String& str)
{
    if (str[0] != '[')
	return -1;
    int pos = str.find(']');
    return pos > 0 ? (pos - 1) : -2;
}

bool ConfigPriv::load(const char* file, ConfigPrivFile* parent, NamedList* section, bool silent)
{
    ConfigPrivFile f(*this,file,parent);
    XDebug(this,DebugCall,">>> load(%s) sect=(%p,'%s') depth=%u warn=%s silent=%s",
	f.fileName(),section,TelEngine::c_safe(section),f.depth(),
	String::boolText(f.warn()),String::boolText(silent));
    if (!f.open(silent,&m_includeFiles)) {
	XDebug(this,DebugCall,"<<< [false] load(%s)",f.fileName());
	return false;
    }

    NamedList* sect = section;
    bool enabled = true;
    bool paramWarnNoSect = (0 == f.depth());
    bool includeSectWarnNoSect = (0 == f.depth());
    bool ok = true;
    while (true) {
	String line;
	if (!f.readLine(line,ok))
	    break;
	if (!line)
	    continue;
	// Comment
	if (line[0] == ';')
	    continue;
	int sLen = sectionLength(line);
	if (-2 == sLen) {
	    if (m_warnings)
		Debug(this,DebugNote,"%s ignoring line '%s' crt_sect='%s'",
		    f.desc(),line.safe(),TelEngine::c_safe(sect));
	    continue;
	}
	if (sLen < 0) {
	    // Not a section
	    addParam(f,sect,line,ok,enabled,paramWarnNoSect);
	    continue;
	}
	String name;
	if (sLen)
	    cfgTrim(name,line.c_str() + 1,sLen);
	if (!name) {
	    if (m_warnings)
		Debug(this,DebugNote,"%s ignoring empty section name crt_sect='%s'",
		    f.desc(),TelEngine::c_safe(sect));
	    continue;
	}
	if (handleEnable(name,enabled)) {
	    XDebug(this,DebugInfo,"%s crt_sect='%s' enabled '%s' -> %s",
		f.desc(),TelEngine::c_safe(sect),line.c_str(),String::boolText(enabled));
	    continue;
	}
	if (!enabled)
	    continue;
	if (include(f,sect,name,ok) || includeSection(f,sect,name,ok,includeSectWarnNoSect))
	    continue;
	paramWarnNoSect = false;
	includeSectWarnNoSect = false;
	sect = addSection(f,name,sect,line,ok);
    }
    if (!f.depth())
	processIncludeSections(ok);
    XDebug(this,DebugCall,"<<< [%s] load(%s)",String::boolText(ok),f.fileName());
    return ok;
}

NamedList* ConfigPriv::addSection(ConfigPrivFile& f, String& name, NamedList* crt,
    const String& line, bool& ok)
{
    replaceParams(name);
    String error;
    if (name) {
	NamedList* sect = m_cfg.createSection(name);
	if (sect) {
	    XDebug(this,DebugInfo,"%s using section (%p,%s) crt=(%p,%s)",
		f.desc(),sect,sect->c_str(),crt,TelEngine::c_safe(crt));
	    return sect;
	}
	ok = false;
	error << "failed to add section '" << name << "'";
    }
    else if (m_warnings) {
	error = "empty section name after replace";
	int len = sectionLength(line);
	if (len > 0) {
	    error << " '";
	    error.append(line.c_str() + 1,len);
	    error << "'";
	}
    }
    if (error) {
	if (crt)
	    error << ". Resetting current '" << crt->c_str() << "'";
	Debug(this,DebugWarn,"%s %s",f.descFull(),error.c_str());
    }
    return 0;
}

static inline bool isParamCont(const String& str)
{
    return str.length() && str.c_str()[str.length() - 1] == '\\';
}

void ConfigPriv::addParam(ConfigPrivFile& f, NamedList* sect, String& line, bool& ok,
    bool enabled, bool warnNoSect)
{
    static const String s_cfgSect = "configuration";

    String key;
    int equ = line.find('=');
    if (equ > 0)
	cfgTrim(key,line,equ);
    else if (equ < 0)
	cfgTrim(key,line,line.length());
    NamedString* param = new NamedString(key);
    if (equ > 0) {
	equ++;
	if (!isParamCont(line))
	    cfgTrim(*param,line.c_str() + equ,line.length() - equ);
	else {
	    ObjList lines;
	    ObjList* add = &lines;
	    add = add->append(new String(line.c_str() + equ,line.length() - equ - 1));
	    while (true) {
		String* s = new String;
		bool done = !f.readLine(*s,ok);
		bool cont = isParamCont(*s);
		if (cont)
		    s->assign(s->c_str(),s->length() - 1);
		add = add->append(s);
		if (!cont || done)
		    break;
	    }
	    param->append(lines);
	    cfgTrim(*param,param->c_str(),param->length());
	}
    }
    if (!(enabled && key && sect)) {
	if (enabled && m_warnings) {
	    if (!key)
		Debug(this,DebugNote,"%s empty parameter name",f.desc());
	    else if (warnNoSect)
		Debug(this,DebugNote,"%s ignoring parameter '%s': no section",
		    f.desc(),param->name().safe());
	}
	TelEngine::destruct(param);
	return;
    }
    if (m_main && *sect == s_cfgSect) {
	if (param->name() == YSTRING("max_depth")) {
	    if (s_maxDepthInit) {
		s_maxDepthInit = false;
		if (s_checkRecursiveInclude)
		    s_maxDepth = param->toInteger(10,0,3,50);
		else
		    s_maxDepth = param->toInteger(3,0,3,10);
		Debug(this,DebugInfo,"max_depth set to %u",s_maxDepth);
	    }
	}
	else if (param->name() == YSTRING("disable_include_silent")) {
	    if (s_disableIncludeSilent.set(param->toBoolean()))
		Debug(this,DebugInfo,"disable_include_silent set to %s",
		    String::boolText(s_disableIncludeSilent));
	}
	else if (param->name() == YSTRING("include_empty")) {
	    if (s_includeEmpty.set(param->toBoolean()))
		Debug(this,DebugInfo,"include_empty set to %s",String::boolText(s_includeEmpty));
	}
	else if (param->name() == YSTRING("check_recursive_include")) {
	    if (s_checkRecursiveInclude.set(param->toBoolean(true)))
		Debug(this,DebugInfo,"check_recursive_include set to %s",
		    String::boolText(s_checkRecursiveInclude));
	    // No recursive check: reset max depth to lower values
	    if (!s_checkRecursiveInclude && !s_maxDepthInit) {
		s_maxDepth = m_cfg.getIntValue(s_cfgSect,"max_depth",3,3,10);
		Debug(this,DebugInfo,"max_depth set to %u",s_maxDepth);
	    }
	}
	else if (param->name() == YSTRING("warnings")) {
	    if (s_warnings.set(param->toBoolean())) {
		m_warnings = s_warnings;
		Debug(this,DebugInfo,"warnings set to %s",String::boolText(s_warnings));
	    }
	}
    }
    XDebug(this,DebugInfo,"%s addParam %s='%s'",m_cfg.c_str(),param->name().safe(),param->safe());
    sect->addParam(param);
}

bool ConfigPriv::handleEnable(String& line, bool& enabled)
{
    if (!line.startSkip("$enabled"))
	return false;
    if ((line == YSTRING("else")) || (line == YSTRING("toggle")))
	enabled = !enabled;
    else if (line.startSkip("elseif") && enabled)
	enabled = false;
    else {
	replaceParams(line);
	bool rev = line.startSkip("$not");
	if (line.startSkip("$loaded"))
	    enabled = Engine::self() && Engine::self()->pluginLoaded(line);
	else if (line.startSkip("$unloaded"))
	    enabled = !(Engine::self() && Engine::self()->pluginLoaded(line));
	else if (line.startSkip("$filled"))
	    enabled = !line.null();
	else if (line.startSkip("$empty"))
	    enabled = line.null();
	else
	    enabled = line.toBoolean(!line.startSkip("$bool"));
	if (rev)
	    enabled = !enabled;
    }
    return true;
}

bool ConfigPriv::include(ConfigPrivFile& f, NamedList* sect, String& line, bool& ok)
{
    String what;
    int inc = getInclude(line,what,s_include);
    if (!inc)
	return false;
    bool noerr = IncludeRequire != inc;
    bool silent = IncludeSilent == inc;
    if (!what && !s_includeEmpty) {
	if (m_warnings && f.warn(silent))
	    Debug(this,noerr ? DebugAll : DebugNote,
		"%s found empty value when processing [%s]",f.desc(),line.safe());
	if (!noerr)
	    ok = false;
	return true;
    }
    String path;
    if (!what.startsWith(Engine::pathSeparator())) {
	path = f.fileName();
	int sep = path.rfind(Engine::pathSeparator());
	if ('/' != *Engine::pathSeparator()) {
	    int s2 = path.rfind('/');
	    if (sep < s2)
		sep = s2;
	}
	switch (sep) {
	    case -1:
		path.clear();
		break;
	    case 0:
		path = Engine::pathSeparator();
		break;
	    default:
		path = path.substr(0,sep);
		path << Engine::pathSeparator();
	}
    }
    path << what;
    if (s_checkRecursiveInclude) {
	// Remove path separator duplicates for proper recursive check
	char s[3] = {*Engine::pathSeparator(),*Engine::pathSeparator(),0};
	for (int pos = 0, offs = 0; (pos = path.find(s,offs)) >= (int)offs; offs = pos)
	    path = path.substr(0,pos + 1) + path.substr(pos + 2);
    }
    ObjList files;
    if (File::listDirectory(path,0,&files)) {
	path << Engine::pathSeparator();
	DDebug(this,DebugAll,"%s loading up to %u files from '%s'",
	    f.desc(),files.count(),path.c_str());
	files.sort(textSort);
	while (String* it = static_cast<String*>(files.remove(false))) {
	    if (!(it->startsWith(".") || it->endsWith("~")
		|| it->endsWith(".bak") || it->endsWith(".tmp")))
		ok = (load(path + *it,&f,sect,silent) || noerr) && ok;
#ifdef XDEBUG
	    else
		Debug(this,DebugAll,"%s skipping over file '%s'",f.desc(),it->c_str());
#endif
	    TelEngine::destruct(it);
	}
    }
    else
	ok = (load(path,&f,sect,silent) || noerr) && ok;
    return true;
}

bool ConfigPriv::includeSection(ConfigPrivFile& f, NamedList* sect, String& line,
    bool& ok, bool warnNoSect)
{
    int inc = getInclude(line,line,s_includeSect,true);
    if (!inc)
	return false;
    if (sect) {
	sect->addParam("[]",line);
	if (!m_includeSections.find(sect))
	    m_includeSections.append(sect)->setDelete(false);
	XDebug(this,DebugAll,"%s section='%s' prepared '%s'",f.desc(),sect->safe(),line.safe());
    }
    else if (warnNoSect) {
	if (inc == IncludeRequire)
	    ok = false;
	if (m_warnings && f.warn(inc == IncludeSilent))
	    Debug(this,DebugNote,"%s found '%s' outside any section",f.desc(),line.safe());
    }
    return true;
}

int ConfigPriv::getInclude(String& buf, String& dest, const TokenDict* dict, bool matchOnly)
{
    if (!dict)
	return 0;
    unsigned int skip = 0;
    for (; dict->token; ++dict) {
	skip = String::c_starts_with(buf.c_str(),dict->token,buf.length());
	if (skip && cfgBlank(buf.c_str()[skip]))
	    break;
    }
    if (!dict->value || matchOnly)
	return dict->value;
    cfgTrim(dest,buf.c_str() + skip,buf.length() - skip);
    replaceParams(dest);
    return dict->value;
}

void ConfigPriv::processInclude(NamedList* sect, ObjList& stack, bool& ok)
{
    if (!sect || m_includeSectProcessed.find(sect))
	return;
    stack.append(sect)->setDelete(false);
#ifdef XDEBUG
    String tmp;
    tmp.append(stack," -> ");
    Debug(this,DebugInfo,"'%s' processing include section stack: %s",m_cfg.c_str(),tmp.safe());
#endif
    for (ObjList* o = sect->paramList()->skipNull(); o;) {
	NamedString* s = static_cast<NamedString*>(o->get());
	int inc = 0;
	String sName;
	if (s->name().length() == 2 && '[' == s->name()[0] && ']' == s->name()[1])
	    inc = getInclude(*s,sName,s_includeSect);
	if (!inc) {
	    o = o->skipNext();
	    continue;
	}
	if (sName) {
	    String error;
	    if (!stack[sName]) {
		// NOTE: We are adding current section to processed after processing it
		//       Handle already processed sections whithout checking for recursive include
		NamedList* incSect = static_cast<NamedList*>(m_includeSectProcessed[sName]);
		if (!incSect) {
		    incSect = m_cfg.getSection(sName);
		    if (incSect && incSect != sect)
			processInclude(incSect,stack,ok);
		    else
			error = incSect ? "recursive include" : "not found";
		}
		if (!error) {
		    XDebug(this,DebugAll,"'%s' including section '%s' in '%s'",
			m_cfg.c_str(),incSect->safe(),sect->safe());
		    for (ObjList* p = incSect->paramList()->skipNull(); p; p = p->skipNext()) {
			NamedString* ns = static_cast<NamedString*>(p->get());
			o->insert(new NamedString(ns->name(),*ns));
			// Update current element (replaced by insert)
			o = o->next();
		    }
		}
	    }
	    else {
		error.append(stack," -> ");
		error = "recursive include stack=" + error;
	    }
	    if (error) {
		if (inc == IncludeRequire)
		    ok = false;
		if (m_warnings && warn(inc == IncludeSilent))
		    Debug(this,DebugNote,"'%s' not including section '%s' in '%s': %s",
			m_cfg.c_str(),s->safe(),sect->safe(),error.c_str());
	    }
	}
	o->remove();
	o = o->skipNull();
	if (o)
	    continue;
	sect->paramList()->compact();
	break;
    }
    stack.remove(sect,false);
    m_includeSectProcessed.insert(sect)->setDelete(false);
}


//
// ConfigPrivFile
//
#ifdef CONFIGURATION_DBG_IO
#define ConfigDebugIO Debug
#else
#ifdef _WINDOWS
#define ConfigDebugIO do { break; } while
#else
#define ConfigDebugIO(arg...)
#endif
#endif
bool ConfigPrivFile::readLine(String& line, bool& ok)
{
#ifdef CONFIGURATION_DBG_IO
    Debugger func(&m_cfg,DebugAll,"readLine"," %s [%u] line_len=%u buffer=%u/%u",
	fileName(),m_line,line.length(),m_offset,m_length);
#endif
    bool newLine = true;
    int blanks = -1;
    while (true) {
	ConfigDebugIO(&m_cfg,DebugAll,">>> readLine loop state=%d buffer=%u/%u",m_state,m_offset,m_length);
	if (m_length) {
	    if (newLine) {
		newLine = false;
		m_line++;
	    }
	    unsigned int start = m_offset;
	    unsigned int offs = start;
	    unsigned int eoln = 0;
	    while (offs < m_length) {
		switch (m_buffer[offs]) {
		    case '\n':
			eoln = 1;
			break;
		    case '\r':
			eoln = 1;
			if ((offs + 1) < m_length && m_buffer[offs + 1] == '\n') {
			    offs++;
			    eoln = 2;
			}
			break;
		    case 0:
			// OOPS !!!
			if (m_warnNul) {
			    m_warnNul = false;
			    Debug(&m_cfg,DebugWarn,
				"%s found NUL byte, handling as end of line",desc());
			}
			eoln = 1;
			break;
		    default:
			if (blanks < 0) {
			    if (cfgBlank(m_buffer[offs])) {
				blanks--;
				start++;
			    }
			    else
				blanks = -blanks - 1;
			}
		}
		offs++;
		if (eoln)
		    break;
	    }
	    if (start < offs)
		line.append(m_buffer + start,offs - start - eoln);
	    ConfigDebugIO(&m_cfg,offs > m_length ? DebugWarn : DebugAll,
		"readLine [%u] len=%u+%u blanks=%d eoln=%u buffer=%u/%u '%s'",
		m_line,line.length() - (offs - start - eoln),(offs - start - eoln),
		blanks,eoln,offs,m_length,line.safe());
	    if (offs < m_length)
		m_offset = offs;
	    else
		m_offset = m_length = 0;
	    if (eoln || m_state < 0)
		return true;
	    // Fall through to read from file
	}
	else if (m_state < 0) {
	    // Nothing read: done. Otherwise: return true (upper layer may handle empty line)
	    if (newLine)
		break;
	    ConfigDebugIO(&m_cfg,DebugAll,"readLine [%u] returning '%s'",m_line,line.safe());
	    return true;
	}

	int rd = m_file.readData(m_buffer,sizeof(m_buffer) - 1);
	ConfigDebugIO(&m_cfg,DebugAll,"readLine read %d",rd);
	if (rd > 0) {
	    m_length = rd;
	    m_buffer[rd] = 0;
	    if (m_state > 0) {
		m_state = 0;
		const char* b = m_buffer;
		if (String::stripBOM(b)) {
		    m_offset = b - m_buffer;
		    ConfigDebugIO(&m_cfg,DebugAll,"readLine stripped BOM");
		}
	    }
	}
	else if (!rd)
	    m_state = -1;
	else {
	    m_state = -2;
	    ok = false;
	}
    }
    // Report file error now
    if (m_state == -2)
	fileError("read");
    ConfigDebugIO(&m_cfg,DebugAll,"readLine done state=%d",m_state);
    return false;
}

bool ConfigPrivFile::open(bool silent, ObjList* track)
{
    const char* err = 0;
    if (track && s_checkRecursiveInclude && (*track)[*this])
	err = "recursive";
    else if (depth() > s_maxDepth)
	err = "refusing to";
    if (err) {
	String stack;
	Debug(&m_cfg,DebugWarn,"%s %s load file at include depth %u%s",
	    fileName(),err,depth(),dumpStack(stack,track).safe());
	return false;
    }
    if (!m_file.openPath(fileName())) {
	bool report = warn(silent);
	// Silent include (and not disabled by config). Warn was requested on load
	// Force warn and we can detect the file exists (no read access ?)
	if (!report && silent && warn())
	    report = File::exists(fileName());
	return report ? fileError("open") : false;
    }
    setTrack(track);
    return true;
}

void ConfigPrivFile::setTrack(ObjList* track)
{
    if (!c_str())
	return;
    if (track) {
	m_track = track;
	m_track->append(this)->setDelete(false);
    }
    else if (m_track) {
	m_track->remove(this,false);
	m_track = 0;
    }
}

bool ConfigPrivFile::fileError(const String& oper)
{
    String e;
    Thread::errorString(e,m_file.error());
    if (!included()) {
	String extra;
	if (oper == "open" && !included())
	    extra << ", using defaults";
	Debug(&m_cfg,DebugNote,"Failed to %s file '%s'%s: %d - %s",
	    oper.safe(),cfg(),extra.safe(),m_file.error(),e.safe());
    }
    else
	Debug(&m_cfg,DebugNote,"'%s' failed to %s included file '%s': %d - %s",
	    cfg(),oper.safe(),fileName(),m_file.error(),e.safe());
    return false;
}

String& ConfigPrivFile::dumpStack(String& buf, ObjList* lst)
{
    buf.clear();
    if (!lst)
	return buf;
    for (lst = lst->skipNull(); lst; lst = lst->skipNext()) {
	ConfigPrivFile* f = static_cast<ConfigPrivFile*>(lst->get());
	buf << f->fileName() << " line=" << f->line() << "\r\n";
    }
    if (buf)
	buf = ". Stack:\r\n-----\r\n" + buf + "-----";
    return buf;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
