/**
 * DataBlock.cpp
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
    unsigned int added = (buf ? bufLen : 0) + extra;
    if (!added)
	return true;
    XDebug("DataBlock",DebugAll,
	"change(%u,%p,%u,%d,%d,%u) add_len=%u m_data=%p m_length=%u allocated=%u [%p]",
	pos,buf,bufLen,extra,extraVal,mayOverlap,added,m_data,m_length,m_allocated,this);
    if (!(buf && bufLen)) {
	buf = 0;
	bufLen = 0;
    }
    if (pos > m_length)
	pos = m_length;
    unsigned int newLen = m_length + added;
    // Allocate a new buffer if input data may overlap with existing
    bool overlap = buf && (mayOverlap || buf == m_data);
    if (!m_data || overlap || newLen > m_allocated) {
	unsigned int aLen = (!m_data || newLen > m_allocated) ? allocLen(newLen) : m_allocated;
	void* data = dbAlloc(aLen,overlap ? 0 : m_data);
	if (!data)
	    return false;
	if (m_data) {
	    if (overlap)             // New buffer
		rebuildDataInsert(data,newLen,m_data,m_length,pos,added);
	    else if (pos < m_length) // Data re-allocated. Insert. Move data
		moveData(data,newLen,m_length - pos,pos + added,pos);
	    // else: Data re-allocated. Append. Old data already in buffer
	    // Avoid free if data was re-allocated
	    clear(overlap);
	}
	assign(data,newLen,false,aLen);
    }
    else {
	// Allocated space did not change. Move data if insert
	if (pos < m_length)
	    moveData(m_data,newLen,m_length - pos,pos + added,pos);
	m_length = newLen;
    }
    if (bufLen)
	::memcpy(data(pos),buf,bufLen);
    if (extra)
	::memset(data(pos + bufLen),extraVal,extra);
    return true;
}

DataBlock& DataBlock::assign(void* value, unsigned int len, bool copyData, unsigned int allocated)
{
    if ((value != m_data) || (len != m_length)) {
	void *odata = m_data;
	unsigned int oldSize = m_allocated;
	m_length = 0;
	m_allocated = 0;
	m_data = 0;
	if (len) {
	    if (copyData) {
		allocated = allocLen(len);
		if (allocated == oldSize && odata && !value) {
		    m_data = odata;
		    ::memset(m_data,0,len);
		}
		else {
		    void* data = dbAlloc(allocated);
		    if (data) {
			if (value)
			    ::memcpy(data,value,len);
			else
			    ::memset(data,0,len);
			m_data = data;
		    }
		}
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

void DataBlock::resize(unsigned int len, bool keepData, bool reAlloc)
{
    if (len == m_length)
	return;
    if (!len) {
	clear();
	return;
    }
    if (!keepData) {
	if (!reAlloc && m_data && len <= m_allocated) {
	    ::memset(m_data,0,len);
	    m_length = len;
	}
	else
	    assign(0,len);
    }
    else if (len < length())
	cut(len,length() - len,reAlloc);
    else
	appendBytes(len - length());
}

void DataBlock::cut(unsigned int pos, unsigned int len, bool reAlloc)
{
    XDebug(DebugAll,"DataBlock::cut(%u,%u,%u) len=%u [%p]",pos,len,reAlloc,m_length,this);
    if (!(m_data && len) || pos >= m_length)
	return;
    if (len > m_length - pos) {
	len = m_length - pos;
	if (!len)
	    return;
    }
    unsigned int newLen = m_length - len;
    if (!newLen) {
	clear();
	return;
    }
    unsigned int lastRemoveIdx = pos + len;
    unsigned int newSize = reAlloc ? allocLen(newLen) : 0;
    void* buf = 0;
    if (newSize && newSize != m_allocated)
	// Realloc buffer if cut from end, alloc a new one otherwise
	buf = dbAlloc(newSize,lastRemoveIdx == m_length ? m_data : 0);
    if (!buf) {
	// Removed without size change or memory allocation error
	// Move data if cut from start/middle. Adjust held length
	if (lastRemoveIdx < m_length)
	    moveData(m_data,m_length,m_length - lastRemoveIdx,pos,lastRemoveIdx);
	m_length = newLen;
	return;
    }
    if (lastRemoveIdx < m_length) {
	rebuildDataRemove(buf,newLen,m_data,m_length,pos,len,0);
	::free(m_data);
    }
    // else: nothing to be done: we hold the original buffer start
    m_data = buf;
    m_length = newLen;
    m_allocated = newSize;
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
    if (m_data)
	rebuildDataInsert(newData,newLen,m_data,m_length,pos,n);
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

void DataBlock::moveData(void* buf, unsigned int bufLen, unsigned int len,
    unsigned int dPos, unsigned int sPos, int fill)
{
    int delta = (int)((int64_t)sPos - dPos);
    if (!(buf && len) || !delta || sPos + len > bufLen || dPos + len > bufLen)
	return;
    bool useCopy = delta >= (int)len;
//#define DataBlock_moveData_debug
#ifdef DataBlock_moveData_debug
    String fillStr;
    if (fill >= 0) {
	if (useCopy)
	    fillStr.printf(" fill: %d pos=%u count=%u",fill,sPos,len);
	else if (delta > 0)
	    fillStr.printf(" fill: %d pos=%u count=%u",fill,sPos + len - delta,delta);
	else
	    fillStr.printf(" fill: %d pos=%u count=%u",fill,sPos,-delta);
    }
    Debug("DataBlock",DebugTest,"moveData(%p,%u) move=%u dPos=%u sPos=%u delta=%d use_copy=%u%s",
	buf,bufLen,len,dPos,sPos,(int)delta,useCopy,fillStr.safe());
#endif
    if (useCopy)
	::memcpy((uint8_t*)buf + dPos,(uint8_t*)buf + sPos,len);
    else
	::memmove((uint8_t*)buf + dPos,(uint8_t*)buf + sPos,len);
    // Reset data at position + moved data if requested
    if (fill >= 0) {
	if (useCopy)
	    // Not overlapped
	    ::memset((uint8_t*)buf + sPos,fill,len);
	else if (delta > 0)
	    // Overlap. Data moved toward buffer start
	    ::memset((uint8_t*)buf + sPos + len - delta,fill,delta);
	else
	    // Overlap. Data moved toward buffer end
	    ::memset((uint8_t*)buf + sPos,fill,-delta);
    }
}

void DataBlock::rebuildDataInsert(void* dest, unsigned int dLen, const void* src, unsigned int sLen,
    unsigned int pos, unsigned int space, int fill)
{
    if (!(src && dest && (space || sLen)) || (space + sLen) > dLen)
	return;
//#define DataBlock_rebuildDataInsert_debug
#ifdef DataBlock_rebuildDataInsert_debug
    String tmp;
    if (!pos)
	tmp.printf("%u dPos=%u sPos=%u",sLen,space,0);
    else if (pos >= sLen)
	tmp.printf("%u dPos=%u sPos=%u",sLen,0,0);
    else
	tmp.printf("(%u dPos=%u sPos=%u) (%u dPos=%u sPos=%u)",pos,0,0,sLen - pos,pos + space,pos);
    if (space && fill >= 0) {
	if (!pos)
	    tmp.printfAppend(" fill %d pos=%u",fill,0);
	else if (pos >= sLen)
	    tmp.printfAppend(" fill %d pos=%u",fill,sLen);
	else
	    tmp.printfAppend(" fill %d pos=%u",fill,pos);
    }
    Debug("DataBlock",DebugTest,"rebuildDataInsert(%p,%u,%p,%u) pos=%u insert=%u copy %s",
	dest,dLen,src,sLen,pos,space,tmp.c_str());
#endif
    if (!pos) {
	// Space insert at start
	// Copy data after inserted space
	if (sLen)
	    ::memcpy((uint8_t*)dest + space,src,sLen);
	if (space && fill >= 0)
	    ::memset((uint8_t*)dest,fill,space);
    }
    else if (pos >= sLen) {
	// Space added
	// Copy data at buffer start
	if (sLen)
	    ::memcpy((uint8_t*)dest,src,sLen);
	if (space && fill >= 0)
	    ::memset((uint8_t*)dest + sLen,fill,space);
    }
    else {
	// Space insert in the middle
	// Copy 'src' before pos to buffer start and 'src' after pos to buffer pos+items
	::memcpy(dest,src,pos);
	::memcpy((uint8_t*)dest + pos + space,(uint8_t*)src + pos,sLen - pos);
	if (space && fill >= 0)
	    ::memset((uint8_t*)dest + pos,fill,space);
    }
}

void DataBlock::rebuildDataRemove(void* dest, unsigned int dLen, const void* src, unsigned int sLen,
    unsigned int pos, unsigned int space, int fillAfter)
{
    if (!(src && dest && space) || pos >= sLen || space >= sLen)
	return;
    if (pos + space > sLen)
	space = sLen - pos;
    unsigned int cp = sLen - space;
    if (cp > dLen)
	return;
//#define DataBlock_rebuildDataRemove_debug
#ifdef DataBlock_rebuildDataRemove_debug
    String tmp;
    if (!pos)
	tmp.printf("%u dPos=%u sPos=%u",cp,0,space);
    else if (pos + space >= sLen)
	tmp.printf("%u dPos=%u sPos=%u",cp,0,0);
    else
	tmp.printf("(%u dPos=%u sPos=%u) (%u dPos=%u sPos=%u)",pos,0,0,cp - pos,pos,pos + space);
    if (fillAfter >= 0 && cp < dLen)
	tmp.printfAppend(" fill_after %d pos=%u count=%u",fillAfter,cp,dLen - cp);
    Debug("DataBlock",DebugTest,"rebuildDataRemove(%p,%u,%p,%u) pos=%u removed=%u copy %s",
	dest,dLen,src,sLen,pos,space,tmp.c_str());
#endif
    if (!pos)
	// Removed from start
	::memcpy(dest,(uint8_t*)src + space,cp);
    else if (pos + space >= sLen)
	// Removed from end
	::memcpy(dest,src,cp);
    else {
	// Removed from middle
	::memcpy(dest,src,pos);
	::memcpy((uint8_t*)dest + pos,(uint8_t*)src + pos + space,cp - pos);
    }
    if (fillAfter >= 0 && cp < dLen)
	::memset((uint8_t*)dest + cp,fillAfter,dLen - cp);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
