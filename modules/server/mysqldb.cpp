/**
 * mysqldb.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This is the MySQL support from Yate.
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

#include <mysql.h>
#include <mysqld_error.h>
#include <errmsg.h>

#ifndef CLIENT_MULTI_STATEMENTS
#define CLIENT_MULTI_STATEMENTS 0
#define mysql_next_result(x) (-1)
#endif

#if (MYSQL_VERSION_ID > 41000)
#define HAVE_MYSQL_410
#else
#define mysql_warning_count(x) (0)
#define mysql_library_init mysql_server_init
#define mysql_library_end mysql_server_end
#endif

// MySQL 8.0.1 removes declaration of my_bool
#ifndef HAVE_MYSQL_MY_BOOL
typedef char my_bool;
#endif

using namespace TelEngine;
namespace { // anonymous

class DbThread;
class DbQuery;
class DbQueryList;
class MySqlConn;
class MyAcct;
class InitThread;

static ObjList s_conns;
static unsigned int s_failedConns;
Mutex s_acctMutex(false,"MySQL::accts");

/**
  * Class MyConn
  * A MySQL connection
  */
class MyConn : public String
{
    friend class DbThread;
    friend class MyAcct;

public:
    // NOTE: Use negative values: mysql uses positive
    enum QueryError {
	ConnDisconnected = -1,
	DbDisconnected = -2,
    };

    inline MyConn(const String& name, MyAcct* conn)
	: String(name),
	  m_conn(0), m_owner(conn),
	  m_thread(0)
	{}
    ~MyConn();

    void closeConn();
    void runQueries();
    int queryDbInternal(DbQuery* query);

    static const TokenDict s_error[];

private:
    MYSQL* m_conn;
    MyAcct* m_owner;
    DbThread* m_thread;
    bool testDb();
};

class QueryStats : public Mutex
{
public:
    inline QueryStats()
	: m_total(0), m_failed(0), m_failedNoConn(0), m_queueTime(0), m_queryTime(0)
	{}
    inline QueryStats(const QueryStats& other)
	{ *this = other; }
    inline QueryStats& operator=(const QueryStats& other) {
	    m_total = other.m_total;
	    m_failed = other.m_failed;
	    m_failedNoConn = other.m_failedNoConn;
	    m_queueTime = other.m_queueTime;
	    m_queryTime = other.m_queryTime;
	    return *this;
	}

    uint64_t m_total;                    // Total queries
    uint64_t m_failed;                   // Tried and failed queries
    uint64_t m_failedNoConn;             // Not tried queries: no connection
    uint64_t m_queueTime;                // Total time queries stayed in queue
    uint64_t m_queryTime;                // Total DB query time
};

/**
  * Class MyAcct
  * A MySQL database account
  */
class MyAcct : public RefObject, public Mutex
{
    friend class MyConn;
public:
    MyAcct(const NamedList* sect);
    ~MyAcct();

    bool initialize(const NamedList& params, bool constr = false);
    bool initDb();
    int initConns();
    void dropDb();
    inline bool ok() const
	{ return 0 != m_connections.skipNull(); }
    inline const char* c_str() const
	{ return m_name; }

    void appendQuery(DbQuery* query);
    inline void resetLostConn() {
	    Lock lck(m_statsMutex);
	    m_failedConns = 0;
	}
    inline void stats(QueryStats& dest)
	{ Lock lck(m_statsMutex); dest = m_stats; }
    inline bool hasConn()
	{ return ((int)(m_poolSize - m_failedConns) > 0 ? true : false); }
    inline void setRetryWhen()
	{ m_retryWhen = Time::msecNow() + m_retryTime * 1000; }
    inline u_int64_t retryWhen()
	{ return m_retryWhen; }
    inline bool shouldRetryInit()
	{ return m_retryTime && m_connections.count() < (unsigned int)m_poolSize; }
    inline int poolSize()
	{ return m_poolSize; }
    inline unsigned int queryRetry() const
	{ return m_queryRetry; }
    virtual const String& toString() const
	{ return m_name; }

protected:
    void queryEnded(DbQuery& query, bool ok = true);

private:
    String m_name;
    unsigned int m_timeout;
    // interval at which connection initialization should be tried
    unsigned int m_retryTime;
    // when should we try initialization again?
    u_int64_t m_retryWhen;

    String m_host;
    String m_user;
    String m_pass;
    String m_db;
    String m_unix;
    unsigned int m_port;
    bool m_compress;
    String m_encoding;
    unsigned int m_queryRetry;
    unsigned int m_warnQueryDuration;    // Warn if query duration exceeds this value

    int m_poolSize;
    ObjList m_connections;
    ObjList m_queryQueue;

    Semaphore m_queueSem;
    Mutex m_queueMutex;

    // stats counters
    QueryStats m_stats;
    unsigned int m_failedConns;
    Mutex m_statsMutex;
};

/**
  * Class DbThread
  * Running thread for a MySQL connection
  */
class DbThread : public Thread
{
public:
    inline DbThread(MyConn* conn)
	: Thread("Mysql Connection"), m_conn(conn)
	{ }
    virtual void run();
    virtual void cleanup();
private:
    MyConn* m_conn;
};

/**
  * Class InitThread
  * Running thread for initializing MySQL connections
  */
class InitThread : public Thread
{
public:
    InitThread();
    ~InitThread();
    virtual void run();
    virtual void cleanup();
};

/**
  * Class MyModule
  * The MySQL database module
  */
class MyModule : public Module
{
public:
    MyModule();
    ~MyModule();
    void statusModule(String& str);
    virtual bool received(Message& msg, int id);
    InitThread* m_initThread;
    inline void startInitThread()
    {
	lock();
	if (!m_initThread)
	    (m_initThread = new InitThread())->startup();
	unlock();
    }
protected:
    virtual void initialize();
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    virtual void genUpdate(Message& msg);
private:
    bool m_init;
};

static MyModule module;

/**
  * Class DbQuery
  * A MySQL query
  */
class DbQuery : public String, public Semaphore
{
    friend class MyConn;
public:
    inline DbQuery(const String& query, Message* msg, uint64_t now = Time::now())
	: String(query),
	  Semaphore(1,"MySQL::query"),
	  m_msg(msg), m_finished(false), m_cancelled(false), m_code(0),
	  m_time(now), m_dequeued(0), m_start(0), m_end(0)
	{ XDebug(&module,DebugAll,"DbQuery '%s' msg=(%p) [%p]",safe(),m_msg,this); }
    inline ~DbQuery()
	{ XDebug(&module,DebugAll,"~DbQuery [%p]",this); }
    inline bool finished() const
	{ return m_finished; }
    inline void setFinished() {
	    m_finished = true;
	    if (!m_msg)
		destruct();
	}
    inline bool cancelled() const
	{ return m_cancelled; }
    inline void setCancelled()
	{ m_cancelled = true; }
    inline uint64_t time() const
	{ return m_time; }
    inline uint64_t dequeue() const
	{ return m_dequeued; }
    inline uint64_t start() const
	{ return m_start; }
    inline uint64_t end() const
	{ return m_end; }
    inline int error() const
	{ return m_code; }
    inline void setError(int code)
	{ m_code = code; }
    inline void setDequeued(uint64_t now = Time::now())
	{ m_dequeued = now; }
    inline void setStart(uint64_t now = Time::now())
	{ m_start = now; }
    inline void setEnd(uint64_t now = Time::now())
	{ m_end = now; }

private:
    Message* m_msg;
    bool m_finished;
    bool m_cancelled;
    int m_code;
    uint64_t m_time;
    uint64_t m_dequeued;
    uint64_t m_start;
    uint64_t m_end;
};

static Mutex s_libMutex(false,"MySQL::lib");
static int s_libCounter = 0;
static unsigned int s_queryRetry = 1;

const TokenDict MyConn::s_error[] = {
    // Our errors
    {"noconn", ConnDisconnected},                    // Connection is not connected when processing the query
    {"noconn", DbDisconnected},                      // Database is not connected when handling the query
    // mysql client errors
#ifdef CR_SERVER_LOST
    {"timeout", CR_SERVER_LOST},                     // Connection closed during query
#endif
#ifdef CR_SERVER_GONE_ERROR
    {"timeout", CR_SERVER_GONE_ERROR},               // Connection closed between queries
#endif
    // mysql server errors
#ifdef ER_QUERY_TIMEOUT
    {"timeout", ER_QUERY_TIMEOUT},                   // Maximum query execution
#endif
#ifdef ER_CLIENT_INTERACTION_TIMEOUT
    {"timeout", ER_CLIENT_INTERACTION_TIMEOUT},      // Client idle timeout
#endif
#ifdef ER_LOCK_DEADLOCK
    {"deadlock", ER_LOCK_DEADLOCK},
#endif
    {0,0}
};

static inline unsigned int getQueryWarnDuration(const NamedList& params, unsigned int defVal = 0)
{
    defVal = params.getIntValue(YSTRING("warn_query_duration"),defVal,0);
    return defVal ? (defVal < 50 ? 50 : defVal) : 0;
}


/**
  * Class MyHandler
  * Message handler for "database" message
  */
class MyHandler : public MessageHandler
{
public:
    MyHandler(unsigned int prio = 100)
	: MessageHandler("database",prio,module.name())
	{ }
    virtual bool received(Message& msg);
};


/**
  * MyConn
  */
MyConn::~MyConn()
{
    m_conn = 0;
    //closeConn();
    Debug(&module,DebugAll,"Database connection '%s' destroyed",c_str());
}

void MyConn::closeConn()
{
    DDebug(&module,DebugInfo,"Database connection '%s' trying to close %p",c_str(),m_conn);
    if (!m_conn)
	return;
    MYSQL* tmp = m_conn;
    m_conn = 0;
    mysql_close(tmp);
    String name(*this);
    if (m_owner)
	m_owner->m_connections.remove(this);
    Debug(&module,DebugInfo,"Database connection '%s' closed",name.c_str());
}

void MyConn::runQueries()
{
    while (m_conn && m_owner) {
	Thread::check();
	m_owner->m_queueSem.lock(Thread::idleUsec());

	Lock mylock(m_owner->m_queueMutex);
	DbQuery* query = static_cast<DbQuery*>(m_owner->m_queryQueue.remove(false));
	if (!query)
	    continue;
	mylock.drop();

	DDebug(&module,DebugAll,"Connection '%s' will try to execute '%s'",
	    c_str(),query->c_str());
	query->setDequeued();
	queryDbInternal(query);
	query->unlock();
	query->setFinished();
	DDebug(&module,DebugAll,"Connection '%s' finished executing query",c_str());
    }
}

bool MyConn::testDb()
{
     return m_conn && !mysql_ping(m_conn);
}

static inline String& dumpUsec(String& buf, uint64_t us)
{
    us = (us + 500) / 1000;
    return buf.printf("%u.%03u",(unsigned int)(us / 1000),(unsigned int)(us % 1000));
}

// perform the query, fill the message with data
//  return number of rows, -1 for error
int MyConn::queryDbInternal(DbQuery* query)
{
    if (!testDb()) {
	if (!query->cancelled()) {
	    Debug(&module,DebugNote,"Connection '%s' query '%s' failed: disconnected",
		c_str(),query->c_str());
	    query->setError(ConnDisconnected);
	}
	m_owner->queryEnded(*query,false);
	return -1;
    }
    m_owner->resetLostConn();

    query->setStart();
    int retry = m_owner->queryRetry();
    do {
	if (!mysql_real_query(m_conn,query->safe(),query->length()))
	    break;
	if (!query->cancelled()) {
	    int err = mysql_errno(m_conn);
#ifdef ER_LOCK_DEADLOCK
	    if (err == ER_LOCK_DEADLOCK && retry-- > 0) {
		Debug(&module,DebugInfo,
		    "Connection '%s' query '%s' failed code=%d %s. Retrying (remaining=%d)",
		    c_str(),query->c_str(),err,mysql_error(m_conn),retry);
		continue;
	    }
#endif
	    Debug(&module,DebugWarn,"Connection '%s' query '%s' failed: code=%d %s",
		c_str(),query->c_str(),err,mysql_error(m_conn));
	    query->setError(err);
	}
	m_owner->queryEnded(*query,false);
	return -1;
    }
    while (true);

#ifdef DEBUG
    uint64_t inter = Time::now();
    unsigned int warnDuration = 1;
#else
    uint64_t inter = 0;
    unsigned int warnDuration = 0;
    if (query->m_msg) {
	warnDuration = getQueryWarnDuration(*(query->m_msg),m_owner->m_warnQueryDuration);
	if (warnDuration)
	    inter = Time::now();
    }
#endif
    int total = 0;
    unsigned int warns = 0;
    unsigned int affected = 0;
    do {
	MYSQL_RES* res = mysql_store_result(m_conn);
	warns += mysql_warning_count(m_conn);
	affected += (unsigned int)mysql_affected_rows(m_conn);
	if (res && !query->cancelled()) {
	    unsigned int cols = mysql_num_fields(res);
	    unsigned int rows = (unsigned int)mysql_num_rows(res);
	    XDebug(&module,DebugAll,
		"Connection '%s' query (%p) got result set %p rows=%u cols=%u [%p]",
		safe(),query,res,rows,cols,this);
	    total += rows;
	    if (query->m_msg) {
		MYSQL_FIELD* fields = mysql_fetch_fields(res);
		query->m_msg->setParam(YSTRING("columns"),cols);
		query->m_msg->setParam(YSTRING("rows"),rows);
		Array *a = new Array(cols,rows+1);
		unsigned int c;
		ObjList** columns = new ObjList*[cols];
		// get top of columns and add field names
		for (c = 0; c < cols; c++) {
		    columns[c] = a->getColumn(c);
		    if (columns[c])
			columns[c]->set(new String(fields[c].name));
		    else
			Debug(&module,DebugCrit,
			    "Connection '%s' query '%s': no array for column %u [%p]",
			    safe(),query->safe(),c,this);
		}
		// and now data row by row
		for (unsigned int r = 1; r <= rows; r++) {
		    MYSQL_ROW row = mysql_fetch_row(res);
		    if (!row)
			break;
		    unsigned long* len = mysql_fetch_lengths(res);
		    for (c = 0; c < cols; c++) {
			// advance pointer in each column
			if (columns[c])
			    columns[c] = columns[c]->next();
			if (!(columns[c] && row[c]))
			    continue;
			bool binary = false;
			switch (fields[c].type) {
			    case MYSQL_TYPE_STRING:
			    case MYSQL_TYPE_VAR_STRING:
			    case MYSQL_TYPE_BLOB:
				// field may hold binary data
				binary = (63 == fields[c].charsetnr);
			    default:
				break;
			}
			if (binary) {
			    if (!len)
				continue;
			    columns[c]->set(new DataBlock(row[c],len[c]));
			}
			else
			    columns[c]->set(new String(row[c]));
		    }
		}
		delete[] columns;
		query->m_msg->userData(a);
		a->deref();
	    }
	}
	if (res)
	    mysql_free_result(res);
    } while (!mysql_next_result(m_conn));

    m_owner->queryEnded(*query);
    if (inter && (query->end() - query->start()) >= (1000 * warnDuration)) {
	String t, q, f;
	Debug(&module,warnDuration > 10 ? DebugNote : DebugAll,
	    "Connection '%s' query time is %s %s+%s query='%s'",c_str(),
	    dumpUsec(t,query->end() - query->start()).c_str(),
	    dumpUsec(q,inter - query->start()).c_str(),
	    dumpUsec(f,query->end() - inter).c_str(),query->c_str());
    }
    if (query->m_msg) {
	query->m_msg->setParam(YSTRING("affected"),affected);
	if (warns)
	    query->m_msg->setParam(YSTRING("warnings"),warns);
    }
    return total;
}

/**
  * MyAcct
  */
MyAcct::MyAcct(const NamedList* sect)
    : Mutex(true,"MySQL::acct"),
      m_name(*sect),
      m_compress(false),
      m_queryRetry(s_queryRetry),
      m_warnQueryDuration(0),
      m_poolSize(sect->getIntValue("poolsize",1,1)),
      m_queueSem(m_poolSize,"MySQL::queue"),
      m_queueMutex(false,"MySQL::queue"),
      m_failedConns(0),
      m_statsMutex(false,"MySQL::stats")
{
    int tout = sect->getIntValue("timeout",10000);
    // round to seconds
    m_timeout = (tout + 500) / 1000;
    // but make sure it doesn't round to zero unless zero was requested
    if (tout && !m_timeout)
	m_timeout = 1;
    m_host = sect->getValue("host");
    m_user = sect->getValue("user","mysql");
    m_pass = sect->getValue("password");
    m_db = sect->getValue("database","yate");
    m_port = sect->getIntValue("port");
    m_unix = sect->getValue("socket");
    m_compress = sect->getBoolValue("compress");
    m_queryRetry = sect->getIntValue(YSTRING("query_retry"),m_queryRetry,1,10);
    m_encoding = sect->getValue("encoding");
    m_retryTime = sect->getIntValue("initretry",10); // default value is 10 seconds
    setRetryWhen(); // set retry interval
    initialize(*sect,true);
}

MyAcct::~MyAcct()
{
    Debug(&module,DebugAll,"Destroying account '%s' [%p]",c_str(),this);
    s_conns.remove(this,false);
    // FIXME: should we try to do it from this thread?
    dropDb();
}

int MyAcct::initConns()
{
    int count = m_connections.count();
    Debug(&module,count != m_poolSize ? DebugInfo : DebugAll,
	"Account '%s' initializing %d/%d connections [%p]",
	c_str(),count,m_poolSize,this);
    // set new retry interval
    setRetryWhen();

    for (int i = count; i < m_poolSize; i++) {
	MyConn* mySqlConn = new MyConn(toString() + "." + String(i), this);

	mySqlConn->m_conn = mysql_init(mySqlConn->m_conn);
	if (!mySqlConn->m_conn) {
	    Debug(&module,DebugCrit,"Could not start connection %d for '%s'",i,c_str());
	    return i;
	}
	DDebug(&module,DebugAll,"Connection '%s' for account '%s' was created",mySqlConn->c_str(),c_str());

	if (m_compress)
	    mysql_options(mySqlConn->m_conn,MYSQL_OPT_COMPRESS,0);

	mysql_options(mySqlConn->m_conn,MYSQL_OPT_CONNECT_TIMEOUT,(const char*)&m_timeout);

#ifdef MYSQL_OPT_READ_TIMEOUT
	mysql_options(mySqlConn->m_conn,MYSQL_OPT_READ_TIMEOUT,(const char*)&m_timeout);
#endif

#ifdef MYSQL_OPT_WRITE_TIMEOUT
	mysql_options(mySqlConn->m_conn,MYSQL_OPT_WRITE_TIMEOUT,(const char*)&m_timeout);
#endif

	if (mysql_real_connect(mySqlConn->m_conn,m_host,m_user,m_pass,m_db,m_port,m_unix,CLIENT_MULTI_STATEMENTS)) {

#ifdef MYSQL_OPT_RECONNECT
	    // this option must be set after connect - bug in mysql client library
	    my_bool reconn = 1;
	    mysql_options(mySqlConn->m_conn,MYSQL_OPT_RECONNECT,(const char*)&reconn);
#endif

#ifdef HAVE_MYSQL_SET_CHARSET
	    if (m_encoding && mysql_set_character_set(mySqlConn->m_conn,m_encoding))
		Debug(&module,DebugWarn,"Failed to set encoding '%s' on connection '%s'",
		    m_encoding.c_str(),mySqlConn->c_str());
#else
	    if (m_encoding)
		Debug(&module,DebugWarn,"Your client library does not support setting the character set");
#endif
	    DbThread* thread = new DbThread(mySqlConn);

	    if (thread->startup())
		m_connections.append(mySqlConn);
	    else {
	    	delete thread;
		TelEngine::destruct(mySqlConn);
	    }
	}
	else {
	    Debug(&module,DebugNote,"Connection '%s' failed to connect to server: %d %s [%p]",
		mySqlConn->c_str(),mysql_errno(mySqlConn->m_conn),mysql_error(mySqlConn->m_conn),
		mySqlConn);
	    TelEngine::destruct(mySqlConn);
	    return i;
	}
    }

    return m_poolSize;
}

bool MyAcct::initialize(const NamedList& params, bool constr)
{
    m_warnQueryDuration = getQueryWarnDuration(params);
    if (constr) {
	Debug(&module,DebugNote,
	    "Created account '%s' poolsize=%d db='%s' host='%s' port=%u timeout=%u [%p]",
	    c_str(),m_poolSize,m_db.safe(),m_host.safe(),m_port,m_timeout,this);
	return true;
    }
    if (ok())
	return true;
    Debug(&module,DebugNote,"Reinitializing account '%s' [%p]",c_str(),this);
    return initDb();
}

// initialize the database connection
bool MyAcct::initDb()
{
    s_libMutex.lock();
    if (0 == s_libCounter++) {
	DDebug(&module,DebugAll,"Initializing the MySQL library");
	mysql_library_init(0,0,0);
    }
    s_libMutex.unlock();

    Lock lck(this);
    int n = initConns();
    if (n != m_poolSize) {
	if (!n)
	    Alarm(&module,"database",DebugWarn,
		"Could not create any connections for account '%s' re-trying in %d seconds",
		c_str(),m_retryTime);
	else
	    Alarm(&module,"database",DebugMild,
		"Initialized %d of %d connection(s) for account '%s' re-trying in %d seconds",
		n,m_poolSize,c_str(),m_retryTime);
	lck.drop();
	module.startInitThread();
    }
    return true;
}

// drop the connection
void MyAcct::dropDb()
{
    Lock mylock(this);

    ObjList* o = m_connections.skipNull();
    for (; o; o = o->skipNext()) {
	MyConn* c = static_cast<MyConn*>(o->get());
	if (c)
	    c->closeConn();
    }
    m_queryQueue.clear();
    Debug(&module,DebugNote,"Database account '%s' closed [%p]",c_str(),this);

    s_libMutex.lock();
    if (0 == --s_libCounter) {
	DDebug(&module,DebugInfo,"Deinitializing the MySQL library");
	mysql_library_end();
    }
    s_libMutex.unlock();
}

void MyAcct::appendQuery(DbQuery* query)
{
    DDebug(&module, DebugAll, "Account '%s' received a new query %p",c_str(),query);
    m_queueMutex.lock();
    m_queryQueue.append(query);
    m_queueMutex.unlock();
    m_queueSem.unlock();
}

void MyAcct::queryEnded(DbQuery& query, bool ok)
{
    if (query.start() && !query.end())
	query.setEnd();
    Lock lck(m_statsMutex);
    m_stats.m_total++;
    if (!ok) {
	if (!query.start()) {
	    // Not started. Lost connection
	    m_stats.m_failedNoConn++;
	    if (m_failedConns < (unsigned int)m_poolSize)
		m_failedConns++;
	}
	else
	    m_stats.m_failed++;
    }
    m_stats.m_queueTime += query.dequeue() - query.time();
    m_stats.m_queryTime += query.end() - query.start();
    lck.drop();
    module.changed();
}


/**
  * DbThread
  */
void DbThread::run()
{
    mysql_thread_init();
    m_conn->m_thread = this;
    m_conn->runQueries();
}

void DbThread::cleanup()
{
    Debug(&module,DebugInfo,"Cleaning up connection %p thread [%p]",m_conn,this);
    if (m_conn) {
	m_conn->m_thread = 0;
	m_conn->closeConn();
    }
    mysql_thread_end();
}


static inline MyAcct* findDb(const String& account)
{
    if (account.null())
	return 0;
    ObjList* l = s_conns.find(account);
    return l ? static_cast<MyAcct*>(l->get()) : 0;
}

static inline bool findDb(RefPointer<MyAcct>& acct, const String& account)
{
    Lock lck(s_acctMutex);
    acct = findDb(account);
    return acct != 0;
}


/**
  * MyHandler
  */
static inline void fillQueryError(NamedList& params, int code, bool cancelled = false)
{
    if (cancelled)
	params.setParam(YSTRING("error"),"cancelled");
    else if (code) {
	params.setParam(YSTRING("error"),lookup(code,MyConn::s_error,"failure"));
	params.setParam(YSTRING("code"),code);
    }
}

bool MyHandler::received(Message& msg)
{
    const String* str = msg.getParam(YSTRING("account"));
    if (TelEngine::null(str))
	return false;
    RefPointer<MyAcct> db;
    if (!(findDb(db,*str) && db->ok())) {
	if (db)
	    fillQueryError(msg,MyConn::DbDisconnected);
	return false;
    }

    str = msg.getParam(YSTRING("query"));
    if (!TelEngine::null(str)) {
	if (msg.getBoolValue(YSTRING("results"),true)) {
	    DbQuery* q = new DbQuery(*str,&msg);
	    db->appendQuery(q);
	    while (!q->finished()) {
		if (!q->cancelled() && Thread::check(false))
		    q->setCancelled();
		q->lock(Thread::idleUsec());
	    }
	    fillQueryError(msg,q->error(),q->cancelled());
	    TelEngine::destruct(q);
	}
	else
	    db->appendQuery(new DbQuery(*str,0));
    }
    msg.setParam(YSTRING("dbtype"),"mysqldb");
    db = 0;
    return true;
}

/**
  * InitThread
  */
InitThread::InitThread()
    : Thread("Mysql Init")
{
    Debug(&module,DebugAll,"InitThread created [%p]",this);
}

InitThread::~InitThread()
{
    Debug(&module,DebugAll,"InitThread thread terminated [%p]",this);
    module.lock();
    module.m_initThread = 0;
    module.unlock();
}

void InitThread::run()
{
    mysql_thread_init();
    while(!Engine::exiting()) {
	Thread::sleep(1,true);
	bool retryAgain = false;
	s_acctMutex.lock();
	for (ObjList* o = s_conns.skipNull(); o; o = o->skipNext()) {
	    MyAcct* acc = static_cast<MyAcct*>(o->get());
	    if (acc->shouldRetryInit() && acc->retryWhen() <= Time::msecNow()) {
		int count = acc->initConns();
		if (count < acc->poolSize())
		    Debug(&module,(count ? DebugMild : DebugWarn),"Account '%s' has %d initialized connections out of"
			" a pool of %d",acc->c_str(),count,acc->poolSize());
		else
		    Debug(&module,DebugInfo,"All connections for account '%s' have been initialized, pool size is %d",
			  acc->c_str(),acc->poolSize());
	    }
	    if (acc->shouldRetryInit())
		retryAgain = true;
	}
	s_acctMutex.unlock();
	if (!retryAgain)
	    break;
    }
}

void InitThread::cleanup()
{
    Debug(&module,DebugInfo,"InitThread::cleanup() [%p]",this);
    mysql_thread_end();
}

/**
  * MyModule
  */
MyModule::MyModule()
    : Module ("mysqldb","database",true),
      m_initThread(0),
      m_init(true)
{
    Output("Loaded module MySQL based on %s",mysql_get_client_info());
}

MyModule::~MyModule()
{
    Output("Unloading module MySQL");
    s_conns.clear();
    s_failedConns = 0;
    // Wait for expire thread termination
    while (m_initThread)
	Thread::idle();
}

void MyModule::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=Total|Failed|Errors|AvgExecTime|QueueTime|ExecTime",",");
}

void MyModule::statusParams(String& str)
{
    Lock lck(s_acctMutex);
    str.append("conns=",",") << s_conns.count();
    str.append("failed=",",") << s_failedConns;
}

void MyModule::statusDetail(String& str)
{
    QueryStats st;
    Lock lck(s_acctMutex);
    for (ObjList* o = s_conns.skipNull(); o; o = o->skipNext()) {
	MyAcct* acc = static_cast<MyAcct*>(o->get());
	acc->stats(st);
	str.append(acc->c_str(),",") << "=" << st.m_total << "|" << st.m_failedNoConn
	    << "|" << st.m_failed << "|";
	if (st.m_total > st.m_failedNoConn)
	    str << (st.m_queryTime / (st.m_total - st.m_failedNoConn) / 1000); // miliseconds
        else
	    str << "0";
	str << "|" << (st.m_queueTime / 1000) << "|" << (st.m_queryTime / 1000);
    }
}

void MyModule::initialize()
{
    Output("Initializing module MySQL");
    Module::initialize();
    Configuration cfg(Engine::configFile("mysqldb"));
    NamedList* general = cfg.createSection(YSTRING("general"));
    if (m_init) {
	Engine::install(new MyHandler(cfg.getIntValue("general","priority",100)));
	installRelay(Halt);
    }
    m_init = false;
    s_queryRetry = general->getIntValue(YSTRING("query_retry"),1,1,10);
    s_failedConns = 0;
    for (unsigned int i = 0; i < cfg.sections(); i++) {
	NamedList* sec = cfg.getSection(i);
	if (!sec || (*sec == "general"))
	    continue;
	Lock lck(s_acctMutex);
	RefPointer<MyAcct> db = findDb(*sec);
	if (db) {
	    lck.drop();
	    db->initialize(*sec);
	    continue;
	}
	MyAcct* conn = new MyAcct(sec);
	s_conns.insert(conn);
	lck.drop();
	if (!conn->initDb()) {
	    lck.acquire(s_acctMutex);
	    s_conns.remove(conn);
	    s_failedConns++;
	}
    }
}

bool MyModule::received(Message& msg, int id)
{
    if (id == Halt) {
	if (m_initThread)
	    m_initThread->cancel(true);
    }
    return Module::received(msg,id);
}

void MyModule::genUpdate(Message& msg)
{
    QueryStats st;
    unsigned int index = 0;
    Lock lock(s_acctMutex);
    for (ObjList* o = s_conns.skipNull(); o; o = o->skipNext()) {
	String idx(index++);
	MyAcct* acc = static_cast<MyAcct*>(o->get());
	acc->stats(st);
	msg.setParam("database." + idx,acc->toString());
	msg.setParam("total." + idx,st.m_total);
	msg.setParam("failed." + idx,st.m_failedNoConn);
	msg.setParam("errorred." + idx,st.m_failed);
	msg.setParam("hasconn." + idx,acc->hasConn());
	msg.setParam("querytime." + idx,st.m_queryTime);
	msg.setParam("queryqueue." + idx,st.m_queueTime);
    }
    msg.setParam(YSTRING("count"),index);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
