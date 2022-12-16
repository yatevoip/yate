/**
 * DataBlock.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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

#include <string.h>
#include <stdlib.h>

using namespace TelEngine;

namespace { // anonymous

extern "C" {
#include "a2s.h"
#include "a2u.h"
#include "u2a.h"
#include "u2s.h"

static unsigned char s2a[65536];
static unsigned char s2u[65536];
}

class InitG711
{
public:
    InitG711()
    {
	int i;
	unsigned char val;
	// positive side of mu-Law
	for (i = 0, val = 0xff; i <= 32767; i++) {
	    if ((val > 0x80) && ((i - 4) >= (int)(unsigned int)u2s[val]))
		val--;
	    s2u[i] = val;
	}
	// negative side of mu-Law
	for (i = 32768, val = 0; i <= 65535; i++) {
	    if ((val < 0x7e) && ((i - 12) >= (int)(unsigned int)u2s[val]))
		val++;
	    s2u[i] = val;
	}
	unsigned char v;
	// positive side of A-Law
	for (i = 0, v = 0, val = 0xd5; i <= 32767; i++) {
	    if ((v < 0x7f) && ((i - 8) >= (int)(unsigned int)a2s[val]))
		val = (++v) ^ 0xd5;
	    s2a[i] = val;
	}
	// negative side of A-Law
	for (i = 32768, v = 0xff, val = 0x2a; i <= 65535; i++) {
	    if ((v > 0x80) && ((i - 8) >= (int)(unsigned int)a2s[val]))
		val = (--v) ^ 0xd5;
	    s2a[i] = val;
	}
    }
};

static InitG711 s_initG711;

}; // anonymous namespace

static const DataBlock s_empty;

static inline void* dbAlloc(unsigned int n, void* oldBuf = 0)
{
    void* data = ::realloc(oldBuf,n);
    if (!data)
	Debug("DataBlock",DebugFail,"realloc(%u) returned NULL!",n);
    return data;
}

const DataBlock& DataBlock::empty()
{
    return s_empty;
}

DataBlock::DataBlock(unsigned int overAlloc)
    : m_data(0), m_length(0), m_allocated(0), m_overAlloc(overAlloc)
{
}

DataBlock::DataBlock(const DataBlock& value)
    : GenObject(),
      m_data(0), m_length(0), m_allocated(0), m_overAlloc(value.overAlloc())
{
    assign(value.data(),value.length());
}

DataBlock::DataBlock(const DataBlock& value, unsigned int overAlloc)
    : GenObject(),
      m_data(0), m_length(0), m_allocated(0), m_overAlloc(overAlloc)
{
    assign(value.data(),value.length());
}

DataBlock::DataBlock(void* value, unsigned int len, bool copyData, unsigned int overAlloc)
    : m_data(0), m_length(0), m_allocated(0), m_overAlloc(overAlloc)
{
    assign(value,len,copyData);
}

DataBlock::~DataBlock()
{
    clear();
}

void* DataBlock::getObject(const String& name) const
{
    if (name == YATOM("DataBlock"))
	return const_cast<DataBlock*>(this);
    return GenObject::getObject(name);
}

void DataBlock::clear(bool deleteData)
{
    m_length = 0;
    m_allocated = 0;
    if (m_data) {
	void *data = m_data;
	m_data = 0;
	if (deleteData)
	    ::free(data);
    }
}

// Change (insert or append data) the current block
bool DataBlock::change(unsigned int pos, const void* buf, unsigned int bufLen,
    unsigned int extra, int extraVal, bool mayOverlap)
{
    unsigned int addLen = (buf ? bufLen : 0) + extra;
    if (!addLen)
	return true;
    XDebug("DataBlock",DebugAll,
	"change(%u,%p,%u,%d,%d,%u) add_lenlen=%u m_data=%p m_length=%u allocated=%u [%p]",
	pos,buf,bufLen,extra,extraVal,mayOverlap,addLen,m_data,m_length,m_allocated,this);
    if (!(buf && bufLen)) {
	buf = 0;
	bufLen = 0;
    }
    if (pos > m_length)
	pos = m_length;
    unsigned int newLen = m_length + addLen;
    void* data = 0;
    unsigned int aLen = 0;
    // Allocate a new buffer if input data may overlap with existing
    bool overlap = buf && (mayOverlap || buf == m_data);
    if (!m_data || overlap || newLen > m_allocated) {
	aLen = allocLen(newLen);
	// Append to existing: Realloc data. Avoid free
	void* reallocAppend = (!overlap && pos == m_length) ? m_data : 0;
	data = dbAlloc(aLen,reallocAppend);
	if (!data)
	    return false;
	if (reallocAppend)
	    clear(false);
	else
	    copyData(data,m_data,m_length,pos,addLen);
    }
    else {
	moveData(m_data,m_length,pos,addLen);
	data = m_data;
    }
    if (bufLen)
	::memcpy((uint8_t*)data + pos,buf,bufLen);
    if (extra)
	::memset((uint8_t*)data + pos + bufLen,extraVal,extra);
    if (aLen)
	assign(data,newLen,false,aLen);
    else
	m_length = newLen;
    return true;
}

#define DB_CHANGE_UINT_FUNC \
    unsigned int n = 0; \
    if (lsb) { \
	while (len--) { \
	    buf[n++] = (uint8_t)value; \
	    value = value >> 8; \
	} \
    } \
    else { \
	uint8_t sh = (len - 1) * 8; \
	while (len--) { \
	    buf[n++] = (uint8_t)(value >> sh); \
	    sh -= 8; \
	} \
    } \
    return change(pos,(const void*)buf,n,0,0,false)

bool DataBlock::change8(unsigned int pos, uint64_t value, unsigned int len, bool lsb)
{
    if (!len)
	return true;
    if (len > 8)
	len = 8;
    uint8_t buf[8] = {0,0,0,0,0,0,0,0};
    DB_CHANGE_UINT_FUNC;
}

bool DataBlock::change4(unsigned int pos, uint32_t value, unsigned int len, bool lsb)
{
    if (!len)
	return true;
    if (len > 4)
	len = 4;
    uint8_t buf[4] = {0,0,0,0};
    DB_CHANGE_UINT_FUNC;
}

DataBlock& DataBlock::assign(void* value, unsigned int len, bool copyData, unsigned int allocated)
{
    if ((value != m_data) || (len != m_length)) {
	void *odata = m_data;
	m_length = 0;
	m_allocated = 0;
	m_data = 0;
	if (len) {
	    if (copyData) {
		allocated = allocLen(len);
		void *data = ::malloc(allocated);
		if (data) {
		    if (value)
			::memcpy(data,value,len);
		    else
			::memset(data,0,len);
		    m_data = data;
		}
		else
		    Debug("DataBlock",DebugFail,"malloc(%d) returned NULL!",allocated);
	    }
	    else {
		if (allocated < len)
		    allocated = len;
		m_data = value;
	    }
	    if (m_data) {
		m_length = len;
		m_allocated = allocated;
	    }
	}
	if (odata && (odata != m_data))
	    ::free(odata);
    }
    return *this;
}

void DataBlock::truncate(unsigned int len)
{
    if (!len)
	clear();
    else if (len < m_length)
	assign(m_data,len);
}

void DataBlock::cut(int len)
{
    if (!len)
	return;

    int ofs = 0;
    if (len < 0)
	ofs = len = -len;

    if ((unsigned)len >= m_length) {
	clear();
	return;
    }

    assign(ofs+(char *)m_data,m_length - len);
}

DataBlock& DataBlock::operator=(const DataBlock& value)
{
    assign(value.data(),value.length());
    return *this;
}

unsigned int DataBlock::allocLen(unsigned int len) const
{
    // allocate a multiple of 8 bytes
    unsigned int over = (8 - (len & 7)) & 7;
    if (over < m_overAlloc)
	return (len + m_overAlloc + 7) & ~7;
    else
	return len + over;
}

bool DataBlock::convert(const DataBlock& src, const String& sFormat,
    const String& dFormat, unsigned maxlen)
{
    if (sFormat == dFormat) {
	operator=(src);
	return true;
    }
    unsigned sl = 0, dl = 0;
    void *ctable = 0;
    if (sFormat == YSTRING("slin")) {
	sl = 2;
	dl = 1;
	if (dFormat == YSTRING("alaw"))
	    ctable = s2a;
	else if (dFormat == YSTRING("mulaw"))
	    ctable = s2u;
    }
    else if (sFormat == YSTRING("alaw")) {
	sl = 1;
	if (dFormat == YSTRING("mulaw")) {
	    dl = 1;
	    ctable = a2u;
	}
	else if (dFormat == YSTRING("slin")) {
	    dl = 2;
	    ctable = a2s;
	}
    }
    else if (sFormat == YSTRING("mulaw")) {
	sl = 1;
	if (dFormat == YSTRING("alaw")) {
	    dl = 1;
	    ctable = u2a;
	}
	else if (dFormat == YSTRING("slin")) {
	    dl = 2;
	    ctable = u2s;
	}
    }
    if (!ctable) {
	clear();
	return false;
    }
    unsigned len = src.length();
    if (maxlen && (maxlen < len))
	len = maxlen;
    len /= sl;
    if (!len) {
	clear();
	return true;
    }
    resize(len * dl);
    if ((sl == 1) && (dl == 1)) {
	unsigned char *s = (unsigned char *) src.data();
	unsigned char *d = (unsigned char *) data();
	unsigned char *c = (unsigned char *) ctable;
	while (len--)
	    *d++ = c[*s++];
    }
    else if ((sl == 1) && (dl == 2)) {
	unsigned char *s = (unsigned char *) src.data();
	unsigned short *d = (unsigned short *) data();
	unsigned short *c = (unsigned short *) ctable;
	while (len--)
	    *d++ = c[*s++];
    }
    else if ((sl == 2) && (dl == 1)) {
	unsigned short *s = (unsigned short *) src.data();
	unsigned char *d = (unsigned char *) data();
	unsigned char *c = (unsigned char *) ctable;
	while (len--)
	    *d++ = c[*s++];
    }
    return true;
}

// Decode a single nibble, return -1 on error
inline signed char hexDecode(char c)
{
    if (('0' <= c) && (c <= '9'))
	return c - '0';
    if (('A' <= c) && (c <= 'F'))
	return c - 'A' + 10;
    if (('a' <= c) && (c <= 'f'))
	return c - 'a' + 10;
    return -1;
}

static inline bool retResult(bool ok, int result, int* res)
{
    if (res)
	*res = result;
    return ok;
}

// Change data from a hexadecimal string representation.
// Each octet must be represented in the input string with 2 hexadecimal characters.
// If a separator is specified, the octets in input string must be separated using
//  exactly 1 separator. Only 1 leading or 1 trailing separators are allowed
bool DataBlock::changeHex(unsigned int pos, const char* data, unsigned int len, char sep,
    bool guessSep, bool emptyOk, int* res)
{
    if (!(data && len))
	return retResult(emptyOk,0,res);

    if (!sep && guessSep && len > 2) {
	const char* s = " :;.,-/|";
	while (char c = *s++) {
	    unsigned int offs = 2;
	    if (data[0] == c)
		offs++;
	    if (len == offs || data[offs] == c) {
		sep = c;
		break;
	    }
	}
    }
    
    // Calculate the destination buffer length
    unsigned int n = 0;
    if (!sep) {
	if (0 != (len % 2))
	    return retResult(false,-3,res);
	n = len / 2;
    }
    else {
	// Remove leading and trailing separators
	if (data[0] == sep) {
	    data++;
	    len--;
	}
	if (len && data[len - 1] == sep)
	    len--;
	// No more leading and trailing separators allowed
	if (!len)
	    return retResult(emptyOk,0,res);
	if (2 != (len % 3))
	    return retResult(false,-3,res);
	n = (len + 1) / 3;
    }
    if (!n)
	return retResult(emptyOk,0,res);

    unsigned int newLen = m_length + n;
    unsigned int aLen = allocLen(newLen);
    void* newData = dbAlloc(aLen);
    if (!newData)
	return retResult(false,-1,res);
    if (pos > m_length)
	pos = m_length;
    char* buf = (char*)newData + pos;
    unsigned int iBuf = 0;
    for (unsigned int i = 0; i < len; i += (sep ? 3 : 2)) {
	signed char c1 = hexDecode(*data++);
	signed char c2 = hexDecode(*data++);
	if (c1 == -1 || c2 == -1 || (sep && (iBuf != n - 1) && (sep != *data++)))
	    break;
	buf[iBuf++] = (c1 << 4) | c2;
    }
    if (iBuf < n) {
	::free(newData);
	return retResult(false,-2,res);
    }
    copyData(newData,m_data,m_length,pos,n);
    assign(newData,newLen,false,aLen);
    return retResult(true,n,res);
}

static inline bool dbIsEscape(char c, char extraEsc)
{
    return c == '\0' || c == '\r' || c == '\n' || c == '\\' || c == '\'' || c == extraEsc;
}

String& DataBlock::sqlEscape(String& str, const void* data, unsigned int len, char extraEsc)
{
    if (!(data && len))
	return str;
    unsigned int useLen = len;
    char* ds = (char*)data;
    for (unsigned int i = 0; i < len; i++) {
	if (dbIsEscape(*ds++,extraEsc))
	    useLen++;
    }
    // No escape needed ?
    if (useLen == len)
	return str.append((const char*)data,len);
    unsigned int sLen = str.length();
    str.append(' ',useLen);
    char* d = ((char*)(str.c_str())) + sLen;
    ds = (char*)data;
    for (unsigned int i = 0; i < len; i++) {
	char c = *ds++;
	if (dbIsEscape(c,extraEsc)) {
	    *d++ = '\\';
	    switch (c) {
		case '\0':
		    c = '0';
		    break;
		case '\r':
		    c = 'r';
		    break;
		case '\n':
		    c = 'n';
		    break;
	    }
	}
	*d++ = c;
    }
    return str;
}

void DataBlock::moveData(void* buf, unsigned int len, unsigned int pos, unsigned int space)
{
    if (!buf || pos >= len)
	return;
    unsigned int delta = pos + space;
    if (!delta)
	return;
    uint8_t* src = (uint8_t*)buf;
    uint8_t* dest = (uint8_t*)buf + delta;
    if (pos) {
	// Insert middle. Keep old data until pos. Copy the rest
	len -= pos;
	src += pos;
    }
    if (delta < len)
	::memmove(dest,src,len);
    else
	::memcpy(dest,src,len);
}

void DataBlock::copyData(void* dest, const void* src, unsigned int len, unsigned int pos,
    unsigned int space)
{
    if (!(src && dest && len))
	return;
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (!pos)
	// Data insert before existing, copy old data after it
	::memcpy(d + space,s,len);
    else if (pos == len)
	// Data added to existing, copy old at start
	::memcpy(d,s,len);
    else if (space) {
	// Insert middle
	::memcpy(d,s,pos);
	::memcpy(d + pos + space,s + pos,len - pos);
    }
    else
	::memcpy(d,s,len);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
