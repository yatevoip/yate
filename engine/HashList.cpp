/**
 * HashList.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2005 Null Team
 *
 * Idea and initial implementation (as HashTable) by Maciek Kaminski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "yateclass.h"

using namespace TelEngine;

HashList::HashList(unsigned int size)
    : m_size(size), m_lists(0)
{
    XDebug(DebugAll,"HashList::HashList(%u) [%p]",size,this);
    if (m_size < 1)
	m_size = 1;
    if (m_size > 1024)
	m_size = 1024;
    m_lists = new ObjList* [m_size];
    for (unsigned int i = 0; i < m_size; i++)
	m_lists[i] = 0;
}

HashList::~HashList()
{
    XDebug(DebugAll,"HashList::~HashList() [%p]",this);
    clear();
    delete[] m_lists;
}

void* HashList::getObject(const String& name) const
{
    if (name == "HashList")
	return const_cast<HashList*>(this);
    return GenObject::getObject(name);
}

unsigned int HashList::count() const
{
    unsigned int c = 0;
    for (unsigned int i = 0; i < m_size; i++)
	if (m_lists[i])
	    c += m_lists[i]->count();
    return c;
}

GenObject* HashList::operator[](const String& str) const
{
    ObjList *obj = find(str);
    return obj ? obj->get() : 0;
}

ObjList* HashList::find(const GenObject* obj) const
{
    XDebug(DebugAll,"HashList::find(%p) [%p]",obj,this);
    if (!obj)
	return 0;
    unsigned int i = obj->toString().hash() % m_size;
    return m_lists[i] ? m_lists[i]->find(obj) : 0;
}

ObjList* HashList::find(const String& str) const
{
    XDebug(DebugAll,"HashList::find(\"%s\") [%p]",str.c_str(),this);
    unsigned int i = str.hash() % m_size;
    return m_lists[i] ? m_lists[i]->find(str) : 0;
}

ObjList* HashList::append(const GenObject* obj)
{
    XDebug(DebugAll,"HashList::append","(%p) [%p]",obj,this);
    if (!obj)
	return 0;
    unsigned int i = obj->toString().hash() % m_size;
    if (!m_lists[i])
	m_lists[i] = new ObjList;
    return m_lists[i]->append(obj);
}

GenObject* HashList::remove(GenObject* obj, bool delobj)
{
    ObjList *n = find(obj);
    return n ? n->remove(delobj) : 0;
}

void HashList::clear()
{
    XDebug(DebugAll,"HashList::clear()"," [%p]",this);
    for (unsigned int i = 0; i < m_size; i++)
	if (m_lists[i]) {
	    m_lists[i]->destruct();
	    m_lists[i] = 0;
	}
}

/* vi: set ts=8 sw=4 sts=4 noet: */
