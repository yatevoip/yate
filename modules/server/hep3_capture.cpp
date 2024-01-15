/**
 * hep3_capture.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * HEP3/EEP capture support module
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

#include <yatephone.h>
#include <string.h>

#define MAX_WRITE_TRIES  320

using namespace TelEngine;

namespace {

class Hep3Module;
class Hep3Msg;
class Hep3CaptAgent;
class Hep3CaptServer;


class Hep3Msg : public GenObject
{
public:
    enum Hep3ChunkTypes {
	CT_IP_PROTO_FAMILY = 0x0001, // uint8, IP protocol family
	CT_IP_PROTO_ID     = 0x0002, // uint8, IP protocol ID (UDP, TCP, ETC.)
	CT_IPV4_SRC_ADDR   = 0x0003, // inet4-addr, IPv4 source address
	CT_IPV4_DST_ADDR   = 0x0004, // inet4-addr, IPv4 destination address
	CT_IPV6_SRC_ADDR   = 0x0005, // inet6-addr, IPv6 source address
	CT_IPV6_DST_ADDR   = 0x0006, // inet6-addr, IPv6 destination address
	CT_SRC_PORT        = 0x0007, // uint16, protocol source port (UDP, TCP, SCTP)
	CT_DST_PORT        = 0x0008, // uint16, protocol destination port (UDP, TCP, SCTP)
	CT_TIMESTAMP_SEC   = 0x0009, // uint32, timestamp, seconds since 01/01/1970 (epoch)
	CT_TIMESTAMP_USEC  = 0x000A, // uint32, timestamp microseconds offset (added to timestamp)
	CT_PROTOCOL_TYPE   = 0x000B, // uint8, protocol type (SIP/H323/RTP/MGCP/M2UA)
	CT_CAPT_AGENT_ID   = 0x000C, // uint32, capture agent ID (202, 1201, 2033...)
	CT_KEEPALIVE_TIMER = 0x000D, // uint16, keep alive timer (sec)
	CT_AUTH_KEY        = 0x000E, // octet-string, authenticate key (plain text / TLS connection)
	CT_PAYLOAD         = 0x000F, // octet-string, captured packet payload
	CT_PAYLOAD_ZIP     = 0x0010, // octet-string captured compressed payload (gzip/inflate)
	CT_UUID            = 0x0011, // octet-stringInternal correlation id
	CT_VLAN_ID         = 0x0012, // uint16, Vlan ID
	CT_CAPT_AGENT_NAME = 0x0013, // octet-string, capture agent ID (“node1”, “node2”, “node3”...)
	CT_SRC_MAC         = 0x0014, // uint64, source MAC
	CT_DST_MAC         = 0x0015, // uint64, Destination MAC
	CT_ETH_TYPE        = 0x0016, // uint16 Ethernet Type
	CT_TCP_FLAG        = 0x0017, // uint8, TCP Flag [SYN.PUSH...]
	CT_IP_TOS          = 0x0018, // uint8, IP TOS
	// reserved values 0x19 - 0x1f
	CT_MOS             = 0x0020, // uint16, MOS value
	CT_RFACTOR         = 0x0021, // uint16, R-Factor
	CT_GEO_LOCATION    = 0x0022, // octet-string, GEO Location
	CT_JITTER          = 0x0023, // uint32, jitter
	CT_TRANSACT_TYPE   = 0x0024, // octet-string, Transaction type [call, registration]
	CT_PAYLOAD_JSON_KEYS = 0x0025, // octet-string, Payload JSON Keys
	CT_TAGS_VALUES       = 0x0026, // octet-string, Tags’ values
	CT_TAG_TYPE          = 0x0027, // uint16, Type of tag
	CT_EVENT_TYPE        = 0x0028, // uint16, Event type [recording|interception|
	CT_GROUP_ID          = 0x0029, // octet-stringGroup ID
    };

    enum Hep3ProtocolTypes {
	PROTO_RESERVED = 0x00, // reserved
	PROTO_SIP      = 0x01, // SIP
	PROTO_XMPP = 0x02, //XMPP
	PROTO_SDP = 0x03, // SDP
	PROTO_RTP = 0x04, // RTP
	PROTO_RTCP_JSON = 0x05, // RTCP JSON
	PROTO_MGCP = 0x06, // MGCP
	PROTO_MEGACO = 0x07, // MEGACO (H.248)
	PROTO_M2UA = 0x08, // M2UA (SS7/SIGTRAN)
	PROTO_M3UA = 0x09, // M3UA (SS7/SIGTRAN)
	PROTO_IAX = 0x0a, // IAX
	PROTO_H3222 = 0x0b, // H3222
	PROTO_H321 = 0x0c, // H321
	PROTO_M2PA = 0x0d, // M2PA
	PROTO_MOS_FULL = 0x22, // MOS full report [JSON]
	PROTO_MOS_SHORT = 0x23, // MOS short report. Please use mos chunk 0x20 [JSON]
	PROTO_SIP_JSON = 0x32, //SIP JSON
	// 0x33 RESERVED
	// 0x34 RESERVED
	PROTO_DNS_JSON = 0x35, // DNS JSON
	PROTO_M3UA_JSON = 0x36, // M3UA JSON (ISUP)
	PROTO_RTSP = 0x37, // RTSP (JSON)
	PROTO_DIAMETER_JSON = 0x38, // DIAMETER (JSON)
	PROTO_GSM_MAP_JSON = 0x39, // GSM MAP (JSON)
	PROTO_RTCP_PION = 0x3a, // RTCP PION
	// 0x3b RESERVED
	PROTO_CDR = 0x3c, // CDR (can be for call and registration transaction)
    };

    enum Hep3VendorId {
	Generic = 0x0000, // No specific vendor, generic chunk types, see above
	Freeswitch = 0x0001, // FreeSWITCH
	KamailioSER = 0x0002, // Kamailio/SER
	OpenSips = 0x0003, // OpenSIPS
	Asterisk = 0x0004, // Asterisk
	HomerProject = 0x0005, // Homer Project
	SipXesc = 0x0006, // SipXecs
	YetiSwitch = 0x0007, // Yeti Switch
	Genesis = 0x0008, // Genesys (https://www.genesys.com/)
    };

    enum Hep3EventType {
	// 0x000 reserved
	Recording   = 0x001, // Recording
	RecordingLI = 0x002, // Recording LI
    };

    Hep3Msg()
	: m_ts(0)
    { }

    inline bool build(Hep3CaptAgent* agent, const CaptureInfo& info, uint8_t* data, unsigned int len)
    {
	m_ts = info.ts();
	return buildMsg(m_data,agent,info,data,len);
    }

    inline uint64_t ts() const
	{ return m_ts; }
    inline const DataBlock& data() const
	{ return m_data; }

    static bool buildMsg(DataBlock& out, Hep3CaptAgent* agent, const CaptureInfo& info, uint8_t* data, unsigned int len);

private:
    DataBlock m_data;
    uint64_t m_ts;
};

class Hep3Thread : public Thread
{
public:
    Hep3Thread(Hep3CaptServer* srv);
    ~Hep3Thread();
    void run();
    void cleanup();

private:
    Hep3CaptServer* m_server;
};

class Hep3CaptServer : public RefObject
{
public:
    enum SocketTypes {
	SKT_UDP = 1,
	SKT_TCP,
	SKT_SCTP,
	SKT_TLS,
    };
    Hep3CaptServer(const char* name);
    ~Hep3CaptServer();
    bool initialize(const NamedList& params);
    bool connectSocket();
    void terminate();
    Hep3CaptAgent* createAgent(const NamedList& params);
    bool transmit();
    void setThread(Hep3Thread* thr);

    inline unsigned int authKeyLen() const
    {
	RLock l(m_lock);
	return m_authKey.length();
    }

    inline void copyAuthKey(uint8_t* dst)
    {
	if (!dst)
	    return;
	RLock l(m_lock);
	::memcpy(dst,m_authKey.data(),m_authKey.length());
    }

    inline uint32_t captureId() const
	{ return m_captureId; }

    inline bool payloadZipped() const
	{ return m_payloadZipped; }

    inline const String& toString() const
	{ return m_name; }

    inline const SocketAddr& localAddress() const
    {
	RLock l(m_lock);
	return m_localAddr;
    }

    inline const SocketAddr& remoteAddress() const
    {
	RLock l(m_lock);
	return m_remAddr;
    }

    inline uint64_t sentPkts() const
	{ return m_sentPkts; }
    inline uint64_t sendFailedPkts() const
	{ return m_pktsFailedSend; }
    inline uint64_t queueDroppedPkts() const
	{ return m_pktQueueDropped; }
    inline uint64_t oldDroppedPkts() const
	{ return m_pktsOldDropped; }
    inline unsigned int queueSize() const
	{ return m_msgQueueSize; }
    inline unsigned int enqueuedPkts() const
	{ return m_pktsEnqueued; }
    inline bool valid() const
	{ return m_socket.valid(); }

    bool enqueue(Hep3Msg* msg);
    Hep3Msg* dequeue();
    bool congested();

    static const TokenDict s_socketTypes[];

private:
    String m_name;
    Socket m_socket;
    SocketAddr m_localAddr;
    SocketAddr m_remAddr;
    int m_transport;
    mutable RWLock m_lock;
    DataBlock m_authKey;
    uint32_t m_captureId;
    bool m_payloadZipped;
    AtomicUInt64 m_sentPkts;
    AtomicUInt64 m_pktQueueDropped;
    AtomicUInt64 m_pktsOldDropped;
    AtomicUInt64 m_pktsFailedSend;
    AtomicUInt64 m_pktsEnqueued;
    mutable RWLock m_msgQueueLck;
    ObjList m_msgQueue;
    ObjList* m_msgQueueAppend;
    AtomicInt m_msgQueueSize;
    AtomicUInt m_msgQueueHighThreshold;
    AtomicUInt m_msgQueueLowThreshold;
    AtomicUInt m_congestNotif;
    Hep3Thread* m_thread;
    AtomicUInt m_maxAge;
    unsigned int m_maxWriteTries;
    unsigned int m_maxWriteTriesCfg;
    bool m_terminated;
};


class Hep3CaptAgent : public Capture
{
public:
    Hep3CaptAgent(const char* name, Hep3CaptServer* server)
    : Capture(name), m_server(server), m_addIpAddrs(false),
      m_hep3Proto(Hep3Msg::PROTO_RESERVED),
      m_ipFamily(AF_UNSPEC), m_ipProto(IPPROTO_IP), m_compressor(0)
    { }

    virtual ~Hep3CaptAgent()
    {
	WLock l(m_lock);
	m_server = 0;
	TelEngine::destruct(m_compressor);
    }

    bool initialize(const NamedList& params);
    bool write(const uint8_t* data, unsigned int len, const CaptureInfo& info);
    void* getObject(const String& name) const;

    inline bool payloadZipped() const
	{ return !!m_compressor; }

    inline unsigned int authKeyLen() const
    {
	RLock l(m_lock);
	return m_server ? m_server->authKeyLen() : 0;
    }

    inline void copyAuthKey(uint8_t* dst)
    {
	RLock l(m_lock);
	if (m_server)
	    m_server->copyAuthKey(dst);
    }
    inline bool ipAddrs() const
	{ return m_addIpAddrs; }

    inline unsigned int hep3Proto() const
	{ return m_hep3Proto; }

    inline unsigned int ipFamily() const
	{ return m_ipFamily;}

    inline unsigned int ipProto() const
	{ return m_ipProto;}

    inline uint32_t captureId() const
    {
	RLock l(m_lock);
	return m_server ? m_server->captureId() : 0;
    }

    inline bool compress(DataBlock& out, uint8_t* data, unsigned int len)
    {
	RLock l(m_lock);
	if (!m_compressor)
	    return false;
	return m_compressor->compress(data,len,out) > 0;
    }

    virtual const String& toString() const
	{ return name(); }

    virtual bool valid() const
    {
	RLock l(m_lock);
	return m_server && m_server->valid();
    }

private:
    mutable RWLock m_lock;
    RefPointer<Hep3CaptServer> m_server;
    bool m_addIpAddrs;
    SocketAddr m_localAddr;
    unsigned int m_hep3Proto;
    unsigned int m_ipFamily;
    unsigned int m_ipProto;
    Compressor* m_compressor;
};

class Hep3Module : public Module
{
public:
    enum Relay {
	Hep3Agent = Private,
    };
    Hep3Module();
    virtual ~Hep3Module();
    //inherited methods
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusModule(String& str);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
private:
    bool m_first;
    RWLock m_serversLck;
    ObjList m_servers;
};


INIT_PLUGIN(Hep3Module);

static const TokenDict s_payloadProtos[] = {
    {"reserved",      Hep3Msg::PROTO_RESERVED},
    {"unknown",       Hep3Msg::PROTO_RESERVED},
    {"SIP",           Hep3Msg::PROTO_SIP},
    {"sip",           Hep3Msg::PROTO_SIP},
    {"XMPP",          Hep3Msg::PROTO_XMPP},
    {"xmpp",          Hep3Msg::PROTO_XMPP},
    {"SDP",           Hep3Msg::PROTO_SDP},
    {"sdp",           Hep3Msg::PROTO_SDP},
    {"RTP",           Hep3Msg::PROTO_RTP},
    {"rtp",           Hep3Msg::PROTO_RTP},
    {"RTCP_JSON",     Hep3Msg::PROTO_RTCP_JSON},
    {"rtcp_json",     Hep3Msg::PROTO_RTCP_JSON},
    {"MGCP",          Hep3Msg::PROTO_MGCP},
    {"mgcp",          Hep3Msg::PROTO_MGCP},
    {"MEGACO",	      Hep3Msg::PROTO_MEGACO},
    {"megaco",        Hep3Msg:: PROTO_MEGACO},
    {"M2UA",	      Hep3Msg::PROTO_M2UA},
    {"m2ua",          Hep3Msg::PROTO_M2UA},
    {"M3UA",          Hep3Msg::PROTO_M3UA},
    {"m3ua",          Hep3Msg::PROTO_M3UA},
    {"IAX",           Hep3Msg::PROTO_IAX },
    {"iax",           Hep3Msg::PROTO_IAX },
    {"H3222",         Hep3Msg::PROTO_H3222},
    {"h3222",         Hep3Msg::PROTO_H3222},
    {"H321",          Hep3Msg::PROTO_H321},
    {"h321",          Hep3Msg::PROTO_H321},
    {"M2PA",          Hep3Msg::PROTO_M2PA},
    {"m2pa",          Hep3Msg::PROTO_M2PA},
    {"MOS_FULL",      Hep3Msg::PROTO_MOS_FULL},
    {"mos_full",      Hep3Msg::PROTO_MOS_FULL},
    {"MOS_SHORT",     Hep3Msg::PROTO_MOS_SHORT},
    {"mos_short",     Hep3Msg::PROTO_MOS_SHORT},
    {"SIP_JSON",      Hep3Msg::PROTO_SIP_JSON },
    {"sip_json",      Hep3Msg::PROTO_SIP_JSON },
    {"DNS_JSON",      Hep3Msg::PROTO_DNS_JSON},
    {"dns_json",      Hep3Msg::PROTO_DNS_JSON},
    {"M3UA_JSON",     Hep3Msg:: PROTO_M3UA_JSON},
    {"m3ua_json",     Hep3Msg::PROTO_M3UA_JSON},
    {"RTSP",          Hep3Msg::PROTO_RTSP},
    {"rtsp",          Hep3Msg:: PROTO_RTSP},
    {"DIAMETER_JSON", Hep3Msg::PROTO_DIAMETER_JSON},
    {"diameter_json", Hep3Msg::PROTO_DIAMETER_JSON},
    {"GSM_MAP_JSON",  Hep3Msg::PROTO_GSM_MAP_JSON},
    {"gsm_map_json",  Hep3Msg::PROTO_GSM_MAP_JSON},
    {"RTCP_PION",     Hep3Msg::PROTO_RTCP_PION},
    {"rtcp_pion",     Hep3Msg::PROTO_RTCP_PION},
    {"CDR",           Hep3Msg::PROTO_CDR},
    {"cdr",           Hep3Msg::PROTO_CDR},
    {0, 0 }
};

static const TokenDict s_ipTypes[] = {
    {"unspecified", AF_UNSPEC},
    {"local", AF_LOCAL},
    {"unix", AF_UNIX},
    {"file", AF_FILE},
    {"ipv4", AF_INET},
    {"IPv4", AF_INET},
    {"ipv6", AF_INET6},
    {"IPv6", AF_INET6},
    {0, 0 }
};

static  TokenDict s_ipProtos[] = {
    {"ICMP",    IPPROTO_ICMP},
    {"TCP",     IPPROTO_TCP},
    {"UDP",     IPPROTO_UDP},
    {"IPV6",    IPPROTO_IPV6},
    {"SCTP",    IPPROTO_SCTP},
    {"UDPLITE", IPPROTO_UDPLITE},
    {"RAW",     IPPROTO_RAW},
    {0, 0}
};

// A HEP chunk / IE header
struct hep3_chunk_hdr
{
    uint16_t vendor;
    uint16_t type;
    uint16_t len;

    hep3_chunk_hdr()
    {
	init();
    }

    void init()
    {
	vendor = 0;
	type = 0;
	len = 0;
    }
} __attribute__((packed));

// A HEP uint8 val
struct hep3_chunk_uint8
{
    hep3_chunk_hdr chunk;
    uint8_t val;

    hep3_chunk_uint8()
    {
	init();
    }

    void init()
    {
	chunk.len = htons(sizeof(hep3_chunk_hdr) + sizeof(uint8_t));
    }
} __attribute__((packed));

// A HEP uint16 val
struct hep3_chunk_uint16
{
    hep3_chunk_hdr chunk;
    uint16_t val;

    hep3_chunk_uint16()
    {
	init();
    }

    void init()
    {
	chunk.len = htons(sizeof(hep3_chunk_hdr) + sizeof(uint16_t));
    }
} __attribute__((packed));

// A HEP uint32 val
struct hep3_chunk_uint32
{
    hep3_chunk_hdr chunk;
    uint32_t val;

    hep3_chunk_uint32()
    {
	init();
    }

    void init()
    {
	chunk.len = htons(sizeof(hep3_chunk_hdr) + sizeof(uint32_t));
    }
} __attribute__((packed));

// A HEP uint64 val
struct hep3_chunk_uint64
{
    hep3_chunk_hdr chunk;
    uint64_t val;
    hep3_chunk_uint64()
    {
	init();
    }

    void init()
    {
	chunk.len = htons(sizeof(hep3_chunk_hdr) + sizeof(uint64_t));
    }
} __attribute__((packed));


//struct hep3_chunk_str
//{
//    hep3_chunk_hdr chunk;
//    char* val;
//} __attribute__((packed));

//struct hep3_chunk_binary
//{
//    hep3_chunk_hdr chunk;
//    uint8_t* data;
//} __attribute__((packed));

// A HEP IPv4 address val
struct hep3_chunk_ipv4
{
    hep3_chunk_hdr chunk;
    in_addr data;
    hep3_chunk_ipv4()
    {
	init();
    }

    void init()
    {
	chunk.len = htons(sizeof(hep3_chunk_hdr) + sizeof(in_addr));
    }
} __attribute__((packed));

// A HEP IPv6 address val
struct hep3_chunk_ipv6
{
    hep3_chunk_hdr chunk;
    in6_addr data;

    hep3_chunk_ipv6()
    {
	init();
    }

    void init()
    {
	chunk.len = htons(sizeof(hep3_chunk_hdr) + sizeof(in6_addr));
    }
} __attribute__((packed));

// common HEP3 header
struct hep3_hdr
{
    char id[4];
    u_int16_t length;

    hep3_hdr ()
    {
	init();
    }

    void init()
    {
	id[0] = 'H';
	id[1] = 'E';
	id[2] = 'P';
	id[3] = '3';
    }
} __attribute__((packed));

// common IEs
struct hep3_msg_common
{
    hep3_hdr          header;
    hep3_chunk_uint8  ip_family;
    hep3_chunk_uint8  ip_proto;
    hep3_chunk_uint16 src_port;
    hep3_chunk_uint16 dst_port;
    hep3_chunk_uint32 time_sec;
    hep3_chunk_uint32 time_usec;
    hep3_chunk_uint8  proto;
    hep3_chunk_uint32 capt_id;

    void init()
    {
	header.init();
	ip_family.init();
	ip_family.chunk.type = htons(Hep3Msg::CT_IP_PROTO_FAMILY);
	ip_proto.init();
	ip_proto.chunk.type  = htons(Hep3Msg::CT_IP_PROTO_ID);
	src_port.init();
	src_port.chunk.type  = htons(Hep3Msg::CT_SRC_PORT);
	dst_port.init();
	dst_port.chunk.type  = htons(Hep3Msg::CT_DST_PORT);
	time_sec.init();
	time_sec.chunk.type  = htons(Hep3Msg::CT_TIMESTAMP_SEC);
	time_usec.init();
	time_usec.chunk.type = htons(Hep3Msg::CT_TIMESTAMP_USEC);
	proto.init();
	proto.chunk.type     = htons(Hep3Msg::CT_PROTOCOL_TYPE);
	capt_id.init();
	capt_id.chunk.type   = htons(Hep3Msg::CT_CAPT_AGENT_ID);
    }
} __attribute__((packed));

// Source and destination IPv4 addresses
struct hep3_msg_ipv4_addrs
{
    hep3_chunk_ipv4 src_addr;
    hep3_chunk_ipv4 dst_addr;

    hep3_msg_ipv4_addrs()
    {
	init();
    }

    void init()
    {
	src_addr.init();
	src_addr.chunk.type = htons(Hep3Msg::CT_IPV4_SRC_ADDR);
	dst_addr.init();
	dst_addr.chunk.type = htons(Hep3Msg::CT_IPV4_DST_ADDR);
    }
} __attribute__((packed));

// Source and destination IPv6 addresses
struct hep3_msg_ipv6_addrs
{
    hep3_chunk_ipv6 src_addr;
    hep3_chunk_ipv6 dst_addr;

    hep3_msg_ipv6_addrs()
    {
	init();
    }
    void init()
    {
	src_addr.init();
	src_addr.chunk.type = htons(Hep3Msg::CT_IPV6_SRC_ADDR);
	dst_addr.init();
	dst_addr.chunk.type = htons(Hep3Msg::CT_IPV6_DST_ADDR);
    }
} __attribute__((packed));


const TokenDict Hep3CaptServer::s_socketTypes[] = {
    {"udp",  Hep3CaptServer::SKT_UDP},
    {"UDP",  Hep3CaptServer::SKT_UDP},
    {"tcp",  Hep3CaptServer::SKT_TCP},
    {"TCP",  Hep3CaptServer::SKT_TCP},
    {"sctp", Hep3CaptServer::SKT_SCTP},
    {"sctp", Hep3CaptServer::SKT_SCTP},
    {0,0}
};

Hep3CaptServer::Hep3CaptServer(const char* name)
    : m_name(name), m_transport(0), m_lock("Hep3CaptServer"), m_sentPkts(0),
      m_pktQueueDropped(0), m_pktsOldDropped(0), m_pktsFailedSend(0),
      m_pktsEnqueued(0),m_msgQueueLck("Hep3CaptServerQueue"), m_msgQueueAppend(&m_msgQueue),
      m_msgQueueSize(0), m_msgQueueHighThreshold(1000), m_msgQueueLowThreshold(250),
      m_congestNotif(0), m_thread(0), m_maxAge(0), m_maxWriteTries(MAX_WRITE_TRIES),
      m_maxWriteTriesCfg(MAX_WRITE_TRIES), m_terminated(false)
{
    DDebug(&__plugin,DebugAll,"Hep3CaptServer::Hep3CaptServer(%s) [%p]",name,this);
}

Hep3CaptServer::~Hep3CaptServer()
{
    terminate();
    DDebug(&__plugin,DebugAll,"Hep3CaptServer::~Hep3CaptServer() [%p]",this);
}

void Hep3CaptServer::terminate()
{
    WLock l(m_lock);
    m_socket.terminate();
    if (m_thread)
	m_thread->cancel();
    m_terminated = true;
    l.drop();
    while (m_thread)
	Thread::idle();
}

bool Hep3CaptServer::initialize(const NamedList& params)
{
#if DEBUG
    String str;
    params.dump(str,"\r\n");
    Debug(&__plugin,DebugAll,"Hep3CaptServer::initialize() params=%s [%p]",str.c_str(),this);
#endif
    WLock l(m_lock);
    m_authKey.clear();
    m_authKey.append(params["auth_key"]);
    if (!m_authKey.length())
	m_authKey.unHexify(params["auth_key_hex"]);
    m_captureId = htonl(params.getIntValue("capture_id"));
    m_payloadZipped = params.getBoolValue("compress",false);

    m_msgQueueHighThreshold = params.getIntValue("max_queue_size",m_msgQueueHighThreshold);
    m_msgQueueLowThreshold = m_msgQueueHighThreshold / 4;
    if (m_msgQueueHighThreshold <= 0) {
	m_msgQueueHighThreshold = 0;
	Debug(&__plugin,DebugInfo,"Congestion mechanism was deactivated [%p]", this);
    } else if (m_msgQueueHighThreshold < 2) {
	Debug(&__plugin,DebugInfo,"Overriding configured max_queue_size=%u, setting it to 2 [%p]",
		(unsigned int)m_msgQueueHighThreshold, this);
	m_msgQueueHighThreshold = 2;
	m_msgQueueLowThreshold = 1;
    }
    if (m_msgQueueHighThreshold && !m_msgQueueLowThreshold)
	// in case division by 4 returns 0
	m_msgQueueLowThreshold = 1;

    m_maxAge = params.getIntValue("max_msg_age",m_maxAge,0) * 1000;
    m_maxWriteTries = m_maxWriteTriesCfg = params.getIntValue("max_write_tries",m_maxWriteTriesCfg,1);

    if (m_thread)
	return true;
    m_thread = new Hep3Thread(this);
    if (!m_thread->startup()) {
	Debug(&__plugin,DebugWarn,"Failed to start processing thread for server='%s' [%p]",
		toString().c_str(),this);
	return false;
    }
    if (m_socket.valid())
	return true;
    unsigned int transport = params.getIntValue("socket_type",s_socketTypes,SKT_UDP);
    if (transport == SKT_SCTP || transport == SKT_TLS) {
	Debug(&__plugin,DebugStub,"Missing %s transport support for connection to %s [%p]",
		params.getValue("socket_type"),toString().c_str(),this);
	return false;
    }
    m_transport = transport;

    SocketAddr rAddr;
    rAddr.host(params.getValue("remote_host"));
    rAddr.port(params.getIntValue("remote_port"));
    if(!(rAddr.host() && rAddr.port() && rAddr.valid())) {
	Debug(&__plugin,DebugConf,"Failed to initialize: invalid remote address '%s:%u' for server '%s' [%p]",
		params.getValue("remote_host"),params.getIntValue("remote_port"),
		toString().c_str(),this);
	return false;
    }

    SocketAddr lAddr;
    const String& lHost = params["local_host"];
    if (lHost) {
	lAddr.host(lHost);
	lAddr.port(params.getIntValue("local_port"));
	if (!(lAddr.host() && lAddr.port() && lAddr.valid())) {
	    Debug(&__plugin,DebugConf,"Failed to initialize: invalid local address '%s:%u' for server '%s' [%p]",
		    lHost.c_str(),params.getIntValue("local_port"),toString().c_str(),this);
	    return false;
	}
    }

    if (lAddr.valid() && lAddr.family() != rAddr.family()) {
	Debug(&__plugin,DebugConf,"Failed to initialize: mismatched socket families for "
		"local (%s) and remote (%s) addresses for server '%s' [%p]",
		lAddr.familyName(),rAddr.familyName(),toString().c_str(),this);
	return false;
    }

    m_remAddr = rAddr;
    m_localAddr = lAddr;
    connectSocket();
    return true;
}

bool Hep3CaptServer::connectSocket()
{
    DDebug(&__plugin,DebugInfo,"Hep3CaptServer::connectSocket() '%s' [%p]",toString().c_str(),this);
    m_socket.terminate();

    if (!m_socket.create(m_remAddr.family(), m_transport == SKT_UDP ? SOCK_DGRAM : SOCK_STREAM)) {
	Debug(&__plugin,DebugWarn,"Failed to create socket for server %s, error=%s(%d) [%p]",
		toString().c_str(),::strerror(m_socket.error()),m_socket.error(),this);
	return false;
    }
    if (!m_socket.setBlocking(false)) {
	Debug(&__plugin,DebugWarn,"Could not set non-blocking mode on socket towards %s, error=%s(%d) [%p]",
		toString().c_str(),::strerror(m_socket.error()),m_socket.error(),this);
	return false;
    }
    if (!m_socket.setLinger(0))
	    Debug(&__plugin,DebugWarn,"Failed to set socket for server %s non-lingering : error=%s(%d) [%p]",
		toString().c_str(),::strerror(m_socket.error()),m_socket.error(),this);
    if (!m_socket.bind(m_localAddr)) {
	Debug(&__plugin,DebugConf,"Failed to bind on '%s' for server %s, error=%s(%d) [%p]",
		m_localAddr.addr().toString().c_str(),toString().c_str(),::strerror(m_socket.error()),
		m_socket.error(),this);
	return false;
    }
    bool tout = false;
    if (!m_socket.connectAsync(m_remAddr,5000000,&tout)) {
	if (tout)
	    Debug(&__plugin,DebugWarn,"Timeout connecting to %s - %s:%u [%p]",
		    toString().c_str(),m_remAddr.host().c_str(),m_remAddr.port(),this);
	else
	    Debug(&__plugin,DebugWarn,"Failed to connect to %s - %s:%u, error=%s(%d) [%p]",
		    toString().c_str(),m_remAddr.host().c_str(),m_remAddr.port(),
		    ::strerror(m_socket.error()),m_socket.error(),this);
	m_socket.terminate();
	return false;
    }
    Debug(&__plugin,DebugInfo,"Connected to %s - %s:%u [%p]",toString().c_str(),
		m_remAddr.host().c_str(),m_remAddr.port(),this);
    return true;
}

Hep3CaptAgent* Hep3CaptServer::createAgent(const NamedList& params)
{
#ifdef DEBUG
    String str;
    params.dump(str,"\r\n");
    Debug(&__plugin,DebugInfo,"Hep3CaptServer::createAgent() '%s'[%p] with parameters\r\n%s",
	    m_name.c_str(),this,str.c_str());
#endif
    const String& name = params[YSTRING("agent")];
    Hep3CaptAgent* agent = new Hep3CaptAgent(name,this);
    if (!agent->initialize(params)) {
	Debug(&__plugin,DebugWarn,"Failed to initialize capture agent '%s' [%p]",name.c_str(),this);
	TelEngine::destruct(agent);
	return 0;
    }
    return agent;
}

bool Hep3CaptServer::enqueue(Hep3Msg* msg)
{
    if (!msg)
	return false;
    WLock l(m_msgQueueLck);
    while (m_msgQueueHighThreshold && (unsigned int)m_msgQueueSize >= m_msgQueueHighThreshold) {
        if (m_msgQueue.next() == m_msgQueueAppend)
		m_msgQueueAppend = &m_msgQueue;
	m_msgQueue.remove();
	m_pktQueueDropped++;
	if (--m_msgQueueSize < 0) {
	    Debug(&__plugin,DebugWarn,"Reached negative count for TX message queue, resetting to queue count[%p]",this);
	    m_msgQueueSize = m_msgQueue.count();
	}
    }
    m_msgQueueAppend = m_msgQueueAppend->append(msg);
    m_msgQueueSize++;
    m_pktsEnqueued++;
    congested();
    return true;
}

Hep3Msg* Hep3CaptServer::dequeue()
{
    WLock l(m_msgQueueLck);
    if (m_msgQueue.next() == m_msgQueueAppend)
	m_msgQueueAppend = &m_msgQueue;
    Hep3Msg* msg = static_cast<Hep3Msg*> (m_msgQueue.remove(false));
    if (msg) {
	if (--m_msgQueueSize < 0) {
	    Debug(&__plugin,DebugWarn,"Reached negative count for TX message queue, resetting to queue count[%p]",this);
	    m_msgQueueSize = m_msgQueue.count();
	}
    } else
	m_msgQueueSize = 0;
    congested();
#ifdef XDEBUG
    if (msg)
	Debug(&__plugin,DebugAll,"Hep3CaptServer::dequeue(): Dequeued msg (%p) [%p]",msg,this);
#endif
    return msg;
}

bool Hep3CaptServer::congested()
{
    if (!(m_msgQueueHighThreshold && m_msgQueueSize))
	return false;

    if (m_congestNotif) {
	if ((unsigned int)m_msgQueueSize <= m_msgQueueLowThreshold) {
	    Alarm(&__plugin,"performance",DebugNote,"Hep3Server '%s': Exited congestion state, current queued messages=%d[%p]",
		    toString().c_str(),(int)m_msgQueueSize,this);
	    m_congestNotif = 0;
	}
    } else if ((unsigned int)m_msgQueueSize >= m_msgQueueHighThreshold) {
	Alarm(&__plugin,"performance",DebugWarn,"Hep3Server '%s': Congestion detected, queued messages=%d, threshold=%d [%p]",
		toString().c_str(),(int)m_msgQueueSize,(int)m_msgQueueHighThreshold,this);
	m_congestNotif = 1;
    }
    return m_congestNotif;
}

bool Hep3CaptServer::transmit()
{
    Hep3Msg* msg = dequeue();
    if (!msg)
	return false;
    if (m_maxAge && (Time::now() - msg->ts()) >= m_maxAge) {
	DDebug(&__plugin,DebugMild,"Hep3Server '%s':Dropping old packet of age %lu > %u microseconds [%p]",
		toString().c_str(),Time::now() - msg->ts(),(unsigned int)m_maxAge,this);
	TelEngine::destruct(msg);
	m_pktsOldDropped++;
	return true;
    }
    RLock l(m_lock);
    int ret = -1;
    if (!m_socket.valid() && !m_terminated) {
	if (!connectSocket()) {
	    TelEngine::destruct(msg);
	    return false;
	}
    }
    switch (m_transport) {
	case SKT_UDP:
	    // fire and forget, we do not care about outcome
	    ret = m_socket.writeData(msg->data().data(),msg->data().length());
	    break;
	case SKT_TCP:
	{
	    unsigned int tries = 0;
	    uint8_t* buf = (uint8_t*)msg->data().data();
	    unsigned int len = msg->data().length();
	    while (m_socket.valid() && len > 0) {
		if (Thread::check(false))
		    break;
		if (++tries >= m_maxWriteTries) {
		    DDebug(&__plugin,DebugMild,"Hep3Server '%s': Abandoning transmit of packet of"
			    " length=%u, number of tries=%u > max_tries(%u) [%p]",
			    toString().c_str(),msg->data().length(),tries,m_maxWriteTries,this);
		    break;
		}
		bool writeOk = false, error = false;
		if (!m_socket.select(0,&writeOk,&error,Thread::idleUsec())) {
		    if (!m_socket.canRetry()) {
			Debug(&__plugin,DebugMild,"Hep3Server '%s': socket select failure, error=%s(%d) [%p]",
			    toString().c_str(),::strerror(m_socket.error()),m_socket.error(),this);
			m_socket.terminate();
			break;
		    }
		    Thread::idle();
		    continue;
		}
		if (!writeOk) {
		    if (error) {
			// we have an error, attempt to detect it
			uint8_t b;
			int r = m_socket.readData(&b,1);
			if (r < 0) {
			    if (!m_socket.canRetry()) {
				Debug(&__plugin,DebugMild,"Hep3Server '%s': socket write failure, error=%s(%d) [%p]",
				    toString().c_str(),::strerror(m_socket.error()),m_socket.error(),this);
				m_socket.terminate();
				break;
			    }
			}
			Thread::idle();
		    }
		    continue;
		}
		int w = m_socket.writeData(buf,len);
		if (w < 0) {
		    if (!m_socket.canRetry()) {
			Debug(&__plugin,DebugMild,"Hep3Server '%s': socket write failure, error=%s(%d) [%p]",
			    toString().c_str(),::strerror(m_socket.error()),m_socket.error(),this);
			m_socket.terminate();
			break;
		    }
		    Thread::idle();
		}
		else {
		    buf += w;
		    len -= w;
		}
	    }
	    if (tries > 1) {
		if (m_maxWriteTries >= m_maxWriteTriesCfg)
		    Alarm(&__plugin,"performance",DebugMild,"Hep3Server '%s': TCP connection entered congested state [%p]",
			    toString().c_str(),this);
		if (m_maxWriteTries > 2)
		    m_maxWriteTries /= 2;
	    }
	    else {
		if (m_maxWriteTries < m_maxWriteTriesCfg) {
		    m_maxWriteTries *= 2;
		    if (m_maxWriteTries >= m_maxWriteTriesCfg) {
			m_maxWriteTries = m_maxWriteTriesCfg;
			Alarm(&__plugin,"performance",DebugNote,"Hep3Server '%s': TCP connection exited congested state [%p]",
				toString().c_str(),this);
		    }
		}
	    }
	    ret = msg->data().length() - len;
	    break;
	}
	default:
	    ret = -1;
	    break;
    }
    if (ret < 0)
	m_pktsFailedSend++;
    else
	m_sentPkts++;
    TelEngine::destruct(msg);
    return true;
}

void Hep3CaptServer::setThread(Hep3Thread* thr)
{
    WLock l(m_lock);
    if (m_thread && thr && m_thread != thr) {
	Debug(&__plugin,DebugWarn,"Cannot replace running thread '%p' with new one '%p' [%p]",m_thread,thr,this);
	return;
    }
    m_thread = thr;
}

Hep3Thread::Hep3Thread(Hep3CaptServer* srv)
    : m_server(srv)
{
    DDebug(&__plugin,DebugAll,"Hep3Thread::Hep3Thread(srv=%p) [%p]",srv,this);
}

Hep3Thread::~Hep3Thread()
{
    DDebug(&__plugin,DebugAll,"Hep3Thread::~Hep3Thread3() [%p]",this);
}

void Hep3Thread::run()
{
    while (true) {
	if (Thread::check(false))
	    break;
	if (!m_server->transmit())
	    Thread::idle();
    }
}

void Hep3Thread::cleanup()
{
    DDebug(&__plugin,DebugAll,"Hep3Thread::cleanup() [%p]",this);
    m_server->setThread(0);
}


bool Hep3Msg::buildMsg(DataBlock& out, Hep3CaptAgent* agent, const CaptureInfo& info, uint8_t* data, unsigned int len)
{
    if (!(agent && data && len))
	return false;

    // compute necessary headers length
    unsigned int hdrLen = sizeof(struct hep3_msg_common);
    if (agent->ipAddrs() && info.srcAddr())
	hdrLen += info.srcAddr()->family() == AF_INET ? sizeof(struct hep3_msg_ipv4_addrs) : sizeof(struct hep3_msg_ipv6_addrs);
    if (agent->authKeyLen()) {
	hdrLen += sizeof(struct hep3_chunk_hdr);
	hdrLen += agent->authKeyLen();
    }
    hdrLen += sizeof(struct hep3_chunk_hdr);
    // TODO see about extra parameters

    // pre-allocate space for necessary headers
    out.resize(hdrLen);
    uint8_t* buf = (uint8_t*) out.data();

    // set common parameters
    struct hep3_msg_common* common = (struct hep3_msg_common*) buf;
    common->init();
    common->ip_family.val = info.srcAddr() ? info.srcAddr()->family() : 0;

    // common parameters
    common->ip_proto.val = agent->ipProto();
    common->src_port.val = htons(info.srcPort());
    common->dst_port.val = htons(info.dstPort());
    common->time_sec.val = htonl(info.ts() / 1000000);
    common->time_usec.val = htonl(info.ts() % 1000000);
    common->proto.val = agent->hep3Proto();
    common->capt_id.val = agent->captureId();

    // advance in pre-allocated buffer
    buf += sizeof(struct hep3_msg_common);
    if (agent->ipAddrs() && info.srcAddr()) {
	if (AF_INET ==  info.srcAddr()->family()) {
	    struct hep3_msg_ipv4_addrs* addrs = (struct hep3_msg_ipv4_addrs*) buf;
	    addrs->init();
	    SocketAddr::copyAddr((uint8_t*)&addrs->src_addr.data,info.srcAddr()->address());
	    SocketAddr::copyAddr((uint8_t*)&addrs->dst_addr.data,info.dstAddr()->address());
	    buf += sizeof(struct hep3_msg_ipv4_addrs);
	}
	else {
	    struct hep3_msg_ipv6_addrs* addrs = (struct hep3_msg_ipv6_addrs*) buf;
	    addrs->init();
	    SocketAddr::copyAddr((uint8_t*)&addrs->src_addr.data,info.srcAddr()->address());
	    SocketAddr::copyAddr((uint8_t*)&addrs->dst_addr.data,info.dstAddr()->address());
	    buf += sizeof(struct hep3_msg_ipv6_addrs);
	}
    }

    if (agent->authKeyLen()) {
	struct hep3_chunk_hdr* auth = (struct hep3_chunk_hdr*) buf;
	auth->vendor = 0x0000;
	auth->type = htons(CT_AUTH_KEY);
	auth->len = htons(sizeof(struct hep3_chunk_hdr) + agent->authKeyLen());
	uint8_t* data = (uint8_t*)(buf + sizeof(struct hep3_chunk_hdr));
	agent->copyAuthKey(data);
	buf += sizeof(struct hep3_chunk_hdr) + agent->authKeyLen();
    }

    struct hep3_chunk_hdr* payloadHdr = (struct hep3_chunk_hdr*) buf;
    payloadHdr->init();
    payloadHdr->type = htons(agent->payloadZipped() ? Hep3Msg::CT_PAYLOAD_ZIP : Hep3Msg::CT_PAYLOAD);
    if (agent->payloadZipped()) {
	DataBlock zipped;
	agent->compress(zipped,data,len);
	if (!zipped.length())
	    return false;
	payloadHdr->len = htons(sizeof(struct hep3_chunk_hdr) + zipped.length());
	out.append(zipped);
    }
    else {
	payloadHdr->len = htons(sizeof(struct hep3_chunk_hdr) + len);
	out.append(data,len);
    }
    // set total length
    ((uint16_t*)out.data())[2] = htons(out.length());
    return true;
}

bool Hep3CaptAgent::initialize(const NamedList& params)
{
    WLock l(m_lock);
    if (!m_server)
	return false;
    m_addIpAddrs = params.getBoolValue("add_ip_addrs",true);
    if (m_addIpAddrs) {
	m_localAddr.host(params.getValue("src_addr"));
	m_localAddr.port(params.getIntValue("src_port"));
    }
    m_hep3Proto = params.getIntValue("payload_proto",s_payloadProtos,m_hep3Proto);
    m_ipFamily = params.getIntValue("ip_type",s_ipTypes,m_ipFamily);
    m_ipProto = params.getIntValue("ip_proto",s_ipProtos,m_ipProto);
    bool needZip = params.getBoolValue("compress",m_server->payloadZipped());
    if (needZip) {
	Message msg("engine.compress");
	msg.userData(this);
	msg.addParam("format","zlib",false);
	msg.addParam("name",toString());
	msg.addParam("data_type",params.getValue("compress_data_type","binary"));
	Engine::dispatch(msg);
	if (!m_compressor) {
	    Debug(&__plugin,DebugWarn,"Failed to obtain compressor for capture agent '%s' [%p]",
		    toString().c_str(),this);
	    return false;
	}
    }
    return true;
}

bool Hep3CaptAgent::write(const uint8_t* data, unsigned int len, const CaptureInfo& info)
{
    Hep3Msg* msg = new Hep3Msg();
    if (!msg->build(this,info,const_cast<uint8_t*>(data),len)) {
	TelEngine::destruct(msg);
	return false;
    }
    RLock l(m_lock);
    return m_server && m_server->enqueue(msg);
}

void* Hep3CaptAgent::getObject(const String& name) const
{
    if (name == "Compressor*")
	return (void*)&m_compressor;
    if (name == "Hep3CaptAgent")
	return (void*)this;
    if (name == "Capture")
	return (void*)this;
    return RefObject::getObject(name);
}

Hep3Module::Hep3Module()
    : Module("hep3_capture","misc",true),
      m_first(true), m_serversLck("Hep3Module")
{
    Output("Loaded module HEP3/EEP capture");
}

Hep3Module::~Hep3Module()
{
    Output("Unloaded module HEP3/EEP capture");
    m_servers.clear();
}

void Hep3Module::initialize()
{
    Output("Initializing module HEP3/EEP capture");

    // read configuration
    Configuration cfg(Engine::configFile("hep3_capture"));
    cfg.load();

    if (m_first) {
	m_first = false;
	Module::initialize();
	installRelay(Hep3Agent,"hep3.capture");
    }
    // setup servers
    WLock l(m_serversLck);
    for (unsigned int i = 0; i < cfg.sections(); i++) {
	NamedList* sect = cfg.getSection(i);
	if (!sect)
	    continue;
	String name(*sect);
	if (!(name.startSkip("server") && name))
	    continue;

	ObjList* o = m_servers.find(name);
	Hep3CaptServer* s = (o ? static_cast<Hep3CaptServer*>(o->get()) : 0);
	if (!sect->getBoolValue("enable",true)) {
	    if (s) {
		s->terminate();
		m_servers.remove(s);
	    }
	    continue;
	}
	if (!s) {
	    s = new Hep3CaptServer(name);
	    m_servers.append(s);
	}
	if (!s->initialize(*sect)) {
	    Debug(&__plugin,DebugInfo,"Failed to initialize server '%s' [%p]",name.c_str(),this);
	    m_servers.remove(name);
	}
    }
}

bool Hep3Module::received(Message& msg, int id)
{
    if (Hep3Agent == id) {
	RLock l(m_serversLck);
	RefPointer<Hep3CaptServer> server = static_cast<Hep3CaptServer*>(m_servers[msg[YSTRING("server")]]);
	l.drop();
	if (!server)
	    return false;
	Capture* a = server->createAgent(msg);
	if (!a)
	    return false;
	Capture** pAgent = static_cast<Capture**>(msg.userObject(YATOM("Capture*")));
	*pAgent = a;
	return true;
    }
    return Module::received(msg,id);
}

void Hep3Module::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=RemAddres|LocalAddress|QueueSize|TotalPkts|SentPkts|SendFailed|QueueDropped|OldDropped|Congested",",");

}

void Hep3Module::statusParams(String& str)
{
    RLock l(m_serversLck);
    str.append("count=",",") << m_servers.count();
}

void Hep3Module::statusDetail(String& str)
{
    RLock l(m_serversLck);
    for (ObjList* o = m_servers.skipNull(); o; o = o->skipNext()) {
	Hep3CaptServer* srv = static_cast<Hep3CaptServer*>(o->get());
	str.append(srv->toString(),",") << "=" << srv->remoteAddress().host()
		<< ":" << srv->remoteAddress().port()<< "|" << srv->localAddress().host()
		<< ":" << srv->localAddress().port() << "|" << srv->queueSize()
		<< "|" << srv->enqueuedPkts() << "|" << srv->sentPkts()
		<< "|" << srv->sendFailedPkts() << "|" << srv->queueDroppedPkts()
		<< "|" << srv->oldDroppedPkts() <<  "|" << srv->congested();
    }
}

};

/* vi: set ts=8 sw=4 sts=4 noet: */
