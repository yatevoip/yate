/**
 * media.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * SDP media handling
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

#include <yatesdp.h>

namespace TelEngine {

const TokenDict SDPMedia::s_sdpDir[] = {
    {"sendrecv", DirBidir},
    {"sendonly", DirSend},
    {"recvonly", DirRecv},
    {"inactive", DirInactive},
    {0, 0},
};

/*
 * SDPMedia
 */
SDPMedia::SDPMedia(const char* media, const char* transport, const char* formats,
    int rport, int lport)
    : NamedList(media),
    m_audio(true), m_video(false), m_modified(false), m_securable(true), m_haveRfc3833(false),
    m_localChanged(false),
    m_transport(transport), m_formats(formats),
    m_lDir(0), m_rDir(0),
    m_enabler(0), m_ptr(0)
{
    DDebug(DebugAll,"SDPMedia::SDPMedia('%s','%s','%s',%d,%d) [%p]",
	media,transport,formats,rport,lport,this);
    if (String::operator!=(YSTRING("audio"))) {
	m_audio = false;
	m_video = String::operator==(YSTRING("video"));
	m_suffix << "_" << media;
    }
    int q = m_formats.find(',');
    m_format = m_formats.substr(0,q);
    if (rport >= 0)
	m_rPort = rport;
    if (lport >= 0)
	m_lPort = lport;
}

SDPMedia::~SDPMedia()
{
    DDebug(DebugAll,"SDPMedia::~SDPMedia() '%s' [%p]",c_str(),this);
}

const char* SDPMedia::fmtList() const
{
    if (m_formats)
	return m_formats.c_str();
    if (m_format)
	return m_format.c_str();
    // unspecified audio assumed to support G711
    if (m_audio)
	return "alaw,mulaw";
    return 0;
}

// Compare this media with another one
bool SDPMedia::sameAs(const SDPMedia* other, bool ignorePort, bool checkStarted) const
{
    if (!other)
	return false;
    const SDPMedia& m = *other;
    if (m.transport() != m_transport)
	return false;
    if (m.remotePort() != m_rPort)
	return false;
    checkStarted = checkStarted && isStarted();
    if (!checkStarted) {
	return (m.formats() == m_formats) &&
	    ((ignorePort && m.remotePort() && m_rPort) || (m.remotePort() == m_rPort));
    }

    // Check format
    ObjList* lst = m.formats().split(',',false);
    bool found = (0 != lst->find(m_format));
    TelEngine::destruct(lst);
    if (!found) {
	XDebug(DebugAll,"SDPMedia::sameAs(%p) format='%s' other_formats='%s': not found [%p]",
	    other,m_format.c_str(),m.formats().c_str(),this);
	return false;
    }

    // Check payload format
    int pLoad = SDPMedia::payloadMapping(mappings(),m_format);
    int oPLoad = SDPMedia::payloadMapping(m.mappings(),m_format);
    if (pLoad == -1 || oPLoad == -1 || pLoad != oPLoad) {
	XDebug(DebugAll,
	    "SDPMedia::sameAs(%p) format='%s' pload=%d (%s) other_pload=%d (%s): not matched [%p]",
	    other,m_format.c_str(),pLoad,mappings().c_str(),oPLoad,m.mappings().c_str(),this);
	return false;
    }

    // Check RFC 2833
    if (m_rfc2833.payload(m_format) != m.m_rfc2833.payload(m_format)) {
	XDebug(DebugAll,
	    "SDPMedia::sameAs(%p) format='%s' rfc2833=%d other_rfc2833=%d: not matched [%p]",
	    other,m_format.c_str(),m_rfc2833.payload(m_format),m.m_rfc2833.payload(m_format),this);
	return false;
    }

    // TODO: Check crypto
    return true;
}

// Update members with data taken from a SDP, return true if something changed
bool SDPMedia::update(const char* formats, int rport, int lport, bool force)
{
    DDebug(DebugAll,"SDPMedia::update('%s',%d,%d,%s) [%p]",
	formats,rport,lport,String::boolText(force),this);
    bool chg = false;
    String tmp(formats);
    if (tmp && (m_formats != tmp)) {
	if (tmp.find(',') < 0) {
	    // single format received, check if acceptable
	    if (m_formats && !force && m_formats.find(tmp) < 0) {
		TraceDebug(m_traceId,m_enabler,DebugInfo,"Not changing to '%s' from '%s' [%p]",
		    formats,m_formats.c_str(),m_ptr);
		tmp.clear();
	    }
	}
	else if (m_formats && !force) {
	    // from received list keep only already offered formats
	    ObjList* l1 = tmp.split(',',false);
	    ObjList* l2 = m_formats.split(',',false);
	    for (ObjList* fmt = l1->skipNull(); fmt; ) {
		if (l2->find(fmt->get()->toString()))
		    fmt = fmt->skipNext();
		else {
		    fmt->remove();
		    fmt = fmt->skipNull();
		}
	    }
	    tmp.clear();
	    tmp.append(l1,",");
	    TelEngine::destruct(l1);
	    TelEngine::destruct(l2);
	    if (tmp.null())
		TraceDebug(m_traceId,m_enabler,DebugInfo,"Not changing formats '%s' [%p]",
		    m_formats.c_str(),m_ptr);
	}
	if (tmp && (m_formats != tmp)) {
	    chg = true;
	    m_formats = tmp;
	    int q = m_formats.find(',');
	    m_format = m_formats.substr(0,q);
	    TraceDebug(m_traceId,m_enabler,DebugAll,"Choosing offered '%s' format '%s' [%p]",
		c_str(),m_format.c_str(),m_ptr);
	}
    }
    if (rport >= 0) {
	tmp = rport;
	if (m_rPort != tmp) {
	    chg = true;
	    m_rPort = tmp;
	}
    }
    if (lport >= 0) {
	tmp = lport;
	if (m_lPort != tmp) {
	    m_localChanged = true;
	    chg = true;
	    m_lPort = tmp;
	}
    }
    return chg;
}

// Update members from a dispatched "chan.rtp" message
void SDPMedia::update(const NamedList& msg, bool pickFormat)
{
    DDebug(DebugAll,"SDPMedia::update('%s',%s) [%p]",
	msg.c_str(),String::boolText(pickFormat),this);
    m_id = msg.getValue("rtpid",m_id);
    m_lPort = msg.getValue("localport",m_lPort);
    if (pickFormat) {
	const char* format = msg.getValue("format");
	if (format) {
	    m_format = format;
	    if ((m_formats != m_format) && (msg.getIntValue("remoteport") > 0)) {
		TraceDebug(m_traceId,m_enabler,DebugAll,"Choosing started '%s' format '%s' [%p]",
		    c_str(),format,m_ptr);
		m_formats = m_format;
	    }
	}
    }
}

// Add or replace a parameter by name and value, set the modified flag
void SDPMedia::parameter(const char* name, const char* value, bool append)
{
    if (!name)
	return;
    m_modified = true;
    if (append)
	addParam(name,value);
    else
	setParam(name,value);
}

// Add or replace a parameter, set the modified flag
void SDPMedia::parameter(NamedString* param, bool append)
{
    if (!param)
	return;
    m_modified = true;
    if (append)
	addParam(param);
    else
	setParam(param);
}

void SDPMedia::crypto(const char* desc, bool remote)
{
    String& sdes = remote ? m_rCrypto : m_lCrypto;
    if (sdes != desc) {
	sdes = desc;
	m_modified = true;
    }
    if (remote && !desc)
	m_securable = false;
}

void SDPMedia::direction(int value, bool remote)
{
    int& v = remote ? m_rDir : m_lDir;
    if (v == value)
	return;
    DDebug(DebugAll,"SDPMedia set %s direction %s -> %s [%p]",remote ? "remote" : "local",
	lookup(v,s_sdpDir),lookup(value,s_sdpDir),this);
    v = value;
}

// Retrieve negotiated media direction to be sent to remote or set in RTP
int SDPMedia::direction(int sessLDir)
{
    int dir = m_lDir ? m_lDir : sessLDir;
    if (!m_rDir || dir)
	return dir;
    switch (m_rDir) {
	case DirBidir:
	case DirInactive:
	    return m_rDir;
	case DirSend:
	    return DirRecv;
	case DirRecv:
	    return DirSend;
    }
    return 0;
}

// Put the list of net media in a parameter list
void SDPMedia::putMedia(NamedList& msg, bool putPort)
{
    msg.addParam("media" + suffix(),"yes");
    msg.addParam("formats" + suffix(),formats());
    msg.addParam("transport" + suffix(),transport());
    if (mappings())
	msg.addParam("rtp_mapping" + suffix(),mappings());
    if (isAudio())
	m_rfc2833.put(msg);
    if (putPort)
	msg.addParam("rtp_port" + suffix(),remotePort());
    if (remoteCrypto())
	msg.addParam("crypto" + suffix(),remoteCrypto());
    // must handle encryption differently
    const char* enc = getValue("encryption");
    if (enc)
	msg.addParam("encryption" + suffix(),enc);
    clearParam("encryption");
    unsigned int n = length();
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* param = getParam(i);
	if (param)
	    msg.addParam("sdp" + suffix() + "_" + param->name(),*param);
    }
}

// Copy RTP related data from old media
void SDPMedia::keepRtp(const SDPMedia& other)
{
    m_haveRfc3833 = other.m_haveRfc3833;
    m_formats = other.m_formats;
    m_format = other.m_format;
    m_rfc2833 = other.m_rfc2833;
    m_id = other.m_id;
    m_rPort = other.m_rPort;
    m_lPort = other.m_lPort;
    crypto(other.m_rCrypto,true);
    crypto(other.m_lCrypto,false);
}

int SDPMedia::payloadMapping(const String& mappings, const String& fmt)
{
    if (!(mappings && fmt))
	return -2;
    String tmp = fmt;
    tmp << "=";
    ObjList* lst = mappings.split(',',false);
    int payload = -2;
    for (ObjList* pl = lst; pl; pl = pl->next()) {
	String* mapping = static_cast<String*>(pl->get());
	if (!mapping)
	    continue;
	if (mapping->startsWith(tmp)) {
	    payload = mapping->substr(tmp.length()).toInteger(-1);
	    break;
	}
    }
    TelEngine::destruct(lst);
    return payload;
}

// Set data used in debug
void SDPMedia::setSdpDebug(DebugEnabler* enabler, void* ptr, const String* traceId)
{
    m_enabler = enabler;
    m_ptr = (m_enabler && ptr) ? ptr : (void*)this;
    if (traceId)
	m_traceId = *traceId;
}

};   // namespace TelEngine

/* vi: set ts=8 sw=4 sts=4 noet: */
