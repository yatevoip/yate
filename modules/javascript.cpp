/**
 * javascript.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Javascript channel support based on libyscript
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

#include <yatepbx.h>
#include <yatescript.h>
#include <yatexml.h>

#define NATIVE_TITLE "[native code]"

#define MIN_CALLBACK_INTERVAL Thread::idleMsec()

#define CALL_NATIVE_METH_STR(obj,meth) \
    if (YSTRING(#meth) == oper.name()) { \
	ExpEvaluator::pushOne(stack,new ExpOperation((obj).meth())); \
	return true; \
    }
#define CALL_NATIVE_METH_INT(obj,meth) \
    if (YSTRING(#meth) == oper.name()) { \
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)(obj).meth())); \
	return true; \
    }

#ifdef DEBUG
#define JS_DEBUG_JsMessage_received
#else
//#define JS_DEBUG_JsMessage_received
#endif
//#define JS_DEBUG_JsMessage_received_explicit

#ifdef XDEBUG
#define JS_DEBUG_SharedJsObject
#define JS_DEBUG_EVENT_NON_TIME
#define JS_DEBUG_EVENT_TIME
#else
//#define JS_DEBUG_SharedJsObject
//#define JS_DEBUG_ScriptInfo
//#define JS_DEBUG_EVENT_NON_TIME
//#define JS_DEBUG_EVENT_TIME
#endif

using namespace TelEngine;
namespace { // anonymous

// Used when needing write access to NamedList parameters
class JsNamedListWrite
{
public:
    JsNamedListWrite(ExpOperation* oper);
    inline NamedList* params()
	{ return m_params; }
    inline unsigned int setJsoParams(unsigned int ret = 0) {
	    if (m_jso && m_params == &m_jsoParams) {
		ret = m_jso->setStringFields(m_jsoParams);
		m_jsoParams.clearParams();
	    }
	    return ret;
	}

private:
    JsObject* m_jso;
    NamedList* m_params;
    NamedList m_jsoParams;
};

// Set a contructor prototype from Engine object held by running context
static JsObject* setEngineConstructorPrototype(GenObject* context, JsObject* jso,
    const String& name);


static inline ScriptContext* getScriptContext(GenObject* gen)
{
    ScriptRun* runner = YOBJECT(ScriptRun,gen);
    if (runner)
	return runner->context();
    return YOBJECT(ScriptContext,gen);
}

static inline const ExpFunction* getFunction(ExpOperation* op)
{
    ExpFunction* f = YOBJECT(ExpFunction,op);
    if (f)
	return f;
    JsFunction* jsf = YOBJECT(JsFunction,op);
    return jsf ? jsf->getFunc() : 0;
}

static inline const String& nonObjStr(const NamedString* ns)
{
    ExpWrapper* w = YOBJECT(ExpWrapper,ns);
    return w ? String::empty() : *ns;
}

// Temporary class used to store an object from received parameter or build a new one to be used
// Safely release created object
template <class Obj> class ExpOpTmpObj
{
public:
    inline ExpOpTmpObj(Obj* obj, ExpOperation& op)
	: m_obj(obj), m_del(!m_obj) 
	{ if (m_del) m_obj = new Obj(op); }
    inline ~ExpOpTmpObj()
	{ if (m_del) TelEngine::destruct(m_obj); }
    inline Obj* operator->() const
	{ return m_obj; }
    inline Obj& operator*() const
	{ return *m_obj; }
private:
    inline ExpOpTmpObj() : m_obj(0), m_del(false) {}
    Obj* m_obj;
    bool m_del;
};

#define JS_EXP_OP_TMP_OBJ(T) \
class T##TmpParam : public ExpOpTmpObj<T> \
{ public: inline T##TmpParam(ExpOperation& op) : ExpOpTmpObj(YOBJECT(T,&op),op) {} }

JS_EXP_OP_TMP_OBJ(JPath);
JS_EXP_OP_TMP_OBJ(XPath);

#undef JS_EXP_OP_TMP_OBJ

static inline void dumpTraceToMsg(Message* msg, ObjList* lst)
{
    if (!(msg && lst))
	return;
    unsigned int count = msg->getIntValue(YSTRING("trace_msg_count"),0);
    static String s_tracePref = "trace_msg_";
    for (ObjList* o = lst->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (TelEngine::null(s))
	    continue;
	msg->setParam(s_tracePref + String(count++),*s);
    }
    msg->setParam(YSTRING("trace_msg_count"),String(count));
}

class ScriptInfo;
class JsEngineWorker;
class JsEngine;

class JsModule : public ChanAssistList
{
public:
    enum {
	Preroute = AssistPrivate,
	EngStart,
    };
    JsModule();
    virtual ~JsModule();
    virtual void initialize();
    virtual void init(int priority);
    virtual ChanAssist* create(Message& msg, const String& id);
    bool unload();
    virtual bool received(Message& msg, int id);
    virtual bool received(Message& msg, int id, ChanAssist* assist);
    void msgPostExecute(const Message& msg, bool handled);
    inline JsParser& parser()
	{ return m_assistCode; }
protected:
    virtual void statusParams(String& str);
    virtual bool commandExecute(String& retVal, const String& line);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
private:
    bool evalContext(String& retVal, const String& cmd, ScriptContext* context = 0, ScriptInfo* si = 0);
    void clearPostHook();
    JsParser m_assistCode;
    MessagePostHook* m_postHook;
    bool m_started;
};

INIT_PLUGIN(JsModule);

class ScriptInfo : public ScriptRunData
{
    YCLASS(ScriptInfo,ScriptRunData)
public:
    // Script type
    enum Type {
	Unknown = 0,
	Static,                          // Global. Loaded from config
	Dynamic,                         // Global. Loaded using command
	MsgHandler,                      // Message handler: loaded from config or from static script
	Eval,                            // Loaded from eval command
	Route,                           // Assist (route) script
    };
    inline ScriptInfo(int type = 0)
	: m_type(type)
	{}
    inline ScriptInfo(const ScriptInfo& other, int type = -1)
	: m_type(type < 0 ? other.type() : type)
	{}
    inline int type() const
	{ return m_type; }
    inline const char* typeName() const
	{ return lookup(type(),s_type); }
    inline void fill(JsObject& jso) {
	    jso.setIntField("type",type());
	    jso.setStringField("type_name",typeName());
	}

    static inline void set(JsObject& jso, ScriptInfo* si) {
	    if (si)
		si->fill(jso);
	    else
		jso.setIntField("type",(int64_t)Unknown);
	}
    static inline ScriptInfo* get(GenObject* gen) {
	    if (!gen)
		return 0;
	    ScriptRun* runner = YOBJECT(ScriptRun,gen);
	    return runner ? YOBJECT(ScriptInfo,runner->userData()) : YOBJECT(ScriptInfo,gen);
	}
    static const TokenDict s_type[];

private:
    int m_type;
};

class ScriptInfoHolder
{
public:
    inline ScriptInfoHolder(ScriptInfo* si = 0, int newType = -1)
	{ setScriptInfo(si,newType); }
    inline ScriptInfo* scriptInfo() const
	{ return m_scriptInfo; }
    inline bool attachScriptInfo(GenObject* gen) {
	    if (!(gen && scriptInfo()))
		return false;
	    ScriptRun* runner = YOBJECT(ScriptRun,gen);
	    if (!runner)
		return false;
	    runner->userData(scriptInfo());
#ifdef JS_DEBUG_ScriptInfo
	    Debug(&__plugin,DebugAll,
		"ScriptInfoHolder::attachScriptInfo() runner=(%p) runner_si=(%p) our_si=(%p) [%p]",
		runner,runner->userData(),scriptInfo(),this);
#endif
	    return true;
	}
    inline void setScriptInfo(GenObject* gen, int newType = -1) {
	    ScriptInfo* si = ScriptInfo::get(gen);
	    // Build a new info ?
	    if (newType >= 0) {
		if (si)
		    si = new ScriptInfo(*si,newType);
		else
		    si = new ScriptInfo(newType);
	    }
	    m_scriptInfo = si;
	    if (newType >= 0)
		TelEngine::destruct(si);
	}

private:
    RefPointer<ScriptInfo> m_scriptInfo;
};

class JsScriptRunBuild : public GenObject
{
public:
    inline JsScriptRunBuild()
	{}
    inline JsScriptRunBuild(GenObject* ctx, const ExpFunction* func = 0,
	ExpOperVector* args = 0, unsigned int argsOffs = 0)
	{ set(ctx,func,args,argsOffs); }
    ~JsScriptRunBuild()
	{ clear(); }
    inline bool valid() const
	{ return m_context && m_code; }
    inline bool set(GenObject* ctx, const ExpFunction* func = 0,
	ExpOperVector* args = 0, unsigned int argsOffs = 0) {
	    ScriptRun* runner = YOBJECT(ScriptRun,ctx);
	    if (runner) {
		m_context = runner->context();
		m_code = runner->code();
		m_scriptInfo = YOBJECT(ScriptInfo,runner->userData());
		clearFunc();
		if (func) {
		    m_func = func->name();
		    if (args)
			m_args.takeFrom(*args,argsOffs);
		}
		if (valid())
		    return true;
	    }
	    clear();
	    return false;
	}
    inline void clear() {
	    clearFunc();
	    m_context = 0;
	}
    inline ScriptRun* createRunner() {
	    if (!m_context || m_context->terminated())
		return 0;
	    ScriptRun* runner = m_code ? m_code->createRunner(m_context,NATIVE_TITLE) : 0;
	    if (runner)
		runner->userData(m_scriptInfo);
	    return runner;
	}
    inline int callFunction(ScriptRun* runner, ObjList& args, bool fin = false) {
	    int ret = ScriptRun::Failed;
	    if (m_func && runner) {
		if (fin)
		    m_args.moveTo(args);
		else
		    m_args.cloneTo(args);
		ret = runner->call(m_func,args);
	    }
	    args.clear();
	    return ret;
	}

protected:
    inline void clearFunc() {
	    m_func.clear();
	    m_args.clear();
	}
    RefPointer<ScriptContext> m_context;
    RefPointer<ScriptCode> m_code;
    RefPointer<ScriptInfo> m_scriptInfo;
    String m_func;
    ExpOperVector m_args;
};

class JsMessage;

class JsAssist : public ChanAssist, public ScriptInfoHolder
{
public:
    enum State {
	NotStarted,
	Routing,
	ReRoute,
	Ended,
	Hangup
    };
    inline JsAssist(ChanAssistList* list, const String& id, ScriptRun* runner)
	: ChanAssist(list, id), ScriptInfoHolder(0,ScriptInfo::Route),
	  m_runner(runner), m_state(NotStarted), m_handled(false), m_repeat(false)
	{ attachScriptInfo(runner); }
    virtual ~JsAssist();
    virtual void msgStartup(Message& msg);
    virtual void msgHangup(Message& msg);
    virtual void msgExecute(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgPreroute(Message& msg);
    virtual bool msgRoute(Message& msg);
    virtual bool msgDisconnect(Message& msg, const String& reason);
    void msgPostExecute(const Message& msg, bool handled);
    bool init();
    bool evalAllocations(String& retVal, unsigned int top);
    inline State state() const
	{ return m_state; }
    inline const char* stateName() const
	{ return stateName(m_state); }
    inline void end()
	{ m_repeat = false; if (m_state < Ended) m_state = Ended; }
    inline JsMessage* message()
	{ return m_message; }
    inline void handled()
	{ m_repeat = false; m_handled = true; }
    inline ScriptContext* context()
	{ return m_runner ? m_runner->context() : 0; }
    Message* getMsg(ScriptRun* runner) const;
    static const char* stateName(State st);
private:
    bool runFunction(const String& name, Message& msg, bool* handled = 0);
    bool runScript(Message* msg, State newState);
    bool setMsg(Message* msg);
    void clearMsg(bool fromChannel);
    ScriptRun* m_runner;
    State m_state;
    bool m_handled;
    bool m_repeat;
    RefPointer<JsMessage> m_message;
};

class SharedJsObject : public RefObject
{
public:
    inline SharedJsObject(int& ok, const String& name, JsObject* jso, const String& owner,
	unsigned int flags = 0, GenObject* context = 0)
	: m_name(name), m_object(0), m_owner(owner)
	{
	    if (!(m_name && jso))
		return;
	    flags |= JsObject::AssignDeepCopy | JsObject::AssignFreezeCopy;
	    ScriptMutex* mtx = 0;
	    m_object = JsObject::copy(ok,jso,flags,0,&mtx,0,context);
#ifdef JS_DEBUG_SharedJsObject
	    String tmp;
	    //if (m_object) { JsObject::dumpRecursive(m_object,tmp); tmp = "\r\n-----\r\n" + tmp + "\r\n-----"; }
	    Debug(&__plugin,DebugAll,"SharedJsObject(%s) obj=(%p) owner='%s' result=%d [%p]%s",
		m_name.c_str(),m_object,m_owner.c_str(),ok,this,tmp.safe());
#endif
	}
    inline ~SharedJsObject()
	{ TelEngine::destruct(m_object); }
    inline const String& name() const
	{ return m_name; }
    inline JsObject* getObject() const
	{ return m_object; }
    inline const String& owner() const
	{ return m_owner; }
    inline JsObject* object(GenObject* context = 0, unsigned int line = 0) const {
	    JsObject* jso = 0;
	    if (context && m_object) {
		// Return a copy in given context if possible
		ScriptContext* ctx = getScriptContext(context);
		if (!ctx)
		    return 0;
		int ok = 0;
		ScriptMutex* mtx = ctx->mutex();
		jso = JsObject::copy(ok,m_object,JsObject::AssignDeepCopy,ctx,&mtx,line);
	    }
	    else if (m_object && m_object->ref())
		jso = m_object;
#ifdef JS_DEBUG_SharedJsObject
	    if (jso) {
		String tmp;
		//JsObject::dumpRecursive(jso,tmp,0xffffffff); tmp = "\r\n-----\r\n" + tmp + "\r\n-----";
		Debug(&__plugin,DebugAll,"SharedJsObject(%s) obj=(%p) returning object (%p) [%p]%s",
		    m_name.c_str(),m_object,jso,this,tmp.safe());
	    }
#endif
	    return jso;
	}
    virtual const String& toString() const
	{ return name(); }

protected:
    String m_name;
    JsObject* m_object;
    String m_owner;
};

class SharedObjList : public String
{
public:
    inline SharedObjList(const char* name = 0)
	: String(name ? name : "SharedObjList"), m_lock(c_str())
	{}
    inline ~SharedObjList()
	{ clear(); }
    inline bool set(SharedJsObject* jsh, bool force = true) {
	    if (!(jsh && jsh->getObject() && jsh->ref()))
		return false;
	    WLock lck(m_lock);
	    ObjList* exist = m_objects.find(jsh->name());
	    GenObject* old = 0;
	    if (!exist)
		m_objects.append(jsh);
	    else if (force)
		old = exist->set(jsh,false);
	    else {
		lck.drop();
		TelEngine::destruct(jsh);
		return false;
	    }
#ifdef JS_DEBUG_SharedJsObject
	    Debug(&__plugin,DebugAll,"SharedObjList(%s) %s %s=(%p) [%p]",
		c_str(),(exist ? "replaced" : "added"),jsh->name().c_str(),jsh,this);
#endif
	    lck.drop();
	    TelEngine::destruct(old);
	    return true;
	}
    inline JsObject* get(const String& name, GenObject* context = 0, unsigned int line = 0) {
	    if (!name)
		return 0;
	    RefPointer<SharedJsObject> jsh;
	    return find(jsh,name) ? jsh->object(context,line) : 0;
	}
    inline SharedJsObject* remove(const String& name, ObjList* owner = 0, bool delObj = true) {
	    if (!name)
		return 0;
	    ObjList* origOwner = owner;
	    SharedJsObject* jsh = 0;
	    WLock lck(m_lock);
	    if (owner) {
		for (ObjList* o = m_objects.skipNull(); o;) {
		    if (name == static_cast<SharedJsObject*>(o->get())->owner()) {
			owner = owner->append(o->remove(false));
			o = o->skipNull();
		    }
		    else
			o = o->skipNext();
		}
	    }
	    else
		jsh = static_cast<SharedJsObject*>(m_objects.remove(name,false));
	    lck.drop();
#ifdef JS_DEBUG_SharedJsObject
	    if (!origOwner)
		Debug(&__plugin,DebugAll,"SharedObjList(%s) removed %s (%p) del=%u [%p]",
		    c_str(),name.c_str(),jsh,delObj,this);
	    else if (origOwner->skipNull())
		Debug(&__plugin,DebugAll,"SharedObjList(%s) removed %u item(s) owner=%s [%p]",
		    c_str(),origOwner->count(),name.c_str(),this);
#endif
	    if (delObj) {
		TelEngine::destruct(jsh);
		if (origOwner)
		    origOwner->clear();
	    }
	    return jsh;
	}
    inline bool find(RefPointer<SharedJsObject>& found, const String& name) {
	    if (!name)
		return false;
	    RLock lck(m_lock);
	    found = static_cast<SharedJsObject*>(m_objects[name]);
	    return 0 != found;
	}
    inline void clear() {
	    ObjList tmp;
	    m_objects.move(&tmp,&m_lock);
#ifdef JS_DEBUG_SharedJsObject
	    if (tmp.skipNull())
		Debug(&__plugin,DebugAll,"SharedObjList(%s) removed %u item(s) [%p]",c_str(),tmp.count(),this);
#endif
	}

protected:
    RWLock m_lock;
    ObjList m_objects;
};

class JsGlobal;

class JsGlobalInstance : public RefObject, public ScriptInfoHolder
{
public:
    JsGlobalInstance(JsGlobal* owner, unsigned int index);
    ~JsGlobalInstance();
    unsigned int runMain();
    inline ScriptContext* context()
	{ return m_context; }
    inline void setInstanceCount(unsigned int n) {
	    if (!n || n == m_instanceCount)
		return;
	    m_instanceCount = n;
	    if (m_context) 
		m_context->setInstance(m_instance,m_instanceCount);
	}
    void scheduleInitEvent();
    const String& toString() const
	{ return m_name; }
private:
    JsGlobal* m_owner;
    RefPointer<ScriptContext> m_context;
    String m_name;
    unsigned int m_instance;
    unsigned int m_instanceCount;
    bool m_reinitEvent;
};

class JsGlobal : public NamedString, public ScriptInfoHolder
{
public:
    JsGlobal(const char* scriptName, const char* fileName, int type, bool relPath = true,
	unsigned int instances = 1);
    virtual ~JsGlobal();
    bool load();
    bool fileChanged(const char* fileName) const;
    bool updateInstances(unsigned int instances);
    inline int type() const
	{ return scriptInfo() ? scriptInfo()->type() : ScriptInfo::Unknown; }
    inline const char* typeName() const
	{ return lookup(type(),ScriptInfo::s_type); }
    inline JsParser& parser()
	{ return m_jsCode; }
    inline const String& fileName()
	{ return m_file; }
    inline unsigned int instances() const
	{ return m_instanceCount; }
    inline JsGlobalInstance* getInstance(unsigned int idx) const
    {
	String str = name();
	if (idx)
	    str << "/" << idx;
	JsGlobalInstance* inst = static_cast<JsGlobalInstance*>(m_instances[str]);
	if (inst && inst->ref())
	    return inst;
	return 0;
    }
    inline JsGlobalInstance* getInstance(const String& name) const
    {
	JsGlobalInstance* inst = static_cast<JsGlobalInstance*>(m_instances[name]);
	if (inst && inst->ref())
	    return inst;
	return 0;
    }
    bool runMain();
    static void markUnused();
    static inline void freeUnused()
	{ unload(true); }
    static void reloadDynamic();
    static bool initScript(const String& scriptName, const String& fileName, 
	int type, bool relPath = true, unsigned int instances = 0);
    static bool reloadScript(const String& scriptName);
    static void loadScripts(const NamedList* sect, const NamedList* instSect);
    static void loadHandlers(const NamedList* sect, bool handler = true);
    inline static ObjList& globals()
	{ return s_globals; }
    inline static ObjList& handlers()
	{ return s_handlers; }
    inline static ObjList& posthooks()
	{ return s_posthooks; }
    static inline void unloadAll() {
	    unload(false);
	    s_sharedObj.clear();
	}
    static void unload(bool freeUnused);

    static Mutex s_mutex;
    static bool s_keepOldOnFail;
    static SharedObjList s_sharedObj;

private:
    static bool buildNewScript(Lock& lck, ObjList* old, const String& scriptName,
	const String& fileName, int type, bool relPath, bool fromInit = false, unsigned int instances = 0);

    JsParser m_jsCode;
    bool m_inUse;
    String m_file;
    unsigned int m_instanceCount;
    ObjList m_instances;
    static ObjList s_globals;
    static ObjList s_handlers;           // Global message handlers using single script runners
    static ObjList s_posthooks;          // Global message posthooks using single script runners
};

class JsShared : public JsObject
{
    YCLASS(JsShared,JsObject)
public:
    inline JsShared(ScriptMutex* mtx)
	: JsObject("SharedVars",mtx,true)
	{
	    params().addParam(new ExpFunction("inc"));
	    params().addParam(new ExpFunction("dec"));
	    params().addParam(new ExpFunction("get"));
	    params().addParam(new ExpFunction("set"));
	    params().addParam(new ExpFunction("add"));
	    params().addParam(new ExpFunction("sub"));
	    params().addParam(new ExpFunction("create"));
	    params().addParam(new ExpFunction("clear"));
	    params().addParam(new ExpFunction("clearAll"));
	    params().addParam(new ExpFunction("exists"));
	    params().addParam(new ExpFunction("getVars"));
	    setVars(String::empty());
	}
    inline JsShared(ScriptMutex* mtx, unsigned int line, const String& varsName = String::empty())
	: JsObject(mtx,"[object SharedVars]",line)
	{ setVars(varsName); }
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    static inline uint64_t modulo(ExpOperation* mod) {
	    if (!(mod && mod->isInteger()))
		return 0;
	    int64_t m = mod->number();
	    return m > 1 ? --m : 0;
	}

protected:
    inline void setVars(const String& name) {
	    if (!name)
		m_vars = &Engine::sharedVars();
	    else
		SharedVars::getList(m_vars,name);
	    m_varsName = name;
	}
    inline JsShared(ScriptMutex* mtx, const char* name, unsigned int line, const String& varsName = String::empty())
	: JsObject(mtx,name,line)
	{ setVars(varsName); }
    virtual JsObject* clone(const char* name, const ExpOperation& oper) const
	{ return new JsShared(mutex(),name,oper.lineNumber(),m_varsName); }
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

private:
    RefPointer<SharedVars> m_vars;
    String m_varsName;
};

class JsSharedObjects : public JsObject
{
    YCLASS(JsSharedObjects,JsObject)
public:
    inline JsSharedObjects(ScriptMutex* mtx)
	: JsObject("SharedObjects",mtx,true)
	{
	    XDebug(DebugAll,"SharedObjectList() [%p]",this);
	    params().addParam(new ExpFunction("set"));
	    params().addParam(new ExpFunction("get"));
	    params().addParam(new ExpFunction("create"));
	    params().addParam(new ExpFunction("clear"));
	    params().addParam(new ExpFunction("clearAll"));
	    params().addParam(new ExpFunction("exists"));
	    params().addParam(new ExpFunction("description"));
	}
    inline JsSharedObjects(const String& owner, ScriptMutex* mtx, unsigned int line, const char* name = 0)
	: JsObject(mtx,name ? name : "[object JsSharedObjects]",line,false), m_owner(owner)
	{
	    XDebug(DebugAll,"~JsSharedObjects('%s') [%p]",TelEngine::c_safe(name),this);
	}
    virtual ~JsSharedObjects()
	{
	    XDebug(DebugAll,"~JsSharedObjects() [%p]",this);
	}
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual JsObject* clone(const char* name, const ExpOperation& oper) const
	{ return new JsSharedObjects(m_owner,mutex(),oper.lineNumber(),name); }
private:
    String m_owner;
};

class JsEvent : public RefObject
{
public:
    // Event type
    // Keep non time event in ascending order of reverse priority
    // I.e. type with less value will be sent to script before other events
    enum Type {
	EvTime = 0,
	EvReInit,
    };
    // Time event to be called on script
    inline JsEvent(unsigned int id, unsigned int interval, bool repeat,
	const ExpFunction& callback, ExpOperVector& args)
	: m_type(EvTime), m_id(id), m_repeat(repeat), m_fire(0), m_interval(interval),
	m_callback(callback.name(),1)
	{
	    m_args.takeFrom(args);
	    XDebug(&__plugin,DebugAll,"JsEvent(%u,%u,%s,%s) %d %s [%p]",
		m_id,m_interval,String::boolText(m_repeat),m_callback.name().c_str(),
		m_type,typeName(),this);
	}
    // Non time event to be called on script
    inline JsEvent(JsEvent* ev)
	: m_type(ev->type()), m_id(ev->id()), m_repeat(false), m_fire(0), m_interval(0),
	m_callback(ev->m_callback.name(),1)
	{
	    m_args.cloneFrom(ev->m_args);
	    XDebug(&__plugin,DebugAll,"JsEvent(%p) %d %s [%p]",ev,type(),typeName(),this);
	}
    // Non time event: set in a list waiting for event to occur
    inline JsEvent(unsigned int id, int type, bool repeat, const ExpFunction& callback,
	ExpOperVector& args)
	: m_type(type), m_id(id), m_repeat(repeat), m_fire(0), m_interval(0),
	m_callback(callback.name(),1)
	{
	    m_args.takeFrom(args);
	    XDebug(&__plugin,DebugAll,"JsEvent(%d,%u,%s) name=%s [%p]",
		m_id,m_type,String::boolText(m_repeat),typeName(),this);
	}
    ~JsEvent()
	{ XDebug(&__plugin,DebugAll,"~JsEvent %d %s [%p]",type(),typeName(),this); }
    inline int type() const
	{ return m_type; }
    inline bool isTimeEvent() const
	{ return EvTime == type(); }
    inline unsigned int id() const
	{ return m_id; }
    inline bool repeat() const
	{ return m_repeat; }
    inline const char* typeName() const
	{ return typeName(type()); }
    inline uint64_t fireTime() const
	{ return m_fire; }
    inline void fireTime(uint64_t now)
	{ m_fire = (now ? now : Time::msecNow()) + m_interval; }
    inline bool timeout(uint64_t& whenMs) const
	{ return whenMs >= m_fire; }
    inline void process(ScriptRun* runner) {
	    if (!runner)
		return;
	    ObjList args;
	    if (m_repeat)
		m_args.cloneTo(args);
	    else
		m_args.moveTo(args);
	    runner->call(m_callback.name(),args);
	}

    static inline ObjList* findHolder(unsigned int id, ObjList& list) {
	    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
		if (static_cast<JsEvent*>(o->get())->id() == id)
		    return o;
	    }
	    return 0;
	}
    static inline bool canRepeat(int type)
	{ return type == EvTime || type == EvReInit; }
    static inline const char* typeName(int type)
	{ return lookup(type,s_evName); }
    static const TokenDict s_evName[];

private:
    int m_type;                          // Event type
    unsigned int m_id;                   // Event ID
    bool m_repeat;                       // Repeat event
    uint64_t m_fire;                     // Time event fire time
    unsigned int m_interval;             // Time event interval
    ExpFunction m_callback;              // Callback function
    ExpOperVector m_args;                // Callback arguments
};

class JsEngineWorker : public Thread, public ScriptInfoHolder
{
public:
    JsEngineWorker(JsEngine* engine, ScriptContext* context, ScriptCode* code);
    ~JsEngineWorker();
    bool init();
    unsigned int addEvent(const ExpFunction& callback, int type, bool repeat, ExpOperVector& args,
	unsigned int interval = 0);
    bool removeEvent(unsigned int id, bool time, bool repeat);
    static void scheduleEvent(GenObject* context, int ev);

protected:
    virtual void run();
    unsigned int postponeEvent(JsEvent* ev, uint64_t now = 0);

private:
    ObjList m_events;
    ObjList m_installedEvents;           // Installed non time events
    Mutex m_eventsMutex;
    unsigned int m_id;
    ScriptRun* m_runner;
    RefPointer<JsEngine> m_engine;
};

class JsSemaphore : public JsObject
{
    YCLASS(JsSemaphore,JsObject)
public:
    inline JsSemaphore(ScriptMutex* mtx)
	: JsObject("Semaphore",mtx,true), m_constructor(0), m_exit(false)
	{
	    XDebug(DebugAll,"JsSemaphore::JsSemaphore() [%p]",this);
	    params().addParam(new ExpFunction("wait"));
	    params().addParam(new ExpFunction("signal"));
	}

    inline JsSemaphore(JsSemaphore* constructor, ScriptMutex* mtx, unsigned int line, unsigned int maxCount, unsigned int initialCount,
	    const char* name)
	: JsObject(mtx,"[object Semaphore]",line,false), m_name(name),
	  m_semaphore(maxCount,m_name.c_str(),initialCount), m_constructor(constructor), m_exit(false)
	{
	    XDebug(DebugAll,"JsSemaphore::JsSemaphore(%u,'%s',%u) [%p]",maxCount, 
		   name,initialCount,this);
	}
    virtual ~JsSemaphore()
	{
	    XDebug(DebugAll,"~JsSemaphore() [%p]",this);
	    if (m_constructor)
		m_constructor->removeSemaphore(this);
	    // Notify all the semaphores that we are exiting.
	    Lock myLock(mutex());
	    JsSemaphore* js = 0;;
	    while ((js = static_cast<JsSemaphore*>(m_semaphores.remove(false))))
		js->forceExit();
	}

    void runAsync(ObjList& stack, long maxWait);
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    void removeSemaphore(JsSemaphore* sem);
    void forceExit();
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    String m_name;
    Semaphore m_semaphore;
    JsSemaphore* m_constructor;
    ObjList m_semaphores;
    bool m_exit;
};

class JsHashList : public JsObject
{
public:
    inline JsHashList(ScriptMutex* mtx)
	: JsObject("HashList",mtx,true),
	  m_list()
	{
	    XDebug(DebugAll,"JsHashList::JsHashList() [%p]",this);
	    params().addParam(new ExpFunction("count"));
	}
    inline JsHashList(unsigned int size, ScriptMutex* mtx, unsigned int line)
	: JsObject(mtx,"[object HashList]",line,false),
	  m_list(size)
	{
	    XDebug(DebugAll,"JsHashList::JsHashList(%u) [%p]",size,this);
	}
    virtual ~JsHashList()
	{
	    XDebug(DebugAll,"~JsHashList() [%p]",this);
	    m_list.clear();
	}
    virtual void* getObject(const String& name) const;
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual void fillFieldNames(ObjList& names);
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual void clearField(const String& name)
	{ m_list.remove(name); }
    virtual const HashList* getHashListParams() const
	{ return &m_list; }

protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    HashList m_list;
};

class JsURI : public JsObject
{
public:
    inline JsURI(ScriptMutex* mtx)
	: JsObject("URI",mtx,true)
	{
	    XDebug(DebugAll,"JsURI::JsURI() [%p]",this);
	    params().addParam(new ExpFunction("getDescription"));
	    params().addParam(new ExpFunction("getProtocol"));
	    params().addParam(new ExpFunction("getUser"));
	    params().addParam(new ExpFunction("getHost"));
	    params().addParam(new ExpFunction("getPort"));
	    params().addParam(new ExpFunction("getExtra"));
	    params().addParam(new ExpFunction("getCanonical"));
	}
    inline JsURI(const char* str, ScriptMutex* mtx, unsigned int line)
	: JsObject(mtx,str,line,false),
	  m_uri(str)
	{
	    XDebug(DebugAll,"JsURI::JsURI('%s') [%p]",TelEngine::c_safe(str),this);
	}
    virtual ~JsURI()
	{
	    XDebug(DebugAll,"~JsURI() [%p]",this);
	}
    virtual void* getObject(const String& name) const;
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    URI m_uri;
};

class JsMatchingItem : public JsObject
{
    YCLASS_DATA(m_match,JsMatchingItem,JsObject)
public:
    enum BuildObjFlags {
	BuildObjForceBoolProps = MatchingItemDump::DumpPrivate,
	BuildObjForceEmptyName = MatchingItemDump::DumpPrivate << 1,
    };
    inline JsMatchingItem(ScriptMutex* mtx)
	: JsObject("MatchingItem",mtx,true), m_match(0)
	{
	    XDebug(DebugAll,"JsMatchingItem() [%p]",this);
	    params().addParam(new ExpFunction("matches"));
	    params().addParam(new ExpFunction("getDesc"));
	    params().addParam(new ExpFunction("dump"));
	    params().addParam(new ExpFunction("dumpList"));
	    params().addParam(new ExpFunction("dumpXml"));
	}
    inline JsMatchingItem(MatchingItemBase* match, ScriptMutex* mtx, unsigned int line,
	const char* name = 0)
	: JsObject(mtx,name ? name : "[object MatchingItem]",line,false), m_match(match)
	{
	    XDebug(DebugAll,"JsMatchingItem(%p) [%p]",m_match,this);
	}
    virtual ~JsMatchingItem()
	{
	    XDebug(DebugAll,"~JsMatchingItem() [%p]",this);
	    TelEngine::destruct(m_match);
	}
    inline MatchingItemBase* copyMatching(bool optimize = false) const {
	    MatchingItemBase* ret = m_match ? m_match->copy(): 0;
	    if (ret && optimize && ret->type() == MatchingItemBase::TypeList)
		return MatchingItemList::optimize(static_cast<MatchingItemList*>(ret));
	    return ret;
	}
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual void initConstructor(JsFunction* construct) {
	    construct->params().addParam(new ExpFunction("validate"));
	    construct->params().addParam(new ExpFunction("load"));
	}
    virtual JsObject* cloneForCopy(GenObject* context, ScriptMutex** mtx = 0,
	unsigned int line = 0) const
	{ return build(copyMatching(),context,mtx ? *mtx : mutex(),line); }
    // Build from args: value[,name[,params]]
    static MatchingItemBase* buildItemFromArgs(ExpOperVector& args, String* error = 0);
    // Build from object: {name:"", value:???, params:{}, ...}
    static MatchingItemBase* buildItemFromObj(GenObject* gen, uint64_t flags,
	String* error, bool allowObjValue = true);
    static MatchingItemBase* buildItem(GenObject* value, const String* name,
	const NamedList* params, uint64_t flags, String* error = 0, bool allowObjValue = true,
	const String& type = String::empty());
    static JsObject* buildJsObj(const MatchingItemBase* item,
	GenObject* context, unsigned int line, ScriptMutex* mutex, uint64_t flags = 0);
    static MatchingItemBase* buildFilter(const String& name, GenObject* value,
	GenObject* flt = 0, bool emptyValueOk = true);
    static inline JsMatchingItem* build(MatchingItemBase* mi, GenObject* context,
	ScriptMutex* mtx, unsigned int line, bool fail = true) {
	    return static_cast<JsMatchingItem*>(setEngineConstructorPrototype(context,
		new JsMatchingItem(mi,mtx,line),YSTRING("MatchingItem")));
	}

protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual JsObject* clone(const char* name, const ExpOperation& oper) const
	{ return new JsMatchingItem(copyMatching(),mutex(),oper.lineNumber(),name); }

private:
    MatchingItemBase* m_match;
};

#define MKDEBUG(lvl) params().addParam(new ExpOperation((int64_t)Debug ## lvl,"Debug" # lvl))
#define MKTIME(typ) params().addParam(new ExpOperation((int64_t)SysUsage:: typ ## Time,# typ "Time"))
#define MKDUMP(typ) params().addParam(new ExpOperation((int64_t)JsObject:: Dump ## typ,"Dump" # typ))
#define MKSCRIPTYPE(typ) params().addParam(new ExpOperation((int64_t)ScriptInfo:: typ, "ScriptType" # typ))
#define MKEVENT(typ) params().addParam(new ExpOperation((int64_t)JsEvent:: Ev ## typ,"Event" # typ))
class JsEngine : public JsObject, public DebugEnabler
{
    YCLASS(JsEngine,JsObject)
public:
    inline JsEngine(ScriptMutex* mtx, const char* name = 0)
	: JsObject("Engine",mtx,true),
	  m_worker(0), m_debugName("javascript")
	{
	    if (TelEngine::null(name))
		m_id.printf("(%p)",this);
	    else
		m_id.printf("%s(%p)",name,this);
	    debugName(m_debugName);
	    debugChain(&__plugin);
	    MKDEBUG(Fail);
	    MKDEBUG(Test);
	    MKDEBUG(Crit);
	    MKDEBUG(GoOn);
	    MKDEBUG(Conf);
	    MKDEBUG(Stub);
	    MKDEBUG(Warn);
	    MKDEBUG(Mild);
	    MKDEBUG(Note);
	    MKDEBUG(Call);
	    MKDEBUG(Info);
	    MKDEBUG(All);
	    MKTIME(Wall);
	    MKTIME(User);
	    MKTIME(Kernel);
	    MKDUMP(PropOnly),
	    MKDUMP(FuncOnly),
	    MKDUMP(Func),
	    MKDUMP(Prop),
	    MKDUMP(Recursive),
	    MKDUMP(Type),
	    MKDUMP(Proto),
	    MKDUMP(PropObjType),
	    MKEVENT(ReInit),
	    params().addParam(new ExpFunction("output"));
	    params().addParam(new ExpFunction("debug"));
	    params().addParam(new ExpFunction("traceDebug"));
	    params().addParam(new ExpFunction("trace"));
	    params().addParam(new ExpFunction("setTraceId"));
	    params().addParam(new ExpFunction("alarm"));
	    params().addParam(new ExpFunction("traceAlarm"));
	    params().addParam(new ExpFunction("lineNo"));
	    params().addParam(new ExpFunction("fileName"));
	    params().addParam(new ExpFunction("fileNo"));
	    params().addParam(new ExpFunction("creationLine"));
	    params().addParam(new ExpFunction("sleep"));
	    params().addParam(new ExpFunction("usleep"));
	    params().addParam(new ExpFunction("yield"));
	    params().addParam(new ExpFunction("idle"));
	    params().addParam(new ExpFunction("restart"));
	    params().addParam(new ExpFunction("init"));
	    params().addParam(new ExpFunction("dump_r"));
	    params().addParam(new ExpFunction("print_r"));
	    params().addParam(new ExpFunction("dump_var_r"));
	    params().addParam(new ExpFunction("print_var_r"));
	    params().addParam(new ExpFunction("dump_root_r"));
	    params().addParam(new ExpFunction("print_root_r"));
	    params().addParam(new ExpFunction("dump_t"));
	    params().addParam(new ExpFunction("print_t"));
	    params().addParam(new ExpFunction("dump_t_params"));
	    params().addParam(new ExpFunction("debugName"));
	    params().addParam(new ExpFunction("debugLevel"));
	    params().addParam(new ExpFunction("debugEnabled"));
	    params().addParam(new ExpFunction("debugAt"));
	    params().addParam(new ExpFunction("setDebug"));
	    params().addParam(new ExpFunction("uptime"));
	    params().addParam(new ExpFunction("started"));
	    params().addParam(new ExpFunction("exiting"));
	    params().addParam(new ExpFunction("accepting"));
	    params().addParam(new ExpFunction("getCongestion"));
	    params().addParam(new ExpFunction("setCongestion"));
	    if (name) {
		m_schedName << "js:" << name;
		params().addParam(new ExpOperation(name,"name"));
	    }
	    else
		m_schedName = "JsScheduler";
	    params().addParam(new ExpWrapper(new JsShared(mtx),"shared"));
	    params().addParam(new ExpFunction("runParams"));
	    params().addParam(new ExpFunction("configFile"));
	    params().addParam(new ExpFunction("setInterval"));
	    params().addParam(new ExpFunction("clearInterval"));
	    params().addParam(new ExpFunction("setTimeout"));
	    params().addParam(new ExpFunction("clearTimeout"));
	    params().addParam(new ExpFunction("setEvent"));
	    params().addParam(new ExpFunction("clearEvent"));
	    params().addParam(new ExpFunction("loadLibrary"));
	    params().addParam(new ExpFunction("loadObject"));
	    params().addParam(new ExpFunction("replaceParams"));
	    params().addParam(new ExpFunction("pluginLoaded"));
	    params().addParam(new ExpFunction("atob"));
	    params().addParam(new ExpFunction("btoa"));
	    params().addParam(new ExpFunction("atoh"));
	    params().addParam(new ExpFunction("htoa"));
	    params().addParam(new ExpFunction("btoh"));
	    params().addParam(new ExpFunction("htob"));
	    params().addParam(new ExpFunction("instanceIndex"));
	    params().addParam(new ExpFunction("instanceCount"));
	    addConstructor(params(),"Semaphore",new JsSemaphore(mtx));
	    addConstructor(params(),"HashList",new JsHashList(mtx));
	    addConstructor(params(),"URI",new JsURI(mtx));
	    addConstructor(params(),"SharedVars",new JsShared(mtx));
	    addConstructor(params(),"SharedObjects",new JsSharedObjects(mtx));
	    addConstructor(params(),"MatchingItem",new JsMatchingItem(mtx));
	    MKSCRIPTYPE(Unknown);
	    MKSCRIPTYPE(Static);
	    MKSCRIPTYPE(Dynamic);
	    MKSCRIPTYPE(MsgHandler);
	    MKSCRIPTYPE(Eval);
	    MKSCRIPTYPE(Route);
	    params().addParam(new ExpFunction("scriptType"));
	    params().addParam(new ExpFunction("scriptInfo"));
	}
    static void initialize(ScriptContext* context, const char* name = 0);
    inline void resetWorker()
	{ m_worker = 0; }
    inline const String& id() const
	{ return m_id; }
    inline JsEngineWorker* worker()
	{ return m_worker; }
    inline const String& schedName() const
	{ return m_schedName; }
    inline const String& getDebugName() const
	{ return m_debugName; }
    void setDebug(String str);
    // Retrieve JsEngine object held by running context
    // 'eng' given: unsafe, lock context, return reference
    static inline JsEngine* get(GenObject* context, RefPointer<JsEngine>* eng = 0) {
	    ScriptContext* ctx = getScriptContext(context);
	    if (!ctx)
		return 0;
	    if (!eng)
		return YOBJECT(JsEngine,ctx->params().getParam(YSTRING("Engine")));
	    Lock lck(ctx->mutex());
	    *eng = YOBJECT(JsEngine,ctx->params().getParam(YSTRING("Engine")));
	    return *eng;
	}

protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual void destroyed();
    bool setEvent(ObjList& stack, const ExpOperation& oper, GenObject* context,
	bool time, bool repeat = false);
    bool clearEvent(ObjList& stack, const ExpOperation& oper, GenObject* context,
	bool time, bool repeat = false);

private:
    JsEngineWorker* m_worker;
    String m_debugName;
    String m_schedName;
    String m_id;
};
#undef MKDEBUG
#undef MKTIME
#undef MKDUMP
#undef MKSCRIPTYPE
#undef MKEVENT

static JsObject* setEngineConstructorPrototype(GenObject* context, JsObject* jso,
    const String& name)
{
    if (!jso)
	return 0;
    if (context) {
	ScriptContext* ctx = getScriptContext(context);
	JsEngine* eng = ctx ? YOBJECT(JsEngine,ctx->params().getParam(YSTRING("Engine"))) : 0;
	if (eng)
	    jso->setPrototype(eng->params(),name);
    }
    return jso;
}


class JsMessage : public JsObject
{
public:
    inline JsMessage(ScriptMutex* mtx, bool allowSingleton = false)
	: JsObject("Message",mtx,true),
	  m_message(0), m_dispatch(false), m_owned(false), m_trackPrio(true),
	  m_traceLvl(DebugInfo), m_traceLst(0), m_allowSingleton(allowSingleton)
	{
	    XDebug(&__plugin,DebugAll,"JsMessage::JsMessage() [%p]",this);
	    params().addParam(new ExpFunction("enqueue"));
	    params().addParam(new ExpFunction("dispatch"));
	    params().addParam(new ExpFunction("name"));
	    params().addParam(new ExpFunction("broadcast"));
	    params().addParam(new ExpFunction("retValue"));
	    params().addParam(new ExpFunction("msgTime"));
	    params().addParam(new ExpFunction("msgAge"));
	    params().addParam(new ExpFunction("getParam"));
	    params().addParam(new ExpFunction("setParam"));
	    params().addParam(new ExpFunction("getColumn"));
	    params().addParam(new ExpFunction("getRow"));
	    params().addParam(new ExpFunction("getResult"));
	    params().addParam(new ExpFunction("copyParams"));
	    params().addParam(new ExpFunction("clearParam"));
	    params().addParam(new ExpFunction("trace"));
	}
    inline JsMessage(Message* message, ScriptMutex* mtx, unsigned int line, bool disp, bool owned = false)
	: JsObject(mtx,"[object Message]",line),
	  m_message(message), m_dispatch(disp), m_owned(owned), m_trackPrio(true),
	  m_traceLvl(DebugInfo), m_traceLst(0)
	{
	    XDebug(&__plugin,DebugAll,"JsMessage::JsMessage(%p) [%p]",message,this);
	    setTrace();
	}
    virtual ~JsMessage();
    virtual void* getObject(const String& name) const;
    virtual NamedList* nativeParams() const
	{ return m_message; }
    virtual void fillFieldNames(ObjList& names)
	{ if (m_message) ScriptContext::fillFieldNames(names,*m_message); }
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual void initConstructor(JsFunction* construct)
	{
	    construct->params().addParam(new ExpFunction("install"));
	    construct->params().addParam(new ExpFunction("uninstall"));
	    construct->params().addParam(new ExpFunction("handlers"));
	    construct->params().addParam(new ExpFunction("uninstallHook"));
	    construct->params().addParam(new ExpFunction("installHook"));
	    construct->params().addParam(new ExpFunction("installPostHook"));
	    construct->params().addParam(new ExpFunction("uninstallPostHook"));
	    construct->params().addParam(new ExpFunction("posthooks"));
	    construct->params().addParam(new ExpFunction("trackName"));
	    if (m_allowSingleton) {
		construct->params().addParam(new ExpFunction("installSingleton"));
		construct->params().addParam(new ExpFunction("uninstallSingleton"));
		construct->params().addParam(new ExpFunction("handlersSingleton"));
		construct->params().addParam(new ExpFunction("installPostHookSingleton"));
		construct->params().addParam(new ExpFunction("posthooksSingleton"));
	    }
	}
    inline void clearMsg()
    { 
	dumpTraceToMsg(m_message,m_traceLst);
	m_message = 0;
	m_owned = false;
	m_dispatch = false; 
	setTrace();
    }
    inline void setMsg(Message* message)
	{ m_message = message; m_owned = false; m_dispatch = false; setTrace(); }
    static void initialize(ScriptContext* context, bool allowSingleton = false);
    void runAsync(ObjList& stack, Message* msg, bool owned);
    static inline JsMessage* build(Message* message, ScriptContext* ctx, unsigned int line,
	bool disp, bool owned = false) {
	    JsMessage* jm = new JsMessage(message,ctx->mutex(),line,disp,owned);
	    jm->setPrototype(ctx,YSTRING("Message"));
	    return jm;
	}
    static inline void build(ObjList& args, Message* message, ScriptContext* ctx, unsigned int line,
	bool disp, bool owned = false)
	{ args.append(new ExpWrapper(build(message,ctx,line,disp,owned),"message")); }

protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    void getColumn(ObjList& stack, const ExpOperation* col, GenObject* context, unsigned int lineNo);
    void getRow(ObjList& stack, const ExpOperation* row, GenObject* context, unsigned int lineNo);
    void getResult(ObjList& stack, const ExpOperation& row, const ExpOperation& col, GenObject* context);
    bool install(ObjList& stack, const ExpOperation& oper, GenObject* context, bool regular);
    bool uninstall(ObjList& stack, const ExpOperation& oper, GenObject* context, bool regular);
    bool setPostHook(ObjList& stack, const ExpOperation& oper, GenObject* context, bool set, bool regular = true);
    bool listHandlers(ObjList& stack, const ExpOperation& oper, GenObject* context, bool regular,
	bool post = false);
    bool installHook(ObjList& stack, const ExpOperation& oper, GenObject* context);
    inline void setTrace()
    {
	m_traceId = m_message ? m_message->getValue(YSTRING("trace_id")) : "";
	m_traceLvl = m_message ? m_message->getIntValue(YSTRING("trace_lvl"),DebugInfo,DebugGoOn,DebugAll) : DebugInfo;
	TelEngine::destruct(m_traceLst);
	m_traceLst = m_message ? (m_message->getBoolValue(YSTRING("trace_to_msg"),false) ? new ObjList() : 0) : 0;
    }
    ObjList m_handlers;
    ObjList m_hooks;
    ObjList m_handlersSingleton;
    ObjList m_postHooks;
    String m_trackName;
    Message* m_message;
    bool m_dispatch;
    bool m_owned;
    bool m_trackPrio;
    String m_traceId;
    int m_traceLvl;
    ObjList* m_traceLst;
    bool m_allowSingleton;
};

class JsModuleMessage : public Message
{
    YCLASS(JsModuleMessage,Message)
public:
    inline JsModuleMessage(const char* name, bool broadcast = false)
	: Message(name,0,broadcast), m_dispatchedCb(0), m_accepted(0)
	{}
    virtual ~JsModuleMessage()
	{ TelEngine::destruct(m_dispatchedCb); }
    inline bool setDispatchedCallback(const ExpFunction& func, GenObject* context,
	ExpOperVector& args, unsigned int argsOffs, const NamedList* params = 0) {
	    TelEngine::destruct(m_dispatchedCb);
	    m_dispatchedCb = new JsScriptRunBuild(context,&func,&args,argsOffs);
	    if (!m_dispatchedCb->valid())
		TelEngine::destruct(m_dispatchedCb);
	    else
		m_accepted = getHandled(params);
	    return 0 != m_dispatchedCb;
	}
    static inline bool checkHandled(const Message& msg, bool handled, int cfg)
	{ return !cfg || msg.broadcast() || (cfg > 0) == handled; }
    static inline int getHandled(const NamedList* params) {
	    if (!params)
		return 0;
	    const String& tmp = (*params)[YSTRING("handled")];
	    return tmp ? (tmp.toBoolean() ? 1 : -1) : 0;
	}

protected:
    virtual void dispatched(bool accepted) {
	    JsScriptRunBuild* d = m_dispatchedCb;
	    m_dispatchedCb = 0;
	    if (!d) {
		Message::dispatched(accepted);
		return;
	    }
	    if (checkHandled(*this,accepted,m_accepted)) {
		ScriptRun* runner = d->createRunner();
		if (runner) {
		    ObjList args;
		    JsMessage::build(args,this,runner->context(),0,false);
		    args.append(new ExpOperation(accepted));
		    d->callFunction(runner,args,true);
		    TelEngine::destruct(runner);
		}
	    }
	    TelEngine::destruct(d);
	}

private:
    JsScriptRunBuild* m_dispatchedCb;    // Callback for message dispatched
    int m_accepted;                      // Handle flag 0: any, negative: not handled, positive: handled
};

class JsHandler;
class JsMessageHandle : public ScriptInfoHolder
{
public:
    enum Type {
	Regular = 0,                     // Regular handler in script
	MsgHandlerGlobal,                // Singleton context: global
	MsgHandlerScript,                // Singleton context: in script
    };
    inline JsMessageHandle(JsHandler* handler, const char* name, unsigned int priority,
	const String& func, GenObject* context, unsigned int lineNo, const NamedList* params,
	const char* id = 0)
	: ScriptInfoHolder(ScriptInfo::get(context)),
	m_type(Regular), m_function(func,handler ? 2 : 3), m_lineNo(lineNo),
	m_inUse(true), m_mutex(0), m_script(0), m_handler(handler),
	m_matchesScriptInit(false)
	{
	    if (!handler)
		m_handlerContext = m_id = id;
	    if (params)
		initialize(*params);
	    setFromContext(context);
	    m_desc << name << '=' << func;
	    m_desc.append(m_id,",");
	    trackLife(true,priority);
	}
    inline JsMessageHandle(JsHandler* handler, const String& id, const String& func,
	const String& desc, const char* name, unsigned int priority, const String& handlerContext)
	: ScriptInfoHolder(0,ScriptInfo::MsgHandler),
	m_type(MsgHandlerGlobal), m_function(func,handler ? 2 : 3), m_lineNo(0),
	m_inUse(true), m_mutex(new Mutex(false,cls(handler))), m_id(id),
	m_handlerContext(handlerContext), m_script(0), m_desc(desc), m_handler(handler),
	m_matchesScriptInit(false)
	{
	    trackLife(true,priority);
	}
    inline JsMessageHandle(JsHandler* handler, GenObject* context, const String& id,
	const String& func, const char* name, unsigned int priority,
	const String& handlerContext, unsigned int lineNo, const NamedList* params)
	: ScriptInfoHolder(0,ScriptInfo::MsgHandler),
	m_type(MsgHandlerScript), m_function(func,handler ? 2 : 3), m_lineNo(lineNo),
	m_inUse(true), m_mutex(new Mutex(false,cls(handler))), m_id(id),
	m_handlerContext(handlerContext), m_script(0), m_desc(id), m_handler(handler),
	m_matchesScriptInit(false)
	{
	    if (params)
		initialize(*params);
	    setFromContext(context);
	    if (m_code)
		m_script = new JsGlobal("","",ScriptInfo::MsgHandler);
	    trackLife(true,priority);
	}
    virtual ~JsMessageHandle()
	{
	    trackLife(false);
	    TelEngine::destruct(m_script);
	    if (m_mutex)
		delete m_mutex;
	}

    inline int type() const
	{ return m_type; }
    inline bool regular() const
	{ return type() == Regular; }
    inline JsHandler* handler() const
	{ return m_handler; }
    inline const ExpFunction& function() const
	{ return m_function; }
    inline const String& id() const
	{ return m_id; }
    inline const String& handlerContext() const
	{ return m_handlerContext; }
    inline bool inUse() const
	{ return m_inUse; }
    inline void setInUse(bool on)
	{ m_inUse = on; }
    inline const char* desc() const
	{ return m_desc; }
    inline void fillInfo(String& buf) {
	    buf << m_desc;
	    Lock lck(m_mutex);
	    if (m_script)
		buf << " - " << *m_script;
	}
    bool initialize(const NamedList& params, const String& scriptName = String::empty(),
	const String& scriptFile = String::empty(), const String& prefix = String::empty());
    void prepare(GenObject* name, GenObject* value, const NamedList* params = 0,
	GenObject* msgName = 0, const String& trackName = String::empty(), bool trackPrio = true);

    static bool install(GenObject* gen);
    static bool uninstall(GenObject* gen);
    static inline void uninstall(ObjList& list, const String& id) {
	    ObjList* o = findId(id,list);
	    if (o)
		uninstall(o->remove(false));
	}
    static inline void uninstall(ObjList& list) {
	    for (ObjList* o = list.skipNull(); o; o = o->skipNull())
		uninstall(o->remove(false));
	}
    static ObjList* findId(const String& id, ObjList& list);
    static inline const char* cls(bool handler)
	{ return handler ? "JsHandler" : "JsPostHook"; }
    static inline const char* clsType(bool handler)
	{ return handler ? "handler" : "posthook"; }

protected:
    inline void setFromContext(GenObject* context) {
	    ScriptRun* runner = YOBJECT(ScriptRun,context);
	    if (!runner)
		return;
	    m_context = runner->context();
	    m_code = runner->code();
	}
    bool handle(Message& msg, bool handled = false);

private:
    inline void trackLife(bool create = true, unsigned int prio = 0) {
#ifdef XDEBUG
	    String extra;
	    if (create) {
		if (m_handler)
		    extra << " priority=" << prio;
	    }
	    Debug(&__plugin,DebugAll,"%s type=%d %s%s %s [%p]",
		cls(handler()),type(),desc(),extra.safe(),create ? "created" : "destroyed",this);
#endif
	}
    int m_type;
    ExpFunction m_function;
    RefPointer<ScriptContext> m_context;
    RefPointer<ScriptCode> m_code;
    unsigned int m_lineNo;
    bool m_inUse;                        // Handler is still present in config
    Mutex* m_mutex;                      // Protect data
    String m_id;                         // Handler id (used to check if present)
    String m_loadExt;                    // Load extensions setup
    String m_debug;                      // Configured debug
    String m_handlerContext;             // Context to be passed to script
    JsGlobal* m_script;                  // Parsed script
    String m_desc;
    JsHandler* m_handler;                // Pointer to derived message handler
    bool m_matchesScriptInit;            // Handler matches script.init
};

class JsHandler : public MessageHandler, public JsMessageHandle
{
    YCLASS(JsHandler,MessageHandler)
    friend class JsMessageHandle;
public:
    inline JsHandler(const char* name, unsigned priority, const String& func,
	GenObject* context, unsigned int lineNo, const NamedList* params)
	: MessageHandler(name,priority,__plugin.name()),
	JsMessageHandle(this,name,priority,func,context,lineNo,params)
	{}
    inline JsHandler(const String& id, const String& func, const String& desc,
	const char* name, unsigned int priority, const String& handlerContext)
	: MessageHandler(name,priority,__plugin.name()),
	JsMessageHandle(this,id,func,desc,name,priority,handlerContext)
	{}
    inline JsHandler(GenObject* context, const String& id, const String& func,
	const char* name, unsigned int priority,
	const String& handlerContext, unsigned int lineNo, const NamedList* params)
	: MessageHandler(name,priority,__plugin.name()),
	JsMessageHandle(this,context,id,func,name,priority,handlerContext,lineNo,params)
	{}
    virtual bool received(Message& msg)
	{ return false; }

protected:
    virtual bool receivedInternal(Message& msg)
	{ return handle(msg); }
};

class JsPostHook : public MessagePostHook, public JsMessageHandle
{
    YCLASS(JsPostHook,MessagePostHook)
public:
    inline JsPostHook(const String& func, const String& id,
	GenObject* context, unsigned int lineNo, const NamedList* params)
	: JsMessageHandle(0,(const char*)0,(unsigned int)0,func,context,lineNo,params,id),
	m_handled(JsModuleMessage::getHandled(params))
	{}
    inline JsPostHook(const String& id, const String& func, const String& desc,
	const String& handlerContext, const NamedList& params)
	: JsMessageHandle(0,id,func,desc,(const char*)0,(unsigned int)0,handlerContext),
	m_handled(JsModuleMessage::getHandled(&params))
	{}
    inline JsPostHook(GenObject* context, const String& id, const String& func,
	const String& handlerContext, unsigned int lineNo, const NamedList* params)
	: JsMessageHandle(0,context,id,func,(const char*)0,(unsigned int)0,
	    handlerContext,lineNo,params),
	m_handled(JsModuleMessage::getHandled(params))
	{}
    virtual void dispatched(const Message& msg, bool handled) {
	    if (JsModuleMessage::checkHandled(msg,handled,m_handled))
		handle((Message&)msg,handled);
	}
    inline int handled() const
	{ return m_handled; }

private:
    int m_handled;                       // Handle flag 0: any, negative: not handled, positive: handled
};

class JsMessageQueue : public MessageQueue, public ScriptInfoHolder
{
    YCLASS(JsMessageQueue,MessageQueue)
public:
    inline JsMessageQueue(unsigned int line, const ExpFunction* received,const char* name, unsigned threads, const ExpFunction* trap, unsigned trapLunch, GenObject* context)
	: MessageQueue(name,threads), m_lineNo(line), m_receivedFunction(0), m_trapFunction(0), m_trapLunch(trapLunch), m_trapCalled(false)
	{
	    ScriptRun* runner = YOBJECT(ScriptRun,context);
	    if (runner) {
		m_context = runner->context();
		m_code = runner->code();
	    }
	    if (received)
		m_receivedFunction = new ExpFunction(received->name(),1);
	    if (trap)
		m_trapFunction = new ExpFunction(trap->name(),0);
	}
    virtual ~JsMessageQueue()
    {
	TelEngine::destruct(m_receivedFunction);
	TelEngine::destruct(m_trapFunction);
    }
    virtual bool enqueue(Message* msg);
    bool matchesFilters(const NamedList& filters);
protected:
    virtual void received(Message& msg);
private:
    unsigned int m_lineNo;
    ExpFunction* m_receivedFunction;
    ExpFunction* m_trapFunction;
    RefPointer<ScriptContext> m_context;
    RefPointer<ScriptCode> m_code;
    unsigned int m_trapLunch;
    bool m_trapCalled;
};

class JsFile : public JsObject
{
    YCLASS(JsFile,JsObject)
public:
    inline JsFile(ScriptMutex* mtx)
	: JsObject("File",mtx,true)
	{
	    XDebug(DebugAll,"JsFile::JsFile() [%p]",this);
	    params().addParam(new ExpFunction("exists"));
	    params().addParam(new ExpFunction("remove"));
	    params().addParam(new ExpFunction("rename"));
	    params().addParam(new ExpFunction("mkdir"));
	    params().addParam(new ExpFunction("rmdir"));
	    params().addParam(new ExpFunction("getFileTime"));
	    params().addParam(new ExpFunction("setFileTime"));
	    params().addParam(new ExpFunction("getContent"));
	    params().addParam(new ExpFunction("setContent"));
	    params().addParam(new ExpFunction("listDirectory"));
	}
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

class JsConfigSection;

class JsConfigFile : public JsObject
{
public:
    inline JsConfigFile(ScriptMutex* mtx)
	: JsObject("ConfigFile",mtx,true)
	{
	    XDebug(DebugAll,"JsConfigFile::JsConfigFile() [%p]",this);
	    params().addParam(new ExpFunction("name"));
	    params().addParam(new ExpFunction("load"));
	    params().addParam(new ExpFunction("save"));
	    params().addParam(new ExpFunction("count"));
	    params().addParam(new ExpFunction("sections"));
	    params().addParam(new ExpFunction("getSection"));
	    params().addParam(new ExpFunction("getValue"));
	    params().addParam(new ExpFunction("getIntValue"));
	    params().addParam(new ExpFunction("getBoolValue"));
	    params().addParam(new ExpFunction("setValue"));
	    params().addParam(new ExpFunction("addValue"));
	    params().addParam(new ExpFunction("setValues"));
	    params().addParam(new ExpFunction("addValues"));
	    params().addParam(new ExpFunction("clearSection"));
	    params().addParam(new ExpFunction("clearKey"));
	    params().addParam(new ExpFunction("keys"));
	}
    inline JsConfigFile(ScriptMutex* mtx, unsigned int line, const char* name, bool warn)
	: JsObject(mtx,"[object ConfigFile]",line),
	  m_config(name,warn)
	{
	    XDebug(DebugAll,"JsConfigFile::JsConfigFile('%s') [%p]",TelEngine::c_safe(name),this);
	}
    virtual void* getObject(const String& name) const;
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    static void initialize(ScriptContext* context);
    inline Configuration& config()
	{ return m_config; }
    inline const Configuration& config() const
	{ return m_config; }
    bool runFuncKeys(ObjList& stack, const ExpOperation& oper, GenObject* context,
	JsConfigSection* jSect = 0);

protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    Configuration m_config;
};

class JsConfigSection : public JsObject
{
    friend class JsConfigFile;
    YCLASS(JsConfigSection,JsObject)
public:
    inline NamedList* section()
	{ return m_owner->config().getSection(toString()); }
    virtual NamedList* nativeParams() const
	{ return m_owner->config().getSection(toString()); }

protected:
    inline JsConfigSection(JsConfigFile* owner, const char* name, unsigned int lineNo)
	: JsObject(owner->mutex(),name,lineNo,true),
	  m_owner(owner)
	{
	    XDebug(DebugAll,"JsConfigSection::JsConfigSection(%p,'%s') [%p]",owner,name,this);
	    params().addParam(new ExpFunction("configFile"));
	    params().addParam(new ExpFunction("getValue"));
	    params().addParam(new ExpFunction("getIntValue"));
	    params().addParam(new ExpFunction("getBoolValue"));
	    params().addParam(new ExpFunction("setValue"));
	    params().addParam(new ExpFunction("addValue"));
	    params().addParam(new ExpFunction("setValues"));
	    params().addParam(new ExpFunction("addValues"));
	    params().addParam(new ExpFunction("clearKey"));
	    params().addParam(new ExpFunction("keys"));
	}
    virtual bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    RefPointer<JsConfigFile> m_owner;
};

/**
 * XML path class
 * @short Javascript XML path
 */
class JsXPath : public JsObject
{
public:
    inline JsXPath(ScriptMutex* mtx)
	: JsObject("XPath",mtx,true),
	m_path(0,XPath::LateParse)
	{
	    XDebug(DebugAll,"JsXPath::JsXPath() [%p]",this);
	    params().addParam(new ExpFunction("valid"));
	    params().addParam(new ExpFunction("absolute"));
	    params().addParam(new ExpFunction("getPath"));
	    params().addParam(new ExpFunction("getItems"));
	    params().addParam(new ExpFunction("getError"));
	    params().addParam(new ExpFunction("describeError"));
	}
    inline JsXPath(ScriptMutex* mtx, const char* name, unsigned int line, const XPath& path)
	: JsObject(mtx,name,line),
	  m_path(path)
	{}
    virtual void* getObject(const String& name) const;
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual void initConstructor(JsFunction* construct)
	{
	    // Find contents
	    construct->params().addParam(new ExpOperation((int64_t)XPath::FindXml,"FindXml"));
	    construct->params().addParam(new ExpOperation((int64_t)XPath::FindText,"FindText"));
	    construct->params().addParam(new ExpOperation((int64_t)XPath::FindAttr,"FindAttr"));
	    construct->params().addParam(new ExpOperation((int64_t)XPath::FindAny,"FindAny"));
	    // XPath flags
	    construct->params().addParam(new ExpOperation((int64_t)XPath::StrictParse,"StrictParse"));
	    construct->params().addParam(new ExpOperation((int64_t)XPath::IgnoreEmptyResult,"IgnoreEmptyResult"));
	    construct->params().addParam(new ExpOperation((int64_t)XPath::NoXmlNameCheck,"NoXmlNameCheck"));
	    // Static functions
	    construct->params().addParam(new ExpFunction("escapeString"));
	}
    virtual const XPath& path() const
	{ return m_path; }
    virtual const String& toString() const
	{ return m_path; }
    static void initialize(ScriptContext* context);

protected:
    inline JsXPath(ScriptMutex* mtx, unsigned int line, const char* path, unsigned int flags = 0)
	: JsObject(mtx,path,line),
	  m_path(path,flags)
	{}
    virtual JsObject* clone(const char* name, const ExpOperation& oper) const
	{ return new JsXPath(mutex(),name,oper.lineNumber(),m_path); }
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

private:
    XPath m_path;
};

class JsXML : public JsObject
{
public:
    inline JsXML(ScriptMutex* mtx)
	: JsObject("XML",mtx,true),
	  m_xml(0)
	{
	    XDebug(DebugAll,"JsXML::JsXML() [%p]",this);
	    params().addParam(new ExpFunction("put"));
	    params().addParam(new ExpFunction("getOwner"));
	    params().addParam(new ExpFunction("getParent"));
	    params().addParam(new ExpFunction("unprefixedTag"));
	    params().addParam(new ExpFunction("getTag"));
	    params().addParam(new ExpFunction("getAttribute"));
	    params().addParam(new ExpFunction("setAttribute"));
	    params().addParam(new ExpFunction("removeAttribute"));
	    params().addParam(new ExpFunction("attributes"));
	    params().addParam(new ExpFunction("addChild"));
	    params().addParam(new ExpFunction("getChild"));
	    params().addParam(new ExpFunction("getChildren"));
	    params().addParam(new ExpFunction("clearChildren"));
	    params().addParam(new ExpFunction("addText"));
	    params().addParam(new ExpFunction("getText"));
	    params().addParam(new ExpFunction("setText"));
	    params().addParam(new ExpFunction("compactText"));
	    params().addParam(new ExpFunction("getChildText"));
	    params().addParam(new ExpFunction("getChildByPath"));
	    params().addParam(new ExpFunction("getChildrenByPath"));
	    params().addParam(new ExpFunction("getTextByPath"));
	    params().addParam(new ExpFunction("getAnyByPath"));
	    params().addParam(new ExpFunction("xmlText"));
	    params().addParam(new ExpFunction("replaceParams"));
	    params().addParam(new ExpFunction("saveFile"));
	}
    inline JsXML(ScriptMutex* mtx, unsigned int line, XmlElement* xml, JsXML* owner = 0)
	: JsObject(mtx,"[object XML]",line,false),
	  m_xml(xml), m_owner(owner)
	{
	    XDebug(DebugAll,"JsXML::JsXML(%p,%p) [%p]",xml,owner,this);
	    if (owner) {
		JsObject* proto = YOBJECT(JsObject,owner->params().getParam(protoName()));
		if (proto && proto->ref())
		    params().addParam(new ExpWrapper(proto,protoName()));
	    }
	}
    virtual ~JsXML()
	{
	    if (m_owner) {
		m_xml = 0;
		m_owner = 0;
	    }
	    else
		TelEngine::destruct(m_xml);
	}
    virtual void* getObject(const String& name) const;
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    virtual void initConstructor(JsFunction* construct)
	{
	    construct->params().addParam(new ExpOperation((int64_t)0,"PutObject"));
	    construct->params().addParam(new ExpOperation((int64_t)1,"PutText"));
	    construct->params().addParam(new ExpOperation((int64_t)2,"PutBoth"));
	    construct->params().addParam(new ExpFunction("loadFile"));
	}
    inline JsXML* owner()
	{ return m_owner ? (JsXML*)m_owner : this; }
    inline const XmlElement* element() const
	{ return m_xml; }
    virtual JsObject* cloneForCopy(GenObject* context, ScriptMutex** mtx = 0,
	unsigned int line = 0) const
	{ return build(m_xml ? new XmlElement(*m_xml) : 0,context,mtx ? *mtx : mutex(),line); }
    static void initialize(ScriptContext* context);
    static inline JsXML* build(XmlElement* xml, GenObject* context,
	ScriptMutex* mtx, unsigned int line) {
	    return static_cast<JsXML*>(setEngineConstructorPrototype(context,
		new JsXML(mtx,line,xml),YSTRING("XML")));
	}

protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    inline ExpWrapper* xmlWrapper(const ExpOperation& oper, const XmlElement* xml)
	{ return new ExpWrapper(new JsXML(mutex(),oper.lineNumber(),(XmlElement*)xml,owner())); }
    inline ExpOperation* buildAny(const GenObject* gen, const ExpOperation& oper, GenObject* context) {
	    if (!gen)
		return 0;
	    GenObject* g = (GenObject*)gen;
	    XmlElement* xml = YOBJECT(XmlElement,g);
	    if (xml)
		return xmlWrapper(oper,xml);
	    // Attribute ?
	    NamedString* ns = YOBJECT(NamedString,g);
	    if (!ns)
		return new ExpOperation(g->toString().safe(),"text");
	    JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
	    jso->setStringField("name",ns->name());
	    jso->setStringField("value",*ns);
	    return new ExpWrapper(jso,"attribute");
	}
    static XmlElement* getXml(const String* obj, bool take);
    static XmlElement* buildXml(const String* name, const String* text = 0);
    XmlElement* m_xml;
    RefPointer<JsXML> m_owner;
};

class JsHasher : public JsObject
{
    YCLASS(JsHasher,JsObject)
public:
    inline JsHasher(ScriptMutex* mtx)
	: JsObject("Hasher",mtx,true),
	  m_hasher(0)
	{
	    XDebug(DebugAll,"JsHasher::JsHasher() [%p]",this);
	    params().addParam(new ExpFunction("update"));
	    params().addParam(new ExpFunction("hmac"));
	    params().addParam(new ExpFunction("hexDigest"));
	    params().addParam(new ExpFunction("clear"));
	    params().addParam(new ExpFunction("finalize"));
	    params().addParam(new ExpFunction("hashLength"));
	    params().addParam(new ExpFunction("hmacBlockSize"));
	}
    inline JsHasher(GenObject* context, ScriptMutex* mtx, unsigned int line, Hasher* h)
	: JsObject(mtx,"[object Hasher]",line,false),
	  m_hasher(h)
	{
	    XDebug(DebugAll,"JsHasher::JsHasher(%p) [%p]",m_hasher,this);
	    setPrototype(context,YSTRING("Hasher"));
	}
    virtual ~JsHasher()
	{
	    if (m_hasher) {
		delete m_hasher;
		m_hasher = 0;
	    }
	}
    virtual void initConstructor(JsFunction* construct)
	{
	    construct->params().addParam(new ExpFunction("fips186prf"));
	}
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
private:
    Hasher* m_hasher;
};

class JsJSON : public JsObject
{
    YCLASS(JsJSON,JsObject)
public:
    inline JsJSON(ScriptMutex* mtx)
	: JsObject("JSON",mtx,true)
	{
	    params().addParam(new ExpFunction("parse"));
	    params().addParam(new ExpFunction("stringify"));
	    params().addParam(new ExpFunction("loadFile"));
	    params().addParam(new ExpFunction("saveFile"));
	    params().addParam(new ExpFunction("replaceParams"));
	    params().addParam(new ExpFunction("replaceReferences"));
	    params().addParam(new ExpFunction("findPath"));
	}
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    void replaceParams(GenObject* obj, const NamedList& params, bool sqlEsc, char extraEsc);
};

class JsDNS : public JsObject
{
    YCLASS(JsDNS,JsObject)
public:
    inline JsDNS(ScriptMutex* mtx)
	: JsObject("DNS",mtx,true)
	{
	    params().addParam(new ExpFunction("query"));
	    params().addParam(new ExpFunction("queryA"));
	    params().addParam(new ExpFunction("queryAaaa"));
	    params().addParam(new ExpFunction("queryNaptr"));
	    params().addParam(new ExpFunction("querySrv"));
	    params().addParam(new ExpFunction("queryTxt"));
	    params().addParam(new ExpFunction("resolve"));
	    params().addParam(new ExpFunction("local"));
	    params().addParam(new ExpFunction("pack"));
	    params().addParam(new ExpFunction("unpack"));
	    params().addParam(new ExpFunction("dscp"));
	}
    static void initialize(ScriptContext* context);
    void runQuery(ObjList& stack, const String& name, Resolver::Type type, GenObject* context, unsigned int lineNo);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

class JsChannel : public JsObject
{
    YCLASS(JsChannel,JsObject)
public:
    inline JsChannel(JsAssist* assist, ScriptMutex* mtx)
	: JsObject("Channel",mtx,false), m_assist(assist)
	{
	    params().addParam(new ExpFunction("id"));
	    params().addParam(new ExpFunction("peerid"));
	    params().addParam(new ExpFunction("status"));
	    params().addParam(new ExpFunction("direction"));
	    params().addParam(new ExpFunction("answered"));
	    params().addParam(new ExpFunction("answer"));
	    params().addParam(new ExpFunction("hangup"));
	    params().addParam(new ExpFunction("callTo"));
	    params().addParam(new ExpFunction("callJust"));
	    params().addParam(new ExpFunction("playFile"));
	    params().addParam(new ExpFunction("recFile"));
	}
    static void initialize(ScriptContext* context, JsAssist* assist);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    void callToRoute(ObjList& stack, const ExpOperation& oper, GenObject* context, const NamedList* params);
    void callToReRoute(ObjList& stack, const ExpOperation& oper, GenObject* context, const NamedList* params);
    JsAssist* m_assist;
};

class JsEngAsync : public ScriptAsync
{
    YCLASS(JsEngAsync,ScriptAsync)
public:
    enum Oper {
	AsyncSleep,
	AsyncUsleep,
	AsyncYield,
	AsyncIdle
    };
    inline JsEngAsync(ScriptRun* runner, Oper op, int64_t val = 0)
	: ScriptAsync(runner),
	  m_oper(op), m_val(val)
	{ XDebug(DebugAll,"JsEngAsync %d " FMT64,op,val); }
    virtual bool run();
private:
    Oper m_oper;
    int64_t m_val;
};

class JsMsgAsync : public ScriptAsync
{
    YCLASS(JsMsgAsync,ScriptAsync)
public:
    inline JsMsgAsync(ScriptRun* runner, ObjList* stack, JsMessage* jsMsg, Message* msg, bool owned)
	: ScriptAsync(runner),
	  m_stack(stack), m_msg(jsMsg), m_message(msg), m_owned(owned)
	{ XDebug(DebugAll,"JsMsgAsync"); }
    virtual bool run()
	{ m_msg->runAsync(*m_stack,m_message,m_owned); return true; }
private:
    ObjList* m_stack;
    RefPointer<JsMessage> m_msg;
    Message* m_message;
    bool m_owned;
};

class JsSemaphoreAsync : public ScriptAsync
{
    YCLASS(JsSemaphoreAsync,ScriptAsync)
public:
    inline JsSemaphoreAsync(ScriptRun* runner, ObjList* stack, JsSemaphore* se, long wait)
	: ScriptAsync(runner), m_stack(stack), m_semaphore(se), m_wait(wait)
	{ XDebug(DebugAll,"JsSemaphoreAsync(%p, %ld) [%p]",se,wait,this); }

    virtual ~JsSemaphoreAsync()
	{ XDebug(DebugAll,"~JsSemaphoreAsync() [%p]",this); }
    virtual bool run()
    {
	if (m_wait > 0)
	    m_wait *= 1000;
	m_semaphore->runAsync(*m_stack, m_wait);
	return true;
    }
private:
    ObjList* m_stack;
    RefPointer<JsSemaphore> m_semaphore;
    long m_wait;
};

class JsDnsAsync : public ScriptAsync
{
    YCLASS(JsDnsAsync,ScriptAsync)
public:
    inline JsDnsAsync(ScriptRun* runner, JsDNS* jsDns, ObjList* stack,
	const String& name, Resolver::Type type, GenObject* context, unsigned int lineNo)
	: ScriptAsync(runner),
	  m_stack(stack), m_name(name), m_type(type), m_context(context), m_dns(jsDns), m_lineNo(lineNo)
	{ XDebug(DebugAll,"JsDnsAsync"); }
    virtual bool run()
	{ m_dns->runQuery(*m_stack,m_name,m_type,m_context,m_lineNo); return true; }
private:
    ObjList* m_stack;
    String m_name;
    Resolver::Type m_type;
    GenObject* m_context;
    RefPointer<JsDNS> m_dns;
    unsigned int m_lineNo;
};

class JsPostExecute : public MessagePostHook
{
public:
    virtual void dispatched(const Message& msg, bool handled)
	{ if (msg == YSTRING("call.execute")) __plugin.msgPostExecute(msg,handled); }
};

static String s_basePath;
static String s_libsPath;
static bool s_engineStop = false;
static bool s_allowAbort = false;
static bool s_allowTrace = false;
static bool s_allowLink = true;
static bool s_trackObj = false;
static unsigned int s_trackCreation = 0;
static bool s_autoExt = true;
static unsigned int s_maxFile = 500000;

const TokenDict ScriptInfo::s_type[] = {
    {"static",  Static},
    {"dynamic", Dynamic},
    {"handler", MsgHandler},
    {"eval",    Eval},
    {"route",   Route},
    {0,0}
};

const TokenDict JsEvent::s_evName[] = {
    {"time",   EvTime},
    {"reinit", EvReInit},
    {0,0}
};

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow) {
	s_engineStop = true;
	JsGlobal::unloadAll();
	return __plugin.unload();
    }
    return true;
}

// Load extensions in a script context
static bool contextLoad(ScriptContext* ctx, const char* name, const char* libs = 0, const char* objs = 0)
{
    if (!ctx)
	return false;
    bool start = !(libs || objs);
    Message msg("script.init",0,start);
    msg.userData(ctx);
    msg.addParam("module",__plugin.name());
    msg.addParam("language","javascript");
    msg.addParam("startup",String::boolText(start));
    if (name)
	msg.addParam("instance",name);
    if (libs)
	msg.addParam("libraries",libs);
    if (objs)
	msg.addParam("objects",objs);
    return Engine::dispatch(msg);
}

// Load extensions in a script runner context
static bool contextLoad(ScriptRun* runner, const char* name, const char* libs = 0, const char* objs = 0)
{
    return runner && contextLoad(runner->context(),name,libs,objs);
}

// Initialize a script context, populate global objects
static void contextInit(ScriptRun* runner, const char* name = 0, bool autoExt = s_autoExt, JsAssist* assist = 0)
{
    if (!runner)
	return;
    ScriptContext* ctx = runner->context();
    if (!ctx)
	return;
    ScriptInfo* si = ScriptInfo::get(runner);
#ifdef JS_DEBUG_ScriptInfo
    Debug(&__plugin,DebugAll,"contextInit runner=(%p) scriptInfo: %d (%p)",
	runner,si ? si->type() : ScriptInfo::Unknown,si);
#endif
    JsObject::initialize(ctx);
    JsEngine::initialize(ctx,name);
    if (assist)
	JsChannel::initialize(ctx,assist);
    // Allow installing singleton handlers for static/dynamic scripts
    // Allow it for the first instance only only if multiple instances are not used or called in the first instance
    // This would avoid installing useless handlers with same code
    bool allowSingleton = ctx->instanceIndex() < 2 && si
	&& (si->type() == ScriptInfo::Static || si->type() == ScriptInfo::Dynamic);
    JsMessage::initialize(ctx,allowSingleton);
    JsFile::initialize(ctx);
    JsConfigFile::initialize(ctx);
    JsXML::initialize(ctx);
    JsHasher::initialize(ctx);
    JsJSON::initialize(ctx);
    JsDNS::initialize(ctx);
    JsXPath::initialize(ctx);
    if (autoExt)
	contextLoad(ctx,name);
}

// sort list of object allocations descending
static int counterSort(GenObject* obj1, GenObject* obj2, void* context)
{
    const NamedCounter* s1 = static_cast<NamedCounter*>(obj1);
    const NamedCounter* s2 = static_cast<NamedCounter*>(obj2);
    int c1 = s1 ? s1->count() : 0;
    int c2 = s2 ? s2->count() : 0;
    return c1 < c2 ? 1 : (c1 > c2 ? -1 : 0);
}

// Sort and dump a list of object counters
static void dumpAllocations(String& out, ObjList* counters, unsigned int count, ScriptCode* code)
{
    if (!(counters && code))
	return;
    counters->sort(counterSort);

    unsigned int i = 0;
    for (ObjList* o = counters->skipNull(); o && i < count; o = o->skipNext(), i++) {
	NamedCounter* c = static_cast<NamedCounter*>(o->get());
	uint64_t line = c->toString().toUInt64();
	String fn;
	unsigned int fl = 0;
	code->getFileLine(line,fn,fl,false);
	out << "\r\n" << fn << ":" << fl << " " << c->count();
    }
}

// Obtain a string with top object allocations from context ctx
static bool evalCtxtAllocations(String& retVal, unsigned int count, ScriptContext* ctx,
		ScriptCode* code, const String& scrName)
{
    if (!(ctx && code)) {
	retVal << "Script '" << scrName << "' has no associated context\r\n";
	return true;
    }
    ObjList* objCounters = ctx->countAllocations();
    if (!objCounters) {
	retVal << "Script '" << scrName << "' has no active object tracking\r\n";
	return true;
    }
    String tmp;
    dumpAllocations(tmp,objCounters,count,code);
    if (!tmp)
	retVal << "Script '" << scrName << "' has no active object tracking counters\r\n";
    else
	retVal << "Top " << count << " object allocations for '" << scrName <<"':" << tmp << "\r\n";
    TelEngine::destruct(objCounters);
    return true;
}

// Obtain a string with top object allocations from all script instances contexts 
static bool evalInstanceAllocations(String& retVal, unsigned int count, ObjList& list,
		ScriptCode* code, const String& scrName)
{
    if (!code) {
	retVal << "Script '" << scrName << "' has no associated code\r\n";
	return true;
    }
    ObjList objCounters;

    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
	ObjList* l = static_cast<ObjList*>(o->get());
	for (ObjList* j = l->skipNull(); j; j = j->skipNext()) {
	    NamedCounter* nInt = static_cast<NamedCounter*>(j->get());
	    NamedCounter* total = static_cast<NamedCounter*>(objCounters[nInt->toString()]);
	    if (total)
		total->add(nInt->count());
	    else {
		j->set(0,false);
		objCounters.insert(nInt);
	    }
	}
	o->set(0);
    }
    String tmp;
    dumpAllocations(tmp,&objCounters,count,code);
    if (!tmp)
	retVal << "Script '" << scrName << "' has no active object tracking counters\r\n";
    else
	retVal << "Top " << count << " object allocations for '" << scrName <<"':" << tmp << "\r\n";
    return true;
}

// Utility: return a list of parameters to be used for replaceParams
static inline const NamedList* getReplaceParams(GenObject* gen)
{
    JsObject* obj = YOBJECT(JsObject,gen);
    if (obj) {
	if (obj->nativeParams())
	    return obj->nativeParams();
	return &obj->params();
    }
    return YOBJECT(NamedList,gen);
}

// Build a tabular dump of an Object or Array
static void dumpTable(const ExpOperation& oper, String& str, const char* eol,
    const NamedList* hdrMap = 0, const NamedList* params = 0)
{
    class Header : public ObjVector
    {
    public:
	Header(const char* name, const NamedList* params, const NamedList* hdrMap,
	    const String& title = String::empty())
	    : ObjVector(1,true,10), m_name(name), m_width(0), m_widthMax(0),
	    m_titleAlign(String::Left), m_dataAlign(String::Left) {
		    if (params) {
			m_titleAlign = getAlign((*params)["column_align_title_" + m_name]);
			m_dataAlign = getAlign((*params)["column_align_data_" + m_name]);
			m_width = params->getIntValue("column_width_fixed_" + m_name,0,0,4096);
			if (m_width)
			    m_widthMax = m_width;
			else {
			    m_width = params->getIntValue("column_width_min_" + m_name,0,0,4096);
			    if (!m_width)
				m_widthMax = params->getIntValue("column_width_max_" + m_name,0,0,4096);
			}
		    }
		    const String& t = title ? title : (hdrMap ? (*hdrMap)[m_name] : String::empty());
		    m_title = buildValue(t ? t : m_name);
		    set(m_title,0);
		}
	virtual const String& toString() const
	    { return m_name; }
	inline unsigned int rows() const
	    { return length() - 1; }
	inline void addString(const String& val, unsigned int row) {
		unsigned int n = rows();
		if (row > n && resize(row + 1,true) > n)
		    set(buildValue(val),row);
	    }
	inline String& dump(String& buf, unsigned int row, unsigned int col,
	    unsigned int* prevSp = 0) const {
		if (!m_width)
		    return buf;
		const String* val = static_cast<const String*>(at(row));
		if (TelEngine::null(val)) {
		    unsigned int sp = (col ? 1 : 0) + m_width;
		    if (!prevSp)
			return buf.append(' ',sp);
		    *prevSp += sp;
		    return buf;
		}
		if (prevSp) {
		    buf.append(' ',(col ? 1 : 0) + *prevSp);
		    *prevSp = 0;
		}
		else if (col)
		    buf << ' ';
		if (val->length() >= m_width)
		    return buf.append(val->c_str(),m_width);
		int align = row ? m_dataAlign : m_titleAlign;
		if (prevSp) {
		    if (String::Left == align) {
			*prevSp = m_width - val->length();
			return buf << *val;
		    }
		    if (String::Center == align) {
			unsigned int len = val->length() + m_width / 2 - val->length() / 2;
			*prevSp = m_width - len;
			return buf.appendFixed(len,*val,' ',String::Right);
		    }
		}
		return buf.appendFixed(m_width,*val,' ',align);
	    }
	inline String& dumpSep(String& buf) const {
		if (!m_width)
		    return buf;
		if (buf)
		    buf << ' ';
		return buf.append('-',m_width);
	    }

    private:
	inline String* buildValue(const String& val) {
		String* buf = new String(val.c_str(),
		    (!m_widthMax || val.length() <= m_widthMax) ? val.length() : m_widthMax);
		if (m_width < buf->length())
		    m_width = buf->length();
		return buf;
	    }
	inline int getAlign(const String& val) {
		if (val == YSTRING("right"))
		    return String::Right;
		else if (val == YSTRING("center"))
		    return String::Center;
		return String::Left;
	    }

	String m_name;
	String* m_title;
	unsigned int m_width;
	unsigned int m_widthMax;
	int m_titleAlign;
	int m_dataAlign;
    };

    const JsObject* jso = YOBJECT(JsObject,&oper);
    if (!jso || JsParser::isNull(oper)) {
	if (JsParser::isUndefined(oper))
	    str = "undefined";
	else
	    str = oper;
	return;
    }
    ObjList header;
    const JsArray* jsa = YOBJECT(JsArray,jso);
    if (jsa) {
	// Array. Each item is a table row
	// Array of Objects: each row is an object, property names are header string
	// [ { name1: "val11", name2: "val12" }, { name1: "val21", name3: "val23" } ]
	// Array of Arrays: each row is an array of strings, first row contains header strings
	// [ [ "name1", "name2", "name3" ], [ "val11", "val12" ], ["val21", undefined, "val23" ] ]
	const JsArray* jsaRow = 0;
	unsigned int cols = 0;
	unsigned int row = 0;
	for (int i = 0; i < jsa->length(); i++) {
	    jso = YOBJECT(JsObject,jsa->params().getParam(String(i)));
	    if (!i) {
		// Require first index to be a valid entry (it MUST contain the headers)
		if (!jso)
		    break;
		jsaRow = YOBJECT(JsArray,jso);
		if (jsaRow) {
		    for (unsigned int j = 0; j < (unsigned int)jsaRow->length(); j++) {
			const NamedString* ns = jsaRow->params().getParam(String(j));
			if (!ns)
			    continue;
			cols++;
			header.append(new Header(nonObjStr(ns),params,hdrMap));
		    }
		    if (!cols)
			break;
		    continue;
		}
	    }
	    if (jsaRow) {
		const JsArray* a = YOBJECT(JsArray,jso);
		if (!a)
		    continue;
		row++;
		unsigned int n = a->length();
		if (n > cols)
		    n = cols;
		ObjList* hdr = &header;
		for (unsigned int j = 0; j < n; j++, hdr = hdr->next()) {
		    const NamedString* ns = a->params().getParam(String(j));
		    if (ns)
			(static_cast<Header*>(hdr->get()))->addString(nonObjStr(ns),row);
		}
		continue;
	    }
	    // Array of objects
	    row++;
	    for (ObjList* l = jso->params().paramList()->skipNull(); l; l = l->skipNext()) {
		const NamedString* ns = static_cast<const NamedString*>(l->get());
		if (ns->name() == JsObject::protoName())
		    continue;
		Header* h = static_cast<Header*>(header[ns->name()]);
		if (!h) {
		    h = new Header(ns->name(),params,hdrMap);
		    header.append(h);
		}
		h->addString(nonObjStr(ns),row);
	    }
	}
    }
    else {
	// Object containing Arrays
	// Each propery is a column in table
	// { name1: [ "val11", "val21" ], name2: [ "val12" ], name3: [ undefined, "val23" ] }
	for (ObjList* l = jso->params().paramList()->skipNull(); l; l = l->skipNext()) {
	    const NamedString* ns = static_cast<const NamedString*>(l->get());
	    jsa = YOBJECT(JsArray,ns);
	    if (!jsa)
		continue;
	    Header* h = new Header(ns->name(),params,hdrMap);
	    header.append(h);
	    for (int r = 0; r < jsa->length(); r++) {
		ns = jsa->params().getParam(String(r));
		if (ns)
		    h->addString(nonObjStr(ns),r + 1);
	    }
	}
    }
    bool forceEmpty = false;
    bool allHeaders = false;
    bool optimizeOut = true;
    bool emptyRow = true;
    if (params) {
	forceEmpty = params->getBoolValue(YSTRING("force_empty"));
	allHeaders = params->getBoolValue(YSTRING("all_headers"));
	optimizeOut = params->getBoolValue(YSTRING("optimize_output"),true);
	// Keep old behaviour
	emptyRow = optimizeOut && params->getBoolValue(YSTRING("dump_empty_row"),true);
    }
    if (!header.skipNull()) {
	if (!(hdrMap && forceEmpty))
	    return;
	for (ObjList* o = hdrMap->paramList()->skipNull(); o; o = o->skipNext()) {
	    NamedString* ns = static_cast<NamedString*>(o->get());
	    if (ns->name() != JsObject::protoName())
		header.append(new Header(ns->name(),params,0,*ns));
	}
	if (!header.skipNull())
	    return;
    }
    // Re-arrange headers
    ObjList* hdrs = &header;
    ObjList newHdrs;
    if (hdrMap) {
	hdrs = &newHdrs;
	ObjList* hAdd = hdrs;
	for (ObjList* o = hdrMap->paramList()->skipNull(); o; o = o->skipNext()) {
	    NamedString* ns = static_cast<NamedString*>(o->get());
	    if (ns->name() == JsObject::protoName())
		continue;
	    ObjList* oh = header.find(ns->name());
	    Header* h = oh ? static_cast<Header*>(oh->remove(false)) :
		new Header(ns->name(),params,0,*ns);
	    hAdd = hAdd->append(h);
	}
	if (allHeaders) {
	    for (ObjList* o = 0; 0 != (o = header.skipNull()); )
		hAdd = hAdd->append(o->remove(false));
	}
    }
    str.clear();
    if (!hdrs->skipNull())
	return;
    ObjList lines;
    String* sep = new String;
    ObjList* add = lines.append(sep);
    unsigned int rows = 0;
    unsigned int col = 0;
    unsigned int sp = 0;
    unsigned int* spPtr = optimizeOut ? &sp : 0;
    for (ObjList* l = hdrs->skipNull(); l; l = l->skipNext()) {
	Header* h = static_cast<Header*>(l->get());
	if (rows < h->rows())
	    rows = h->rows();
	h->dump(str,0,col++,spPtr);
	h->dumpSep(*sep);
    }
    if (!*sep || !(rows || forceEmpty))
	return;
    for (unsigned int r = 1; r <= rows; r++) {
	col = 0;
	sp = 0;
	String* line = new String;
	for (ObjList* l = hdrs->skipNull(); l; l = l->skipNext())
	    static_cast<Header*>(l->get())->dump(*line,r,col++,spPtr);
	if (*line || emptyRow)
	    add = add->append(line);
	else
	    TelEngine::destruct(line);
    }
    // This will force appending eol at end
    add = add->append(new String);
    str.append(lines,eol,true);
}

// Check and extract arguments from stack
static bool extractStackArgs(unsigned int minArgc, unsigned int maxArgc, ExpOperVector& args,
    JsObject* obj, ObjList& stack, const ExpOperation& oper, GenObject* context,
    int checkValid = -1)
{
    if (!obj)
	return false;
    obj->extractArgs(stack,oper,context,args);
    if (minArgc > args.length())
	return false;
    if (!maxArgc)
	return true;
    if (maxArgc < args.length())
	return false;
    if (checkValid) {
	if (checkValid < 0)
	    checkValid = minArgc;
	for (int i = 0; i < checkValid; ++i)
	    if (!args[i])
		return false;
    }
    return true;
}

// Extract arguments from stack
// Maximum allowed number of arguments is given by arguments to extract
// Return false if the number of arguments is not the expected one
static bool extractStackArgs(int minArgc, JsObject* obj,
    ObjList& stack, const ExpOperation& oper, GenObject* context, ObjList& args,
    ExpOperation** op1, ExpOperation** op2 = 0, ExpOperation** op3 = 0)
{
    if (!obj)
	return false;
    int argc = obj->extractArgs(stack,oper,context,args);
    if (minArgc > argc)
	return false;
    switch (argc) {
#define EXTRACT_ARG_CHECK(var,n) { \
    case n: \
	if (!var) \
	    return false; \
	*var = static_cast<ExpOperation*>(args[n - 1]); \
}
	EXTRACT_ARG_CHECK(op3,3);
	EXTRACT_ARG_CHECK(op2,2);
	EXTRACT_ARG_CHECK(op1,1);
	return true;
#undef EXTRACT_ARG_CHECK
	case 0:
	    if (!minArgc)
		return true;
    }
    return false;
}

// Copy parameters from one list to another skipping those starting with two underlines
static void copyObjParams(NamedList& dest, const NamedList* src)
{
    if (!src)
	return;
    for (const ObjList* o = src->paramList()->skipNull(); o; o = o->skipNext()) {
	const NamedString* p = static_cast<const NamedString*>(o->get());
	if (!(p->name().startsWith("__") || YOBJECT(ExpWrapper,p)))
	    dest.setParam(p->name(),*p);
    }
}


JsNamedListWrite::JsNamedListWrite(ExpOperation* oper)
    : m_jso(JsParser::isFilled(oper) ? YOBJECT(JsObject,oper) : 0), m_params(0), m_jsoParams("")
{
    if (!m_jso)
	return;
    JsConfigSection* sect = YOBJECT(JsConfigSection,m_jso);
    if (sect)
	m_params = sect->section();
    else if (!m_jso->frozen()) {
	m_params = m_jso->nativeParams();
	if (!m_params)
	    m_params = &m_jsoParams;
    }
}


bool JsEngAsync::run()
{
    switch (m_oper) {
	case AsyncSleep:
	    Thread::sleep((unsigned int)m_val);
	    break;
	case AsyncUsleep:
	    Thread::usleep((unsigned long)m_val);
	    break;
	case AsyncYield:
	    Thread::yield();
	    break;
	case AsyncIdle:
	    Thread::idle();
	    break;
    }
    return true;
}


bool JsEngine::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (oper.name() == YSTRING("output")) {
	String str;
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		continue;
	    if (*op) {
		if (str)
		    str = *op + " " + str;
		else
		    str = *op;
	    }
	    TelEngine::destruct(op);
	}
	if (str) {
	    const String& traceId = YOBJECT(ScriptRun,context) ? context->traceId() : String::empty();
	    if (traceId)
		Output("Trace:%s %s",traceId.c_str(),str.c_str());
	    else
		Output("%s",str.c_str());
	}
    }
    else if (oper.name() == YSTRING("debug")) {
	int level = DebugNote;
	String str;
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		continue;
	    if ((i == 1) && oper.number() > 1 && op->isInteger())
		level = (int)op->number();
	    else if (*op) {
		if (str)
		    str = *op + " " + str;
		else
		    str = *op;
	    }
	    TelEngine::destruct(op);
	}
	if (str) {
	    int limit = s_allowAbort ? DebugFail : DebugTest;
	    if (level > DebugAll)
		level = DebugAll;
	    else if (level < limit)
		level = limit;
	    TraceDebug(YOBJECT(ScriptRun,context) ? context->traceId() : "",this,level,"%s",str.c_str());
	}
    }
    else if (oper.name() == YSTRING("traceDebug") || oper.name() == YSTRING("trace")) {
	ObjList args;
	unsigned int c = extractArgs(stack,oper,context,args);
	if (c < 2)
	    return false;
	ExpOperation* traceID = static_cast<ExpOperation*>(args[0]);
	ExpOperation* op = static_cast<ExpOperation*>(args[1]);

	int level = DebugNote;
	int limit = s_allowAbort ? DebugFail : DebugTest;
	if (oper.number() > 1 && op->isInteger()) {
	    level = (int)op->number();
	    if (level > DebugAll)
		level = DebugAll;
	    else if (level < limit)
		level = limit;
	}
	
	String str;
	for (unsigned int i = 2; i < c; i++) {
	    ExpOperation* op = static_cast<ExpOperation*>(args[i]);
	    if (!op)
		continue;
	    else if (*op) {
		if (str)
		    str << " ";
		str << *op;
	    }
	}
	if (str) {
	    const char* t = 0;
	    if (!(TelEngine::null(*traceID) || JsParser::isNull(*traceID)))
		t = *traceID;
	    if (oper.name() == YSTRING("trace"))
		Trace(t,this,level,"%s",str.c_str());
	    else
		TraceDebug(t,this,level,"%s",str.c_str());
	}
    }
    else if (oper.name() == YSTRING("alarm") || oper.name() == YSTRING("traceAlarm")) {
	int idx = (oper.name() == YSTRING("traceAlarm")) ? 1 : 0;
	if (oper.number() < 2 + idx)
	    return false;
	int level = -1;
	String info;
	String str;
	String traceId = YOBJECT(ScriptRun,context) ? context->traceId() : String::empty();
	for (int i = (int)oper.number(); i; i--) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		continue;
	    if (i == 0 + idx)
		traceId = *op;
	    else if (i == 1 + idx) {
		if (level < 0) {
		    if (op->isInteger())
			level = (int)op->number();
		    else {
			TelEngine::destruct(op);
			return false;
		    }
		}
		else
		    info = *op;
	    }
	    else if ((i == 2 + idx) && oper.number() > 2 + idx && op->isInteger())
		level = (int)op->number();
	    else if (*op) {
		if (str)
		    str = *op + " " + str;
		else
		    str = *op;
	    }
	    TelEngine::destruct(op);
	}
	if (str && level >= 0) {
	    int limit = s_allowAbort ? DebugFail : DebugTest;
	    if (level > DebugAll)
		level = DebugAll;
	    else if (level < limit)
		level = limit;
	    TraceAlarm(traceId,this,info,level,"%s",str.c_str());
	}
    }
    else if (oper.name() == YSTRING("setTraceId")) {
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	String tmp;
	switch (oper.number()) {
	    case 1:
	    {
		ExpOperation* op = popValue(stack,context);
		if (!op)
		    return false;
		if (!JsParser::isNull(*op))
		    tmp = *op;
		TelEngine::destruct(op);
		break;
	    }
	    case 0:
		// reset trace ID
		break;
	    default:
		return false;
	}
	runner->setTraceId(tmp);
    }
    else if (oper.name() == YSTRING("lineNo")) {
	if (oper.number())
	    return false;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)runner->currentLineNo()));
    }
    else if (oper.name() == YSTRING("fileName") || oper.name() == YSTRING("fileNo")) {
	if (oper.number() > 1)
	    return false;
	bool wholePath = false;
	if (oper.number() == 1) {
            ExpOperation* op = popValue(stack,context);
	    if (!op)
		return false;
	    wholePath = op->valBoolean();
	}
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	String fileName = runner->currentFileName(wholePath);
	if (oper.name() == YSTRING("fileNo"))
	    fileName << ":" << runner->currentLineNo();
	ExpEvaluator::pushOne(stack,new ExpOperation(fileName));
    }
    else if (oper.name() == YSTRING("creationLine")) {
	ObjList args;
	unsigned int c = extractArgs(stack,oper,context,args);
	if (c < 1)
	    return false;
	JsObject* jso= 0;
	ExpWrapper* w = YOBJECT(ExpWrapper,args[0]);
	if (w)
	    jso = YOBJECT(JsObject,w->object());
	if (!jso)
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0,0));
	else {
	    bool wholePath = false;
	    ExpOperation* op = static_cast<ExpOperation*>(args[1]);
	    if (op)
		wholePath = op->valBoolean();
	    ScriptRun* runner = YOBJECT(ScriptRun,context);
	    if (!(runner && runner->code()))
		return false;
	    String fn;
	    unsigned int fl = 0;
	    runner->code()->getFileLine(jso->lineNo(),fn,fl,wholePath);
	    ExpEvaluator::pushOne(stack,new ExpOperation(fn << ":" << fl));
	}
    }
    else if (oper.name() == YSTRING("sleep")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	int64_t val = op->valInteger();
	TelEngine::destruct(op);
	if (val < 0)
	    val = 0;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	runner->insertAsync(new JsEngAsync(runner,JsEngAsync::AsyncSleep,val));
	runner->pause();
    }
    else if (oper.name() == YSTRING("usleep")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	int64_t val = op->valInteger();
	TelEngine::destruct(op);
	if (val < 0)
	    val = 0;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	runner->insertAsync(new JsEngAsync(runner,JsEngAsync::AsyncUsleep,val));
	runner->pause();
    }
    else if (oper.name() == YSTRING("yield")) {
	if (oper.number() != 0)
	    return false;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	runner->insertAsync(new JsEngAsync(runner,JsEngAsync::AsyncYield));
	runner->pause();
    }
    else if (oper.name() == YSTRING("idle")) {
	if (oper.number() != 0)
	    return false;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	runner->insertAsync(new JsEngAsync(runner,JsEngAsync::AsyncIdle));
	runner->pause();
    }
    else if (oper.name() == YSTRING("dump_r")) {
	String buf;
	if (oper.number() == 0) {
	    ScriptRun* run = YOBJECT(ScriptRun,context);
	    if (run)
		dumpRecursive(run->context(),buf);
	    else
		dumpRecursive(context,buf);
	}
	else if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		return false;
	    dumpRecursive(op,buf);
	    TelEngine::destruct(op);
	}
	else
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(buf));
    }
    else if (oper.name() == YSTRING("print_r")) {
	if (oper.number() == 0) {
	    ScriptRun* run = YOBJECT(ScriptRun,context);
	    if (run)
		printRecursive(run->context());
	    else
		printRecursive(context);
	}
	else if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		return false;
	    printRecursive(op);
	    TelEngine::destruct(op);
	}
	else
	    return false;
    }
    else if (oper.name() == YSTRING("dump_var_r")) {
	// str = Engine.dump_var_r(obj[,flags])
	ObjList args;
	ExpOperation* obj = 0;
	ExpOperation* flags = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&obj,&flags))
	    return false;
	String buf;
	dumpRecursive(obj,buf,flags ? flags->valInteger(DumpPropOnly) : DumpPropOnly);
	ExpEvaluator::pushOne(stack,new ExpOperation(buf));
    }
    else if (oper.name() == YSTRING("print_var_r")) {
	// Engine.print_var_r(obj[,flags])
	ObjList args;
	ExpOperation* obj = 0;
	ExpOperation* flags = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&obj,&flags))
	    return false;
	printRecursive(obj,flags ? flags->valInteger(DumpPropOnly) : DumpPropOnly);
    }
    else if (oper.name() == YSTRING("dump_root_r")) {
	// str = Engine.dump_root_r([flags])
	ObjList args;
	ExpOperation* flags = 0;
	if (!extractStackArgs(0,this,stack,oper,context,args,&flags))
	    return false;
	String buf;
	ScriptRun* run = YOBJECT(ScriptRun,context);
	dumpRecursive(run ? run->context() : context,buf,
	    flags ? flags->valInteger(DumpPropOnly) : DumpPropOnly);
	ExpEvaluator::pushOne(stack,new ExpOperation(buf));
    }
    else if (oper.name() == YSTRING("print_root_r")) {
	// Engine.print_root_r([flags])
	ObjList args;
	ExpOperation* flags = 0;
	if (!extractStackArgs(0,this,stack,oper,context,args,&flags))
	    return false;
	ScriptRun* run = YOBJECT(ScriptRun,context);
	printRecursive(run ? run->context() : context,
	    flags ? flags->valInteger(DumpPropOnly) : DumpPropOnly);
    }
    else if (oper.name() == YSTRING("dump_t") || oper.name() == YSTRING("print_t")) {
	ObjList args;
	ExpOperation* opObj = 0;
	ExpOperation* hdrMap = 0;
	ExpOperation* params = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&opObj,&hdrMap,&params))
	    return false;
	String buf;
	dumpTable(*opObj,buf,"\r\n",getObjParams(hdrMap),getObjParams(params));
	if (oper.name() == YSTRING("dump_t"))
	    ExpEvaluator::pushOne(stack,new ExpOperation(buf));
	else if (buf && Debugger::outputTimestamp())
	    Output("\r\n%s",buf.safe());
	else
	    Output("%s",buf.safe());
    }
    else if (oper.name() == YSTRING("dump_t_params")) {
	JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
	jso->setBoolField("column_width",true);
	jso->setBoolField("column_align",true);
	ExpEvaluator::pushOne(stack,new ExpWrapper(jso,oper.name()));
    }
    else if (oper.name() == YSTRING("debugName")) {
	if (oper.number() == 0)
	    ExpEvaluator::pushOne(stack,new ExpOperation(m_debugName));
	else if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    String tmp;
	    if (op && !JsParser::isNull(*op))
		tmp = *op;
	    TelEngine::destruct(op);
	    tmp.trimSpaces();
	    if (tmp.null())
		tmp = "javascript";
	    m_debugName = tmp;
	    debugName(m_debugName);
	}
	else
	    return false;
    }
    else if (oper.name() == YSTRING("debugLevel")) {
	if (oper.number() == 0)
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)debugLevel()));
	else if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    if (op && op->isInteger())
		debugLevel((int)op->valInteger());
	    TelEngine::destruct(op);
	}
	else
	    return false;
    }
    else if (oper.name() == YSTRING("debugEnabled")) {
	if (oper.number() == 0)
	    ExpEvaluator::pushOne(stack,new ExpOperation(debugEnabled()));
	else if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    if (op)
		debugEnabled(op->valBoolean());
	    TelEngine::destruct(op);
	}
	else
	    return false;
    }
    else if (oper.name() == YSTRING("debugAt")) {
	if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    if (!(op && op->isInteger())) {
		TelEngine::destruct(op);
		return false;
	    }
	    ExpEvaluator::pushOne(stack,new ExpOperation(debugAt((int)op->valInteger())));
	    TelEngine::destruct(op);
	}
	else
	    return false;
    }
    else if (oper.name() == YSTRING("setDebug")) {
	if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    if (!op)
		return false;
	    setDebug(*op);
	    TelEngine::destruct(op);
	}
	else
	    return false;
    }
    else if (oper.name() == YSTRING("runParams")) {
	if (oper.number() == 0) {
	    JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
	    jso->params().copyParams(Engine::runParams());
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,oper.name()));
	}
	else if (oper.number() == 1) {
	    ExpOperation* op = popValue(stack,context);
	    if (op)
		ExpEvaluator::pushOne(stack,new ExpOperation(Engine::runParams()[*op]));
	    TelEngine::destruct(op);
	}
	else
	    return false;
    }
    else if (oper.name() == YSTRING("configFile")) {
	bool user = false;
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 2:
		user = static_cast<ExpOperation*>(args[1])->valBoolean();
		// fall through
	    case 1:
		ExpEvaluator::pushOne(stack,new ExpOperation(
		    Engine::configFile(*static_cast<ExpOperation*>(args[0]),user)));
		break;
	    default:
		return false;
	}
    }
    else if (oper.name() == YSTRING("setInterval"))
	return setEvent(stack,oper,context,true,true);
    else if (oper.name() == YSTRING("setTimeout"))
	return setEvent(stack,oper,context,true,false);
    else if (oper.name() == YSTRING("setEvent"))
	return setEvent(stack,oper,context,false);
    else if (oper.name() == YSTRING("clearInterval"))
	return clearEvent(stack,oper,context,true,true);
    else if (oper.name() == YSTRING("clearTimeout"))
	return clearEvent(stack,oper,context,true,false);
    else if (oper.name() == YSTRING("clearEvent"))
	return clearEvent(stack,oper,context,false);
    else if (oper.name() == YSTRING("loadLibrary") || oper.name() == YSTRING("loadObject")) {
	bool obj = oper.name() == YSTRING("loadObject");
	bool ok = false;
	ObjList args;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	int argc = extractArgs(stack,oper,context,args);
	if (runner && argc) {
	    ok = true;
	    for (int i = 0; i < argc; i++) {
		ExpOperation* op = static_cast<ExpOperation*>(args[i]);
		if (!op || op->isBoolean() || op->isNumber() || YOBJECT(ExpWrapper,op))
		    ok = false;
		else if (obj)
		    ok = contextLoad(runner,0,0,*op) && ok;
		else
		    ok = contextLoad(runner,0,*op) && ok;
	    }
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("pluginLoaded")) {
	ObjList args;
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(
	    Engine::self()->pluginLoaded(*static_cast<ExpOperation*>(args[0]))));
    }
    else if (oper.name() == YSTRING("replaceParams")) {
	ObjList args;
	int argc = extractArgs(stack,oper,context,args);
	if (argc < 2 || argc > 4)
	    return false;
	GenObject* arg0 = args[0];
	ExpOperation* text = static_cast<ExpOperation*>(arg0);
	bool sqlEsc = (argc >= 3) && static_cast<ExpOperation*>(args[2])->valBoolean();
	char extraEsc = 0;
	if (argc >= 4)
	    extraEsc = static_cast<ExpOperation*>(args[3])->at(0);
	const NamedList* params = getReplaceParams(args[1]);
	if (params) {
	    String str(*text);
	    params->replaceParams(str,sqlEsc,extraEsc);
	    ExpEvaluator::pushOne(stack,new ExpOperation(str,text->name()));
	}
	else {
	    args.remove(arg0,false);
	    ExpEvaluator::pushOne(stack,text);
	}
    }
    else if (oper.name() == YSTRING("restart")) {
	ObjList args;
	int argc = extractArgs(stack,oper,context,args);
	if (argc > 2)
	    return false;
	bool ok = s_allowAbort;
	if (ok) {
	    int code = 0;
	    if (argc >= 1) {
		code = static_cast<ExpOperation*>(args[0])->valInteger();
		if (code < 0)
		    code = 0;
	    }
	    bool gracefull = (argc >= 2) && static_cast<ExpOperation*>(args[1])->valBoolean();
	    ok = Engine::restart(code,gracefull);
	}
	else
	    Debug(&__plugin,DebugNote,"Engine restart is disabled by allow_abort configuration");
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("init")) {
	bool ok = true;
	if (!oper.number())
	    Engine::init();
	else if (oper.number() == 1) {
	    ExpOperation* module = popValue(stack,context);
	    if (!module)
		return false;
	    ok = Engine::init(*module);
	    TelEngine::destruct(module);
	}
	else
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("uptime")) {
	SysUsage::Type typ = SysUsage::WallTime;
	bool msec = false;
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 2:
		msec = static_cast<ExpOperation*>(args[1])->valBoolean();
		// fall through
	    case 1:
		typ = (SysUsage::Type)static_cast<ExpOperation*>(args[0])->toInteger(typ);
		// fall through
	    case 0:
		break;
	    default:
		return false;
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(
	    msec ? (int64_t)SysUsage::msecRunTime(typ) : (int64_t)SysUsage::secRunTime(typ)));
    }
    else if (oper.name() == YSTRING("started")) {
	if (oper.number() != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(Engine::started()));
    }
    else if (oper.name() == YSTRING("exiting")) {
	if (oper.number() != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(Engine::exiting()));
    }
    else if (oper.name() == YSTRING("accepting")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
		ExpEvaluator::pushOne(stack,new ExpOperation(
		    lookup(Engine::accept(),Engine::getCallAcceptStates())));
		break;
	    case 1:
		{
		    int arg = static_cast<ExpOperation*>(args[0])->toInteger(
			Engine::getCallAcceptStates(),-1);
		    if ((Engine::Accept <= arg) && (Engine::Reject >= arg))
			Engine::setAccept((Engine::CallAccept)arg);
		}
		break;
	    default:
		return false;
	}
    }
    else if (oper.name() == YSTRING("getCongestion")) {
	if (oper.number() != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)Engine::getCongestion()));
    }
    else if (oper.name() == YSTRING("setCongestion")) {
	ExpOperation* op = 0;
	switch (oper.number()) {
	    case 0:
		break;
	    case 1:
		op = popValue(stack,context);
		if (op)
		    break;
		// fall through
	    default:
		return false;
	}
	Engine::setCongestion(c_str(op));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("atob")) {
	// str = Engine.atob(b64_str)
	ObjList args;
	int argc = extractArgs(stack,oper,context,args);
	if (argc < 1)
	    return false;
	Base64 b64;
	b64 << *static_cast<ExpOperation*>(args[0]);
	DataBlock buf;
	if (b64.decode(buf)) {
	    String tmp((const char*)buf.data(),buf.length());
	    ExpEvaluator::pushOne(stack,new ExpOperation(tmp,"bin"));
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpOperation(false));
    }
    else if (oper.name() == YSTRING("btoa")) {
	// b64_str = Engine.btoa(str,line_len,add_eol)
	ObjList args;
	int argc = extractArgs(stack,oper,context,args);
	if (argc < 1)
	    return false;
	int len = 0;
	bool eol = false;
	if (argc >= 3)
	    eol = static_cast<ExpOperation*>(args[2])->valBoolean();
	if (argc >= 2) {
	    len = static_cast<ExpOperation*>(args[1])->valInteger();
	    if (len < 0)
		len = 0;
	}
	Base64 b64;
	b64 << *static_cast<ExpOperation*>(args[0]);
	String buf;
	b64.encode(buf,len,eol);
	ExpEvaluator::pushOne(stack,new ExpOperation(buf,"b64"));
    }
    else if (oper.name() == YSTRING("atoh")) {
	// hex_str = Engine.atoh(b64_str,hex_sep,hex_upcase)
	ObjList args;
	int argc = extractArgs(stack,oper,context,args);
	if (argc < 1)
	    return false;
	Base64 b64;
	b64 << *static_cast<ExpOperation*>(args[0]);
	DataBlock buf;
	if (b64.decode(buf)) {
	    char sep = (argc >= 2) ? static_cast<ExpOperation*>(args[1])->at(0) : '\0';
	    bool upCase = (argc >= 3) && static_cast<ExpOperation*>(args[2])->valBoolean();
	    String tmp;
	    tmp.hexify(buf.data(),buf.length(),sep,upCase);
	    ExpEvaluator::pushOne(stack,new ExpOperation(tmp,"hex"));
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpOperation(false));
    }
    else if (oper.name() == YSTRING("htoa")) {
	// b64_str = Engine.htoa(hex_str,line_len,add_eol)
	ObjList args;
	int argc = extractArgs(stack,oper,context,args);
	if (argc < 1)
	    return false;
	Base64 b64;
	if (b64.unHexify(*static_cast<ExpOperation*>(args[0]))) {
	    int len = 0;
	    bool eol = false;
	    if (argc >= 3)
		eol = static_cast<ExpOperation*>(args[2])->valBoolean();
	    if (argc >= 2) {
		len = static_cast<ExpOperation*>(args[1])->valInteger();
		if (len < 0)
		    len = 0;
	    }
	    String buf;
	    b64.encode(buf,len,eol);
	    ExpEvaluator::pushOne(stack,new ExpOperation(buf,"b64"));
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpOperation(false));
    }
    else if (oper.name() == YSTRING("btoh")) {
	// hex_str = Engine.btoh(str[,sep[,upCase]])
	ObjList args;
	ExpOperation* data = 0;
	ExpOperation* sep = 0;
	ExpOperation* upCase = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&data,&sep,&upCase))
	    return false;
	String tmp;
	tmp.hexify((void*)data->c_str(),data->length(),(sep ? sep->at(0) : 0),
	    (upCase && upCase->toBoolean()));
	ExpEvaluator::pushOne(stack,new ExpOperation(tmp,"hex"));
    }
    else if (oper.name() == YSTRING("htob")) {
	// str = Engine.unHexify(hex_str[,sep])
	ObjList args;
	ExpOperation* data = 0;
	ExpOperation* sep = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&data,&sep))
	    return false;
	bool ok = true;
	DataBlock buf;
	if (!sep)
	    ok = buf.unHexify(data->c_str(),data->length());
	else
	    ok = buf.unHexify(data->c_str(),data->length(),sep->at(0));
	if (ok) {
	    String tmp((const char*)buf.data(),buf.length());
	    ExpEvaluator::pushOne(stack,new ExpOperation(tmp,"bin"));
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpOperation(false));
    }
    else if (oper.name() == YSTRING("instanceIndex")) {
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!(runner && runner->context()))
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)runner->context()->instanceIndex()));
    }
    else if (oper.name() == YSTRING("instanceCount")) {
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!(runner && runner->context()))
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)runner->context()->instanceCount()));
    }
    else if (oper.name() == YSTRING("scriptType")) {
	ScriptInfo* si = ScriptInfo::get(context);
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)(si ? si->type() : ScriptInfo::Unknown)));
    }
    else if (oper.name() == YSTRING("scriptInfo")) {
	JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
	ScriptInfo::set(*jso,ScriptInfo::get(context));
	ExpEvaluator::pushOne(stack,new ExpWrapper(jso,oper.name()));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsEngine::destroyed()
{
    ObjList remove;
    JsGlobal::s_sharedObj.remove(id(),&remove);
    JsObject::destroyed();
    if (!m_worker)
	return;
    m_worker->cancel();
    while (m_worker)
	Thread::idle();
}

bool JsEngine::setEvent(ObjList& stack, const ExpOperation& oper, GenObject* context,
    bool time, bool repeat)
{
    // setInterval / setTimeout (func,interval[,callback_args ...])
    // setEvent(func,type[,params[,callback_args...])
    ExpOperVector args;
    if (!extractStackArgs(2,0,args,this,stack,oper,context))
	return false;
    const ExpFunction* callback = getFunction(args[0]);
    if (!callback)
	return false;
    unsigned int interval = 0;
    int type = 0;
    if (time) {
	interval = args[1]->toInteger();
	type = JsEvent::EvTime;
    }
    else {
	type = lookup(*args[1],JsEvent::s_evName);
	if (!type) {
	    int tmp = args[1]->toInteger();
	    if (lookup(tmp,JsEvent::s_evName))
		type = tmp;
	}
	if (!type || type == JsEvent::EvTime)
	    return false;
	// We can notify reinit to tracked scripts only
	ScriptInfo* si = ScriptInfo::get(context);
	if (!(si && (si->type() == ScriptInfo::Static || si->type() == ScriptInfo::Dynamic))) {
	    ExpEvaluator::pushOne(stack,new ExpOperation(false));
	    return true;
	}
    }

    // Start worker
    if (!m_worker) {
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!(runner && runner->context() && runner->code()))
	    return false;
	m_worker = new JsEngineWorker(this,runner->context(),runner->code());
	if (!m_worker->init()) {
	    m_worker = 0;
	    delete m_worker;
	    return false;
	}
    }
    if (!time) {
	repeat = JsEvent::canRepeat(type);
	JsObject* jso = YOBJECT(JsObject,args[2]);
	if (jso) {
	    if (repeat)
		jso->getBoolField(YSTRING("repeat"),repeat);
	}
    }    
    ExpOperVector cbArgs;
    unsigned int id = m_worker->addEvent(*callback,type,repeat,
	cbArgs.cloneFrom(args,time ? 2 : 3),interval);
    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)id));
    return true;
}

bool JsEngine::clearEvent(ObjList& stack, const ExpOperation& oper, GenObject* context,
    bool time, bool repeat)
{
    // clearInterval / clearTimeout / clearEvent (id)
    if (!m_worker)
	return false;
    ExpOperVector args;
    if (!extractStackArgs(1,0,args,this,stack,oper,context))
	return false;
    ExpOperation* id = static_cast<ExpOperation*>(args[0]);
    bool ret = m_worker->removeEvent((unsigned int)id->valInteger(),time,repeat);
    ExpEvaluator::pushOne(stack,new ExpOperation(ret));
    return true;
}

void JsEngine::initialize(ScriptContext* context, const char* name)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("Engine")))
	addObject(params,"Engine",new JsEngine(mtx,name));
}

void JsEngine::setDebug(String str)
{
    if (!str)
	return;
    if (str.startSkip("level")) {
	int dbg = debugLevel();
	str >> dbg;
	if (str == YSTRING("+")) {
	    if (debugLevel() > dbg)
		dbg = debugLevel();
	}
	else if (str == YSTRING("-")) {
	    if (debugLevel() < dbg)
		dbg = debugLevel();
	}
	debugLevel(dbg);
    }
    else if (str == YSTRING("reset"))
	debugChain(&__plugin);
    else if (str == YSTRING("engine"))
	debugCopy();
    else if (str.isBoolean())
	debugEnabled(str.toBoolean(debugEnabled()));
}


bool JsShared::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsShared::runNative '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    if (oper.name() == YSTRING("inc")) {
	ObjList args;
	ExpOperation* param = 0;
	ExpOperation* mod = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&param,&mod))
	    return false;
	if (m_vars)
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_vars->inc(*param,modulo(mod))));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("dec")) {
	ObjList args;
	ExpOperation* param = 0;
	ExpOperation* mod = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&param,&mod))
	    return false;
	if (m_vars)
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_vars->dec(*param,modulo(mod))));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("get")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* param = popValue(stack,context);
	if (!param)
	    return false;
	String buf;
	if (m_vars)
	    m_vars->get(*param,buf);
	TelEngine::destruct(param);
	ExpEvaluator::pushOne(stack,new ExpOperation(buf));
    }
    else if (oper.name() == YSTRING("set")) {
	if (oper.number() != 2)
	    return false;
	ExpOperation* val = popValue(stack,context);
	if (!val)
	    return false;
	ExpOperation* param = popValue(stack,context);
	if (!param) {
	    TelEngine::destruct(val);
	    return false;
	}
	if (m_vars)
	    m_vars->set(*param,*val);
	TelEngine::destruct(param);
	TelEngine::destruct(val);
    }
    else if (oper.name() == YSTRING("add") || oper.name() == YSTRING("sub")) {
	ObjList args;
	ExpOperation* param = 0;
	ExpOperation* val = 0;
	ExpOperation* mod = 0;
	if (!extractStackArgs(2,this,stack,oper,context,args,&param,&val,&mod))
	    return false;
	if (m_vars) {
	    int64_t value = val->isInteger() ? val->number() : 0;
	    if (oper.name() == YSTRING("add"))
		value = (int64_t)m_vars->add(*param,value > 0 ? value : 0,modulo(mod));
	    else
		value = (int64_t)m_vars->sub(*param,value > 0 ? value : 0,modulo(mod));
	    ExpEvaluator::pushOne(stack,new ExpOperation(value));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("create")) {
	// create(name[,value])
	ExpOperVector args;
	if (!extractStackArgs(1,0,args,this,stack,oper,context))
	    return false;
	if (m_vars)
	    m_vars->create(*args[0],TelEngine::c_safe(args[1]));
    }
    else if (oper.name() == YSTRING("clear")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* param = popValue(stack,context);
	if (!param)
	    return false;
	if (m_vars)
	    m_vars->clear(*param);
	TelEngine::destruct(param);
    }
    else if (oper.name() == YSTRING("clearAll")) {
	if (m_vars)
	    m_vars->clearAll();
    }
    else if (oper.name() == YSTRING("exists")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* param = popValue(stack,context);
	if (!param)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(m_vars && m_vars->exists(*param)));
	TelEngine::destruct(param);
    }
    else if (oper.name() == YSTRING("getVars")) {
	// getVars([params])
	// params:
	//   js_props: Boolean. Force Javascript ExpOperation in returned result. Default: true
	//   autonum: Boolean. Force ExpOperation auto number in returned result. Default: false.
	//     Ignored if not returning ExpOperation
	//   autobool: Boolean. Detect booleans, put them in returned result. Default: false.
	//     Ignored if not returning ExpOperation
	//   prefix: String. Optional prefix for variables
	//   skip_prefix: Boolean. Skip prefix when returned. Default: true. Ignored if prefix is empty
	ObjList args;
	ExpOperation* pOp = 0;
	if (!extractStackArgs(0,this,stack,oper,context,args,&pOp))
	    return false;
	if (m_vars) {
	    bool expOper = true;
	    bool autoNum = false;
	    bool autoBool = false;
	    String prefix;
	    bool skipPrefix = true;
	    JsObject* params = YOBJECT(JsObject,pOp);
	    if (params) {
		params->getBoolField(YSTRING("js_props"),expOper);
		if (expOper) {
		    params->getBoolField(YSTRING("autonum"),autoNum);
		    params->getBoolField(YSTRING("autobool"),autoBool);
		}
		params->getStringField(YSTRING("prefix"),prefix);
		if (prefix)
		    params->getBoolField(YSTRING("skip_prefix"),skipPrefix);
	    }
	    JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
	    if (expOper) {
		NamedList tmp("");
		m_vars->copy(tmp,prefix,skipPrefix);
		for (ObjList* o = tmp.paramList()->skipNull(); o; o = o->skipNext()) {
		    NamedString* ns = static_cast<NamedString*>(o->get());
		    if (autoBool && ns->isBoolean())
			jso->params().addParam(new ExpOperation(ns->toBoolean(),ns->name()));
		    else
			jso->params().addParam(new ExpOperation(*ns,ns->name(),autoNum));
		}
	    }
	    else
		m_vars->copy(jso->params(),prefix,skipPrefix);
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,"vars"));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

JsObject* JsShared::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList args;
    ExpOperation* sharedOp = 0;
    if (!extractStackArgs(1,this,stack,oper,context,args,&sharedOp))
	return 0;
    JsShared* obj = new JsShared(mutex(),oper.lineNumber(),*sharedOp);
    if (ref())
	obj->params().addParam(new ExpWrapper(this,protoName()));
    else
	TelEngine::destruct(obj);
    return obj;
}


bool JsSharedObjects::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsSharedObjects::runNative '%s'(" FMT64 ")",
	oper.name().c_str(),oper.number());
    ExpOperVector args;
    bool set = oper.name() == YSTRING("set");
    if (set || oper.name() == YSTRING("create")) {
	// set(name,obj[,persistent[,assignPropsFlags]])
	// create(name,obj[,persistent[,assignPropsFlags]])
	if (!(extractStackArgs(2,0,args,this,stack,oper,context) && *args[0]))
	    return false;
	JsObject* jso = JsParser::objPresent(*args[1]);
	if (!jso)
	    return false;
	int ok = 0;
	String owner;
	if (!(args[2] && args[2]->valBoolean()))
	    owner = m_owner;
	SharedJsObject* jsh = new SharedJsObject(ok,*args[0],jso,owner,
	    args[3] ? args[3]->valInteger() : 0,context);
	if (!JsGlobal::s_sharedObj.set(jsh,set))
	    ok = -1;
	TelEngine::destruct(jsh);
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)ok));
    }
    else if (oper.name() == YSTRING("get")) {
	// get(name)
	if (!extractStackArgs(1,0,args,this,stack,oper,context))
	    return false;
	ExpOperation* name = args[0];
	JsObject* jso = JsGlobal::s_sharedObj.get(*name,context,oper.lineNumber());
	ExpEvaluator::pushOne(stack,JsParser::validExp(jso,*name));
    }
    else if (oper.name() == YSTRING("clear")) {
	// clear(name)
	if (!extractStackArgs(1,0,args,this,stack,oper,context))
	    return false;
	JsGlobal::s_sharedObj.remove(*args[0]);
    }
    else if (oper.name() == YSTRING("clearAll")) {
	// clearAll([owned])
	if (!extractStackArgs(0,0,args,this,stack,oper,context))
	    return false;
	if (!(args[0] && args[0]->valBoolean()))
	    JsGlobal::s_sharedObj.clear();
	else if (m_owner) {
	    ObjList remove;
	    JsGlobal::s_sharedObj.remove(m_owner,&remove);
	}
    }
    else if (oper.name() == YSTRING("exists") || oper.name() == YSTRING("description")) {
	// exists(name)
	// description(name)
	if (!extractStackArgs(1,0,args,this,stack,oper,context))
	    return false;
	ExpOperation* name = args[0];
	RefPointer<SharedJsObject> jsh;
	JsGlobal::s_sharedObj.find(jsh,*name);
	if (oper.name() == YSTRING("exists"))
	    ExpEvaluator::pushOne(stack,new ExpOperation(0 != jsh));
	else {
	    JsObject* jso = 0;
	    if (jsh) {
		jso = new JsObject(context,oper.lineNumber(),mutex());
		jso->setStringField("name",jsh->name());
		jso->setBoolField("owned",m_owner && m_owner == jsh->owner());
		jso->setBoolField("persistent",jsh->owner().null());
	    }
	    ExpEvaluator::pushOne(stack,JsParser::validExp(jso,*name));
	}
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

JsObject* JsSharedObjects::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    JsEngine* eng = JsEngine::get(context);
    String owner;
    if (eng)
	owner = eng->id();
    JsSharedObjects* obj = new JsSharedObjects(owner,mutex(),oper.lineNumber());
    if (ref())
	obj->params().addParam(new ExpWrapper(this,protoName()));
    else
	TelEngine::destruct(obj);
    return obj;
}


JsMessage::~JsMessage()
{
    XDebug(&__plugin,DebugAll,"JsMessage::~JsMessage() [%p]",this);
    if (m_owned)
	TelEngine::destruct(m_message);
    JsMessageHandle::uninstall(m_handlers);
    JsMessageHandle::uninstall(m_handlersSingleton);
    JsMessageHandle::uninstall(m_postHooks);
    for (ObjList* o = m_hooks.skipNull();o;o = o->skipNext()) {
	MessageHook* hook = static_cast<MessageHook*>(o->get());
	Engine::uninstallHook(hook);
    }
    TelEngine::destruct(m_traceLst);
}

void* JsMessage::getObject(const String& name) const
{
    void* obj = (name == YATOM("JsMessage")) ? const_cast<JsMessage*>(this) : JsObject::getObject(name);
    if (m_message && !obj)
	obj = m_message->getObject(name);
    return obj;
}

bool JsMessage::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsMessage::runAssign '%s'='%s'",oper.name().c_str(),oper.c_str());
    if (ScriptContext::hasField(stack,oper.name(),context))
	return JsObject::runAssign(stack,oper,context);
    if (frozen() || !m_message) {
	Debug(&__plugin,DebugWarn,"Message is frozen or missing");
	return false;
    }
    if (JsParser::isUndefined(oper))
	m_message->clearParam(oper.name());
    else
	m_message->setParam(new NamedString(oper.name(),oper));
    return true;
}

bool JsMessage::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsMessage::runNative '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    if (oper.name() == YSTRING("broadcast")) {
	if (oper.number() != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(m_message && m_message->broadcast()));
    }
    else if (oper.name() == YSTRING("name")) {
	if (oper.number() != 0)
	    return false;
	if (m_message)
	    ExpEvaluator::pushOne(stack,new ExpOperation(*m_message));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("retValue")) {
	switch (oper.number()) {
	    case 0:
		if (m_message)
		    ExpEvaluator::pushOne(stack,new ExpOperation(m_message->retValue(),0,true));
		else
		    ExpEvaluator::pushOne(stack,JsParser::nullClone());
		break;
	    case 1:
		{
		    ExpOperation* op = popValue(stack,context);
		    if (!op)
			return false;
		    if (m_message && !frozen())
			m_message->retValue() = *op;
		    TelEngine::destruct(op);
		}
		break;
	    default:
		return false;
	}
    }
    else if (oper.name() == YSTRING("msgTime")) {
	switch (oper.number()) {
	    case 0:
		if (m_message)
		    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_message->msgTime().msec()));
		else
		    ExpEvaluator::pushOne(stack,JsParser::nullClone());
		break;
	    case 1:
		{
		    ExpOperation* op = popValue(stack,context);
		    if (!op)
			return false;
		    uint64_t newTime = 0;
		    if (op->isBoolean()) {
			if (op->valBoolean())
			    newTime = Time::now();
		    }
		    else if (op->isInteger()) {
			if (op->number() > 0)
			    newTime = 1000 * op->number();
		    }
		    TelEngine::destruct(op);
		    if (newTime && m_message && !frozen())
			m_message->msgTime() = newTime;
		    else
			newTime = 0;
		    ExpEvaluator::pushOne(stack,new ExpOperation(0 != newTime));
		}
		break;
	    default:
		return false;
	}
    }
    else if (oper.name() == YSTRING("msgAge")) {
	if (oper.number())
	    return false;
	if (m_message)
	    ExpEvaluator::pushOne(stack,new ExpOperation(
		    (int64_t)(Time::msecNow() - m_message->msgTime().msec())));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getParam")) {
	bool autoNum = true;
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 3:
		// fall through
		autoNum = static_cast<ExpOperation*>(args[2])->valBoolean();
	    case 2:
	    case 1:
		break;
	    default:
		return false;
	}
	const String& name = *static_cast<ExpOperation*>(args[0]);
	const String* val = m_message ? m_message->getParam(name) : 0;
	if (val)
	    ExpEvaluator::pushOne(stack,new ExpOperation(*val,name,autoNum));
	else {
	    if (args[1])
		ExpEvaluator::pushOne(stack,static_cast<ExpOperation*>(args[1])->clone(name));
	    else
		ExpEvaluator::pushOne(stack,new ExpWrapper(0,name));
	}
    }
    else if (oper.name() == YSTRING("setParam")) {
	ObjList args;
	if (extractArgs(stack,oper,context,args) != 2)
	    return false;
	const String& name = *static_cast<ExpOperation*>(args[0]);
	const ExpOperation& val = *static_cast<const ExpOperation*>(args[1]);
	bool ok = m_message && name && !frozen();
	if (ok) {
	    if (JsParser::isUndefined(val))
		m_message->clearParam(name);
	    else
		m_message->setParam(name,val);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("getColumn")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
	    case 1:
		break;
	    default:
		return false;
	}
	getColumn(stack,static_cast<ExpOperation*>(args[0]),context,oper.lineNumber());
    }
    else if (oper.name() == YSTRING("getRow")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
	    case 1:
		break;
	    default:
		return false;
	}
	getRow(stack,static_cast<ExpOperation*>(args[0]),context,oper.lineNumber());
    }
    else if (oper.name() == YSTRING("getResult")) {
	ObjList args;
	if (extractArgs(stack,oper,context,args) != 2)
	    return false;
	if (!(args[0] && args[1]))
	    return false;
	getResult(stack,*static_cast<ExpOperation*>(args[0]),
	    *static_cast<ExpOperation*>(args[1]),context);
    }
    else if (oper.name() == YSTRING("enqueue")) {
	// enqueue([callback[,params[,callback_params...]])
	ExpOperVector args;
	if (!extractStackArgs(0,0,args,this,stack,oper,context))
	    return false;
	bool ok = false;
	if (m_owned && !frozen()) {
	    Message* m = m_message;
	    if (m && args[0]) {
		const ExpFunction* func = getFunction(args[0]);
		JsModuleMessage* cb = func ? YOBJECT(JsModuleMessage,m) : 0;
		if (!(cb && cb->setDispatchedCallback(*func,context,args,2,getObjParams(args[1]))))
		    return false;
	    }
	    clearMsg();
	    if (m)
		freeze();
	    ok = m && Engine::enqueue(m);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("dispatch")) {
	if (oper.number() > 1)
	    return false;
	ObjList args;
	extractArgs(stack,oper,context,args);
	bool ok = false;
	if (m_dispatch && m_message && !frozen()) {
	    Message* m = m_message;
	    bool own = m_owned;
	    clearMsg();
	    ExpOperation* async = static_cast<ExpOperation*>(args[0]);
	    if (async && async->valBoolean()) {
		ScriptRun* runner = YOBJECT(ScriptRun,context);
		if (!runner)
		    return false;
		runner->insertAsync(new JsMsgAsync(runner,&stack,this,m,own));
		runner->pause();
		return true;
	    }
	    ok = Engine::dispatch(*m);
	    m_message = m;
	    m_owned = own;
	    m_dispatch = true;
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("install"))
	return install(stack,oper,context,true);
    else if (oper.name() == YSTRING("installSingleton"))
	return install(stack,oper,context,false);
    else if (oper.name() == YSTRING("installPostHook"))
	return setPostHook(stack,oper,context,true,true);
    else if (oper.name() == YSTRING("installPostHookSingleton"))
	return setPostHook(stack,oper,context,true,false);
    else if (oper.name() == YSTRING("uninstall"))
	return uninstall(stack,oper,context,true);
    else if (oper.name() == YSTRING("uninstallSingleton"))
	return uninstall(stack,oper,context,false);
    else if (oper.name() == YSTRING("uninstallPostHook"))
	return setPostHook(stack,oper,context,false);
    else if (oper.name() == YSTRING("handlers"))
	return listHandlers(stack,oper,context,true);
    else if (oper.name() == YSTRING("handlersSingleton"))
	return listHandlers(stack,oper,context,false);
    else if (oper.name() == YSTRING("posthooks"))
	return listHandlers(stack,oper,context,true,true);
    else if (oper.name() == YSTRING("posthooksSingleton"))
	return listHandlers(stack,oper,context,false,true);
    else if (oper.name() == YSTRING("installHook"))
	return installHook(stack,oper,context);
    else if (oper.name() == YSTRING("uninstallHook")) {
	ObjList args;
	if (extractArgs(stack,oper,context,args) < 1)
	    return false;
	ObjList* o = args.skipNull();
	ExpOperation* name = static_cast<ExpOperation*>(o->get());
	NamedList hook(*name);
	for (;o;o = o->skipNext()) {
	    ExpOperation* filter = static_cast<ExpOperation*>(o->get());
	    ObjList* pair = filter->split('=',false);
	    if (pair->count() == 2)
		hook.addParam(*(static_cast<String*>((*pair)[0])), *(static_cast<String*>((*pair)[1])));
	    TelEngine::destruct(pair);
	}
	for (o = m_hooks.skipNull();o;o = o->skipNext()) {
	    JsMessageQueue* queue = static_cast<JsMessageQueue*>(o->get());
	    if (!queue->matchesFilters(hook))
		continue;
	    Engine::uninstallHook(queue);
	    m_hooks.remove(queue);
	}
    }
    else if (oper.name() == YSTRING("trackName")) {
	ObjList args;
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
		ExpEvaluator::pushOne(stack,new ExpOperation(m_trackName,oper.name()));
		break;
	    case 1:
	    case 2:
		{
		    ExpOperation* name = static_cast<ExpOperation*>(args[0]);
		    ExpOperation* prio = static_cast<ExpOperation*>(args[1]);
		    if (!name)
			return false;
		    m_trackName = *name;
		    m_trackName.trimSpaces();
		    if (prio)
			m_trackPrio = prio->valBoolean();
		    else
			m_trackPrio = true;
		}
		break;
	    default:
		return false;
	}
    }
    else if (oper.name() == YSTRING("copyParams")) {
	if (!m_message)
	    return true;
	ObjList args;
	bool skip = true;
	String prefix;
	NamedList* from = 0;
	NamedList* fromNative = 0;
	switch (extractArgs(stack,oper,context,args)) {
	    case 3:
		skip = static_cast<ExpOperation*>(args[2])->valBoolean(skip);
		// intentional
	    case 2:
		prefix = static_cast<ExpOperation*>(args[1]);
		// intentional
	    case 1:
	    {
		ExpOperation* op = static_cast<ExpOperation*>(args[0]);
		if (JsParser::isUndefined(*op) || JsParser::isNull(*op))
		    return true;
		JsObject* obj = YOBJECT(JsObject,op);
		if (obj) {
		    if (prefix) {
			from = new NamedList("");
			JsObject* subObj = YOBJECT(JsObject,obj->getField(stack,prefix,context));
			if (subObj) {
			    copyObjParams(*from,&subObj->params());
			    if (subObj->nativeParams())
				copyObjParams(*from,subObj->nativeParams());

			    for (ObjList* o = from->paramList()->skipNull(); o; o = o->skipNext()) {
				NamedString* ns = static_cast<NamedString*>(o->get());
				const_cast<String&>(ns->name()) = prefix + "." + ns->name();
			    }
			    prefix += ".";
			}
			else {
			    copyObjParams(*from,&obj->params());
			    if (obj->nativeParams())
				copyObjParams(*from,obj->nativeParams());
			}
		    }
		    else {
			from = &obj->params();
			fromNative = obj->nativeParams();
		    }
		}
		else
		    from = YOBJECT(NamedList,op);
		if (!(from || fromNative))
		    return false;
		break;
	    }
	    default:
		return false;
	}

	if (prefix) {
	    m_message->copySubParams(*from,prefix,skip,true);
	    TelEngine::destruct(from);
	}
	else {
	    if (from)
		copyObjParams(*m_message,from);
	    if (fromNative)
		copyObjParams(*m_message,fromNative);
	}
    }
    else if (oper.name() == YSTRING("clearParam")) {
	if (!m_message)
	    return true;
	ObjList args;
	char sep = 0;
	switch (extractArgs(stack,oper,context,args)) {
	    case 2:
	    {
		ExpOperation* op = static_cast<ExpOperation*>(args[1]);
		if (JsParser::isFilled(op)) {
		    if (op->length() > 1)
			return false;
		    sep = (*op)[0];
		}
		// intentional
	    }
	    case 1:
	    {
		String* name = static_cast<String*>(args[0]);
		if (TelEngine::null(name))
		    return true;
		if (!frozen())
		    m_message->clearParam(*name,sep);
		break;
	    }
	    default:
		return false;
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(!frozen()));
    }
    else if (oper.name() == YSTRING("trace")) {
	if (!m_message)
	    return true;
	ObjList args;
	unsigned int c = extractArgs(stack,oper,context,args);
	if (c < 2)
	    return false;
	ExpOperation* ret = static_cast<ExpOperation*>(args[0]);
	ExpOperation* op = static_cast<ExpOperation*>(args[1]);

	int level = -1;
	int limit = s_allowAbort ? DebugFail : DebugTest;
	if (op->number() > 1 && op->isInteger()) {
	    level = (int)op->number();	    
	    if (level > DebugAll)
		level = DebugAll;
	    else if (level < limit)
		level = limit;
	}

	String str;
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (m_traceId) {
	    if (!runner)
		return false;
	    str = runner->currentFileName();
	    str << ":" << runner->currentLineNo();
	    if (ret->isBoolean())
		str << " - return:" << ret->valBoolean();
	}

	for (unsigned int i = 2; i < c; i++) {
	    ExpOperation* op = static_cast<ExpOperation*>(args[i]);
	    if (!op)
		continue;
	    else if (*op) {
		if (str)
		    str << " ";
		str << *op;
	    }
	}

	DebugEnabler* dbg = &__plugin;
	if (runner && runner->context()) {
	    JsEngine* engine = YOBJECT(JsEngine,runner->context()->params().getParam(YSTRING("Engine")));
	    if (engine)
		dbg = engine;
	}

	if (m_traceId) {
	    if (level > m_traceLvl || level == -1)
		level = m_traceLvl;
	    if (level < limit)
		level = limit;
	    Debug(dbg,level,"Trace:%s %s",m_traceId.c_str(),str.c_str());
	    if (m_traceLst)
		m_traceLst->append(new String(str));
	}
	else if (level > -1 && str)
	    Debug(dbg,level,"%s",str.c_str());

	if (!JsParser::isUndefined(*ret))
	    ExpEvaluator::pushOne(stack,JsParser::isNull(*ret) ? 
		JsParser::nullClone() : new ExpOperation(*ret));
	else
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0,0));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsMessage::runAsync(ObjList& stack, Message* msg, bool owned)
{
    bool ok = Engine::dispatch(*msg);
    if ((m_message || m_owned) && (msg != m_message))
	Debug(&__plugin,DebugWarn,"Message replaced while async dispatching!");
    else {
	m_message = msg;
	m_owned = owned;
	m_dispatch = true;
    }
    ExpEvaluator::pushOne(stack,new ExpOperation(ok));
}

bool JsMessage::install(ObjList& stack, const ExpOperation& oper, GenObject* context,
    bool regular)
{
    // Message.install(func,name[,priority[,filterName[,filterValue[,params]]])
    // Message.installSingleton(func,handlerContext,name[,priority[,filterName[,filterValue[,params]]])
    ExpOperVector args;
    if (!extractStackArgs(regular ? 2 : 3,regular ? 6 : 7,args,this,stack,oper,context))
	return false;
    const ExpFunction* func = getFunction(args[0]);
    unsigned int idx = 1;
    ExpOperation* handlerContext = 0;
    if (!regular) {
	handlerContext = args[idx++];
	if (!*handlerContext)
	    return false;
    }
    ExpOperation* name = args[idx++];
    ExpOperation* priority = args[idx++];
    ExpOperation* filterName = args[idx++];
    ExpOperation* filterValue = args[idx++];
    const NamedList* params = getObjParams(args[idx++]);
    
    if (!(func && *name))
	return false;
    unsigned int prio = 100;
    if (priority) {
	if (!priority->isInteger() || priority->number() < 0)
	    return false;
	prio = (unsigned int)priority->number();
    }

    JsHandler* h = regular ?
	new JsHandler(*name,prio,func->name(),context,oper.lineNumber(),params) :
	new JsHandler(context,*handlerContext,func->name(),*name,prio,*handlerContext,
	    oper.lineNumber(),params);
    h->prepare(filterName,filterValue,params,0,m_trackName,m_trackPrio);
    if (JsMessageHandle::install(h)) {
	ObjList& lst = h->regular() ? m_handlers: m_handlersSingleton;
	if (h->id())
	    JsMessageHandle::uninstall(lst,h->id());
	lst.append(h);
    }
    else
	TelEngine::destruct(h);
    ExpEvaluator::pushOne(stack,new ExpOperation(!!h));
    return true;
}

bool JsMessage::uninstall(ObjList& stack, const ExpOperation& oper, GenObject* context,
    bool regular)
{
    // Message.uninstall([nameOrId[,byId]])
    // Message.uninstallSingleton(id)
    ExpOperVector args;
    if (!extractStackArgs(regular ? 0 : 1,regular ? 2 : 1,args,this,stack,oper,context))
	return false;
    if (!args.length()) {
	JsMessageHandle::uninstall(m_handlers);
	JsMessageHandle::uninstall(m_handlersSingleton);
	return true;
    }
    if (!args[0])
	return false;
    ObjList& lst = regular ? m_handlers : m_handlersSingleton;
    ObjList* rm = 0;
    // Remove regular handler by name
    if (regular && !(args[1] && args[1]->valBoolean()))
	rm = lst.find(*args[0]);
    else
	rm = JsMessageHandle::findId(*args[0],lst);
    if (rm)
	JsMessageHandle::uninstall(rm->remove(false));
    return true;
}

bool JsMessage::setPostHook(ObjList& stack, const ExpOperation& oper, GenObject* context,
    bool set, bool regular)
{
    // Message.installPostHook(func,id[,filterMsg[,filterName[,filterValue[,params]]]])
    // Message.installPostHookSingleton(func,id[,filterMsg[,filterName[,filterValue[,params]]]])
    // Message.uninstallPostHook(id)
    ExpOperVector args;
    if (!extractStackArgs(set ? 2 : 1,set ? 6 : 1,args,this,stack,oper,context))
	return false;

    ExpOperation* id = args[set ? 1 : 0];
    if (!set) {
	if (args.length()) {
	    if (!(id && *id))
		return false;
	    JsMessageHandle::uninstall(m_postHooks,*id);
	}
	else
	    JsMessageHandle::uninstall(m_postHooks);
	return true;
    }

    const ExpFunction* func = getFunction(args[0]);
    if (!(id && *id && func))
	return false;
    ExpOperation* filterMsg = args[2];
    ExpOperation* filterName = args[3];
    ExpOperation* filterValue = args[4];
    const NamedList* params = getObjParams(args[5]);
    JsPostHook* h = regular ?
	new JsPostHook(func->name(),*id,context,oper.lineNumber(),params) :
	new JsPostHook(context,*id,func->name(),*id,oper.lineNumber(),params);
    h->prepare(filterName,filterValue,params,filterMsg);
    if (JsMessageHandle::install(h)) {
	// Remove old
	JsMessageHandle::uninstall(m_postHooks,*id);
	m_postHooks.append(h);
    }
    else
	TelEngine::destruct(h);
    ExpEvaluator::pushOne(stack,new ExpOperation(!!h));
    return true;
}

bool JsMessage::listHandlers(ObjList& stack, const ExpOperation& oper, GenObject* context,
    bool regular, bool post)
{
    // Message.handlers([filter])
    // Message.handlersSingleton([filter])
    // Message.posthooks()
    ObjList args;
    ExpOperation* name = 0;
    if (!extractStackArgs(0,this,stack,oper,context,args,&name))
	return false;
    JsRegExp* rexp = YOBJECT(JsRegExp,name);
    JsArray* jsa = 0;
    ObjList& lst = post ? m_postHooks : (regular ? m_handlers : m_handlersSingleton);
    JsHandler* h = 0;
    JsPostHook* hPost = 0;
    for (ObjList* l = lst.skipNull(); l; l = l->skipNext()) {
	if (post) {
	    hPost = static_cast<JsPostHook*>(l->get());
	    if (regular != !!hPost->regular())
		continue;
	}
	else {
	    h = static_cast<JsHandler*>(l->get());
	    if (rexp) {
		if (!rexp->regexp().matches(*h))
		    continue;
	    }
	    else if (name && (*h != *name))
		continue;
	}
	JsMessageHandle* common = post ?
	    static_cast<JsMessageHandle*>(hPost) :
	    static_cast<JsMessageHandle*>(h);
	if (!jsa)
	    jsa = new JsArray(context,oper.lineNumber(),mutex());
	JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
	if (hPost)
	    jso->params().setParam(new ExpOperation(common->id(),"id"));
	if (h) {
	    jso->params().setParam(new ExpOperation(*h,"name"));
	    jso->params().setParam(new ExpOperation((int64_t)h->priority(),"priority"));
	}
	JsObject* f = JsMatchingItem::buildJsObj(hPost ? hPost->getMsgFilter() : h->getMsgFilter(),
	    context,oper.lineNumber(),mutex());
	if (f)
	    jso->setObjField("msg_filter",f);
	f = JsMatchingItem::buildJsObj(hPost ? hPost->getFilter() : h->getFilter(),
	    context,oper.lineNumber(),mutex());
	if (f)
	    jso->setObjField("filter",f);
	if (h && h->trackName())
	    jso->params().setParam(new ExpOperation(h->trackName(),"trackName"));
	jso->params().setParam(new ExpOperation(common->function().name(),"handler"));
	if (common->handlerContext())
	    jso->params().setParam(new ExpOperation(common->handlerContext(),"message_context"));
	if (h && common->id())
	    jso->params().setParam(new ExpOperation(common->id(),"id"));
	if (hPost) {
	    if (hPost->handled())
		jso->params().setParam(new ExpOperation(hPost->handled() > 0,"handled"));
	}
	jsa->push(new ExpWrapper(jso));
    }
    ExpEvaluator::pushOne(stack,JsParser::validExp(jsa,oper.name()));
    return true;
}

bool JsMessage::installHook(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList args;
    unsigned int argsCount = extractArgs(stack,oper,context,args);
    if (argsCount < 2)
	return false;
    ObjList* o = args.skipNull();
    const ExpFunction* receivedFunc = YOBJECT(ExpFunction,o->get());
    if (!receivedFunc) {
	JsFunction* jsf = YOBJECT(JsFunction,o->get());
	if (jsf)
	    receivedFunc = jsf->getFunc();
    }
    if (receivedFunc) {
	if (argsCount < 3)
	    return false;
	o = o->skipNext();
    }
    ExpOperation* name = static_cast<ExpOperation*>(o->get());
    if (TelEngine::null(name))
	return false;
    o = o->skipNext();
    ExpOperation* threads = static_cast<ExpOperation*>(o->get());
    int threadsCount = threads->toInteger(-1);
    if (threadsCount < 1)
	return false;
    o = o->skipNext();
    const ExpFunction* trapFunction = 0;
    int trapLunch = 0;
    while (o) {
	trapFunction = YOBJECT(ExpFunction,o->get());
	if (!trapFunction) {
	    JsFunction* jsf = YOBJECT(JsFunction,o->get());
	    if (jsf)
		trapFunction = jsf->getFunc();
	}
	if (!trapFunction)
	    break;
	o = o->skipNext();
	if (!o)
	    return false;
	ExpOperation* trap = static_cast<ExpOperation*>(o->get());
	trapLunch = trap->toInteger(-1);
	if (trapLunch < 0)
	    return false;
	o = o->skipNext();
    }
    JsMessageQueue* msgQueue = new JsMessageQueue(oper.lineNumber(),receivedFunc,*name,threadsCount,trapFunction,trapLunch,context);
    for (;o;o = o->skipNext()) {
	ExpOperation* filter = static_cast<ExpOperation*>(o->get());
	ObjList* pair = filter->split('=',false);
	if (pair->count() == 2)
	    msgQueue->addFilter(*(static_cast<String*>((*pair)[0])), *(static_cast<String*>((*pair)[1])));
	TelEngine::destruct(pair);
    }
    msgQueue->ref();
    msgQueue->setScriptInfo(context);
    m_hooks.append(msgQueue);
    return Engine::installHook(msgQueue);
}

void JsMessage::getColumn(ObjList& stack, const ExpOperation* col, GenObject* context, unsigned int lineNo)
{
    Array* arr = m_message ? YOBJECT(Array,m_message->userData()) : 0;
    if (arr && arr->getRows()) {
	int rows = arr->getRows() - 1;
	int cols = arr->getColumns();
	if (col) {
	    // [ val1, val2, val3 ]
	    int idx = -1;
	    if (col->isInteger())
		idx = (int)col->number();
	    else {
		for (int i = 0; i < cols; i++) {
		    GenObject* o = arr->get(i,0);
		    if (o && (o->toString() == *col)) {
			idx = i;
			break;
		    }
		}
	    }
	    if (idx >= 0 && idx < cols) {
		JsArray* jsa = new JsArray(context,lineNo,mutex());
		for (int r = 1; r <= rows; r++) {
		    GenObject* o = arr->get(idx,r);
		    if (o) {
			const DataBlock* d = YOBJECT(DataBlock,o);
			if (d) {
			    String x;
			    jsa->push(new ExpOperation(x.hexify(d->data(),d->length()),0,false));
			}
			else
			    jsa->push(new ExpOperation(o->toString(),0,true));
		    }
		    else
			jsa->push(JsParser::nullClone());
		}
		ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"column"));
		return;
	    }
	}
	else {
	    // { col1: [ val11, val12, val13], col2: [ val21, val22, val23 ] }
	    JsObject* jso = new JsObject(context,lineNo,mutex());
	    for (int c = 0; c < cols; c++) {
		const String* name = YOBJECT(String,arr->get(c,0));
		if (TelEngine::null(name))
		    continue;
		JsArray* jsa = new JsArray(context,lineNo,mutex());
		for (int r = 1; r <= rows; r++) {
		    GenObject* o = arr->get(c,r);
		    if (o) {
			const DataBlock* d = YOBJECT(DataBlock,o);
			if (d) {
			    String x;
			    jsa->push(new ExpOperation(x.hexify(d->data(),d->length()),*name,false));
			}
			else
			    jsa->push(new ExpOperation(o->toString(),*name,true));
		    }
		    else
			jsa->push(JsParser::nullClone());
		}
		jso->params().setParam(new ExpWrapper(jsa,*name));
	    }
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,"columns"));
	    return;
	}
    }
    ExpEvaluator::pushOne(stack,JsParser::nullClone());
}

void JsMessage::getRow(ObjList& stack, const ExpOperation* row, GenObject* context, unsigned int lineNo)
{
    Array* arr = m_message ? YOBJECT(Array,m_message->userData()) : 0;
    if (arr && arr->getRows()) {
	int rows = arr->getRows() - 1;
	int cols = arr->getColumns();
	if (row) {
	    // { col1: val1, col2: val2 }
	    if (row->isInteger()) {
		int idx = (int)row->number() + 1;
		if (idx > 0 && idx <= rows) {
		    JsObject* jso = new JsObject(context,lineNo,mutex());
		    for (int c = 0; c < cols; c++) {
			const String* name = YOBJECT(String,arr->get(c,0));
			if (TelEngine::null(name))
			    continue;
			GenObject* o = arr->get(c,idx);
			if (o) {
			    const DataBlock* d = YOBJECT(DataBlock,o);
			    if (d) {
				String x;
				jso->params().setParam(new ExpOperation(x.hexify(d->data(),d->length()),*name,false));
			    }
			    else
				jso->params().setParam(new ExpOperation(o->toString(),*name,true));
			}
			else
			    jso->params().setParam((JsParser::nullClone(*name)));
		    }
		    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,"row"));
		    return;
		}
	    }
	}
	else {
	    // [ { col1: val11, col2: val12 }, { col1: val21, col2: val22 } ]
	    JsArray* jsa = new JsArray(context,lineNo,mutex());
	    for (int r = 1; r <= rows; r++) {
		JsObject* jso = new JsObject(context,lineNo,mutex());
		for (int c = 0; c < cols; c++) {
		    const String* name = YOBJECT(String,arr->get(c,0));
		    if (TelEngine::null(name))
			continue;
		    GenObject* o = arr->get(c,r);
		    if (o) {
			const DataBlock* d = YOBJECT(DataBlock,o);
			if (d) {
			    String x;
			    jso->params().setParam(new ExpOperation(x.hexify(d->data(),d->length()),*name,false));
			}
			else
			    jso->params().setParam(new ExpOperation(o->toString(),*name,true));
		    }
		    else
			jso->params().setParam((JsParser::nullClone(*name)));
		}
		jsa->push(new ExpWrapper(jso));
	    }
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"rows"));
	    return;
	}
    }
    ExpEvaluator::pushOne(stack,JsParser::nullClone());
}

void JsMessage::getResult(ObjList& stack, const ExpOperation& row, const ExpOperation& col, GenObject* context)
{
    Array* arr = m_message ? YOBJECT(Array,m_message->userData()) : 0;
    if (arr && arr->getRows() && row.isInteger()) {
	int rows = arr->getRows() - 1;
	int cols = arr->getColumns();
	int r = (int)row.number();
	if (r >= 0 && r < rows) {
	    int c = -1;
	    if (col.isInteger())
		c = (int)col.number();
	    else {
		for (int i = 0; i < cols; i++) {
		    GenObject* o = arr->get(i,0);
		    if (o && (o->toString() == col)) {
			c = i;
			break;
		    }
		}
	    }
	    if (c >= 0 && c < cols) {
		GenObject* o = arr->get(c,r + 1);
		if (o) {
		    ExpEvaluator::pushOne(stack,new ExpOperation(o->toString(),0,true));
		    return;
		}
	    }
	}
    }
    ExpEvaluator::pushOne(stack,JsParser::nullClone());
}

JsObject* JsMessage::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsMessage::runConstructor '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
	case 2:
	case 3:
	    break;
	default:
	    return 0;
    }
    ExpOperation* name = static_cast<ExpOperation*>(args[0]);
    ExpOperation* broad = static_cast<ExpOperation*>(args[1]);
    JsObject* objParams = YOBJECT(JsObject,args[2]);
    if (!name)
	return 0;
    if (!ref())
	return 0;
    Message* m = new JsModuleMessage(*name,broad && broad->valBoolean());
    if (objParams) {
	copyObjParams(*m,&objParams->params());
	if (objParams->nativeParams())
	    copyObjParams(*m,objParams->nativeParams());
    }
    const String& traceId = YOBJECT(ScriptRun,context) ? context->traceId() : String::empty();
    if (traceId)
	m->setParam(YSTRING("trace_id"),traceId);
    JsMessage* obj = new JsMessage(m,mutex(),oper.lineNumber(),true,true);
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

void JsMessage::initialize(ScriptContext* context, bool allowSingleton)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("Message")))
	addConstructor(params,"Message",new JsMessage(mtx,allowSingleton));
}


bool JsMessageHandle::handle(Message& msg, bool handled)
{
    bool postHook = !handler();
    bool doHandle = !s_engineStop;
    bool regular = this->regular();
    if (doHandle) {
	if (regular)
	    doHandle = m_code;
	else
	    doHandle = m_script || m_code;
    }
    if (!doHandle) {
	if (m_handler)
	    m_handler->safeNowInternal();
	return false;
    }
    XDebug(&__plugin,DebugAll,"Running %s message %s for '%s' [%p]",
	m_function.name().c_str(),clsType(handler()),desc(),this);
#ifdef JS_DEBUG_JsMessage_received
#ifdef JS_DEBUG_JsMessage_received_explicit
    bool reportDuration = msg.getBoolValue(YSTRING("report_js_duration"));
#else
    bool reportDuration = true;
#endif
    uint64_t tm = Time::now();
#endif
    String dbg, loadExt;
    ScriptRun* runner = 0;
    if (regular) {
	runner = m_code->createRunner(m_context,NATIVE_TITLE);
	attachScriptInfo(runner);
    }
    else {
	Lock lck(m_mutex);
	if (m_script) {
	    if (MsgHandlerGlobal == type()) // Loaded from configuration
		runner = m_script->parser().createRunner(0,NATIVE_TITLE);
	    else                            // Installed inside global script
		runner = m_script->parser().createRunner(m_code,0,NATIVE_TITLE);
	    if (attachScriptInfo(runner)) {
		dbg = m_debug;
		loadExt = m_loadExt;
	    }
	    else if (runner) {
		lck.drop();
		TelEngine::destruct(runner);
	    }
	}
    }
    if (!runner) {
	if (m_handler)
	    m_handler->safeNowInternal();
	return false;
    }
    
    if (!regular) {
	// TODO: Track object creation if we implement a mechanism to investigate it
	//       It is useless to enable tracking for now: the context will be destroyed on return
	//runner->context()->trackObjs(s_trackCreation);
	// Avoid recursive handling of script.init
	bool autoExt = loadExt.toBoolean(s_autoExt) &&
	    (!m_matchesScriptInit || msg != YSTRING("script.init"));
	contextInit(runner,postHook ? "MessagePostHook" : "MessageHandler",autoExt);
	if (dbg || m_context) {
	    JsEngine* eng = JsEngine::get(runner);
	    if (eng) {
		if (dbg)
		    eng->setDebug(dbg);
		else {
		    // Loaded from specific script: propagate debug
		    Lock lck(m_context->mutex());
		    JsEngine* cEng = JsEngine::get(m_context);
		    if (cEng) {
			eng->debugLevel(cEng->debugLevel());
			eng->debugEnabled(cEng->debugEnabled());
		    }
		}
	    }
	}
    }
    JsMessage* jm = new JsMessage(&msg,runner->context()->mutex(),m_lineNo,true);
    jm->setPrototype(runner->context(),YSTRING("Message"));
    jm->ref();
    String name = m_function.name();
    String handlerCtx = m_handlerContext;
    // Starting from here the handler may be safely uninstalled and destroyed
    if (m_handler)
	m_handler->safeNowInternal();
    else
	// Freeze the message
	jm->freeze();

    ObjList args;
    args.append(new ExpWrapper(jm,"message"));
    if (postHook)
	args.append(new ExpOperation(handled));
    if (handlerCtx || !regular)
	args.append(new ExpOperation(handlerCtx));
#ifdef JS_DEBUG_JsMessage_received
    uint64_t run = Time::now();
    uint64_t runScriptInit = 0;
#endif
    ScriptRun::Status rval = ScriptRun::Succeeded;
    if (regular)
	rval = runner->call(name,args);
    else {
	// Init globals and call the function handling the message
#ifdef JS_DEBUG_JsMessage_received
	runScriptInit = Time::now();
#endif
	rval = runner->run();
#ifdef JS_DEBUG_JsMessage_received
	runScriptInit = Time::now() - runScriptInit;
#endif
	if (rval == ScriptRun::Succeeded)
	    rval = runner->call(name,args);
    }
#ifdef JS_DEBUG_JsMessage_received
    run = Time::now() - run;
    if (!regular)
	run -= runScriptInit;
    String info(postHook ? "PostHook" : "Handler");
    info << " type=" << type();
    // NOTE: The following is not thread safe
    JsEngine* eng = JsEngine::get(runner); if (eng) info << " engine='" << eng->getDebugName() << "'";
    info << " for '" << msg.safe() << "' desc='" << desc() << "'";
#endif
    jm->clearMsg();
    bool ok = postHook;
    if (ScriptRun::Succeeded == rval) {
	ExpOperation* op = ExpEvaluator::popOne(runner->stack());
	if (op) {
	    if (!postHook)
		ok = op->valBoolean();
	    TelEngine::destruct(op);
	}
    }
    TelEngine::destruct(jm);
    // Clear now the arguments list: the context may be destroyed
    args.clear();
    // Using a singleton context: cleanup the context
    if (!regular)
	runner->context()->cleanup();
    TelEngine::destruct(runner);

#ifdef JS_DEBUG_JsMessage_received
    if (reportDuration) {
	tm = Time::now() - tm;
	uint64_t rest = tm - (run + runScriptInit);
	String extra;
	if (!regular)
	    extra.printf(" init=%u.%03ums",(unsigned int)(runScriptInit / 1000),
		(unsigned int)(runScriptInit % 1000));
	extra.printfAppend(" (+%u.%03ums)",(unsigned int)(rest / 1000),
	    (unsigned int)(rest % 1000));
	Debug(&__plugin,DebugInfo,"%s ran for %u.%03ums%s [%p]",info.safe(),
	    (unsigned int)(run / 1000),(unsigned int)(run % 1000),extra.safe(),this);
    }
#endif
    return ok;
}

bool JsMessageHandle::initialize(const NamedList& params, const String& scriptName,
    const String& scriptFile, const String& prefix)
{
    Lock lck(m_mutex);
    switch (type()) {
	case MsgHandlerGlobal:
	    break;
	case Regular:
	    if (handler())
		m_handlerContext = m_id = params[YSTRING("id")];
	    return true;
	case MsgHandlerScript:
	    m_loadExt = params[YSTRING("load_extensions")];
	    m_debug = params["debug"];
	    return true;
	default:
	    return false;
    }

    if (prefix) {
	m_loadExt = params[prefix + "load_extensions"];
	m_debug = params[prefix + "debug"];
    }
    else {
	m_loadExt.clear();
	m_debug.clear();
    }
    m_inUse = m_script && !m_script->fileChanged(scriptFile);
    if (m_inUse)
	return true;
    lck.drop();
    JsGlobal* newScript = new JsGlobal(scriptName,scriptFile,ScriptInfo::MsgHandler);
    String err;
    if (newScript->load()) {
	ScriptRun* runner = newScript->parser().createRunner(0,NATIVE_TITLE);
	if (!(runner && runner->callable(m_function.name()))) {
	    err = ": callback function not found";
	    TelEngine::destruct(newScript);
	}
	TelEngine::destruct(runner);
    }
    else
	TelEngine::destruct(newScript);
    bool replace = newScript || (!prefix ? JsGlobal::s_keepOldOnFail :
	params.getBoolValue(prefix + "keep_old_on_fail",JsGlobal::s_keepOldOnFail));
    lck.acquire(m_mutex);
    JsGlobal* old = 0;
    if (replace) {
	old = m_script;
	m_script = newScript;
	setScriptInfo(m_script ? m_script->scriptInfo() : 0);
    }
    bool ok = m_inUse = (0 != m_script);
    lck.drop();
    TelEngine::destruct(old);
    if (ok)
	return true;
    Debug(&__plugin,DebugNote,"Failed to load script for message %s %s (%p)%s",
	handler() ? "handler" : "posthook",desc(),this,err.safe());
    return false;
}

void JsMessageHandle::prepare(GenObject* name, GenObject* value, const NamedList* params,
    GenObject* msgName, const String& trackName, bool trackPrio)
{
    JsPostHook* post = m_handler ? 0 : static_cast<JsPostHook*>(this);
    MessageFilter* flt = m_handler ? static_cast<MessageFilter*>(m_handler)
	: static_cast<MessageFilter*>(post);
    if (name) {
	ExpOperation* op = YOBJECT(ExpOperation,name);
	const String& n = op ? (const String&)*op : name->toString();
	flt->setFilter(JsMatchingItem::buildFilter(n,value,name));
    }
    if (m_handler) {
	m_matchesScriptInit = *m_handler == YSTRING("script.init");
        if (trackName) {
	    if (trackPrio)
		m_handler->trackName(trackName + ":" + String(m_handler->priority()));
	    else
		m_handler->trackName(trackName);
	}
    }
    else if (post) {
	MatchingItemBase* f = JsMatchingItem::buildFilter("message",msgName,msgName,false);
	const NamedList& p = params ? *params : NamedList::empty();
	// engine.timer MUST be explicily allowed if no other message name filter is given
	if (!p.getBoolValue(YSTRING("engine.timer"),!!f)) {
	    MatchingItemString* mi = new MatchingItemString("","engine.timer",false,true);
	    if (f) {
		ObjList tmp;
		tmp.append(f);
		tmp.append(mi);
		MatchingItemList* l = new MatchingItemList("");
		if (l->append(tmp))
		    f = l;
		else
		    TelEngine::destruct(l);
	    }
	    else
		f = mi;
	}
	post->setMsgFilter(f);
	m_matchesScriptInit = post->getMsgFilter() &&
	    post->getMsgFilter()->matchString("script.init");
    }
#if 0
    MatchingItemDump mid;
    mid.m_regexpBasic = 'b';
    mid.m_caseInsentive = 'i';
    String mf, pf;
    mid.dump(flt->getFilter(),pf,"\r\n");
    mid.dump(post ? post->getMsgFilter() : 0,mf,"\r\n");
    if (mf)
	mf = "\r\nMessage filter:" + mf;
    if (pf)
	pf = "\r\nParameters filter:" + pf;
    if (mf || pf) {
	mf = "\r\n-----\r\nid: " + id() + mf + pf + "\r\n-----";
	Debug(&__plugin,DebugTest,"Prepared %s%s",clsType(handler()),mf.safe());
    }
#endif
}

bool JsMessageHandle::install(GenObject* gen)
{
    if (!gen)
	return false;
    JsHandler* h = YOBJECT(JsHandler,gen);
    if (h)
	return Engine::install(h);
    JsPostHook* hPost = YOBJECT(JsPostHook,gen);
    return hPost && Engine::self() && Engine::self()->setHook(hPost);
}

bool JsMessageHandle::uninstall(GenObject* gen)
{
    if (!gen || Engine::exiting())
	return false;
    JsHandler* h = YOBJECT(JsHandler,gen);
    bool ok = false;
    if (h)
	ok = Engine::uninstall(h);
    else {
	JsPostHook* hPost = YOBJECT(JsPostHook,gen);
	ok = hPost && Engine::self() && Engine::self()->setHook(hPost,true);
    }
    TelEngine::destruct(gen);
    return ok;
}

ObjList* JsMessageHandle::findId(const String& id, ObjList& list)
{
    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
	JsHandler* h = YOBJECT(JsHandler,o->get());
	JsPostHook* hPost = h ? 0 : YOBJECT(JsPostHook,o->get());
	if (h) {
	    if (id == h->id())
		return o;
	}
	else if (hPost && id == hPost->id())
	    return o;
    }
    return 0;
}

void JsMessageQueue::received(Message& msg)
{
    if (s_engineStop || !m_code)
	return;
    if (!m_receivedFunction) {
	MessageQueue::received(msg);
	return;
    }
    ScriptRun* runner = m_code->createRunner(m_context,NATIVE_TITLE);
    if (!runner)
	return;
    attachScriptInfo(runner);
    JsMessage* jm = new JsMessage(&msg,runner->context()->mutex(),m_lineNo,true);
    jm->setPrototype(runner->context(),YSTRING("Message"));
    jm->ref();
    ObjList args;
    args.append(new ExpWrapper(jm,"message"));
    runner->call(m_receivedFunction->name(),args);
    jm->clearMsg();
    TelEngine::destruct(jm);
    TelEngine::destruct(runner);
}

bool JsMessageQueue::enqueue(Message* msg)
{
    if (!count())
	m_trapCalled = false;
    bool ret = MessageQueue::enqueue(msg);
    if (!ret || !m_trapLunch || !m_trapFunction || m_trapCalled || count() < m_trapLunch)
	return ret;
    if (s_engineStop || !m_code)
	return ret;

    ScriptRun* runner = m_code->createRunner(m_context,NATIVE_TITLE);
    if (!runner)
	return ret;
    attachScriptInfo(runner);
    ObjList args;
    runner->call(m_trapFunction->name(),args);
    TelEngine::destruct(runner);
    m_trapCalled = true;
    return ret;
}

bool JsMessageQueue::matchesFilters(const NamedList& filters)
{
    const NamedList origFilters = getFilters();
    if (origFilters != filters)
	return false;
    unsigned int ofCount = origFilters.count(), fcount = filters.count();
    if (ofCount != fcount)
	return false;
    if (!ofCount)
	return true;
    for (unsigned int i = 0;i < origFilters.length();i++) {
	NamedString* param = origFilters.getParam(i);
	NamedString* secParam = filters.getParam(*param);
	if (!secParam || *secParam != *param)
	    return false;
    }
    return true;
}


bool JsFile::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsFile::runNative '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    if (oper.name() == YSTRING("exists")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(File::exists(*op)));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("remove")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(File::remove(*op)));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("rename")) {
	if (oper.number() != 2)
	    return false;
	ExpOperation* newName = popValue(stack,context);
	if (!newName)
	    return false;
	ExpOperation* oldName = popValue(stack,context);
	if (!oldName) {
	    TelEngine::destruct(newName);
	    return false;
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(File::rename(*oldName,*newName)));
	TelEngine::destruct(oldName);
	TelEngine::destruct(newName);
    }
    else if (oper.name() == YSTRING("mkdir")) {
	int mode = -1;
	ExpOperation* op = 0;
	switch (oper.number()) {
	    case 2:
		op = popValue(stack,context);
		if (op && op->isInteger())
		    mode = op->number();
		TelEngine::destruct(op);
		// fall through
	    case 1:
		op = popValue(stack,context);
		break;
	    default:
		return false;
	}
	if (!op)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(File::mkDir(*op,0,mode)));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("rmdir")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(File::rmDir(*op)));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("getFileTime")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	if (!op)
	    return false;
	unsigned int epoch = 0;
	int64_t fTime = File::getFileTime(*op,epoch) ? (int64_t)epoch : -1;
	ExpEvaluator::pushOne(stack,new ExpOperation(fTime));
	TelEngine::destruct(op);
    }
    else if (oper.name() == YSTRING("setFileTime")) {
	if (oper.number() != 2)
	    return false;
	ExpOperation* fTime = popValue(stack,context);
	if (!fTime)
	    return false;
	ExpOperation* fName = popValue(stack,context);
	if (!fName) {
	    TelEngine::destruct(fTime);
	    return false;
	}
	bool ok = fTime->isInteger() && (fTime->number() >= 0) &&
	    File::setFileTime(*fName,(unsigned int)fTime->number());
	TelEngine::destruct(fTime);
	TelEngine::destruct(fName);
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("getContent")) {
	// str = File.getContent(name[,binary[,maxlen]])
	bool binary = false;
	int maxRead = 65536;
	ExpOperation* op = 0;
	switch (oper.number()) {
	    case 3:
		op = popValue(stack,context);
		if (op)
		    maxRead = op->toInteger(maxRead,0,0,262144);
		TelEngine::destruct(op);
		// fall through
	    case 2:
		op = popValue(stack,context);
		binary = op && op->toBoolean();
		TelEngine::destruct(op);
		// fall through
	    case 1:
		op = popValue(stack,context);
		break;
	    default:
		return false;
	}
	ExpOperation* ret = 0;
	if (op) {
	    File f;
	    if (f.openPath(*op,false,true,false,false,binary)) {
		DataBlock buf(0,maxRead);
		int rd = f.readData(buf.data(),buf.length());
		if (rd >= 0) {
		    buf.truncate(rd);
		    ret = new ExpOperation("");
		    if (binary)
			ret->hexify(buf.data(),buf.length());
		    else
			ret->assign((const char*)buf.data(),buf.length());
		}
	    }
	}
	TelEngine::destruct(op);
	if (!ret)
	    ret = JsParser::nullClone();
	ExpEvaluator::pushOne(stack,ret);
    }
    else if (oper.name() == YSTRING("setContent")) {
	// len = File.setContent(name,content[,binary|params])
	bool create = true;
	bool append = false;
	bool binary = false;
	bool pubRead = false;
	bool pubWrite = false;
	ExpOperation* op = 0;
	ExpOperation* cont = 0;
	switch (oper.number()) {
	    case 3:
		op = popValue(stack,context);
		{
		    JsObject* obj = YOBJECT(JsObject,op);
		    if (obj) {
			obj->getBoolField(YSTRING("create"),create);
			obj->getBoolField(YSTRING("append"),append);
			obj->getBoolField(YSTRING("binary"),binary);
			obj->getBoolField(YSTRING("pubread"),pubRead);
			obj->getBoolField(YSTRING("pubwrite"),pubWrite);
		    }
		    else
			binary = op && op->toBoolean();
		}
		TelEngine::destruct(op);
		// fall through
	    case 2:
		cont = popValue(stack,context);
		op = popValue(stack,context);
		break;
	    default:
		return false;
	}
	int64_t wr = -1;
	if (op && cont) {
	    File f;
	    if (f.openPath(*op,true,false,create,append,binary,pubRead,pubWrite)) {
		if (binary) {
		    DataBlock buf;
		    if (buf.unHexify(cont->c_str(),cont->length()))
			wr = static_cast<Stream&>(f).writeData(buf);
		}
		else
		    wr = static_cast<Stream&>(f).writeData(*cont);
	    }
	}
	TelEngine::destruct(op);
	TelEngine::destruct(cont);
	ExpEvaluator::pushOne(stack,new ExpOperation(wr));
    }
    else if (oper.name() == YSTRING("listDirectory")) {
	// res = File.listDirectory(path[,params])
	ExpOperation* path = 0;
	ExpOperation* opParams = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&path,&opParams))
	    return false;
	bool file = true;
	bool dir = false;
	JsObject* jso = YOBJECT(JsObject,opParams);
	if (jso) {
	    jso->getBoolField(YSTRING("list_file"),file);
	    if (!file)
		jso->getBoolField(YSTRING("list_dir"),dir);
	}
	ObjList res;
	JsArray* jsa = new JsArray(context,oper.lineNumber(),mutex());
	if (dir || file) {
	    if (File::listDirectory(*path,dir ? &res : 0,file ? &res : 0))
		jsa->push(res);
	    else
		TelEngine::destruct(jsa);
	}
	if (jsa)
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"list"));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsFile::initialize(ScriptContext* context)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("File")))
	addObject(params,"File",new JsFile(mtx));
}

void JsSemaphore::runAsync(ObjList& stack, long maxWait)
{
    if (m_exit) {
	ExpEvaluator::pushOne(stack,new ExpOperation(false));
	return;
    }
    bool ret = m_semaphore.lock(maxWait);
    if (m_exit)
	ret = false;
    ExpEvaluator::pushOne(stack,new ExpOperation(ret));
}

bool JsSemaphore::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsSemaphore::runNative '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    if (oper.name() == YSTRING("wait")) {
	if (oper.number() != 1)
	    return false;
	ExpOperation* op = popValue(stack,context);
	long wait = 0;
	if (JsParser::isNull(*op))
	    wait = -1;
	else if ((wait = op->toInteger()) < 0)
	    wait = 0;
	TelEngine::destruct(op);
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	if (!runner)
	    return false;
	runner->insertAsync(new JsSemaphoreAsync(runner,&stack,this,wait));
	runner->pause();
    }
    else if (oper.name() == YSTRING("signal")) {
	if (oper.number())
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(m_semaphore.unlock()));
    }
    else
	return false;
    return true;
}

void JsSemaphore::removeSemaphore(JsSemaphore* sem)
{
    Lock myLock(mutex());
    m_semaphores.remove(sem,false);
}

void JsSemaphore::forceExit()
{
    m_exit = true;
    m_constructor = 0;
    m_semaphore.unlock();
}

JsObject* JsSemaphore::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList args;
    int maxcount = 1;
    int initialCount = 0;
    const char* name = "JsSemaphore";
    switch (extractArgs(stack,oper,context,args)) {
	case 3:
	    name = static_cast<ExpOperation*>(args[2])->c_str();
	    // Intentional
	case 2:
	    initialCount = static_cast<ExpOperation*>(args[1])->toInteger(-1);
	    if (initialCount < 0)
		initialCount = 0;
	    // Intentional
	case 1:
	    maxcount = static_cast<ExpOperation*>(args[0])->toInteger();
	    if (maxcount < 1)
		maxcount = 1;
	    // Intentional
	case 0:
	    break;
	default:
	    return 0;
    }
    JsSemaphore* sem = new JsSemaphore(this,mutex(),oper.lineNumber(),maxcount,initialCount,name);
    mutex()->lock();
    m_semaphores.append(sem);
    mutex()->unlock();
    // Set the object prototype.
    // Custom because the Constructor is part of Engine object.
    ScriptContext* ctxt = YOBJECT(ScriptContext,context);
    if (!ctxt) {
	ScriptRun* sr = YOBJECT(ScriptRun,context);
	if (!(sr && (ctxt = YOBJECT(ScriptContext,sr->context()))))
	    return sem;
    }
    JsObject* engine = YOBJECT(JsObject,ctxt->params().getParam(YSTRING("Engine")));
    if (!engine)
	return sem;
    JsObject* semCtr = YOBJECT(JsObject,engine->params().getParam(YSTRING("Semaphore")));
    if (semCtr) {
	JsObject* proto = YOBJECT(JsObject,semCtr->params().getParam(YSTRING("prototype")));
	if (proto && proto->ref())
	    sem->params().addParam(new ExpWrapper(proto,protoName()));
    }
    return sem;
}

void* JsConfigFile::getObject(const String& name) const
{
    void* obj = (name == YATOM("JsConfigFile")) ? const_cast<JsConfigFile*>(this) : JsObject::getObject(name);
    if (!obj)
	obj = m_config.getObject(name);
    return obj;
}

bool JsConfigFile::runFuncKeys(ObjList& stack, const ExpOperation& oper, GenObject* context,
    JsConfigSection* jSect)
{
    // Config:  keys(sect)
    // Section: keys()
    ExpOperVector args;
    if (!extractArgs(stack,oper,context,args,jSect ? 0 : 1))
	return false;
    const String& sName = jSect ? jSect->toString() : *static_cast<const String*>(args[0]);
    NamedList* sect = m_config.getSection(sName);
    JsArray* jsa = sect ? JsObject::arrayProps(-1,sect,context,oper.lineNumber(),mutex()) : 0;
    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,oper.name()));
    return true;
}

static inline void handleCfgSetValues(bool set, Configuration& cfg, const String& sName,
    GenObject* params, const String* prefix)
{
    const NamedList* pList = sName ? getReplaceParams(params) : 0;
    if (!pList)
	return;
    NamedList* sect = cfg.createSection(sName);
    if (TelEngine::null(prefix))
	prefix = 0;
    for (ObjList* o = pList->paramList()->skipNull(); o; o = o->skipNext()) {
	NamedString* ns = static_cast<NamedString*>(o->get());
	JsObject* jso = YOBJECT(JsObject,ns);
	if (jso || ns->name() == JsObject::protoName())
	    continue;
	if (set) {
	    if (prefix)
		sect->setParam(*prefix + ns->name(),*ns);
	    else
		sect->setParam(ns->name(),*ns);
	}
	else if (prefix)
	    sect->addParam(*prefix + ns->name(),*ns);
	else
	    sect->addParam(ns->name(),*ns);
    }
}

static inline void handleCfgClearKey(Configuration& cfg, const String& sName, const String& kName,
    const String* kVal = 0)
{
    NamedList* sect = cfg.getSection(sName);
    if (!sect)
	return;
    if (kVal) {
	JsRegExp* r = YOBJECT(JsRegExp,kVal);
	if (r)
	    kVal = static_cast<const String*>(&r->regexp());
    }
    sect->clearParam(kName,0,kVal);
}

bool JsConfigFile::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsConfigFile::runNative '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    if (oper.name() == YSTRING("name")) {
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
		ExpEvaluator::pushOne(stack,new ExpOperation(m_config));
		break;
	    case 1:
		m_config = *static_cast<ExpOperation*>(args[0]);
		break;
	    default:
		return false;
	}
    }
    else if (oper.name() == YSTRING("load")) {
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
	    case 1:
		break;
	    default:
		return false;
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(m_config.load(args[0]
	    && static_cast<ExpOperation*>(args[0])->valBoolean())));
    }
    else if (oper.name() == YSTRING("save")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(m_config.save()));
    }
    else if (oper.name() == YSTRING("count")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_config.sections()));
    }
    else if (oper.name() == YSTRING("sections")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
	unsigned int n = m_config.sections();
	for (unsigned int i = 0; i < n; i++) {
	    NamedList* nl = m_config.getSection(i);
	    if (nl)
		jso->params().addParam(new ExpWrapper(new JsConfigSection(this,*nl,oper.lineNumber()),*nl));
	}
	ExpEvaluator::pushOne(stack,new ExpWrapper(jso,"sections"));
    }
    else if (oper.name() == YSTRING("getSection")) {
	bool create = false;
	switch (extractArgs(stack,oper,context,args)) {
	    case 2:
		create = static_cast<ExpOperation*>(args[1])->valBoolean();
		break;
	    case 1:
		break;
	    default:
		return false;
	}
	const String& name = *static_cast<ExpOperation*>(args[0]);
	if (create ? m_config.createSection(name) : m_config.getSection(name))
	    ExpEvaluator::pushOne(stack,new ExpWrapper(new JsConfigSection(this,name,oper.lineNumber()),name));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getValue")) {
	switch (extractArgs(stack,oper,context,args)) {
	    case 2:
	    case 3:
		break;
	    default:
		return false;
	}
	const String& name = *static_cast<ExpOperation*>(args[1]);
	static const char defVal[] = "default";
	const char* val = m_config.getValue(*static_cast<ExpOperation*>(args[0]),name,defVal);
	if (val == defVal) {
	    if (args[2])
		ExpEvaluator::pushOne(stack,static_cast<ExpOperation*>(args[2])->clone(name));
	    else
		ExpEvaluator::pushOne(stack,new ExpWrapper(0,name));
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpOperation(val,name));
    }
    else if (oper.name() == YSTRING("getIntValue")) {
	int64_t defVal = 0;
	int64_t minVal = LLONG_MIN;
	int64_t maxVal = LLONG_MAX;
	bool clamp = true;
	switch (extractArgs(stack,oper,context,args)) {
	    case 6:
		clamp = static_cast<ExpOperation*>(args[5])->valBoolean(clamp);
		// fall through
	    case 5:
		maxVal = static_cast<ExpOperation*>(args[4])->valInteger(maxVal);
		// fall through
	    case 4:
		minVal = static_cast<ExpOperation*>(args[3])->valInteger(minVal);
		// fall through
	    case 3:
		defVal = static_cast<ExpOperation*>(args[2])->valInteger();
		// fall through
	    case 2:
		break;
	    default:
		return false;
	}
	const String& sect = *static_cast<ExpOperation*>(args[0]);
	const String& name = *static_cast<ExpOperation*>(args[1]);
	ExpEvaluator::pushOne(stack,new ExpOperation(m_config.getInt64Value(sect,name,defVal,minVal,maxVal,clamp),name));
    }
    else if (oper.name() == YSTRING("getBoolValue")) {
	bool defVal = false;
	switch (extractArgs(stack,oper,context,args)) {
	    case 3:
		defVal = static_cast<ExpOperation*>(args[2])->valBoolean();
		// fall through
	    case 2:
		break;
	    default:
		return false;
	}
	const String& sect = *static_cast<ExpOperation*>(args[0]);
	const String& name = *static_cast<ExpOperation*>(args[1]);
	ExpEvaluator::pushOne(stack,new ExpOperation(m_config.getBoolValue(sect,name,defVal),name));
    }
    else if (oper.name() == YSTRING("setValue")) {
	if (extractArgs(stack,oper,context,args) != 3)
	    return false;
	m_config.setValue(*static_cast<ExpOperation*>(args[0]),*static_cast<ExpOperation*>(args[1]),
	    *static_cast<ExpOperation*>(args[2]));
    }
    else if (oper.name() == YSTRING("addValue")) {
	if (extractArgs(stack,oper,context,args) != 3)
	    return false;
	m_config.addValue(*static_cast<ExpOperation*>(args[0]),*static_cast<ExpOperation*>(args[1]),
	    *static_cast<ExpOperation*>(args[2]));
    }
    else if (oper.name() == YSTRING("setValues") || oper.name() == YSTRING("addValues")) {
	// setValues(sect,params[,prefix])
	// addValues(sect,params[,prefix])
	ExpOperation* sName = 0;
	ExpOperation* params = 0;
	ExpOperation* prefix = 0;
	if (!extractStackArgs(2,this,stack,oper,context,args,&sName,&params,&prefix))
	    return false;
	handleCfgSetValues(oper.name() == YSTRING("setValues"),m_config,*sName,params,prefix);
    }
    else if (oper.name() == YSTRING("clearSection")) {
	ExpOperation* op = 0;
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
		break;
	    case 1:
		op = static_cast<ExpOperation*>(args[0]);
		if (JsParser::isUndefined(*op) || JsParser::isNull(*op))
		    op = 0;
		break;
	    default:
		return false;
	}
	m_config.clearSection(op ? (const char*)*op : 0);
    }
    else if (oper.name() == YSTRING("clearKey")) {
	// clearKey(sect,key[,matchValue])
	ExpOperation* sect = 0;
	ExpOperation* key = 0;
	ExpOperation* matchValue = 0;
	if (!extractStackArgs(2,this,stack,oper,context,args,&sect,&key,&matchValue))
	    return false;
	handleCfgClearKey(m_config,*sect,*key,matchValue);
    }
    else if (oper.name() == YSTRING("keys")) {
	if (!runFuncKeys(stack,oper,context))
	    return false;
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

JsObject* JsConfigFile::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsConfigFile::runConstructor '%s'(" FMT64 ") [%p]",oper.name().c_str(),oper.number(),this);
    bool warn = false;
    const char* name = 0;
    ObjList args;
    switch (extractArgs(stack,oper,context,args)) {
	case 2:
	    warn = static_cast<ExpOperation*>(args[1])->valBoolean();
	    // fall through
	case 1:
	    name = static_cast<ExpOperation*>(args[0])->c_str();
	    // fall through
	case 0:
	{
	    JsConfigFile* obj = new JsConfigFile(mutex(),oper.lineNumber(),name,warn);
	    if (!ref()) {
		TelEngine::destruct(obj);
		return 0;
	    }
	    obj->params().addParam(new ExpWrapper(this,protoName()));
	    return obj;
	}
	default:
	    return 0;
    }
}

void JsConfigFile::initialize(ScriptContext* context)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("ConfigFile")))
	addConstructor(params,"ConfigFile",new JsConfigFile(mtx));
}


bool JsConfigSection::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsConfigSection::runNative '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    if (oper.name() == YSTRING("configFile")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpWrapper(m_owner->ref() ? m_owner : 0));
    }
    else if (oper.name() == YSTRING("getValue")) {
	switch (extractArgs(stack,oper,context,args)) {
	    case 2:
	    case 1:
		break;
	    default:
		return false;
	}
	NamedList* sect = m_owner->config().getSection(toString());
	const String& name = *static_cast<ExpOperation*>(args[0]);
	static const char defVal[] = "default";
	const char* val = sect ? sect->getValue(name,defVal) : defVal;
	if (val == defVal) {
	    if (args[1])
		ExpEvaluator::pushOne(stack,static_cast<ExpOperation*>(args[1])->clone(name));
	    else
		ExpEvaluator::pushOne(stack,new ExpWrapper(0,name));
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpOperation(val,name));
    }
    else if (oper.name() == YSTRING("getIntValue")) {
	int64_t val = 0;
	int64_t minVal = LLONG_MIN;
	int64_t maxVal = LLONG_MAX;
	bool clamp = true;
	switch (extractArgs(stack,oper,context,args)) {
	    case 5:
		clamp = static_cast<ExpOperation*>(args[4])->valBoolean(clamp);
		// fall through
	    case 4:
		maxVal = static_cast<ExpOperation*>(args[3])->valInteger(maxVal);
		// fall through
	    case 3:
		minVal = static_cast<ExpOperation*>(args[2])->valInteger(minVal);
		// fall through
	    case 2:
		val = static_cast<ExpOperation*>(args[1])->valInteger();
		// fall through
	    case 1:
		break;
	    default:
		return false;
	}
	const String& name = *static_cast<ExpOperation*>(args[0]);
	NamedList* sect = m_owner->config().getSection(toString());
	if (sect)
	    val = sect->getInt64Value(name,val,minVal,maxVal,clamp);
	else if (val < minVal)
	    val = minVal;
	else if (val > maxVal)
	    val = maxVal;
	ExpEvaluator::pushOne(stack,new ExpOperation(val,name));
    }
    else if (oper.name() == YSTRING("getBoolValue")) {
	bool val = false;
	switch (extractArgs(stack,oper,context,args)) {
	    case 2:
		val = static_cast<ExpOperation*>(args[1])->valBoolean();
		// fall through
	    case 1:
		break;
	    default:
		return false;
	}
	const String& name = *static_cast<ExpOperation*>(args[0]);
	NamedList* sect = m_owner->config().getSection(toString());
	if (sect)
	    val = sect->getBoolValue(name,val);
	ExpEvaluator::pushOne(stack,new ExpOperation(val,name));
    }
    else if (oper.name() == YSTRING("setValue")) {
	if (extractArgs(stack,oper,context,args) != 2)
	    return false;
	NamedList* sect = m_owner->config().getSection(toString());
	if (sect)
	    sect->setParam(*static_cast<ExpOperation*>(args[0]),*static_cast<ExpOperation*>(args[1]));
    }
    else if (oper.name() == YSTRING("addValue")) {
	if (extractArgs(stack,oper,context,args) != 2)
	    return false;
	NamedList* sect = m_owner->config().getSection(toString());
	if (sect)
	    sect->addParam(*static_cast<ExpOperation*>(args[0]),*static_cast<ExpOperation*>(args[1]));
    }
    else if (oper.name() == YSTRING("setValues") || oper.name() == YSTRING("addValues")) {
	// setValues(params[,prefix])
	// addValues(params[,prefix])
	ExpOperation* params = 0;
	ExpOperation* prefix = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&params,&prefix))
	    return false;
	handleCfgSetValues(oper.name() == YSTRING("setValues"),m_owner->config(),toString(),params,prefix);
    }
    else if (oper.name() == YSTRING("clearKey")) {
	// clearKey(key[,matchValue])
	ExpOperation* key = 0;
	ExpOperation* matchValue = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&key,&matchValue))
	    return false;
	handleCfgClearKey(m_owner->config(),toString(),*key,matchValue);
    }
    else if (oper.name() == YSTRING("keys")) {
	if (!m_owner->runFuncKeys(stack,oper,context,this))
	    return false;
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}


JsObject* JsHasher::runConstructor(ObjList& stack, const ExpOperation& oper,
    GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsHasher::runConstructor '%s'(" FMT64 ") [%p]",
	oper.name().c_str(),oper.number(),this);
    ObjList args;
    if (extractArgs(stack,oper,context,args) != 1)
	return 0;
    ExpOperation* name = static_cast<ExpOperation*>(args[0]);
    Hasher* h = 0;
    if (*name == "md5")
	h = new MD5;
    else if (*name == "sha1")
	h = new SHA1;
    else if (*name == "sha256")
	h = new SHA256;
    else
	return 0;
    return new JsHasher(context,mutex(),oper.lineNumber(),h);
}

void JsHasher::initialize(ScriptContext* context)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("Hasher")))
	addConstructor(params,"Hasher",new JsHasher(mtx));
}

bool JsHasher::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsHasher::runNative '%s'(" FMT64 ") [%p]",
	oper.name().c_str(),oper.number(),this);
    if (oper.name() == YSTRING("update")) {
	if (!m_hasher)
	    return false;
	ObjList args;
	ExpOperation* data = 0;
	ExpOperation* isHex = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&data,&isHex))
	    return false;
	bool ok = false;
	if (!(isHex && isHex->valBoolean()))
	    ok = m_hasher->update(*data);
	else {
	    DataBlock tmp;
	    ok = tmp.unHexify(*data) && m_hasher->update(tmp);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("hmac")) {
	if (!m_hasher)
	    return false;
	ObjList args;
	ExpOperation* key = 0;
	ExpOperation* msg = 0;
	ExpOperation* isHex = 0;
	if (!extractStackArgs(2,this,stack,oper,context,args,&key,&msg,&isHex))
	    return false;
	bool ok = false;
	if (!(isHex && isHex->valBoolean()))
	    ok = m_hasher->hmac(*key,*msg);
	else {
	    DataBlock k, m;
	    ok = k.unHexify(*key) && m.unHexify(*msg) && m_hasher->hmac(k,m);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("hexDigest")) {
	if (!m_hasher || oper.number())
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(m_hasher->hexDigest()));
    }
    else if (oper.name() == YSTRING("clear")) {
	if (!m_hasher || oper.number())
	    return false;
	m_hasher->clear();
    }
    else if (oper.name() == YSTRING("finalize")) {
	if (!m_hasher || oper.number())
	    return false;
	m_hasher->finalize();
    }
    else if (oper.name() == YSTRING("hashLength")) {
	if (!m_hasher || oper.number())
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_hasher->hashLength()));
    }
    else if (oper.name() == YSTRING("hmacBlockSize")) {
	if (!m_hasher || oper.number())
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_hasher->hmacBlockSize()));
    }
    else if (oper.name() == YSTRING("fips186prf")) {
	ObjList args;
	ExpOperation* opSeed = 0;
	ExpOperation* opLen = 0;
	ExpOperation* opSep = 0;
	if (!extractStackArgs(2,this,stack,oper,context,args,&opSeed,&opLen,&opSep))
	    return false;
	DataBlock seed, out;
	seed.unHexify(*opSeed);
	SHA1::fips186prf(out,seed,opLen->valInteger());
	if (out.data()) {
	    String tmp;
	    char sep = '\0';
	    if (opSep && !(JsParser::isNull(*opSep) || opSep->isBoolean() || opSep->isNumber()))
		sep = opSep->at(0);
	    tmp.hexify(out.data(),out.length(),sep);
	    ExpEvaluator::pushOne(stack,new ExpOperation(tmp,"hex"));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void* JsXPath::getObject(const String& name) const
{
    void* obj = (name == YATOM("JsXPath")) ? const_cast<JsXPath*>(this)
	: JsObject::getObject(name);
    return obj ? obj : m_path.getObject(name);
}

JsObject* JsXPath::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsXPath::runConstructor '%s'(" FMT64 ") [%p]",
	oper.name().c_str(),oper.number(),this);
    ObjList args;
    ExpOperation* pathOp = 0;
    ExpOperation* second = 0;
    if (!extractStackArgs(1,this,stack,oper,context,args,&pathOp,&second))
	return 0;
    JsXPath* obj = 0;
    if (second) {
	// XPath(str,flags)
	obj = new JsXPath(mutex(),oper.lineNumber(),*pathOp,second->valInteger());
    }
    else {
	// XPath(str) or XPath(JsXPath)
	JsXPath* other = YOBJECT(JsXPath,pathOp);
	if (other)
	    obj = new JsXPath(mutex(),oper.lineNumber(),other->path());
	else
	    obj = new JsXPath(mutex(),oper.lineNumber(),*pathOp);
    }
    if (!ref()) {
	TelEngine::destruct(obj);
	return 0;
    }
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

bool JsXPath::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsXPath::runNative '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    if (oper.name() == YSTRING("valid"))
	ExpEvaluator::pushOne(stack,new ExpOperation(0 == m_path.status()));
    else if (oper.name() == YSTRING("absolute"))
	ExpEvaluator::pushOne(stack,new ExpOperation(m_path.absolute()));
    else if (oper.name() == YSTRING("getPath")) {
	if (!m_path.status()) {
	    String str;
	    m_path.dump(str,true,"/",m_path.absolute());
	    ExpEvaluator::pushOne(stack,new ExpOperation(str));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getItems")) {
	// getItems([escape])
	if (!m_path.status()) {
	    ExpOperation* esc = 0;
	    if (!extractStackArgs(0,this,stack,oper,context,args,&esc))
		return false;
	    ObjList lst;
	    m_path.dump(lst,(esc && esc->isBoolean()) ? esc->toBoolean() : true);
	    JsArray* jsa = new JsArray(context,oper.lineNumber(),mutex());
	    jsa->push(lst);
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"items"));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getError")) {
	if (m_path.status()) {
	    JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
	    jso->params().setParam(new ExpOperation((int64_t)m_path.status(),"status"));
	    jso->params().setParam(new ExpOperation((int64_t)m_path.errorItem(),"errorItem"));
	    if (m_path.error())
		jso->params().setParam(new ExpOperation(m_path.error(),"error"));
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,"error"));
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0));
    }
    else if (oper.name() == YSTRING("describeError")) {
	String tmp;
	if (m_path.describeError(tmp))
	    ExpEvaluator::pushOne(stack,new ExpOperation(tmp,"error"));
	else
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0));
    }
    else if (oper.name() == YSTRING("escapeString")) {
	// XPath.escapeString(str[quot[,literal=true]])
	ExpOperation* str = 0;
	ExpOperation* quot = 0;
	ExpOperation* literal = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&str,&quot,&literal))
	    return false;
	char q = quot ? (*(String*)quot)[0] : '"';
	String tmp;
	XPath::escape(tmp,*str,q,literal ? literal->valBoolean() : true);
	ExpEvaluator::pushOne(stack,new ExpOperation(tmp,"str"));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsXPath::initialize(ScriptContext* context)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("XPath")))
	addConstructor(params,"XPath",new JsXPath(mtx));
}

void* JsXML::getObject(const String& name) const
{
    void* obj = (name == YATOM("JsXML")) ? const_cast<JsXML*>(this) : JsObject::getObject(name);
    if (m_xml && !obj)
	obj = m_xml->getObject(name);
    return obj;
}

bool JsXML::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsXML::runNative '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    if (oper.name() == YSTRING("put")) {
	int argc = extractArgs(stack,oper,context,args);
	if (argc < 2 || argc > 3)
	    return false;
	ScriptContext* list = YOBJECT(ScriptContext,static_cast<ExpOperation*>(args[0]));
	ExpOperation* name = static_cast<ExpOperation*>(args[1]);
	if (!name || !list || !m_xml)
	    return false;
	ExpOperation* text = static_cast<ExpOperation*>(args[2]);
	int put = 0;
	if (text) {
	    if (!text->isBoolean())
		put = text->valInteger();
	    else if (text->toBoolean())
		put = 1;
	}
	NamedList* params = list->nativeParams();
	if (!params)
	    params = &list->params();
	m_xml->exportParam(*params,*name,1 == put || 2 == put,1 != put,-1,true);
    }
    else if (oper.name() == YSTRING("getOwner")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	if (m_owner && m_owner->ref())
	    ExpEvaluator::pushOne(stack,new ExpWrapper(m_owner));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getParent")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	XmlElement* xml = m_xml ? m_xml->parent() : 0;
	if (xml)
	    ExpEvaluator::pushOne(stack,xmlWrapper(oper,xml));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("unprefixedTag")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	if (m_xml)
	    ExpEvaluator::pushOne(stack,new ExpOperation(m_xml->unprefixedTag(),m_xml->unprefixedTag()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getTag")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	if (m_xml)
	    ExpEvaluator::pushOne(stack,new ExpOperation(m_xml->getTag(),m_xml->getTag()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getAttribute")) {
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	if (!name)
	    return false;
	const String* attr = 0;
	if (m_xml)
	    attr = m_xml->getAttribute(*name);
	if (attr)
	    ExpEvaluator::pushOne(stack,new ExpOperation(*attr,name->name()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("setAttribute")) {
	if (!m_xml)
	    return false;
	ExpOperation* name = 0;
	ExpOperation* val = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&name,&val))
	    return false;
	if (JsParser::isUndefined(*name) || JsParser::isNull(*name))
	    return (val == 0);
	if (val) {
	    if (JsParser::isUndefined(*val) || JsParser::isNull(*val))
		m_xml->removeAttribute(*name);
	    else if (*name)
		m_xml->setAttribute(*name,*val);
	}
	else {
	    JsObject* jso = YOBJECT(JsObject,name);
	    if (!jso)
		return false;
	    const ObjList* o = jso->params().paramList()->skipNull();
	    for (; o; o = o->skipNext()) {
		const NamedString* ns = static_cast<const NamedString*>(o->get());
		if (ns->name() != JsObject::protoName())
		    m_xml->setAttribute(ns->name(),*ns);
	    }
	}
    }
    else if (oper.name() == YSTRING("removeAttribute")) {
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	if (!name)
	    return false;
	if (m_xml)
	    m_xml->removeAttribute(*name);
    }
    else if (oper.name() == YSTRING("attributes")) {
	if (extractArgs(stack,oper,context,args))
	    return false;
	const ObjList* o = m_xml ? m_xml->attributes().paramList()->skipNull() : 0;
	JsObject* jso = 0;
	if (o) {
	    jso = new JsObject(context,oper.lineNumber(),mutex());
	    for (; o; o = o->skipNext()) {
		const NamedString* ns = static_cast<const NamedString*>(o->get());
		if (ns->name() != JsObject::protoName())
		    jso->params().addParam(ns->name(),*ns);
	    }
	}
	if (jso)
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jso,"attributes"));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("addChild")) {
	int argc = extractArgs(stack,oper,context,args);
	if (argc < 1 || argc > 2)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	ExpOperation* val = static_cast<ExpOperation*>(args[1]);
	if (!name)
	    return false;
	if (!m_xml)
	    return false;
	JsArray* jsa = YOBJECT(JsArray,name);
	if (jsa) {
	    for (long i = 0; i < jsa->length(); i++) {
		String n((unsigned int)i);
		JsXML* x = YOBJECT(JsXML,jsa->getField(stack,n,context));
		if (x && x->element()) {
		    XmlElement* xml = new XmlElement(*x->element());
		    if (XmlSaxParser::NoError != m_xml->addChild(xml)) {
			TelEngine::destruct(xml);
			return false;
		    }
		}
	    }
	    return true;
	}
	XmlElement* xml = 0;
	JsXML* x = YOBJECT(JsXML,name);
	if (x && x->element())
	    xml = new XmlElement(*x->element());
	else if (!(TelEngine::null(name) || JsParser::isNull(*name)))
	    xml = new XmlElement(name->c_str());
	if (xml && val && !JsParser::isNull(*val))
	    xml->addText(*val);
	if (xml && (XmlSaxParser::NoError == m_xml->addChild(xml)))
	    ExpEvaluator::pushOne(stack,new ExpWrapper(new JsXML(mutex(),oper.lineNumber(),xml,owner())));
	else {
	    TelEngine::destruct(xml);
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
	}
    }
    else if (oper.name() == YSTRING("getChild")) {
	if (extractArgs(stack,oper,context,args) > 2)
	    return false;
	XmlElement* xml = 0;
	if (m_xml) {
	    ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	    ExpOperation* ns = static_cast<ExpOperation*>(args[1]);
	    if (name && (JsParser::isUndefined(*name) || JsParser::isNull(*name)))
		name = 0;
	    if (ns && (JsParser::isUndefined(*ns) || JsParser::isNull(*ns)))
		ns = 0;
	    xml = m_xml->findFirstChild(name,ns);
	}
	if (xml)
	    ExpEvaluator::pushOne(stack,xmlWrapper(oper,xml));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getChildren")) {
	if (extractArgs(stack,oper,context,args) > 2)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	ExpOperation* ns = static_cast<ExpOperation*>(args[1]);
	if (name && (JsParser::isUndefined(*name) || JsParser::isNull(*name)))
	    name = 0;
	if (ns && (JsParser::isUndefined(*ns) || JsParser::isNull(*ns)))
	    ns = 0;
	XmlElement* xml = 0;
	if (m_xml)
	    xml = m_xml->findFirstChild(name,ns);
	if (xml) {
	    JsArray* jsa = new JsArray(context,oper.lineNumber(),mutex());
	    while (xml) {
		jsa->push(xmlWrapper(oper,xml));
		xml = m_xml->findNextChild(xml,name,ns);
	    }
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,"children"));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("clearChildren")) {
	if (extractArgs(stack,oper,context,args))
	    return false;
	if (m_xml)
	    m_xml->clearChildren();
    }
    else if (oper.name() == YSTRING("addText")) {
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpOperation* text = static_cast<ExpOperation*>(args[0]);
	if (!m_xml || !text)
	    return false;
	if (!(TelEngine::null(text) || JsParser::isNull(*text)))
	    m_xml->addText(*text);
    }
    else if (oper.name() == YSTRING("getText")) {
	if (extractArgs(stack,oper,context,args))
	    return false;
	if (m_xml)
	    ExpEvaluator::pushOne(stack,new ExpOperation(m_xml->getText(),m_xml->unprefixedTag()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("setText")) {
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpOperation* text = static_cast<ExpOperation*>(args[0]);
	if (!(m_xml && text))
	    return false;
	if (JsParser::isNull(*text))
	    m_xml->setText("");
	else
	    m_xml->setText(*text);
    }
    else if (oper.name() == YSTRING("compactText")) {
	// compactText([recursive])
	if (extractArgs(stack,oper,context,args) != 1 || !m_xml)
	    return false;
	ExpOperation* recursive = static_cast<ExpOperation*>(args[0]);
	m_xml->compactText(recursive && recursive->valBoolean());
    }
    else if (oper.name() == YSTRING("getChildText")) {
	if (extractArgs(stack,oper,context,args) > 2)
	    return false;
	ExpOperation* name = static_cast<ExpOperation*>(args[0]);
	ExpOperation* ns = static_cast<ExpOperation*>(args[1]);
	if (name && (JsParser::isUndefined(*name) || JsParser::isNull(*name)))
	    name = 0;
	if (ns && (JsParser::isUndefined(*ns) || JsParser::isNull(*ns)))
	    ns = 0;
	XmlElement* xml = 0;
	if (m_xml)
	    xml = m_xml->findFirstChild(name,ns);
	if (xml)
	    ExpEvaluator::pushOne(stack,new ExpOperation(xml->getText(),xml->unprefixedTag()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("getChildByPath") || oper.name() == YSTRING("getChildrenByPath")) {
	// getChildByPath(op). op: JsXPath or string
	// Return: XML or null
	// getChildrenByPath(op). op: JsXPath or string
	// Return: non empty array or null
	ExpOperation* pathOp = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&pathOp))
	    return false;
	ExpOperation* ret = 0;
	if (m_xml) {
	    ObjList lst;
	    bool single = oper.name() == YSTRING("getChildByPath");
	    XPathTmpParam path(*pathOp);
	    XmlElement* xml = path->findXml(*m_xml,single ? 0 : &lst);
	    if (xml) {
		if (single)
		    ret = xmlWrapper(oper,xml);
		else {
		    ObjList* o = lst.skipNull();
		    if (o) {
			JsArray* jsa = new JsArray(context,oper.lineNumber(),mutex());
			for (; o; o = o->skipNext())
			    jsa->push(xmlWrapper(oper,static_cast<XmlElement*>(o->get())));
			ret = new ExpWrapper(jsa,"children");
		    }
		}
	    }
	}
	ExpEvaluator::pushOne(stack,JsParser::validExp(ret));
    }
    else if (oper.name() == YSTRING("getTextByPath")) {
	// getTextByPath(op). op: JsXPath or string
	// Return: string or null
	ExpOperation* pathOp = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&pathOp))
	    return false;
	ExpOperation* ret = 0;
	if (m_xml) {
	    XPathTmpParam path(*pathOp);
	    const String* txt = path->findText(*m_xml);
	    if (txt)
		ret = new ExpOperation(*txt,"text");
	}
	ExpEvaluator::pushOne(stack,JsParser::validExp(ret));
    }
    else if (oper.name() == YSTRING("getAnyByPath")) {
	// getAnyByPath(op[,array[,what]]). op: JsXPath or string
	// Return null or first found element (XML, string or object)
	ExpOperation* pathOp = 0;
	ExpOperation* destOp = 0;
	ExpOperation* whatOp = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&pathOp,&destOp,&whatOp))
	    return false;
	ExpOperation* ret = 0;
	if (m_xml) {
	    XPathTmpParam path(*pathOp);
	    JsArray* jsa = YOBJECT(JsArray,destOp);
	    ObjList lst;
	    unsigned int what = (whatOp && whatOp->isInteger()) ?
		(unsigned int)whatOp->toNumber() : XPath::FindAny;
	    ret = buildAny(path->find(*m_xml,what,jsa ? &lst : 0),oper,context);
	    if (ret && jsa) {
		for (ObjList* o = lst.skipNull(); o; o = o->skipNext())
		    jsa->push(buildAny(o->get(),oper,context));
	    }
	}
	ExpEvaluator::pushOne(stack,JsParser::validExp(ret));
    }
    else if (oper.name() == YSTRING("xmlText")) {
	if (extractArgs(stack,oper,context,args) > 2)
	    return false;
	ExpOperation* op = 0;
	if (m_xml) {
	    int spaces = args[0] ? static_cast<ExpOperation*>(args[0])->number() : 0;
	    const String* line = &String::empty();
	    String indent;
	    String allIndent;
	    if (spaces > 0) {
		static const String crlf = "\r\n";
		line = &crlf;
		indent.assign(' ',spaces);
		if (args[1]) {
		    spaces = static_cast<ExpOperation*>(args[1])->number();
		    if (spaces > 0) {
			allIndent.assign(' ',spaces);
			allIndent = line + allIndent;
			line = &allIndent;
		    }
		}
	    }
	    op = new ExpOperation("",m_xml->unprefixedTag());
	    m_xml->toString(*op,true,*line,indent);
	    op->startSkip(*line,false);
	}
	ExpEvaluator::pushOne(stack,JsParser::validExp(op));
    }
    else if (oper.name() == YSTRING("replaceParams")) {
	if (!m_xml || extractArgs(stack,oper,context,args) != 1)
	    return false;
	const NamedList* params = getReplaceParams(args[0]);
	if (params)
	    m_xml->replaceParams(*params);
    }
    else if (oper.name() == YSTRING("saveFile")) {
	ExpOperation* file = 0;
	ExpOperation* spaces = 0;
	if (!(m_xml && extractStackArgs(1,this,stack,oper,context,args,&file,&spaces)))
	    return false;
	XmlSaxParser::Error code = XmlSaxParser::Unknown;
	XmlDocument doc;
	while (JsParser::isFilled(file)) {
	    code = XmlSaxParser::NoError;
	    NamedString* ns = getField(stack,YSTRING("declaration"),context);
	    if (ns) {
		XmlDeclaration* decl = 0;
		if (const NamedList* params = getReplaceParams(ns)) {
		    NamedList tmp("");
		    // version is required. It will be overridden if present
		    tmp.addParam("version","1.0");
		    copyObjParams(tmp,params);
		    decl = new XmlDeclaration(tmp);
		}
		else {
		    ExpOperation* oper = YOBJECT(ExpOperation,ns);
		    if (oper && oper->isBoolean() && oper->toBoolean())
			decl = new XmlDeclaration;
		}
		if (decl && !doc.addChildSafe(decl,&code))
		    break;
	    }
	    // TODO: Other children present before root (Comment(s), DOCTYPE, CDATA)
	    code = doc.addChild(m_xml);
	    if (code != XmlSaxParser::NoError)
		break;
	    // TODO: Other children present after root (Comment(s))
	    int sp = spaces ? spaces->number() : 0;
	    int error = 0;
	    if (sp > 0)
		error = doc.saveFile(*file,true,String(' ',sp));
	    else
		error = doc.saveFile(*file,true,String::empty(),true,0);
	    if (error)
		code = XmlSaxParser::IOError;
	    break;
	}
	// Remove root from doc to avoid object delete
	doc.takeRoot();
	ExpEvaluator::pushOne(stack,new ExpOperation(code == XmlSaxParser::NoError));
    }
    else if (oper.name() == YSTRING("loadFile")) {
	ExpOperation* file = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&file,0))
	    return false;
	XmlDocument doc;
	JsXML* xml = 0;
	if (JsParser::isFilled(file) &&
	    (doc.loadFile(*file) == XmlSaxParser::NoError) && doc.root(true) &&
	    ref()) {
	    xml = new JsXML(mutex(),oper.lineNumber(),doc.takeRoot(true));
	    xml->params().addParam(new ExpWrapper(this,protoName()));
	    const XmlFragment& before = doc.getFragment(true);
	    for (const ObjList* b = before.getChildren().skipNull(); b; b = b->skipNext()) {
		XmlChild* ch = static_cast<XmlChild*>(b->get());
		XmlDeclaration* decl = ch->xmlDeclaration();
		if (decl) {
		    JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
		    jso->addFields(decl->getDec());
		    xml->params().addParam(new ExpWrapper(jso,"declaration"));
		    continue;
		}
		// TODO: Other children present before root (Comment(s), DOCTYPE, CDATA)
	    }
	    // TODO: Other children present after root (Comment(s))
	}
	ExpEvaluator::pushOne(stack,JsParser::validExp(xml));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

JsObject* JsXML::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsXML::runConstructor '%s'(" FMT64 ") [%p]",oper.name().c_str(),oper.number(),this);
    JsXML* obj = 0;
    ObjList args;
    int n = extractArgs(stack,oper,context,args);
    ExpOperation* arg1 = static_cast<ExpOperation*>(args[0]);
    ExpOperation* arg2 = static_cast<ExpOperation*>(args[1]);
    switch (n) {
	case 1:
	    {
		// new XML(xmlObj), new XML("<xml>document</xml>") or new XML("element-name")
		XmlElement* xml = buildXml(arg1);
		if (!xml)
		    xml = getXml(arg1,false);
		if (!xml)
		    return JsParser::nullObject();
		obj = new JsXML(mutex(),oper.lineNumber(),xml);
	    }
	    break;
	case 2:
	    {
		// new XML(object,"field-name") or new XML("element-name","text-content")
		XmlElement* xml = buildXml(arg1,arg2);
		if (xml) {
		    obj = new JsXML(mutex(),oper.lineNumber(),xml);
		    break;
		}
	    }
	    // fall through
	case 3:
	    {
		// new XML(object,"field-name",bool)
		JsObject* jso = YOBJECT(JsObject,arg1);
		if (!jso || !arg2)
		    return 0;
		ExpOperation* arg3 = static_cast<ExpOperation*>(args[2]);
		bool take = arg3 && arg3->valBoolean();
		XmlElement* xml = getXml(jso->getField(stack,*arg2,context),take);
		if (!xml)
		    return JsParser::nullObject();
		obj = new JsXML(mutex(),oper.lineNumber(),xml);
	    }
	    break;
	default:
	    return 0;
    }
    if (!obj)
	return 0;
    if (!ref()) {
	TelEngine::destruct(obj);
	return 0;
    }
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

XmlElement* JsXML::getXml(const String* obj, bool take)
{
    if (!obj)
	return 0;
    XmlElement* xml = 0;
    NamedPointer* nptr = YOBJECT(NamedPointer,obj);
    if (nptr) {
	xml = YOBJECT(XmlElement,nptr);
	if (xml) {
	    if (take) {
		nptr->takeData();
		return xml;
	    }
	    return new XmlElement(*xml);
	}
    }
    else if (!take) {
	xml = YOBJECT(XmlElement,obj);
	if (xml)
	    return new XmlElement(*xml);
    }
    XmlDomParser parser;
    if (!(parser.parse(obj->c_str()) || parser.completeText()))
	return 0;
    if (parser.document())
	return parser.document()->takeRoot(true);
    return 0;
}

XmlElement* JsXML::buildXml(const String* name, const String* text)
{
    if (TelEngine::null(name) || name->getObject(YSTRING("JsObject")))
	return 0;
    static const Regexp s_elemName("^[[:alpha:]_][[:alnum:]_.-]*$");
    if (name->startsWith("xml",false,true) || !s_elemName.matches(*name))
	return 0;
    return new XmlElement(name->c_str(),TelEngine::c_str(text));
}

void JsXML::initialize(ScriptContext* context)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("XML")))
	addConstructor(params,"XML",new JsXML(mtx));
}

void* JsHashList::getObject(const String& name) const
{
    void* obj = (name == YATOM("JsHashList")) ? const_cast<JsHashList*>(this) : JsObject::getObject(name);
    if (!obj)
	obj = m_list.getObject(name);
    return obj;
}

JsObject* JsHashList::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsHashList::runConstructor '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    unsigned int cnt = 17; // default value for HashList
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
	{
	    ExpOperation* op = static_cast<ExpOperation*>(args[0]);
	    if (!op || !op->isInteger() || op->toNumber() <= 0)
		return 0;
	    cnt = op->toNumber();
	    break;
	}
	case 0:
	    break;
	default:
	    return 0;
    }

    JsHashList* obj = new JsHashList(cnt,mutex(),oper.lineNumber());
    if (!ref()) {
	TelEngine::destruct(obj);
	return 0;
    }
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

void JsHashList::fillFieldNames(ObjList& names)
{
    JsObject::fillFieldNames(names);
    ScriptContext::fillFieldNames(names,m_list);
#ifdef XDEBUG
    String tmp;
    tmp.append(names,",");
    Debug(DebugInfo,"JsHashList::fillFieldNames: %s",tmp.c_str());
#endif
}

bool JsHashList::runField(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsHashList::runField() '%s' in '%s' [%p]",
	oper.name().c_str(),toString().c_str(),this);
    ExpOperation* obj = static_cast<ExpOperation*>(m_list[oper.name()]);
    if (obj) {
	ExpWrapper* wrp = YOBJECT(ExpWrapper,obj);
	if (wrp)
	    ExpEvaluator::pushOne(stack,wrp->clone(oper.name()));
	else
	    ExpEvaluator::pushOne(stack,new ExpOperation(*obj,oper.name()));
	return true;
    }
    return JsObject::runField(stack,oper,context);
}

bool JsHashList::runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(DebugAll,"JsHashList::runAssign() '%s'='%s' (%s) in '%s' [%p]",
	oper.name().c_str(),oper.c_str(),oper.typeOf(),toString().c_str(),this);
    if (frozen()) {
	Debug(DebugWarn,"Object '%s' is frozen",toString().c_str());
	return false;
    }
    ObjList* obj = m_list.find(oper.name());
    ExpOperation* cln = 0;
    ExpFunction* ef = YOBJECT(ExpFunction,&oper);
    if (ef)
	cln = ef->ExpOperation::clone();
    else {
	ExpWrapper* w = YOBJECT(ExpWrapper,&oper);
	if (w) {
	    JsFunction* jsf = YOBJECT(JsFunction,w->object());
	    if (jsf)
		jsf->firstName(oper.name());
	    cln = w->clone(oper.name());
	}
	else
	    cln = oper.clone();
    }
    if (!obj)
	m_list.append(cln);
    else
	obj->set(cln);
    return true;
}

bool JsHashList::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    if (YSTRING("count") == oper.name()) {
	ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)m_list.count()));
	return true;
    }
    return JsObject::runNative(stack,oper,context);
}

void* JsURI::getObject(const String& name) const
{
    void* obj = (name == YATOM("JsURI")) ? const_cast<JsURI*>(this) : JsObject::getObject(name);
    if (!obj)
	obj = m_uri.getObject(name);
    return obj;
}

JsObject* JsURI::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsURI::runConstructor '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    ObjList args;
    const char* str = 0;
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
	{
	    ExpOperation* op = static_cast<ExpOperation*>(args[0]);
	    if (!op)
		return 0;
	    str = *op;
	    break;
	}
	case 0:
	    break;
	default:
	    return 0;
    }

    JsURI* obj = new JsURI(str,mutex(),oper.lineNumber());
    if (!ref()) {
	TelEngine::destruct(obj);
	return 0;
    }
    obj->params().addParam(new ExpWrapper(this,protoName()));
    return obj;
}

bool JsURI::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    CALL_NATIVE_METH_STR(m_uri,getDescription);
    CALL_NATIVE_METH_STR(m_uri,getProtocol);
    CALL_NATIVE_METH_STR(m_uri,getUser);
    CALL_NATIVE_METH_STR(m_uri,getHost);
    CALL_NATIVE_METH_INT(m_uri,getPort);
    CALL_NATIVE_METH_STR(m_uri,getExtra);
    if (YSTRING("getCanonical") == oper.name()) {
	String str;
	if (m_uri.getProtocol())
	    str += m_uri.getProtocol() + ":";
	if (m_uri.getUser())
	    str += m_uri.getUser();
	if (m_uri.getHost()) {
	    if (m_uri.getUser())
		str << "@";
	    if (m_uri.getPort())
		SocketAddr::appendTo(str,m_uri.getHost(),m_uri.getPort());
	    else
		str << m_uri.getHost();
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(str));
	return true;
    }
    return JsObject::runNative(stack,oper,context);
}


//
// JsMatchingItem
//
JsObject* JsMatchingItem::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    // new MatchingItem(value[,name[,params]])
    ExpOperVector args;
    if (!extractStackArgs(1,0,args,this,stack,oper,context))
	return 0;
    JsMatchingItem* cp = YOBJECT(JsMatchingItem,args[0]);
    MatchingItemBase* mi = cp ? cp->copyMatching() : buildItemFromArgs(args);
    if (mi && ref()) {
	JsMatchingItem* obj = new JsMatchingItem(mi,mutex(),oper.lineNumber());
	obj->params().addParam(new ExpWrapper(this,protoName()));
	return obj;
    }
    TelEngine::destruct(mi);
    return 0;
}

bool JsMatchingItem::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ExpOperVector args;
    if (oper.name() == YSTRING("matches")) {
	// matches(val[,params])
	if (!extractStackArgs(1,0,args,this,stack,oper,context))
	    return false;
	bool ok = false;
	if (m_match) {
	    ExpOperation* val = args[0];
	    JsObject* jso = JsParser::objPresent(val);
	    const NamedList* list = jso ? getObjParams(jso) : 0;
	    const String* str = (jso || JsParser::isMissing(val)) ? 0 : val;
	    const NamedList* params = getObjParams(args[1]);
	    if (params) {
		MatchingParams mParams;
		int lvl = params->getIntValue(YSTRING("track_level"));
		if (lvl > 0) {
		    DebugEnabler* dbg = JsEngine::get(context);
		    if (dbg && dbg->debugAt(lvl)) {
			mParams.m_dbg = dbg;
			mParams.m_level = lvl;
		    }
		}
		const MatchingItemBase* m = mParams.matches(*m_match,list,str);
		ok = m;
		if (m && m->id() && params->getParam(YSTRING("id"))) {
		    jso = YOBJECT(JsObject,args[1]);
		    if (jso)
			jso->setStringField("id",m->id());
		}
	    }
	    else
		ok = list ? m_match->matchListParam(*list) : m_match->matchStringOpt(str);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("getDesc")) {
	// getDesc([params])
	if (!extractStackArgs(0,0,args,this,stack,oper,context))
	    return false;
	unsigned int f = 0;
	const NamedList* params = getObjParams(args[0]);
	if (params) {
	    f = (*params)[YSTRING("flags")].encodeFlags(MatchingItemDump::flagsDict());
	    if (params->getBoolValue(YSTRING("force_bool_props")))
		f |= BuildObjForceBoolProps;
	    if (params->getBoolValue(YSTRING("force_empty_name")))
		f |= BuildObjForceEmptyName;
	}
	ExpEvaluator::pushOne(stack,
	    JsParser::validExp(buildJsObj(m_match,context,oper.lineNumber(),mutex(),f),"desc"));
    }
    else if (oper.name() == YSTRING("dump")) {
	// dump([params[,indent,addIndent]])
	//   Return: string
	if (!extractStackArgs(0,0,args,this,stack,oper,context))
	    return false;
	String indent("\r\n");
	String addIndent("  ");
	if (args[1]) {
	    if (args[1]->isNumber()) {
		int64_t n = args[1]->number();
		if (n > 0)
		    indent.append(' ',n < 100 ? n : 100);
		addIndent.clear();
		n = args[2] ? args[2]->number() : 2;
		if (n > 0)
		    addIndent.append(' ',n < 100 ? n : 100);
	    }
	    else {
		JsParser::setString(indent,args[1]);
		JsParser::setString(addIndent,args[2]);
	    }
	}
	ExpOperation* res = new ExpOperation("","dump");
	MatchingItemDump::dumpItem(m_match,*res,indent,addIndent,getObjParams(args[0]));
	ExpEvaluator::pushOne(stack,res);
    }
    else if (oper.name() == YSTRING("dumpList")) {
	// dumpList([dest[,prefix[,params]])
	//   Return:
	//   Boolean false if we hold no matching item
	//   undefined if 'dest' is boolean false and no item was dumped
	//   Object with data if 'dest' is not false and not writable
	//   Number of dumped items if 'dest' is writable
	if (!extractStackArgs(0,0,args,this,stack,oper,context))
	    return false;
	JsNamedListWrite wr(args[0]);
	if (!m_match)
	    ExpEvaluator::pushOne(stack,new ExpOperation(false));
	else if (wr.params()) {
	    unsigned int n = MatchingItemDump::dumpItemList(m_match,*(wr.params()),
		JsParser::getString(args[1]),getObjParams(args[2]));
	    n = wr.setJsoParams(n);
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)n,"count"));
	}
	else {
	    NamedList tmp("");
	    unsigned int n = MatchingItemDump::dumpItemList(m_match,tmp,
		JsParser::getString(args[1]),getObjParams(args[2]));
	    if (!n && args[0] && args[0]->isBoolean() && args[0]->valBoolean())
		ExpEvaluator::pushOne(stack,new ExpWrapper((GenObject*)0));
	    else {
		JsObject* jso = new JsObject(context,oper.lineNumber(),mutex());
		jso->setStringFields(tmp);
		ExpEvaluator::pushOne(stack,new ExpWrapper(jso,"list"));
	    }
	}
    }
    else if (oper.name() == YSTRING("dumpXml")) {
	// dumpXml([params])
	//   Return: XML object, null if we hold no matching item
	if (!extractStackArgs(0,0,args,this,stack,oper,context))
	    return false;
	XmlElement* xml = MatchingItemDump::dumpItemXml(m_match,getObjParams(args[0]));
	JsXML* x = xml ? JsXML::build(xml,context,mutex(),oper.lineNumber()) : 0;
	ExpEvaluator::pushOne(stack,JsParser::validExp(x,"xml"));
    }
    else if (oper.name() == YSTRING("validate")) {
	// validate(value[,name[,params]])
	// Return: Object (success), null/undefined (empty optimized matching), string (error)
	if (!extractStackArgs(1,0,args,this,stack,oper,context))
	    return false;
	String error;
	MatchingItemBase* mi = buildItemFromArgs(args,&error);
	if (mi || !error) {
	    JsObject* jso = buildJsObj(mi,context,oper.lineNumber(),mutex());
	    ExpEvaluator::pushOne(stack,JsParser::validExp(jso,"match",0 != mi));
	    TelEngine::destruct(mi);
	}
	else
	    ExpEvaluator::pushOne(stack,new ExpOperation(error,"error"));
    }
    else if (oper.name() == YSTRING("load")) {
	// load(src[,params[,prefix]])
	// Return: Object (success), null (no input), undefined (empty optimized matching), string (error)
	if (!extractStackArgs(1,0,args,this,stack,oper,context))
	    return false;
	XmlElement* xml = YOBJECT(XmlElement,args[0]);
	const NamedList* src = xml ? 0 : getObjParams(args[0]);
	if (xml || src) {
	    const NamedList* params = getObjParams(args[1]);
	    ObjList ignoreName, allowName, ignoreType, allowType;
	    MatchingItemLoad miLoad;
	    if (params) {
		for (ObjList* o = params->paramList()->skipNull(); o; o = o->skipNext()) {
		    NamedString* ns = static_cast<NamedString*>(o->get());
		    if (ns->name() == YSTRING("flags"))
			miLoad.m_flags = ns->encodeFlags(MatchingItemLoad::loadFlags());
		    else if (ns->name() == YSTRING("ignore_name"))
			miLoad.m_ignoreName = ns->split(ignoreName,',',false);
		    else if (ns->name() == YSTRING("allow_name"))
			miLoad.m_allowName = ns->split(allowName,',',false);
		    else if (ns->name() == YSTRING("ignore_type"))
			miLoad.m_ignoreType = ns->split(ignoreType,',',false);
		    else if (ns->name() == YSTRING("allow_type"))
			miLoad.m_allowType = ns->split(allowType,',',false);
		    else if (ns->name() == YSTRING("warn_level"))
			miLoad.m_warnLevel = ns->toInteger();
		}
	    }
	    if (!params || params->getBoolValue(YSTRING("debug"),true))
		miLoad.m_dbg = JsEngine::get(context);
	    String error;
	    MatchingItemBase* mi = xml ? miLoad.loadXml(xml,&error) :
		miLoad.loadItem(*src,&error,TelEngine::c_str(args[2]));
	    if (mi)
		ExpEvaluator::pushOne(stack,new ExpWrapper(
		    build(mi,context,mutex(),oper.lineNumber()),"match"));
	    else if (error)
		ExpEvaluator::pushOne(stack,new ExpOperation(error,"error"));
	    else
		ExpEvaluator::pushOne(stack,new ExpWrapper((GenObject*)0));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

MatchingItemBase* JsMatchingItem::buildItemFromArgs(ExpOperVector& args, String* error)
{
    const NamedList* params = getObjParams(args[2]);
    uint64_t flags = 0;
    if (params) {
	const String* f = params->getParam(YSTRING("flags"));
	if (f)
	    flags = f->encodeFlags(MatchingItemLoad::loadFlags());
    }
    return buildItem(args[0],args[1],0,flags,error);
}

MatchingItemBase* JsMatchingItem::buildItemFromObj(GenObject* gen, uint64_t flags,
    String* error, bool allowObjValue)
{
    JsObject* jso = YOBJECT(JsObject,gen);
    if (!jso)
	return 0;
    JsArray* jsa = YOBJECT(JsArray,gen);
    if (jsa)
	return buildItem(jsa,0,0,flags,error,allowObjValue);
    const NamedList& jsp = jso->params();
    const String& type = jsp[YSTRING("type")];
    if (type)
	return buildItem(jsp.getParam(YSTRING("value")),jsp.getParam(YSTRING("name")),
	    &jsp,flags,error,false,type);
    // Retrieve params from 'params' property or object itself
    GenObject* params = jsp.getParam(YSTRING("params"));
    return buildItem(jsp.getParam(YSTRING("value")),jsp.getParam(YSTRING("name")),
	params ? getObjParams(params) : getObjParams(jso),flags,error,allowObjValue);
}

MatchingItemBase* JsMatchingItem::buildItem(GenObject* value, const String* name,
    const NamedList* params, uint64_t flags, String* error, bool allowObjValue,
    const String& type)
{
    const char* n = TelEngine::c_safe(name);
    bool negated = params && params->getBoolValue(YSTRING("negated"));
    int missingMatch = params ? lookup((*params)[YSTRING("missing")],
	MatchingItemBase::missingMatchDict()) : 0;
    const char* id = params ? params->getValue(YSTRING("id")) : 0;
    JsObject* jso = YOBJECT(JsObject,value);
#if 0
    String t;
    if (jso) {
	if (YOBJECT(JsArray,jso))
	    t = " array";
	else if (YOBJECT(JsRegExp,jso))
	    t = " regexp";
	else if (jso->params()[YSTRING("type")])
	    t << " type:" << jso->params()[YSTRING("type")];
	else
	    t << " object";
    }
    Debugger dbg(DebugTest,"JsMatchingItem::buildItem"," value=(%p)%s"
	" name=%s allowObjValue=%u type=%s",
	value,t.safe(),n,allowObjValue,type.safe());
#endif
    MatchingItemBase* ret = 0;
    while (true) {
	if (!jso) {
	    ExpOperation* oper = YOBJECT(ExpOperation,value);
	    const String& val = JsParser::isMissing(oper) ? String::empty() : *oper;
	    int t = type ? lookup(type,MatchingItemBase::typeDict()) : 0;
	    if (!type || t == MatchingItemBase::TypeString) {
		bool ci = params && params->getBoolValue(YSTRING("caseinsensitive"));
		ret = new MatchingItemString(n,val,ci,negated,missingMatch,id);
	    }
	    else if (t == MatchingItemBase::TypeRegexp) {
		bool ci = params && params->getBoolValue(YSTRING("caseinsensitive"));
		bool extended = !(params && params->getBoolValue(YSTRING("basic")));
		ret = MatchingItemRegexp::build(n,val,0,error ? true : false,
		    negated ? 1 : 0,ci,extended,missingMatch,id);
		if (!ret && error)
		    error->printf("invalid regexp '%s'",val.safe());
	    }
	    else if (t == MatchingItemBase::TypeXPath) {
		MatchingItemBase* match = 0;
		if (params) {
		    JsObject* m = YOBJECT(JsObject,params->getParam(YSTRING("match")));
		    match = m ? buildItemFromObj(m,flags | MatchingItemLoad::InternalInXPathMatch,error) : 0;
		    if (!match && error && *error)
			break;
		}
		ret = MatchingItemXPath::build(n,val,error,error ? true : false,
		    match,negated,missingMatch,id);
		if (error && *error)
		    TelEngine::destruct(ret);
	    }
	    else if (t == MatchingItemBase::TypeRandom) {
		ret = MatchingItemRandom::build(val,0,error ? true : false,
		    negated,n,missingMatch,id);
		if (!ret && error)
		    error->printf("invalid random value '%s'",val.safe());
	    }
	    else if (error)
		error->printf("unkown type '%s'",type.safe());
	    break;
	}
	// Array of items
	JsArray* jsa = YOBJECT(JsArray,jso);
	if (jsa) {
	    ObjList items;
	    ObjList* add = &items;
	    for (int32_t i = 0; i < jsa->length(); ++i) {
		MatchingItemBase* it = buildItemFromObj(jsa->at(i),flags,error);
		if (it)
		    add = add->append(it);
		else if (error && *error) {
		    items.clear();
		    break;
		}
	    }
	    if (items.skipNull()) {
		MatchingItemList* l = new MatchingItemList(n,
		    !(params && params->getBoolValue(YSTRING("any"))),negated,missingMatch,id);
		if (l->append(items))
		    ret = l;
		else
		    TelEngine::destruct(l);
	    }
	    break;
	}
	// Regexp
	JsRegExp* rex = YOBJECT(JsRegExp,jso);
	if (rex) {
	    const Regexp& r = rex->regexp();
	    if (params) {
		bool ci = params->getBoolValue(YSTRING("caseinsensitive"),r.isCaseInsensitive());
		bool extended = !params->getBoolValue(YSTRING("basic"),!r.isExtended());
		if (ci != r.isCaseInsensitive() || extended != r.isExtended()) {
		    Regexp tmp(r,extended,ci);
		    ret = new MatchingItemRegexp(n,tmp,negated,missingMatch,id);
		    break;
		}
	    }
	    ret = new MatchingItemRegexp(n,r,negated,missingMatch,id);
	    break;
	}
	if (allowObjValue && !YOBJECT(JsMatchingItem,jso))
	    // 'value' is no longer allowed to be a non handled object
	    ret = buildItemFromObj(jso,flags,error,false);
	else if (error)
	    *error = "object not allowed";
	break; 
    }
    if (!ret)
	return 0;
    if (!ret->name() && MatchingItemLoad::nameRequired(ret->type(),flags)) {
	if (error)
	    *error = "empty name";
	TelEngine::destruct(ret);
    }
    else if (ret->type() == MatchingItemBase::TypeList) {
	if (0 == (flags & MatchingItemLoad::NoOptimize))
	    return MatchingItemList::optimize(static_cast<MatchingItemList*>(ret),flags);
	if (!static_cast<MatchingItemList*>(ret)->length())
	    TelEngine::destruct(ret);
    }
    return ret;
}

JsObject* JsMatchingItem::buildJsObj(const MatchingItemBase* item,
    GenObject* context, unsigned int line, ScriptMutex* mtx, uint64_t flags)
{
    if (!item)
	return 0;
    JsObject* jso = new JsObject(context,line,mtx);
    if (0 == (flags & MatchingItemDump::IgnoreName) &&
	(item->name() || 0 != (flags & BuildObjForceEmptyName)))
	jso->setStringField("name",item->name());
    bool forceBoolProps = 0 != (flags & BuildObjForceBoolProps);
    if (item->type() == MatchingItemBase::TypeString) {
	const MatchingItemString* str = static_cast<const MatchingItemString*>(item);
	jso->setStringField("value",str->value());
	if (forceBoolProps || str->caseInsensitive())
	    jso->setBoolField("caseinsensitive",str->caseInsensitive());
    }
    else if (item->type() == MatchingItemBase::TypeRegexp) {
	const Regexp& r = static_cast<const MatchingItemRegexp*>(item)->value();
	JsRegExp* rex = new JsRegExp(mtx,r,line,r,r.isCaseInsensitive(),r.isExtended());
	rex->setPrototype(context,YSTRING("RegExp"));
	jso->setObjField("value",rex);
    }
    else if (item->type() == MatchingItemBase::TypeXPath) {
	const MatchingItemXPath* x = static_cast<const MatchingItemXPath*>(item);
	jso->setStringField("value",x->value());
	jso->setStringField("type",item->typeName());
	if (x->match()) {
	    JsObject* o = buildJsObj(x->match(),context,line,mtx,flags);
	    if (o)
		jso->setObjField("match",o);
	}
    }
    else if (item->type() == MatchingItemBase::TypeRandom) {
	ExpOperation* op = new ExpOperation("","value");
	static_cast<const MatchingItemRandom*>(item)->dumpValue(*op);
	jso->params().setParam(op);
	jso->setStringField("type",item->typeName());
    }
    else if (item->type() == MatchingItemBase::TypeList) {
	const MatchingItemList* list = static_cast<const MatchingItemList*>(item);
	JsArray* jsa = new JsArray(context,line,mtx);
	for (unsigned int i = 0; i < list->length(); ++i) {
	    JsObject* o = buildJsObj(list->at(i),context,line,mtx,flags);
	    if (o)
		jsa->push(new ExpWrapper(o));
	}
	jso->setObjField("value",jsa);
	if (forceBoolProps || !list->matchAll())
	    jso->setBoolField("any",!list->matchAll());
    }
    else {
	MatchingItemCustom* c = YOBJECT(MatchingItemCustom,item);
	if (c && c->valueStr())
	    jso->setStringField("value",*(c->valueStr()));
	jso->setStringField("type",item->typeName());
    }
    if (forceBoolProps || item->negated())
	jso->setBoolField("negated",item->negated());
    const char* s = lookup(item->missingMatch(),MatchingItemBase::missingMatchDict());
    if (s)
	jso->setStringField("missing",s);
    if (item->id())
	jso->setStringField("id",item->id());
    return jso;
}

MatchingItemBase* JsMatchingItem::buildFilter(const String& name, GenObject* value,
    GenObject* flt, bool emptyValueOk)
{
    if (flt) {
	JsMatchingItem* mi = YOBJECT(JsMatchingItem,flt);
	if (mi)
	    return mi->copyMatching(true);
    }
    if (!(value && name))
	return 0;
    ExpOperation* op = YOBJECT(ExpOperation,value);
    if (op) {
	JsRegExp* rexp = YOBJECT(JsRegExp,op);
	if (rexp)
	    return new MatchingItemRegexp(name,rexp->regexp());
	return (*op || emptyValueOk) ? new MatchingItemString(name,*op) : 0;
    }
    Regexp* rexp = YOBJECT(Regexp,value);
    if (rexp)
	return new MatchingItemRegexp(name,*rexp);
    const String& s = value->toString();
    if (!s.startsWith("^"))
	return (s || emptyValueOk) ? new MatchingItemString(name,s) : 0;
    Regexp r("",true);
    bool negated = s.length() > 1 && s.endsWith("^");
    if (negated)
	r = s.substr(0,s.length() - 1);
    else
	r = s;
    return new MatchingItemRegexp(name,r,negated);
}


bool JsJSON::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList args;
    if (oper.name() == YSTRING("parse")) {
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpOperation* op = JsParser::parseJSON(static_cast<ExpOperation*>(args[0])->c_str(),mutex(),&stack,context,&oper);
	if (!op)
	    op = new ExpWrapper(0,"JSON");
	ExpEvaluator::pushOne(stack,op);
    }
    else if (oper.name() == YSTRING("stringify")) {
	if (extractArgs(stack,oper,context,args) < 1)
	    return false;
	int spaces = args[2] ? static_cast<ExpOperation*>(args[2])->number() : 0;
	ExpOperation* op = JsObject::toJSON(static_cast<ExpOperation*>(args[0]),spaces);
	if (!op)
	    op = new ExpWrapper(0,"JSON");
	ExpEvaluator::pushOne(stack,op);
    }
    else if (oper.name() == YSTRING("loadFile")) {
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpOperation* op = 0;
	ExpOperation* file = static_cast<ExpOperation*>(args[0]);
	if (JsParser::isFilled(file)) {
	    File f;
	    if (f.openPath(*file)) {
		int64_t len = f.length();
		if (len > 0 && len <= 65536) {
		    DataBlock buf(0,len + 1);
		    char* text = (char*)buf.data();
		    if (f.readData(text,len) == len) {
			text[len] = '\0';
			op = JsParser::parseJSON(text,mutex(),&stack,context,&oper);
		    }
		}
	    }
	}
	if (!op)
	    op = new ExpWrapper(0,"JSON");
	ExpEvaluator::pushOne(stack,op);
    }
    else if (oper.name() == YSTRING("saveFile")) {
	if (extractArgs(stack,oper,context,args) < 2)
	    return false;
	ExpOperation* file = static_cast<ExpOperation*>(args[0]);
	bool ok = JsParser::isFilled(file);
	if (ok) {
	    ok = false;
	    int spaces = args[2] ? static_cast<ExpOperation*>(args[2])->number() : 0;
	    ExpOperation* op = JsObject::toJSON(static_cast<ExpOperation*>(args[1]),spaces);
	    if (op) {
		File f;
		if (f.openPath(*file,true,false,true)) {
		    int len = op->length();
		    ok = f.writeData(op->c_str(),len) == len;
		}
	    }
	    TelEngine::destruct(op);
	}
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("replaceParams")) {
	ObjList args;
	int argc = extractArgs(stack,oper,context,args);
	if (argc < 2 || argc > 4)
	    return false;
	const NamedList* params = getReplaceParams(args[1]);
	if (params) {
	    bool sqlEsc = (argc >= 3) && static_cast<ExpOperation*>(args[2])->valBoolean();
	    char extraEsc = 0;
	    if (argc >= 4)
		extraEsc = static_cast<ExpOperation*>(args[3])->at(0);
	    replaceParams(args[0],*params,sqlEsc,extraEsc);
	}
    }
    else if (oper.name() == YSTRING("replaceReferences")) {
	// JSON.replaceReferences(obj)
	// Return boolean (success/failure)
	ExpOperation* op = 0;
	if (!extractStackArgs(1,this,stack,oper,context,args,&op))
	    return false;
	bool ok = JsObject::resolveReferences(op);
	ExpEvaluator::pushOne(stack,new ExpOperation(ok));
    }
    else if (oper.name() == YSTRING("findPath")) {	
	// JSON.findPath(obj,path). 'path' may be a JPath object or string
	// Return found data, undefined if not found
	ExpOperation* op = 0;
	ExpOperation* pathOp = 0;
	if (!extractStackArgs(2,this,stack,oper,context,args,&op,&pathOp))
	    return false;
	JPathTmpParam jp(*pathOp);
	ExpOperation* res = JsObject::find(op,*jp);
	if (res)
	    ExpEvaluator::pushOne(stack,res->clone());
	else
	    ExpEvaluator::pushOne(stack,new ExpWrapper((GenObject*)0));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsJSON::replaceParams(GenObject* obj, const NamedList& params, bool sqlEsc, char extraEsc)
{
    ExpOperation* oper = YOBJECT(ExpOperation,obj);
    if (!oper || JsParser::isNull(*oper) || JsParser::isUndefined(*oper) ||
	YOBJECT(JsFunction,oper) || YOBJECT(ExpFunction,oper))
	return;
    JsObject* jso = YOBJECT(JsObject,oper);
    JsArray* jsa = YOBJECT(JsArray,jso);
    if (jsa) {
	if (jsa->length() <= 0)
	    return;
	for (int32_t i = 0; i < jsa->length(); i++) {
	    NamedString* p = jsa->params().getParam(String(i));
	    if (p)
		replaceParams(p,params,sqlEsc,extraEsc);
	}
    }
    else if (jso) {
	NamedString* proto = jso->params().getParam(protoName());
	for (ObjList* o = jso->params().paramList()->skipNull(); o; o = o->skipNext()) {
	    NamedString* p = static_cast<NamedString*>(o->get());
	    if (p != proto)
		replaceParams(p,params,sqlEsc,extraEsc);
	}
    }
    else if (!(oper->isBoolean() || oper->isNumber()))
	params.replaceParams(*oper,sqlEsc,extraEsc);
}

void JsJSON::initialize(ScriptContext* context)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("JSON")))
	addObject(params,"JSON",new JsJSON(mtx));
}


bool JsDNS::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList args;
    if (oper.name().startsWith("query")) {
	String type = oper.name().substr(5);
	ExpOperation* arg = 0;
	ExpOperation* async = 0;
	int argc = extractArgs(stack,oper,context,args);
	if (type.null() && (argc >= 2)) {
	    type = static_cast<ExpOperation*>(args[0]);
	    arg = static_cast<ExpOperation*>(args[1]);
	    async = static_cast<ExpOperation*>(args[2]);
	}
	else if (type && (argc >= 1)) {
	    arg = static_cast<ExpOperation*>(args[0]);
	    async = static_cast<ExpOperation*>(args[1]);
	}
	else
	    return false;
	type.toUpper();
	int qType = lookup(type,Resolver::s_types,-1);
	if ((qType < 0) || JsParser::isEmpty(arg))
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0,"DNS"));
	else {
	    if (async && async->valBoolean()) {
		ScriptRun* runner = YOBJECT(ScriptRun,context);
		if (!runner)
		    return false;
		runner->insertAsync(new JsDnsAsync(runner,this,&stack,*arg,(Resolver::Type)qType,context,oper.lineNumber()));
		runner->pause();
		return true;
	    }
	    runQuery(stack,*arg,(Resolver::Type)qType,context,oper.lineNumber());
	}
    }
    else if ((oper.name() == YSTRING("resolve")) || (oper.name() == YSTRING("local"))) {
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpOperation* op = 0;
	if (JsParser::isFilled(static_cast<ExpOperation*>(args[0]))) {
	    String tmp = static_cast<ExpOperation*>(args[0]);
	    if ((tmp[0] == '[') && (tmp[tmp.length() - 1] == ']'))
		tmp = tmp.substr(1,tmp.length() - 2);
	    SocketAddr rAddr;
	    if (rAddr.host(tmp)) {
		if (oper.name() == YSTRING("resolve"))
		    op = new ExpOperation(rAddr.host(),"IP");
		else {
		    SocketAddr lAddr;
		    if (lAddr.local(rAddr))
			op = new ExpOperation(lAddr.host(),"IP");
		}
	    }
	}
	if (!op)
	    op = new ExpWrapper(0,"IP");
	ExpEvaluator::pushOne(stack,op);
    }
    else if (oper.name().startsWith("pack")) {
	char sep = '\0';
	ExpOperation* op;
	switch (extractArgs(stack,oper,context,args)) {
	    case 2:
		op = static_cast<ExpOperation*>(args[1]);
		if (op->isBoolean())
		    sep = op->valBoolean() ? ' ' : '\0';
		else if ((op->length() == 1) && !op->isNumber())
		    sep = op->at(0);
		// fall through
	    case 1:
		op = 0;
		if (JsParser::isFilled(static_cast<ExpOperation*>(args[0]))) {
		    String tmp = static_cast<ExpOperation*>(args[0]);
		    if ((tmp[0] == '[') && (tmp[tmp.length() - 1] == ']'))
			tmp = tmp.substr(1,tmp.length() - 2);
		    SocketAddr addr;
		    if (addr.host(tmp)) {
			DataBlock d;
			addr.copyAddr(d);
			if (d.length()) {
			    tmp.hexify(d.data(),d.length(),sep);
			    op = new ExpOperation(tmp,"IP");
			}
		    }
		}
		if (!op)
		    op = new ExpWrapper(0,"IP");
		ExpEvaluator::pushOne(stack,op);
		break;
	    default:
		return false;
	}
    }
    else if (oper.name().startsWith("unpack")) {
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpOperation* op = 0;
	DataBlock d;
	if (d.unHexify(*static_cast<ExpOperation*>(args[0]))) {
	    SocketAddr addr;
	    if (addr.assign(d))
		op = new ExpOperation(addr.host(),"IP");
	}
	if (!op)
	    op = new ExpWrapper(0,"IP");
	ExpEvaluator::pushOne(stack,op);
    }
    else if (oper.name().startsWith("dscp")) {
	if (extractArgs(stack,oper,context,args) != 1)
	    return false;
	ExpOperation* op = static_cast<ExpOperation*>(args[0]);
	if (!op)
	    return false;
	int val = op->toInteger(Socket::tosValues(),-1);
	if (0 <= val && 0xfc >= val)
	    ExpEvaluator::pushOne(stack,new ExpOperation((int64_t)(val & 0xfc),"DSCP"));
	else
	    ExpEvaluator::pushOne(stack,new ExpWrapper(0,"DSCP"));
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsDNS::runQuery(ObjList& stack, const String& name, Resolver::Type type, GenObject* context, unsigned int lineNo)
{
    if (!Resolver::init()) {
	ExpEvaluator::pushOne(stack,JsParser::nullClone());
	return;
    }
    JsArray* jsa = 0;
    ObjList res;
    if (Resolver::query(type,name,res) == 0) {
	jsa = new JsArray(context,lineNo,mutex());
	switch (type) {
	    case Resolver::A4:
	    case Resolver::A6:
	    case Resolver::Txt:
		for (ObjList* l = res.skipNull(); l; l = l->skipNext()) {
		    TxtRecord* r = static_cast<TxtRecord*>(l->get());
		    jsa->push(new ExpOperation(r->text()));
		}
		break;
	    case Resolver::Naptr:
		for (ObjList* l = res.skipNull(); l; l = l->skipNext()) {
		    NaptrRecord* r = static_cast<NaptrRecord*>(l->get());
		    JsObject* jso = new JsObject(context,lineNo,mutex());
		    jso->params().setParam(new ExpOperation(r->flags(),"flags"));
		    jso->params().setParam(new ExpOperation(r->serv(),"service"));
		    // Would be nice to create a RegExp here but does not stringify properly
		    jso->params().setParam(new ExpOperation(r->regexp(),"regexp"));
		    jso->params().setParam(new ExpOperation(r->repTemplate(),"replacement"));
		    jso->params().setParam(new ExpOperation(r->nextName(),"name"));
		    jso->params().setParam(new ExpOperation((int64_t)r->ttl(),"ttl"));
		    jso->params().setParam(new ExpOperation((int64_t)r->order(),"order"));
		    jso->params().setParam(new ExpOperation((int64_t)r->pref(),"preference"));
		    jsa->push(new ExpWrapper(jso));
		}
		break;
	    case Resolver::Srv:
		for (ObjList* l = res.skipNull(); l; l = l->skipNext()) {
		    SrvRecord* r = static_cast<SrvRecord*>(l->get());
		    JsObject* jso = new JsObject(context,lineNo,mutex());
		    jso->params().setParam(new ExpOperation((int64_t)r->port(),"port"));
		    jso->params().setParam(new ExpOperation(r->address(),"name"));
		    jso->params().setParam(new ExpOperation((int64_t)r->ttl(),"ttl"));
		    jso->params().setParam(new ExpOperation((int64_t)r->order(),"order"));
		    jso->params().setParam(new ExpOperation((int64_t)r->pref(),"preference"));
		    jsa->push(new ExpWrapper(jso));
		}
		break;
	    default:
		break;
	}
    }
    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa,lookup(type,Resolver::s_types)));
}

void JsDNS::initialize(ScriptContext* context)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("DNS")))
	addObject(params,"DNS",new JsDNS(mtx));
}


/**
 * class JsEngineWorker
 */

JsEngineWorker::JsEngineWorker(JsEngine* engine, ScriptContext* context, ScriptCode* code)
    : Thread(engine->schedName()), m_eventsMutex(false,"JsEngine"), m_id(0),
    m_runner(code->createRunner(context,NATIVE_TITLE)), m_engine(engine)
{
    setScriptInfo(context);
    DDebug(&__plugin,DebugAll,"Creating JsEngineWorker engine=%p [%p]",(void*)m_engine,this);
}

JsEngineWorker::~JsEngineWorker()
{
    DDebug(&__plugin,DebugAll,"Destroying JsEngineWorker engine=%p [%p]",(void*)m_engine,this);
    m_events.clear();
    m_installedEvents.clear();
    if (m_engine)
	m_engine->resetWorker();
    m_engine = 0;
    TelEngine::destruct(m_runner);
}

bool JsEngineWorker::init()
{
    if (m_runner) {
	if (startup())
	    return true;
	Debug(m_engine,DebugWarn,"JsEngine failed to start worker thread [%p]",(void*)m_engine);
    }
    else
	Debug(m_engine,DebugWarn,"JsEngine failed to create runner [%p]",(void*)m_engine);
    return false;
}

unsigned int JsEngineWorker::addEvent(const ExpFunction& callback, int type, bool repeat,
    ExpOperVector& args, unsigned int interval)
{
    Lock lck(m_eventsMutex);
    // TODO find a better way to generate the id's
    unsigned int id = ++m_id;
    if (type == JsEvent::EvTime) {
	if (interval < MIN_CALLBACK_INTERVAL)
	    interval = MIN_CALLBACK_INTERVAL;
	return postponeEvent(new JsEvent(id,interval,repeat,callback,args));
    }
#ifdef JS_DEBUG_EVENT_NON_TIME
    Debug(m_engine,DebugInfo,"JsEngine adding event %u type=%s repeat=%s [%p]",
	id,JsEvent::typeName(type),String::boolText(repeat),(void*)m_engine);
#endif
    m_installedEvents.append(new JsEvent(id,type,repeat,callback,args));
    return id;
}
    
bool JsEngineWorker::removeEvent(unsigned int id, bool time, bool repeat)
{
    Lock lck(m_eventsMutex);
    ObjList* foundInstalled = 0;
    if (!time) {
	// Remove from installed list
	foundInstalled = JsEvent::findHolder(id,m_installedEvents);
	if (foundInstalled) {
#ifdef JS_DEBUG_EVENT_NON_TIME
	    Debug(m_engine,DebugInfo,"JsEngine removing %s event %u [%p]",
		static_cast<JsEvent*>(foundInstalled->get())->typeName(),id,(void*)m_engine);
#endif
	    foundInstalled->remove();
	}
    }
    // Remove from postponed list
    ObjList* postponed = JsEvent::findHolder(id,m_events);
    if (!postponed)
	return time ? false : (0 != foundInstalled);
    JsEvent* ev = static_cast<JsEvent*>(postponed->get());
    if (time) {
	if (!ev->isTimeEvent() || ev->repeat() != repeat)
	    return false;
    }
    else if (ev->isTimeEvent())
	return foundInstalled;
    postponed->remove();
    return true;
}

void JsEngineWorker::run()
{
    while (!Thread::check(false)) {
	if (m_engine->refcount() == 1) {
	    m_engine->resetWorker();
	    return;
	}
	Lock myLock(m_eventsMutex);
	ObjList* o = m_events.skipNull();
	if (!o) {
	    myLock.drop();
	    Thread::idle();
	    continue;
	}
	RefPointer<JsEvent> ev = static_cast<JsEvent*>(o->get());
	if (ev->isTimeEvent()) {
	    uint64_t now = Time::msecNow();
	    if (!ev->timeout(now)) {
		myLock.drop();
		ev = 0;
		Thread::idle();
		continue;
	    }
	    if (o->remove(!ev->repeat()))
		postponeEvent(ev,now);
	}
	else
	    o->remove();
	myLock.drop();
	if (m_runner)
	    m_runner->reset();
	ev->process(m_runner);
	ev = 0;
    }
}

unsigned int JsEngineWorker::postponeEvent(JsEvent* ev, uint64_t now)
{
    if (!ev)
	return 0;
    if (ev->isTimeEvent()) {
	if (!now)
	    now = Time::msecNow();
	ev->fireTime(now);
#ifdef JS_DEBUG_EVENT_TIME
	Debug(m_engine,DebugAll,"JsEngine scheduling %s event %u fire in %ums [%p]",
	    ev->typeName(),ev->id(),(unsigned int)(ev->fireTime() - now),(void*)m_engine);
#endif
	for (ObjList* o = m_events.skipNull(); o; o = o->skipNext()) {
	    JsEvent* crt = static_cast<JsEvent*>(o->get());
	    if (!crt->isTimeEvent() || crt->fireTime() <= ev->fireTime())
		continue;
	    o->insert(ev);
	    return ev->id();
	}
    }
    else {
	// Non time event
#ifdef JS_DEBUG_EVENT_NON_TIME
	Debug(m_engine,DebugAll,"JsEngine scheduling %s event %u [%p]",
	    ev->typeName(),ev->id(),(void*)m_engine);
#endif
	for (ObjList* o = m_events.skipNull(); o; o = o->skipNext()) {
	    JsEvent* crt = static_cast<JsEvent*>(o->get());
	    // Insert event before any other time event or non time event with lower priority
	    // Same id: replace
	    // Not same id + same type: continue (schedule after last event with same type)
	    if (crt->isTimeEvent() || crt->type() > ev->type())
		o->insert(ev);
	    else if (ev->id() == crt->id())
		o->set(ev);
	    else
		continue;
	    return ev->id();
	}
    }
    m_events.append(ev);
    return ev->id();
}

void JsEngineWorker::scheduleEvent(GenObject* context, int type)
{
    if (!context)
	return;
    // Find Engine in given context
    RefPointer<JsEngine> eng;
    JsEngine::get(context,&eng);
    JsEngineWorker* worker = eng ? eng->worker() : 0;
    if (!worker)
	return;
    Lock lck(worker->m_eventsMutex);
    for (ObjList* o = worker->m_installedEvents.skipNull(); o;) {
	JsEvent* ev = static_cast<JsEvent*>(o->get());
	if (ev->type() != type) {
	    o = o->skipNext();
	    continue;
	}
	if (ev->repeat()) {
	    ev = new JsEvent(ev);
	    o = o->skipNext();
	}
	else {
	    o->remove(false);
	    o = o->skipNull();
	}
	worker->postponeEvent(ev);
    }
}


bool JsChannel::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    XDebug(&__plugin,DebugAll,"JsChannel::runNative '%s'(" FMT64 ")",oper.name().c_str(),oper.number());
    if (oper.name() == YSTRING("id")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (ja)
	    ExpEvaluator::pushOne(stack,new ExpOperation(ja->id()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("peerid")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (!ja)
	    return false;
	RefPointer<CallEndpoint> cp = ja->locate();
	String id;
	if (cp)
	    cp->getPeerId(id);
	if (id)
	    ExpEvaluator::pushOne(stack,new ExpOperation(id));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("status")) {
	if (oper.number())
	    return false;
	RefPointer<CallEndpoint> cp;
	RefPointer<JsAssist> ja = m_assist;
	if (ja)
	    cp = ja->locate();
	Channel* ch = YOBJECT(Channel,cp);
	if (ch) {
	    String tmp;
	    ch->getStatus(tmp);
	    ExpEvaluator::pushOne(stack,new ExpOperation(tmp));
	}
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("direction")) {
	if (oper.number())
	    return false;
	RefPointer<CallEndpoint> cp;
	RefPointer<JsAssist> ja = m_assist;
	if (ja)
	    cp = ja->locate();
	Channel* ch = YOBJECT(Channel,cp);
	if (ch)
	    ExpEvaluator::pushOne(stack,new ExpOperation(ch->direction()));
	else
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
    }
    else if (oper.name() == YSTRING("answered")) {
	if (oper.number())
	    return false;
	RefPointer<CallEndpoint> cp;
	RefPointer<JsAssist> ja = m_assist;
	if (ja)
	    cp = ja->locate();
	Channel* ch = YOBJECT(Channel,cp);
	ExpEvaluator::pushOne(stack,new ExpOperation(ch && ch->isAnswered()));
    }
    else if (oper.name() == YSTRING("answer")) {
	if (oper.number())
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (ja) {
	    Message* m = new Message("call.answered");
	    m->addParam("targetid",ja->id());
	    Engine::enqueue(m);
	}
    }
    else if (oper.name() == YSTRING("hangup")) {
	bool peer = false;
	ExpOperation* params = 0;
	switch (oper.number()) {
	    case 3:
		params = popValue(stack,context);
		peer = params && params->valBoolean();
		TelEngine::destruct(params);
		// fall through
	    case 2:
		params = popValue(stack,context);
		// fall through
	    case 1:
		break;
	    default:
		return false;
	}
	ExpOperation* op = popValue(stack,context);
	ScriptRun* runner = YOBJECT(ScriptRun,context);
	RefPointer<JsAssist> ja = m_assist;
	if (ja) {
	    NamedList* lst = YOBJECT(NamedList,params);
	    if (!lst) {
		ScriptContext* ctx = YOBJECT(ScriptContext,params);
		if (ctx)
		    lst = &ctx->params();
	    }
	    String id;
	    if (peer) {
		RefPointer<CallEndpoint> cp = ja->locate();
		if (cp)
		    cp->getPeerId(id);
	    }
	    if (!id)
		id = ja->id();
	    Message* m = new Message("call.drop");
	    m->addParam("id",id);
	    copyObjParams(*m,lst);
	    if (op && !op->null()) {
		m->addParam("reason",*op);
		// there may be a race between chan.disconnected and call.drop so set in both
		Message* msg = ja->getMsg(runner);
		if (msg) {
		    msg->setParam((ja->state() == JsAssist::Routing) ? "error" : "reason",*op);
		    copyObjParams(*msg,lst);
		}
	    }
	    ja->end();
	    Engine::enqueue(m);
	}
	TelEngine::destruct(op);
	TelEngine::destruct(params);
	if (runner)
	    runner->pause();
    }
    else if (oper.name() == YSTRING("callTo") || oper.name() == YSTRING("callJust")) {
	ExpOperation* params = 0;
	switch (oper.number()) {
	    case 2:
		params = popValue(stack,context);
		// fall through
	    case 1:
		break;
	    default:
		return false;
	}
	ExpOperation* op = popValue(stack,context);
	if (!op) {
	    op = params;
	    params = 0;
	}
	if (!op)
	    return false;
	RefPointer<JsAssist> ja = m_assist;
	if (!ja) {
	    TelEngine::destruct(op);
	    TelEngine::destruct(params);
	    return false;
	}
	NamedList* lst = YOBJECT(NamedList,params);
	if (!lst) {
	    ScriptContext* ctx = YOBJECT(ScriptContext,params);
	    if (ctx)
		lst = &ctx->params();
	}
	switch (ja->state()) {
	    case JsAssist::Routing:
		callToRoute(stack,*op,context,lst);
		break;
	    case JsAssist::ReRoute:
		callToReRoute(stack,*op,context,lst);
		break;
	    default:
		break;
	}
	TelEngine::destruct(op);
	TelEngine::destruct(params);
	if (oper.name() == YSTRING("callJust"))
	    ja->end();
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

void JsChannel::callToRoute(ObjList& stack, const ExpOperation& oper, GenObject* context, const NamedList* params)
{
    ScriptRun* runner = YOBJECT(ScriptRun,context);
    if (!runner)
	return;
    Message* msg = m_assist->getMsg(YOBJECT(ScriptRun,context));
    if (!msg) {
	Debug(&__plugin,DebugWarn,"JsChannel::callToRoute(): No message!");
	return;
    }
    if (oper.null() || JsParser::isNull(oper) || JsParser::isUndefined(oper)) {
	Debug(&__plugin,DebugWarn,"JsChannel::callToRoute(): Invalid target!");
	return;
    }
    copyObjParams(*msg,params);
    msg->retValue() = oper;
    m_assist->handled();
    runner->pause();
}

void JsChannel::callToReRoute(ObjList& stack, const ExpOperation& oper, GenObject* context, const NamedList* params)
{
    ScriptRun* runner = YOBJECT(ScriptRun,context);
    if (!runner)
	return;
    RefPointer<CallEndpoint> ep;
    Message* msg = m_assist->getMsg(YOBJECT(ScriptRun,context));
    Channel* chan = msg ? YOBJECT(Channel,msg->userData()) : 0;
    if (!chan) {
	ep = m_assist->locate();
	chan = YOBJECT(Channel,ep);
    }
    if (!chan) {
	Debug(&__plugin,DebugWarn,"JsChannel::callToReRoute(): No channel!");
	return;
    }
    String target = oper;
    target.trimSpaces();
    if (target.null() || JsParser::isNull(oper) || JsParser::isUndefined(oper)) {
	Debug(&__plugin,DebugWarn,"JsChannel::callToRoute(): Invalid target!");
	return;
    }
    Message* m = chan->message("call.execute",false,true);
    m->setParam("callto",target);
    // copy params except those already set
    if (msg) {
	unsigned int n = msg->length();
	for (unsigned int i = 0; i < n; i++) {
	    const NamedString* p = msg->getParam(i);
	    if (p && !m->getParam(p->name()))
		m->addParam(p->name(),*p);
	}
    }
    copyObjParams(*m,params);
    Engine::enqueue(m);
    m_assist->handled();
    runner->pause();
}

void JsChannel::initialize(ScriptContext* context, JsAssist* assist)
{
    if (!context)
	return;
    ScriptMutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("Channel")))
	addObject(params,"Channel",new JsChannel(assist,mtx));
}


#define MKSTATE(x) { #x, JsAssist::x }
static const TokenDict s_states[] = {
    MKSTATE(NotStarted),
    MKSTATE(Routing),
    MKSTATE(ReRoute),
    MKSTATE(Ended),
    MKSTATE(Hangup),
    { 0, 0 }
};

JsAssist::~JsAssist()
{
    if (m_runner) {
	ScriptContext* context = m_runner->context();
	if (m_runner->callable("onUnload")) {
	    ScriptRun* runner = m_runner->code()->createRunner(context,NATIVE_TITLE);
	    if (runner) {
		attachScriptInfo(runner);
		ObjList args;
		runner->call("onUnload",args);
		TelEngine::destruct(runner);
	    }
	}
	m_message = 0;
	if (context) {
	    Lock mylock(context->mutex());
	    context->params().clearParams();
	}
	TelEngine::destruct(m_runner);
    }
    else
	m_message = 0;
}

const char* JsAssist::stateName(State st)
{
    return lookup(st,s_states,"???");
}

bool JsAssist::init()
{
    if (!m_runner)
	return false;
    contextInit(m_runner,id(),s_autoExt,this);
    if (ScriptRun::Invalid == m_runner->reset(true))
	return false;
    ScriptContext* ctx = m_runner->context();
    ctx->trackObjs(s_trackCreation);
    ScriptContext* chan = YOBJECT(ScriptContext,ctx->getField(m_runner->stack(),YSTRING("Channel"),m_runner));
    if (chan) {
	JsMessage* jsm = YOBJECT(JsMessage,chan->getField(m_runner->stack(),YSTRING("message"),m_runner));
	if (!jsm) {
	    jsm = new JsMessage(0,ctx->mutex(),0,false);
	    ExpWrapper wrap(jsm,"message");
	    if (!chan->runAssign(m_runner->stack(),wrap,m_runner))
		return false;
	}
	if (jsm && jsm->ref()) {
	    jsm->setPrototype(ctx,YSTRING("Message"));
	    JsObject* cc = JsObject::buildCallContext(ctx->mutex(),jsm);
	    jsm->ref();
	    cc->params().setParam(new ExpWrapper(jsm,"message"));
	    ExpEvaluator::pushOne(m_runner->stack(),new ExpWrapper(cc,cc->toString(),true));
	}
    }
    if (!m_runner->callable("onLoad"))
	return true;
    ScriptRun* runner = m_runner->code()->createRunner(m_runner->context(),NATIVE_TITLE);
    if (runner) {
	ObjList args;
	runner->call("onLoad",args);
	TelEngine::destruct(runner);
	return true;
    }
    return false;
}

bool JsAssist::evalAllocations(String& retVal, unsigned int top)
{
    if (!m_runner) {
	retVal << "Script " << toString() << " has no associated runner\r\n";
	return true;
    }
    return evalCtxtAllocations(retVal,top,m_runner->context(),m_runner->code(),toString());
}

Message* JsAssist::getMsg(ScriptRun* runner) const
{
    if (!runner)
	runner = m_runner;
    if (!runner)
	return 0;
    ScriptContext* ctx = runner->context();
    if (!ctx)
	return 0;
    ObjList stack;
    ScriptContext* chan = YOBJECT(ScriptContext,ctx->getField(stack,YSTRING("Channel"),runner));
    if (!chan)
	return 0;
    JsMessage* jsm = YOBJECT(JsMessage,chan->getField(stack,YSTRING("message"),runner));
    if (!jsm)
	return 0;
    return static_cast<Message*>(jsm->nativeParams());
}

bool JsAssist::setMsg(Message* msg)
{
    if (!m_runner)
	return false;
    ScriptContext* ctx = m_runner->context();
    if (!ctx)
	return false;
    Lock mylock(ctx->mutex());
    if (!mylock.locked())
	return false;
    if (m_message)
	return false;
    ObjList stack;
    ScriptContext* chan = YOBJECT(ScriptContext,ctx->getField(stack,YSTRING("Channel"),m_runner));
    if (!chan)
	return false;
    JsMessage* jsm = YOBJECT(JsMessage,chan->getField(stack,YSTRING("message"),m_runner));
    if (jsm)
	jsm->setMsg(msg);
    else
	return false;
    m_message = jsm;
    m_handled = false;
    return true;
}

void JsAssist::clearMsg(bool fromChannel)
{
    Lock mylock((m_runner && m_runner->context()) ? m_runner->context()->mutex() : 0);
    if (!m_message)
	return;
    m_message->clearMsg();
    m_message = 0;
    if (fromChannel && mylock.locked()) {
	ObjList stack;
	ScriptContext* chan = YOBJECT(ScriptContext,m_runner->context()->getField(stack,YSTRING("Channel"),m_runner));
	if (chan) {
	    static const ExpWrapper s_undef(0,"message");
	    chan->runAssign(stack,s_undef,m_runner);
	}
    }
}

bool JsAssist::runScript(Message* msg, State newState)
{
    XDebug(&__plugin,DebugInfo,"JsAssist::runScript('%s') for '%s' in state %s",
	msg->c_str(),id().c_str(),stateName());

    if (m_state >= Ended)
	return false;
    if (m_state < newState)
	m_state = newState;
#ifdef DEBUG
    u_int64_t tm = Time::now();
#endif
    if (!setMsg(msg)) {
	Debug(&__plugin,DebugWarn,"Failed to set message '%s' in '%s'",
	    msg->c_str(),id().c_str());
	return false;
    }

    m_repeat = true;
    do {
	switch (m_runner->execute()) {
	    case ScriptRun::Incomplete:
		break;
	    case ScriptRun::Invalid:
	    case ScriptRun::Succeeded:
		if (m_state < Ended)
		    m_state = Ended;
		// fall through
	    default:
		m_repeat = false;
		break;
	}
    } while (m_repeat);
    bool handled = m_handled;
    clearMsg(m_state >= Ended);
    if (Routing == m_state)
	m_state = ReRoute;

#ifdef DEBUG
    tm = Time::now() - tm;
    Debug(&__plugin,DebugInfo,"Script for '%s' ran for " FMT64U " usec",id().c_str(),tm);
#endif
    return handled;
}

bool JsAssist::runFunction(const String& name, Message& msg, bool* handled)
{
    if (!(m_runner && m_runner->callable(name)))
	return false;
    DDebug(&__plugin,DebugInfo,"Running function %s(message%s) in '%s' state %s",
	name.c_str(),(handled ? ",handled" : ""),id().c_str(),stateName());
#ifdef DEBUG
    u_int64_t tm = Time::now();
#endif
    ScriptRun* runner = __plugin.parser().createRunner(m_runner->context(),NATIVE_TITLE);
    if (!runner)
	return false;
    attachScriptInfo(runner);

    JsMessage* jm = new JsMessage(&msg,runner->context()->mutex(),0,false);
    jm->setPrototype(runner->context(),YSTRING("Message"));
    jm->ref();
    ObjList args;
    args.append(new ExpWrapper(jm,"message"));
    if (handled) {
	jm->freeze();
	args.append(new ExpOperation(*handled,"handled"));
    }
    ScriptRun::Status rval = runner->call(name,args);
    jm->clearMsg();
    bool ok = false;
    if (ScriptRun::Succeeded == rval) {
	ExpOperation* op = ExpEvaluator::popOne(runner->stack());
	if (op) {
	    ok = op->valBoolean();
	    TelEngine::destruct(op);
	}
    }
    TelEngine::destruct(jm);
    TelEngine::destruct(runner);

#ifdef DEBUG
    tm = Time::now() - tm;
    Debug(&__plugin,DebugInfo,"Call to %s() ran for " FMT64U " usec",name.c_str(),tm);
#endif
    return ok;
}

void JsAssist::msgStartup(Message& msg)
{
    runFunction("onStartup",msg);
}

void JsAssist::msgHangup(Message& msg)
{
    runFunction("onHangup",msg);
}

void JsAssist::msgExecute(Message& msg)
{
    runFunction("onExecute",msg);
}

bool JsAssist::msgRinging(Message& msg)
{
    return runFunction("onRinging",msg);
}

bool JsAssist::msgAnswered(Message& msg)
{
    return runFunction("onAnswered",msg);
}

bool JsAssist::msgPreroute(Message& msg)
{
    return runFunction("onPreroute",msg);
}

bool JsAssist::msgRoute(Message& msg)
{
    return runScript(&msg,Routing);
}

bool JsAssist::msgDisconnect(Message& msg, const String& reason)
{
    return runFunction("onDisconnected",msg) || runScript(&msg,ReRoute);
}

void JsAssist::msgPostExecute(const Message& msg, bool handled)
{
    runFunction("onPostExecute",const_cast<Message&>(msg),&handled);
}

JsGlobalInstance::JsGlobalInstance(JsGlobal* owner, unsigned int index)
    : ScriptInfoHolder(owner->scriptInfo()),
    m_owner(owner), m_instance(index), m_instanceCount(0), m_reinitEvent(false)
{
    m_name << owner->toString();
    if (index)
	m_name << "/" << index;
    Debug(&__plugin,DebugInfo,"JsGlobalInstance(%p,%u) created %s '%s' [%p]",
	m_owner,index,m_owner->typeName(),m_name.c_str(),this);
}

JsGlobalInstance::~JsGlobalInstance()
{
    Debug(&__plugin,DebugInfo,"JsGlobalInstance %s '%s' destroyed [%p]",
	m_owner->typeName(),m_name.c_str(),this);
    if (m_owner->parser().callable("onUnload")) {
	ScriptRun* runner = m_owner->parser().createRunner(m_context,NATIVE_TITLE);
	if (runner) {
	    attachScriptInfo(runner);
	    ObjList args;
	    runner->call("onUnload",args);
	    TelEngine::destruct(runner);
	}
    }
    if (m_context)
	m_context->cleanup();
}
  
unsigned int JsGlobalInstance::runMain()
{
    DDebug(&__plugin,DebugInfo,"JsGlobalInstance::runMain() %s",m_name.c_str());
    m_instanceCount = m_owner->instances();
    ScriptRun* runner = m_owner->parser().createRunner(m_context,0,m_instance,m_instanceCount);
    if (!runner)
	return ScriptRun::Failed;
    attachScriptInfo(runner);
    if (!m_context)
	m_context = runner->context();
    m_context->trackObjs(s_trackCreation);
    contextInit(runner,toString());
    ScriptRun::Status st = runner->run();
    TelEngine::destruct(runner);
    return st;
}

void JsGlobalInstance::scheduleInitEvent()
{
    if (m_reinitEvent)
	JsEngineWorker::scheduleEvent(m_context,JsEvent::EvReInit);
    else
	m_reinitEvent = true;
}


ObjList JsGlobal::s_globals;
ObjList JsGlobal::s_handlers;
ObjList JsGlobal::s_posthooks;
Mutex JsGlobal::s_mutex(false,"JsGlobal");
bool JsGlobal::s_keepOldOnFail = false;
SharedObjList JsGlobal::s_sharedObj("Global");

JsGlobal::JsGlobal(const char* scriptName, const char* fileName, int type, bool relPath,
    unsigned int instances)
    : NamedString(scriptName,fileName), ScriptInfoHolder(0,type),
    m_inUse(true), m_file(fileName), m_instanceCount(instances)
{
    m_jsCode.basePath(s_basePath,s_libsPath);
    if (relPath)
	m_jsCode.adjustPath(*this);
    m_jsCode.setMaxFileLen(s_maxFile);
    m_jsCode.link(s_allowLink);
    m_jsCode.trace(s_allowTrace);
}

JsGlobal::~JsGlobal()
{
    DDebug(&__plugin,DebugAll,"Unloading %s script '%s'",typeName(),name().c_str());
    m_instances.clear();
}

bool JsGlobal::load()
{
    DDebug(&__plugin,DebugAll,"Loading %s script '%s' from '%s'",typeName(),name().c_str(),c_str());
    if (m_jsCode.parseFile(*this)) {
	Debug(&__plugin,DebugInfo,"Parsed %s script '%s': %s",typeName(),name().c_str(),c_str());
	return true;
    }
    if (*this)
	Debug(&__plugin,DebugWarn,"Failed to parse %s script '%s': %s",typeName(),name().c_str(),c_str());
    return false;
}

bool JsGlobal::fileChanged(const char* fileName) const
{
    return m_jsCode.scriptChanged(fileName,s_basePath,s_libsPath);
}

bool JsGlobal::updateInstances(unsigned int instances)
{
    // 0 means that it was called from some place where config was not read, so no change
    // or no change in number of instances
    if (!instances || instances == m_instanceCount) {
	// Re-init instances
	for (unsigned int i = 0; i <= m_instanceCount; ++i) {
	    JsGlobalInstance* inst = getInstance(i);
	    if (inst) {
		inst->scheduleInitEvent();
		TelEngine::destruct(inst);
	    }
	}
	return true;
    }
    // we already now that current instance is different from what is requested
    // so if instances is 1 => m_instance count > 1, m_instanceCount == 1 => instance = 1
    // so we need to completely reload the script
    if (instances == 1 || m_instanceCount == 1)
	return false;
    m_instanceCount = instances;
    return runMain();
}

void JsGlobal::markUnused()
{
    for (ObjList* o = s_globals.skipNull(); o; o = o->skipNext()) {
	JsGlobal* script = static_cast<JsGlobal*>(o->get());
	script->m_inUse = ScriptInfo::Static != script->type();
    }
    for (ObjList* o = s_handlers.skipNull(); o; o = o->skipNext())
	static_cast<JsHandler*>(o->get())->setInUse(false);
    for (ObjList* o = s_posthooks.skipNull(); o; o = o->skipNext())
	static_cast<JsPostHook*>(o->get())->setInUse(false);
}

void JsGlobal::reloadDynamic()
{
    Lock mylock(JsGlobal::s_mutex);
    ListIterator iter(s_globals);
    while (JsGlobal* script = static_cast<JsGlobal*>(iter.get()))
	if (ScriptInfo::Dynamic == script->type()) {
	    String filename = script->fileName();
	    String name = script->name();
	    mylock.drop();
	    JsGlobal::initScript(name,filename,script->type(),true);
	    mylock.acquire(JsGlobal::s_mutex);
	}
}

bool JsGlobal::initScript(const String& scriptName, const String& fileName, int type, bool relPath,
    unsigned int instances)
{
    if (fileName.null())
	return false;
    DDebug(&__plugin,DebugInfo,"Initializing %s script '%s' from %s file '%s' instances=%u",
	lookup(type,ScriptInfo::s_type),scriptName.c_str(),(relPath ? "relative" : "absolute"),
	fileName.c_str(),instances);
    Lock mylock(JsGlobal::s_mutex);
    ObjList* o = s_globals.find(scriptName);
    if (o) {
	JsGlobal* script = static_cast<JsGlobal*>(o->get());
	if (script->type() != type) {
	    Debug(&__plugin,DebugWarn,"Trying to load %s script '%s' but it was already loaded as %s",
		lookup(type,ScriptInfo::s_type),scriptName.c_str(),script->typeName());
	    return false;
	}
	if (!script->fileChanged(fileName)) {
	    unsigned int ret = script->updateInstances(instances);
	    script->m_inUse = true;
	    // if positive, we can return. otherwise, it's a transition 
	    // from multiple instances to one or viceversa and we reload the whole script
	    if (ret)
		return true;
	}
    }
    return buildNewScript(mylock,o,scriptName,fileName,type,relPath,true,instances);
}

bool JsGlobal::reloadScript(const String& scriptName)
{
    if (scriptName.null())
	return false;
    Lock mylock(JsGlobal::s_mutex);
    ObjList* o = s_globals.find(scriptName);
    if (!o)
	return false;
    JsGlobal* script = static_cast<JsGlobal*>(o->get());
    String fileName = *script;
    return fileName && buildNewScript(mylock,o,scriptName,fileName,script->type(),false);
}

void JsGlobal::loadScripts(const NamedList* sect, const NamedList* instSect)
{
    if (!sect)
	return;
    unsigned int len = sect->length();
    for (unsigned int i=0; i<len; i++) {
	const NamedString *n = sect->getParam(i);
	if (!n)
	    continue;
	String tmp = *n;
	Engine::runParams().replaceParams(tmp);
	JsGlobal::initScript(n->name(),tmp,ScriptInfo::Static,true,
	    instSect ? instSect->getIntValue(n->name(),0,0) : 0);
    }
}

void JsGlobal::loadHandlers(const NamedList* sect, bool handler)
{
    ObjList seen;
    ObjList* o = sect ? sect->paramList()->skipNull() : 0;
    ObjList& listH = handler ? s_handlers : s_posthooks;
    const char* what = JsMessageHandle::clsType(handler);
    for (; o; o = o->skipNext()) {
	const NamedString* ns = static_cast<const NamedString*>(o->get());
	if (!ns->name() || ns->name().startsWith("handlerparam:"))
	    continue;
	// Handler: name=filename,callback,priority,trackname,parameters_prefix,filter,context,script_name
	// Posthook: id=filename,callback,parameters_prefix,filter,context,msg_name_filter,script_name,handled
	XDebug(&__plugin,DebugAll,"Processing %s %s=%s",what,ns->name().c_str(),ns->safe());
	String scriptFile, callback, priority, trackName, prefix, filter, context,
	    scriptName, msgName, handled;
	String* stringsPost[] = {&scriptFile,&callback,&prefix,&filter,&context,&msgName,&scriptName,&handled,0};
	String* strings[] = {&scriptFile,&callback,&priority,&trackName,&prefix,&filter,&context,&scriptName,0};
	String** setStr = handler ? strings : stringsPost;
	ObjList* params = ns->split(',');
	for (ObjList* o = params; *setStr && o; o = o->next(), ++setStr) {
	    String* str = static_cast<String*>(o->get());
	    if (!TelEngine::null(str))
		**setStr = *str;
	}
	TelEngine::destruct(params);

	Engine::runParams().replaceParams(scriptFile);
	if (!(scriptFile && callback)) {
	    Debug(&__plugin,DebugConf,"Ignoring %s %s='%s': empty script filename or callback",
		what,ns->name().c_str(),ns->safe());
	    continue;
	}
	if (prefix) {
	    prefix = "handlerparam:" + prefix + ":";
	    if (!filter)
		filter = (*sect)[prefix + "filter"];
	    if (!context)
		context = (*sect)[prefix + "context"];
	    if (!handler && !msgName)
		msgName = (*sect)[prefix + "msg_name_filter"];
	}
	if (!scriptName)
	    scriptName = scriptFile;
	int prio = 0;
	NamedList nl(ns->name());
	if (handler) {
	    prio = priority.toInteger(100,0,0);
	    if (!trackName)
		trackName = __plugin.name();
	    else if (trackName.isBoolean()) {
		if (trackName.toBoolean())
		    trackName = __plugin.name();
		else
		    trackName = "";
	    }
	    nl.addParam("filename",scriptFile);
	    nl.addParam("callback",callback);
	    nl.addParam("priority",prio);
	    nl.addParam("trackname",trackName);
	    nl.addParam("filter",filter);
	    nl.addParam("context",context);
	    nl.addParam("script_name",scriptName);
	}
	else {
	    nl.addParam("filename",scriptFile);
	    nl.addParam("callback",callback);
	    nl.addParam("filter",filter);
	    nl.addParam("context",context);
	    nl.addParam("script_name",scriptName);
	    nl.addParam("msg_name_filter",msgName);
	    nl.addParam("handled",handled);
	}
	String id;
	nl.dump(id,"|",'"',true);
	if (seen.find(id))
	    continue;
	DDebug(&__plugin,DebugAll,"Handling global %s id: %s",what,id.safe());
	seen.insert(new String(id));

	Lock lck(s_mutex);
	JsPostHook* hPost = 0;
	JsHandler* h = 0;
	ObjList* old = JsMessageHandle::findId(id,listH);
	if (old) {
	    if (handler)
		h = static_cast<JsHandler*>(old->get());
	    else
		hPost = static_cast<JsPostHook*>(old->get());
	}
	else {
	    String desc, filterName, filterValue;
	    desc << ns->name() << '=' << scriptName << ',' << callback
		<< ',' << context;
	    if (handler)
		desc << ',' << prio;
	    else if (msgName)
		desc << ',' >> msgName;
	    if (filter) {
		int pos = filter.find('=');
		if (pos > 0) {
		    desc << ',' << filter;
		    filterName = filter.substr(0,pos);
		    filterValue = filter.substr(pos + 1);
		}
	    }
	    if (handler) {
		h = new JsHandler(id,callback,desc,ns->name(),prio,context);
		bool prio = !prefix ? true : sect->getBoolValue(prefix + "track_priority",true);
		h->prepare(&filterName,&filterValue,sect,0,trackName,prio);
	    }
	    else {
		hPost = new JsPostHook(id,callback,desc,context,nl);
		const NamedString* ns = prefix ? sect->getParam(prefix + "engine.timer") : 0;
		if (!ns)
		    hPost->prepare(&filterName,&filterValue,sect,&msgName);
		else {
		    NamedList p("");
		    p.copyParams(false,*sect);
		    hPost->prepare(&filterName,&filterValue,&p,&msgName);
		}
	    }
	}
	JsMessageHandle* common = h ? static_cast<JsMessageHandle*>(h) :
	    static_cast<JsMessageHandle*>(hPost);
	GenObject* gen = h ? static_cast<GenObject*>(h) : static_cast<GenObject*>(hPost);
	if (common->initialize(*sect,scriptName,scriptFile,prefix)) {
	    if (old)
		continue;
	    old = listH.append(gen);
	    lck.drop();
	    bool ok = JsMessageHandle::install(gen);
	    lck.acquire(s_mutex);
	    if (old != listH.find(gen))
		continue;
	    if (ok) {
		Debug(&__plugin,DebugInfo,"Added global message %s %s (%p)",what,common->desc(),common);
		continue;
	    }
	    Debug(&__plugin,DebugWarn,"Failed to install global message %s %s (%p)",
		what,common->desc(),common);
	}
	listH.remove(gen,false);
	lck.drop();
	JsMessageHandle::uninstall(gen);
    }
}

bool JsGlobal::runMain()
{
    DDebug(&__plugin,DebugInfo,
	"JsGlobal::runMain(%s) instances=%u current number of instances=%u",
	name().c_str(),m_instanceCount,m_instances.count());
    if (m_instanceCount <= 1) {
	JsGlobalInstance* inst = new JsGlobalInstance(this,0);
	if (ScriptRun::Succeeded != inst->runMain()) {
	    TelEngine::destruct(inst);
	    return false;
	}
	m_instances.append(inst);
	inst->scheduleInitEvent();
    }
    else {
	unsigned int lCount = m_instances.count();
	// add instances if m_instancesCount is increased
	for (unsigned int i = 0; i < m_instanceCount; i++) {
	    JsGlobalInstance* inst = getInstance(i + 1);
	    // get instance returns a refcounted instance
	    if (inst) {
		inst->setInstanceCount(m_instanceCount);
		inst->scheduleInitEvent();
		TelEngine::destruct(inst);
		continue;
	    }
	    inst = new JsGlobalInstance(this,i + 1);
	    if (ScriptRun::Succeeded != inst->runMain()) {
		TelEngine::destruct(inst);
		return false;
	    }
	    m_instances.append(inst);
	    inst->scheduleInitEvent();
	}
	// remove instances if m_instances was decreased
	for (unsigned int i = m_instanceCount; i < lCount; i++) {
	    JsGlobalInstance* inst = getInstance(i + 1);
	    if (!inst)
		continue;
	    m_instances.remove(inst);
	    TelEngine::destruct(inst);
	}
	
    }
    return true;
}

void JsGlobal::unload(bool freeUnused)
{
    ObjList scripts, handlers, posthooks;
    Lock lck(s_mutex);
    if (freeUnused) {
	for (ObjList* o = s_globals.skipNull(); o;) {
	    JsGlobal* g = static_cast<JsGlobal*>(o->get());
	    if (g->m_inUse)
		o = o->skipNext();
	    else {
		Debug(&__plugin,DebugAll,"Removing unused/replaced global script %s (%p)",
		    g->name().c_str(),g);
		scripts.append(o->remove(false));
		o = o->skipNull();
	    }
	}
	for (ObjList* o = s_handlers.skipNull(); o;) {
	    JsHandler* h = static_cast<JsHandler*>(o->get());
	    if (h->inUse())
		o = o->skipNext();
	    else {
		Debug(&__plugin,DebugInfo,"Removing unused/replaced message handler %s (%p)",
		    h->desc(),h);
		handlers.append(o->remove(false));
		o = o->skipNull();
	    }
	}
	for (ObjList* o = s_posthooks.skipNull(); o;) {
	    JsPostHook* h = static_cast<JsPostHook*>(o->get());
	    if (h->inUse())
		o = o->skipNext();
	    else {
		Debug(&__plugin,DebugInfo,"Removing unused/replaced message posthook %s (%p)",
		    h->desc(),h);
		posthooks.append(o->remove(false));
		o = o->skipNull();
	    }
	}
    }
    else {
	s_globals.move(&scripts);
	s_handlers.move(&handlers);
	s_posthooks.move(&posthooks);
	String info;
	if (scripts.skipNull())
	    info << " " << scripts.count() << " script(s)";
	if (handlers.skipNull())
	    info << " " << handlers.count() << " handler(s)";
	if (posthooks.skipNull())
	    info << " " << posthooks.count() << " posthooks(s)";
	if (info)
	    Debug(&__plugin,DebugAll,"Exiting with%s",info.c_str());
    }
    lck.drop();
    scripts.clear();
    JsMessageHandle::uninstall(handlers);
    JsMessageHandle::uninstall(posthooks);
}

bool JsGlobal::buildNewScript(Lock& lck, ObjList* old, const String& scriptName,
    const String& fileName, int type, bool relPath, bool fromInit, unsigned int instances)
{
    bool objCount = s_trackObj && getObjCounting();
    NamedCounter* saved = 0;
    if (objCount)
	saved = Thread::setCurrentObjCounter(getObjCounter("js:" + scriptName,true));
    JsGlobal* oldScript = old ? static_cast<JsGlobal*>(old->get()) : 0;
    if (0 == instances) // keep number of instances if none is given
	instances = oldScript ? oldScript->instances() : 1;
    
    JsGlobal* script = new JsGlobal(scriptName,fileName,type,relPath,instances);
    bool ok = false;
    if (script->load() || !s_keepOldOnFail || !old) {
	if (old)
	    old->set(script,false);
	else
	    s_globals.append(script);
	lck.drop();
	TelEngine::destruct(oldScript);
	ok = script->runMain();
    }
    else {
	// Make sure we don't remove the old one if unused
	if (oldScript && fromInit)
	    oldScript->m_inUse = true;
	lck.drop();
	TelEngine::destruct(script);
    }
    if (objCount)
	Thread::setCurrentObjCounter(saved);
    return ok;
}


static const char* s_cmds[] = {
    "info",
    "eval",
    "reload",
    "load",
    "allocations",
    0
};

static const char* s_cmdsLine = "  javascript {info|eval[=context] instructions...|reload script|load [script=]file|"
	"allocations script top_no}";


JsModule::JsModule()
    : ChanAssistList("javascript",true),
      m_postHook(0), m_started(Engine::started())
{
    Output("Loaded module Javascript");
}

JsModule::~JsModule()
{
    Output("Unloading module Javascript");
    clearPostHook();
}

void JsModule::clearPostHook()
{
    if (m_postHook) {
	Engine::self()->setHook(m_postHook,true);
	TelEngine::destruct(m_postHook);
    }
}

void JsModule::msgPostExecute(const Message& msg, bool handled)
{
    const String& id = msg[YSTRING("id")];
    if (id.null())
	return;
    lock();
    RefPointer <JsAssist> ja = static_cast<JsAssist*>(find(id));
    unlock();
    if (ja)
	ja->msgPostExecute(msg,handled);
}

void JsModule::statusParams(String& str)
{
    Lock lck(JsGlobal::s_mutex);
    str << "globals=" << JsGlobal::globals().count()
	<< ",handlers=" << JsGlobal::handlers().count()
	<< ",posthooks=" << JsGlobal::posthooks().count();
    lck.acquire(this);
    str << ",routing=" << calls().count();
}

bool JsModule::commandExecute(String& retVal, const String& line)
{
    String cmd = line;
    if (!cmd.startSkip(name()))
	return false;
    cmd.trimSpaces();

    if (cmd.null() || cmd == YSTRING("info")) {
	retVal.clear();
	Lock lck(JsGlobal::s_mutex);
	for (ObjList* o = JsGlobal::globals().skipNull(); o ; o = o->skipNext()) {
	    JsGlobal* script = static_cast<JsGlobal*>(o->get());
	    retVal << script->name() << " = " << *script;
	    if (script->instances() > 1)
		retVal << ":" << script->instances();
	    retVal << "\r\n";
	}
	for (ObjList* o = JsGlobal::handlers().skipNull(); o ; o = o->skipNext()) {
	    retVal << "Handler ";
	    static_cast<JsHandler*>(o->get())->fillInfo(retVal);
	    retVal << "\r\n";
	}
	for (ObjList* o = JsGlobal::posthooks().skipNull(); o ; o = o->skipNext()) {
	    retVal << "PostHook ";
	    static_cast<JsPostHook*>(o->get())->fillInfo(retVal);
	    retVal << "\r\n";
	}
	lck.acquire(this);
	for (unsigned int i = 0; i < calls().length(); i++) {
	    ObjList* o = calls().getList(i);
	    for (o ? o = o->skipNull() : 0; o; o = o->skipNext()) {
		JsAssist* assist = static_cast<JsAssist*>(o->get());
		retVal << assist->id() << ": " << assist->stateName() << "\r\n";
	    }
	}
	return true;
    }

    if (cmd.startSkip("reload") && cmd.trimSpaces())
	return JsGlobal::reloadScript(cmd);

    if (cmd.startSkip("eval=",false) && cmd.trimSpaces()) {
	String scr;
	cmd.extractTo(" ",scr).trimSpaces();
	if (scr.null() || cmd.null())
	    return false;
	int pos = scr.find("/");
	String baseScr = scr.substr(0,pos);
	Lock mylock(JsGlobal::s_mutex);;
	JsGlobal* script = static_cast<JsGlobal*>(JsGlobal::globals()[baseScr]);
	if (script) {
	    JsGlobalInstance* inst = script->getInstance(scr);
	    if (inst) {
		RefPointer<ScriptContext> ctxt = inst->context();
		RefPointer<ScriptInfo> si = inst->scriptInfo();
		mylock.drop();
		TelEngine::destruct(inst);
		return evalContext(retVal,cmd,ctxt,si);
	    }
	}
	mylock.acquire(this);
	JsAssist* assist = static_cast<JsAssist*>(calls()[scr]);
	if (assist) {
	    RefPointer<ScriptContext> ctxt = assist->context();
	    RefPointer<ScriptInfo> si = assist->scriptInfo();
	    mylock.drop();
	    return evalContext(retVal,cmd,ctxt,si);
	}
	retVal << "Cannot find script context: " << scr << "\n\r";
	return true;
    }

    if (cmd.startSkip("eval") && cmd.trimSpaces())
	return evalContext(retVal,cmd);

    if (cmd.startSkip("allocations instance") && cmd.trimSpaces()) {
	String scr;
	cmd.extractTo(" ",scr).trimSpaces();
	String baseScr = scr.substr(0,scr.find("/"));
	unsigned int top = cmd.toInteger(25,0,1,100);
	if (scr.null())
	    return false;
	Lock mylock(JsGlobal::s_mutex);;
	JsGlobal* script = static_cast<JsGlobal*>(JsGlobal::globals()[baseScr]);
	if (script) {
	    JsGlobalInstance* inst = script->getInstance(scr);
	    if (inst) {
		bool ret = evalCtxtAllocations(retVal,top,inst->context(),script->parser().code(),scr);
		TelEngine::destruct(inst);
		return ret;
	    }
	}
	mylock.acquire(this);
	RefPointer<JsAssist> assist = static_cast<JsAssist*>(calls()[scr]);
	if (assist) {
	    mylock.drop();
	    return assist->evalAllocations(retVal,top);
	}
	retVal << "Cannot find script context: " << scr << "\n\r";
	return true;
    }

    if (cmd.startSkip("allocations total") && cmd.trimSpaces()) {
	String scr;
	cmd.extractTo(" ",scr).trimSpaces();
	unsigned int top = cmd.toInteger(25,0,1,100);
	if (scr.null())
	    return false;
	Lock mylock(JsGlobal::s_mutex);;
	JsGlobal* script = static_cast<JsGlobal*>(JsGlobal::globals()[scr]);
	if (script) {
	    ObjList list;
	    for (unsigned int i = 0; i <= script->instances(); i++) {
		JsGlobalInstance* inst = script->getInstance(i);
		if (inst) {
		    ObjList* c = inst->context() ? inst->context()->countAllocations() : 0;
		    if (c)
			list.insert(c);
		    TelEngine::destruct(inst);
		}
	    }
	    mylock.drop();
	    return evalInstanceAllocations(retVal,top,list,script->parser().code(),scr);
	}
	mylock.acquire(this);
	RefPointer<JsAssist> assist = static_cast<JsAssist*>(calls()[scr]);
	if (assist) {
	    mylock.drop();
	    return assist->evalAllocations(retVal,top);
	}
	retVal << "Cannot find script context: " << scr << "\n\r";
	return true;
    }

    if (cmd.startSkip("load") && cmd.trimSpaces()) {
	if (!cmd) {
	    retVal << "Missing mandatory argument specifying which file to load\n\r";
	    return true;
	}
	String name;
	int pos = cmd.find('=');
	if (pos > -1) {
	    name = cmd.substr(0,pos);
	    cmd = cmd.c_str() + pos + 1;
	}
	if (!cmd) {
	    retVal << "Missing file name argument\n\r";
	    return true;
	}
	if (cmd.endsWith("/")
#ifdef _WINDOWS
	    || cmd.endsWith("\\")
#endif
	) {
	    retVal << "Missing file name. Cannot load directory '" << cmd <<"'\n\r";
	    return true;
	}

	int extPos = cmd.rfind('.');
	int sepPos = cmd.rfind('/');
#ifdef _WINDOWS
	int backPos = cmd.rfind('\\');
	sepPos = sepPos > backPos ? sepPos : backPos;
#endif
	if (extPos < 0 || sepPos > extPos) { // for "dir.name/filename" cases
	    extPos = cmd.length();
	    cmd += ".js";
	}
	if (!name)
	    name = cmd.substr(sepPos + 1,extPos - sepPos - 1);
	if (!JsGlobal::initScript(name,cmd,ScriptInfo::Dynamic,true))
	    retVal << "Failed to load script from file '" << cmd << "'\n\r";
	return true;
    }

    return false;
}

bool JsModule::evalContext(String& retVal, const String& cmd, ScriptContext* context, ScriptInfo* si)
{
    JsParser parser;
    parser.basePath(s_basePath,s_libsPath);
    parser.setMaxFileLen(s_maxFile);
    parser.link(s_allowLink);
    parser.trace(s_allowTrace);
    if (!parser.parse(cmd)) {
	retVal << "parsing failed\r\n";
	return true;
    }

    ScriptRun* runner = parser.createRunner(context,"[command line]");
    if (!context)
	contextInit(runner);
    ScriptInfoHolder holder(si,si ? -1 : ScriptInfo::Eval);
    holder.attachScriptInfo(runner);
    ScriptRun::Status st = runner->run();
    if (st == ScriptRun::Succeeded) {
	while (ExpOperation* op = ExpEvaluator::popOne(runner->stack())) {
	    retVal << "'" << op->name() << "'='" << *op << "'\r\n";
	    TelEngine::destruct(op);
	}
    }
    else
	retVal << ScriptRun::textState(st) << "\r\n";
    // Not called in script context: cleanup the context
    if (!context)
	runner->context()->cleanup();
    TelEngine::destruct(runner);
    return true;
}

bool JsModule::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;
    if (partLine.null() || (partLine == "help"))
	itemComplete(msg.retValue(),name(),partWord);
    else if (partLine == name()) {
	static const String s_eval("eval=");
	if (partWord.startsWith(s_eval)) {
	    Lock lck(JsGlobal::s_mutex);
	    for (ObjList* o = JsGlobal::globals().skipNull(); o ; o = o->skipNext()) {
		JsGlobal* script = static_cast<JsGlobal*>(o->get());
		if (script->name()) {
		    for (unsigned int i = 0; i <= script->instances(); i++) {
			JsGlobalInstance* inst = script->getInstance(i);
			if (inst) {
			    itemComplete(msg.retValue(),s_eval + inst->toString(),partWord);
			    TelEngine::destruct(inst);
			}
		    }
		}
	    }
	    lck.acquire(this);
	    for (unsigned int i = 0; i < calls().length(); i++) {
		ObjList* o = calls().getList(i);
		for (o ? o = o->skipNull() : 0; o; o = o->skipNext()) {
		    JsAssist* assist = static_cast<JsAssist*>(o->get());
		    itemComplete(msg.retValue(),s_eval + assist->id(),partWord);
		}
	    }
	    return true;
	}
	for (const char** list = s_cmds; *list; list++)
	    itemComplete(msg.retValue(),*list,partWord);
	return true;
    }
    else if (partLine == YSTRING("javascript reload") || partLine == YSTRING("javascript allocations total")) {
	Lock lck(JsGlobal::s_mutex);
	for (ObjList* o = JsGlobal::globals().skipNull(); o ; o = o->skipNext()) {
	    JsGlobal* script = static_cast<JsGlobal*>(o->get());
	    if (script->name())
		itemComplete(msg.retValue(),script->name(),partWord);
	}
	return true;
    }
    else if (partLine == YSTRING("javascript allocations instance")) {
	Lock lck(JsGlobal::s_mutex);
	for (ObjList* o = JsGlobal::globals().skipNull(); o ; o = o->skipNext()) {
	    JsGlobal* script = static_cast<JsGlobal*>(o->get());
	    if (script && script->instances() > 1) {
		for (unsigned int i = 0; i <= script->instances(); i++) {
		    JsGlobalInstance* inst = script->getInstance(i);
		    if (inst) {
			itemComplete(msg.retValue(),inst->toString(),partWord);
			TelEngine::destruct(inst);
		    }
		}
	    }
	}
	return true;
    }
    else if (partLine == YSTRING("javascript allocations")) {
	itemComplete(msg.retValue(),"total",partWord);
	itemComplete(msg.retValue(),"instance",partWord);
	return 0;
    }
    return Module::commandComplete(msg,partLine,partWord);
}

bool JsModule::received(Message& msg, int id)
{
    switch (id) {
	case Help:
	    {
		const String* line = msg.getParam("line");
		if (TelEngine::null(line)) {
		    msg.retValue() << s_cmdsLine << "\r\n";
		    return false;
		}
		if (name() != *line)
		    return false;
	    }
	    msg.retValue() << s_cmdsLine << "\r\n";
	    msg.retValue() << "Controls and executes Javascript commands\r\n";
	    return true;
	case Preroute:
	case Route:
	    {
		const String* chanId = msg.getParam("id");
		if (TelEngine::null(chanId))
		    break;
		Lock mylock(this);
		RefPointer <JsAssist> ca = static_cast<JsAssist*>(find(*chanId));
		switch (id) {
		    case Preroute:
			if (ca) {
			    mylock.drop();
			    return ca->msgPreroute(msg);
			}
			ca = static_cast<JsAssist*>(create(msg,*chanId));
			if (ca) {
			    calls().append(ca);
			    mylock.drop();
			    ca->msgStartup(msg);
			    return ca->msgPreroute(msg);
			}
			return false;
		    case Route:
			if (ca) {
			    mylock.drop();
			    return ca->msgRoute(msg);
			}
			ca = static_cast<JsAssist*>(create(msg,*chanId));
			if (ca) {
			    calls().append(ca);
			    mylock.drop();
			    ca->msgStartup(msg);
			    return ca->msgRoute(msg);
			}
			return false;
		} // switch (id)
	    }
	    break;
	case Ringing:
	case Answered:
	    {
		const String* chanId = msg.getParam("peerid");
		if (TelEngine::null(chanId))
		    return false;
		Lock mylock(this);
		RefPointer <JsAssist> ca = static_cast<JsAssist*>(find(*chanId));
		if (!ca)
		    return false;
		switch (id) {
		    case Ringing:
			return ca && ca->msgRinging(msg);
		    case Answered:
			return ca && ca->msgAnswered(msg);
		}
	    }
	    break;
	case Halt:
	    s_engineStop = true;
	    clearPostHook();
	    JsGlobal::unloadAll();
	    return false;
	case EngStart:
	    if (!m_started) {
		m_started = true;
		Configuration cfg(Engine::configFile("javascript"));
		JsGlobal::loadScripts(cfg.getSection("late_scripts"),cfg.getSection("instances"));
	    }
	    return false;
    } // switch (id)
    return ChanAssistList::received(msg,id);
}

bool JsModule::received(Message& msg, int id, ChanAssist* assist)
{
    return ChanAssistList::received(msg,id,assist);
}

ChanAssist* JsModule::create(Message& msg, const String& id)
{
    if ((msg == YSTRING("chan.startup")) && (msg[YSTRING("direction")] == YSTRING("outgoing")))
	return 0;
    Lock lck(JsGlobal::s_mutex);
    ScriptRun* runner = m_assistCode.createRunner(0,NATIVE_TITLE);
    lck.drop();
    if (!runner)
	return 0;
    DDebug(this,DebugInfo,"Creating Javascript for '%s'",id.c_str());
    JsAssist* ca = new JsAssist(this,id,runner);
    if (ca->init())
	return ca;
    TelEngine::destruct(ca);
    return 0;
}

bool JsModule::unload()
{
    clearPostHook();
    uninstallRelays();
    return true;
}

void JsModule::initialize()
{
    Output("Initializing module Javascript");
    ChanAssistList::initialize();
    setup();
    installRelay(Help);
    if (!m_postHook)
	Engine::self()->setHook(m_postHook = new JsPostExecute);
    Configuration cfg(Engine::configFile("javascript"));
    String tmp = Engine::sharedPath();
    tmp << Engine::pathSeparator() << "scripts";
    tmp = cfg.getValue("general","scripts_dir",tmp);
    Engine::runParams().replaceParams(tmp);
    if (tmp && !tmp.endsWith(Engine::pathSeparator()))
	tmp += Engine::pathSeparator();
    s_basePath = tmp;
    tmp = cfg.getValue("general","include_dir","${configpath}");
    Engine::runParams().replaceParams(tmp);
    if (tmp && !tmp.endsWith(Engine::pathSeparator()))
	tmp += Engine::pathSeparator();
    s_libsPath = tmp;
    s_maxFile = cfg.getIntValue("general","max_length",500000,32768,2097152);
    s_autoExt = cfg.getBoolValue("general","auto_extensions",true);
    s_allowAbort = cfg.getBoolValue("general","allow_abort");
    s_trackObj = cfg.getBoolValue("general","track_objects");
    s_trackCreation = cfg.getIntValue("general","track_obj_life",s_trackCreation,0);
    JsGlobal::s_keepOldOnFail = cfg.getBoolValue("general","keep_old_on_fail");
    bool changed = false;
    if (cfg.getBoolValue("general","allow_trace") != s_allowTrace) {
	s_allowTrace = !s_allowTrace;
	changed = true;
    }
    if (cfg.getBoolValue("general","allow_link",true) != s_allowLink) {
	s_allowLink = !s_allowLink;
	changed = true;
    }
    tmp = cfg.getValue("general","routing");
    Engine::runParams().replaceParams(tmp);
    Lock lck(JsGlobal::s_mutex);
    if (changed || m_assistCode.scriptChanged(tmp,s_basePath,s_libsPath)) {
	m_assistCode.clear();
	m_assistCode.setMaxFileLen(s_maxFile);
	m_assistCode.link(s_allowLink);
	m_assistCode.trace(s_allowTrace);
	m_assistCode.basePath(s_basePath,s_libsPath);
	m_assistCode.adjustPath(tmp);
	if (m_assistCode.parseFile(tmp))
	    Debug(this,DebugInfo,"Parsed routing script: %s",tmp.c_str());
	else if (tmp)
	    Debug(this,DebugWarn,"Failed to parse script: %s",tmp.c_str());
    }
    JsGlobal::markUnused();
    lck.drop();
    JsGlobal::loadHandlers(cfg.getSection(YSTRING("handlers")),true);
    JsGlobal::loadHandlers(cfg.getSection(YSTRING("posthooks")),false);
    JsGlobal::loadScripts(cfg.getSection("scripts"),cfg.getSection("instances"));
    if (m_started)
	JsGlobal::loadScripts(cfg.getSection("late_scripts"),cfg.getSection("instances"));
    JsGlobal::reloadDynamic();
    JsGlobal::freeUnused();
}

void JsModule::init(int priority)
{
    ChanAssistList::init(priority);
    installRelay(Halt,120);
    installRelay(Route,priority);
    installRelay(Ringing,priority);
    installRelay(Answered,priority);
    Engine::install(new MessageRelay("call.preroute",this,Preroute,priority,name()));
    Engine::install(new MessageRelay("engine.start",this,EngStart,150,name()));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
