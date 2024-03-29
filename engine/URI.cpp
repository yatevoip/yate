/**
 * URI.cpp
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

#include "yateclass.h"

using namespace TelEngine;

static const String s_jabber("jabber");
static const String s_xmpp("xmpp");
static const String s_tel("tel");
static int s_rfcTelParser = 1;

URI::URI()
    : m_parsed(false)
{
}

URI::URI(const URI& uri)
    : String(uri), m_parsed(false)
{
    m_desc = uri.getDescription();
    m_proto = uri.getProtocol();
    m_user = uri.getUser();
    m_host = uri.getHost();
    m_port = uri.getPort();
    m_extra = uri.getExtra();
    m_parsed = true;
}

URI::URI(const String& uri)
    : String(uri), m_parsed(false)
{
}

URI::URI(const char* uri)
    : String(uri), m_parsed(false)
{
}

URI::URI(const char* proto, const char* user, const char* host, int port, const char* desc)
    : m_desc(desc), m_proto(proto), m_user(user), m_host(host), m_port(port)
{
    if (desc) {
	if (m_desc)
	    *this << "\"" << m_desc << "\" ";
	*this << "<";
    }
    *this << m_proto << ":";
    if (user)
	*this << m_user;
    if (m_host) {
	if (user)
	    *this << "@";
	if (m_host.find(':') >= 0)
	    *this << "[" << m_host << "]";
	else
	    *this << m_host;
	if (m_port > 0)
	    *this << ":" << m_port;
    }
    if (desc)
	*this << ">";
    m_parsed = true;
}

void URI::changed()
{
    m_parsed = false;
    String::changed();
}

void URI::parse() const
{
    if (m_parsed)
	return;
    DDebug("URI",DebugAll,"parsing '%s' [%p]",c_str(),this);
    clearData();

    // the compiler generates wrong code so use the temporary
    String tmp(*this);
    bool hasDesc = false;
    static const Regexp r1("^[[:space:]]*\"\\([^\"]\\+\\)\"[[:space:]]*\\(.*\\)$");
    if (tmp.matches(r1))
	hasDesc = true;
    else {
	static const Regexp r2("^[[:space:]]*\\([^<]*[^<[:space:]]\\)[[:space:]]*<\\([^>]\\+\\)");
	hasDesc = tmp.matches(r2);
    }
    if (hasDesc) {
	m_desc = tmp.matchString(1);
	tmp = tmp.matchString(2);
	*const_cast<URI*>(this) = tmp;
	DDebug("URI",DebugAll,"new value='%s' [%p]",c_str(),this);
    }

    static const Regexp r3("<\\([^>]\\+\\)>");
    if (tmp.matches(r3)) {
	tmp = tmp.matchString(1);
	*const_cast<URI*>(this) = tmp;
	DDebug("URI",DebugAll,"new value='%s' [%p]",c_str(),this);
    }

    if (s_rfcTelParser) {
	// RFC 3966 The tel URI for Telephone Numbers
	static const Regexp s_rex_tel_global("^([0-9.()-]+)$",true);
	static const Regexp s_rex_tel_local("^([[:xdigit:]*#.()-]+)$",true);
	if (tmp.startSkip("tel:",false,true)) {
	    int idx = tmp.find(';');
	    if (idx > 0)
		m_user.assign(tmp,idx);
	    else
		m_user = tmp;
	    bool ok = false;
	    if (m_user) {
		int errptr = -1;
		m_user.uriUnescapeStr(false,&errptr);
		ok = (errptr < 0);
		XDebug("URI",DebugAll,"tel parsing user '%s' errptr=%d [%p]",m_user.c_str(),errptr,this);
		if (ok) {
		    if ('+' != m_user[0])
			ok = s_rex_tel_local.matches(m_user);
		    else if (m_user.length() > 1) {
			ok = s_rex_tel_global.matches(m_user.c_str() + 1);
			if (!ok && s_rfcTelParser > 1) {
			    ok = s_rex_tel_local.matches(m_user.c_str() + 1);
			    if (ok)
				m_user = m_user.substr(1);
			}
		    }
		    if (ok)
			m_user.removeChars(".()-",4);
		}
	    }
	    if (ok && m_user && ('+' != m_user[0] || m_user.length() > 1)) {
		m_proto = "tel";
		if (idx > 0)
		    m_extra = tmp.substr(idx);
		else
		    m_extra.clear();
		DDebug("URI",DebugAll,"desc='%s' proto='tel' user='%s' extra='%s' [%p]",
		    m_desc.safe(),m_user.c_str(),m_extra.safe(),this);
	    }
	    else
		clearData();
	    m_parsed = true;
	    return;
	}
    }

    // Should be:
    // [proto:[//]][user[:passwd]@]hostname[:port][/path][?param=value[&param=value...]]
    // We parse:
    // [proto:][//][user@]hostname[:port][/path][;params][?params][&params]

    static const Regexp r4("^\\([[:alpha:]][[:alnum:]]\\+:\\)\\?/\\?/\\?\\([^[:space:][:cntrl:]@]\\+@\\)\\?\\([[:alnum:]._+-]\\+\\|[[][[:xdigit:].:]\\+[]]\\)\\(:[0-9]\\+\\)\\?");
    // hack: use while only so we could break out of it
    while (tmp.matches(r4)) {
	int errptr = -1;
	m_proto = tmp.matchString(1).toLower();
	m_proto = m_proto.substr(0,m_proto.length()-1);
	m_user = tmp.matchString(2);
	m_user = m_user.substr(0,m_user.length()-1);
	if (m_proto && s_jabber != m_proto && s_xmpp != m_proto) {
	    m_user = m_user.uriUnescape(&errptr);
	    if (errptr >= 0)
		break;
	}
	m_host = tmp.matchString(3).uriUnescape(&errptr).toLower();
	if (errptr >= 0)
	    break;
	if (m_user.null() && (s_tel == m_proto)) {
	    m_user = m_host;
	    m_host.clear();
	}
	if (m_host[0] == '[')
	    m_host = m_host.substr(1,m_host.length()-2);
	String p = tmp.matchString(4);
	p >> ":" >> m_port;
	DDebug("URI",DebugAll,"desc='%s' proto='%s' user='%s' host='%s' port=%d [%p]",
	    m_desc.c_str(), m_proto.c_str(), m_user.c_str(), m_host.c_str(), m_port, this);
	int index = (tmp.matchLength(4) > 0) ? 4 : 3;
	m_extra = tmp.substr(tmp.matchOffset(index) + tmp.matchLength(index));
	m_parsed = true;
	return;
    }
    // parsing failed - clear all fields but still mark as parsed
    clearData();
    m_parsed = true;
}

void URI::clearData() const
{
    m_desc.clear();
    m_proto.clear();
    m_user.clear();
    m_host.clear();
    m_extra.clear();
    m_port = 0;
}

void URI::setup(const NamedList& params)
{
    const String& v = params["uri_parse_tel_rfc"];
    if (v == "local")
	s_rfcTelParser = 2;
    else
	s_rfcTelParser = v.toBoolean(true) ? 1 : 0;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
