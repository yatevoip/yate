/**
 * yatematchingitem.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Matching item
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2025 Null Team
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

#ifndef __YATEMATCHINGITEM_H
#define __YATEMATCHINGITEM_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <yatexml.h>

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class MatchingItemBase;
class MatchingItemString;
class MatchingItemRegexp;
class MatchingItemXPath;
class MatchingItemRandom;
class MatchingItemList;
class MatchingItemCustom;
class MatchingItemCustomFactory;
class MatchingItemLoad;
class MatchingItemDump;

/**
 * This class holds matching parameters to be passed when matching in item
 * @short Matching item match parameters
 */
class YATE_API MatchingParams : public String
{
    YCLASS(MatchingParams,String)
    friend class MatchingItemList;
public:
    /**
     * Constructor
     * @param name Item name
     */
    inline MatchingParams(const char* name = 0)
	: String(name), m_now(0), m_dbg(0), m_level(0), m_private(0)
	{}

    /**
     * Destructor
     */
    inline ~MatchingParams()
	{ TelEngine::destruct(m_private); }

    /**
     * Run match check on item
     * @param item Item to check
     * @param list Parameters list
     * @param str String to check. Ignored if parameters are given
     * @return Pointer to first level matched item (for lists with any matching
     *  will be the matched item in list). NULL if not matched
     */
    const MatchingItemBase* matches(const MatchingItemBase& item,
	const NamedList* list, const String* str = 0);

    ObjList m_params;                    // Arbitray parameters. May be set during matching
    uint64_t m_now;                      // Current time
    DebugEnabler* m_dbg;                 // Optional pointer to DebugEnabler to be used
    int m_level;                         // Debug level for tracking

private:
    GenObject* m_private;
};

/**
 * Base class for all matching items
 * @short Matching item common interface
 */
class YATE_API MatchingItemBase : public GenObject
{
    YCLASS(MatchingItemBase,GenObject)
    friend class MatchingItemList;
    friend class MatchingItemCustom;
    friend class MatchingItemLoad;
public:
    /**
     * Item type
     */
    enum Type {
	TypeUnknown = 0,
	TypeString,
	TypeRegexp,
	TypeXPath,
	TypeRandom,
	TypeList,
	TypeCustom,
	TypeLastType,
    };

    /**
     * Matching action when parameter is missing
     */
    enum MissingParamMatchAction {
	MissingParamRunMatch = 0,        // Run string match against empty string
	MissingParamMatch,               // Match
	MissingParamNoMatch,             // No match
    };

    /**
     * Constructor
     * @param type Item type
     * @param name Item name
     * @param negated True if matching is negated (return the opposite of match in
     *  public methods), false otherwise
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     */
    MatchingItemBase(int type, const char* name, bool negated = false,
	int missingMatch = 0, const char* id = 0);

    /**
     * Destructor
     */
    virtual ~MatchingItemBase();

    /**
     * Retrieve item type
     * @return Item type
     */
    inline int type() const
	{ return m_type; }

    /**
     * Retrieve the name of this item
     * @return Item name
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Retrieve the id of this item
     * @return Item id
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Check if this item is negated when testing
     * @return True if negated, false otherwise
     */
    inline bool negated() const
	{ return !m_notNegated; }

    /**
     * Retrieve matching action when parameter is missing
     * @return Matching action when parameter is missing
     */
    inline int missingMatch() const
	{ return m_missingMatch; }

    /**
     * String match. Handles matching result negation
     * @param str String to match
     * @param params Optional parameters used during match
     * @return True if matches, false otherwise
     */
    inline bool matchString(const String& str, MatchingParams* params = 0) const
	{ return m_notNegated == runMatchString(str,params); }

    /**
     * Optional string match. Handles matching result negation
     * @param str Pointer to string to match
     * @param params Optional parameters used during match
     * @return True if matches, false otherwise
     */
    inline bool matchStringOpt(const String* str, MatchingParams* params = 0) const
	{ return m_notNegated == runMatchStringOpt(str,params); }

    /**
     * NamedList parameter match. Handles matching result negation
     * @param list List to search for parameter match
     * @param params Optional parameters used during match
     * @return True if matches, false otherwise
     */
    inline bool matchListParam(const NamedList& list, MatchingParams* params = 0) const
	{ return m_notNegated == runMatchListParam(list,params); }

    /**
     * Copy this item
     * @return MatchingItemBase pointer
     */
    inline MatchingItemBase* copy() const
	{ return copyItem(); }

    /**
     * Retrieve item name (suitable for list retrieval)
     * @return Item name
     */
    virtual const String& toString() const;

    /**
     * Retrieve item type name
     * @return Type name
     */
    virtual const char* typeName() const;

    /**
     * Initialize matching item global options
     * @param params Parameters list
     */
    static void setup(const NamedList& params);

    /**
     * Retrieve type name dictionary
     * @return TokenDict pointer
     */
    static const TokenDict* typeDict();

    /**
     * Retrieve missing match dictionary
     * @return TokenDict pointer
     */
    static const TokenDict* missingMatchDict();

protected:
    /**
     * String match to be implemented by descendants
     * @param str String to match
     * @param params Optional parameters used during match
     * @return False
     */
    virtual bool runMatchString(const String& str, MatchingParams* params = 0) const;

    /**
     * NamedList parameter match
     * @param list List to search for parameter match
     * @param params Optional parameters used during match
     * @return True if matches, false otherwise
     */
    virtual bool runMatchListParam(const NamedList& list, MatchingParams* params = 0) const;

    /**
     * Optional string match
     * @param str Pointer to string
     * @param params Optional parameters used during match
     * @return True if matches, false otherwise
     */
    virtual bool runMatchStringOpt(const String* str, MatchingParams* params = 0) const;

    /**
     * Copy this item
     * @return MatchingItemBase pointer, NULL if not implemented
     */
    virtual MatchingItemBase* copyItem() const = 0;

private:
    int m_type;                          // Item type
    String m_name;                       // Item name
    bool m_notNegated;                   // Item is not negated
    int m_missingMatch;                  // Matching action when parameter is missing
    String m_id;                         // Item id
};

/**
 * Match using a string comparison
 * @short String comparison matching item
 */
class YATE_API MatchingItemString : public MatchingItemBase
{
    YCLASS(MatchingItemString,MatchingItemBase)
public:
    /**
     * Constructor
     * @param name Item name
     * @param value String to match
     * @param caseInsensitive Set it to true to do a case insensitive match
     * @param negated True if matching is negated (return the opposite of match in
     *  public methods), false otherwise
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     */
    inline MatchingItemString(const char* name, const char* value, bool caseInsensitive = false,
	bool negated = false, int missingMatch = 0, const char* id = 0)
	: MatchingItemBase(TypeString,name,negated,missingMatch,id), m_value(value),
	m_caseMatch(!caseInsensitive)
	{}

    /**
     * Retrieve the string to match
     * @return String to match
     */
    inline const String& value() const
	{ return m_value; }

    /**
     * Check if this item is using a case insensitive comparison
     * @return True if this item is using a case insensitive comparison
     */
    inline bool caseInsensitive() const
	{ return !m_caseMatch; }

protected:
    /**
     * String match
     * @param str String to match
     * @param params Optional parameters used during match
     * @return True if matched, false otherwise
     */
    virtual bool runMatchString(const String& str, MatchingParams* params = 0) const;

    /**
     * Copy this item
     * @return MatchingItemBase pointer
     */
    virtual MatchingItemBase* copyItem() const;

private:
    String m_value;                      // String to match
    bool m_caseMatch;                    // Non case insensitive match
};

/**
 * Match using a regular expression
 * @short A matching item using a regular expression
 */
class YATE_API MatchingItemRegexp : public MatchingItemBase
{
    YCLASS(MatchingItemRegexp,MatchingItemBase)
public:
    /**
     * Constructor
     * @param name Item name
     * @param value Regular expression
     * @param negated True if matching is negated (return the opposite of match in
     *  public methods), false otherwise
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     */
    inline MatchingItemRegexp(const char* name, const char* value, bool negated = false,
	int missingMatch = 0, const char* id = 0)
	: MatchingItemBase(TypeRegexp,name,negated,missingMatch,id), m_value(value)
	{ m_value.compile(); }

    /**
     * Constructor
     * @param name Item name
     * @param value Regular expression
     * @param negated True if matching is negated (return the opposite of match in
     *  public methods), false otherwise
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     */
    inline MatchingItemRegexp(const char* name, const Regexp& value, bool negated = false,
	int missingMatch = 0, const char* id = 0)
	: MatchingItemBase(TypeRegexp,name,negated,missingMatch,id), m_value(value)
	{ m_value.compile(); }

    /**
     * Retrieve the regular expression used to match
     * @return Regular expression used to match
     */
    inline const Regexp& value() const
	{ return m_value; }

    /**
     * Build a MatchingItemRegexp from string
     * @param name Item name
     * @param str Regexp string
     * @param valid Pointer to flag to be set to valid value flag. Invalid item will returned if this parameter is set
     * @param validate Validate the regular expression. This parameter is ignored if 'invalid' is not NULL
     * @param negated Greater than 0: build a negated match, 0: build a non negated match,
     *  negative: build a negated match if str ends with ^
     * @param insensitive Build a case insensitive regexp
     * @param extended Build a regexp using extended POSIX
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     * @return MatchingItemRegexp pointer, NULL on failure
     */
    static MatchingItemRegexp* build(const char* name, const String& str, bool* valid = 0,
	bool validate = false, int negated = 0, bool insensitive = false, bool extended = false,
	int missingMatch = 0, const char* id = 0);

protected:
    /**
     * String match
     * @param str String to match
     * @param params Optional parameters used during match
     * @return True if matched, false otherwise
     */
    virtual bool runMatchString(const String& str, MatchingParams* params = 0) const;

    /**
     * Copy this item
     * @return MatchingItemBase pointer
     */
    virtual MatchingItemBase* copyItem() const;

private:
    Regexp m_value;                      // Regexp used for matching
};

/**
 * Match using an XPath
 * @short A matching item using an XPath
 */
class YATE_API MatchingItemXPath : public MatchingItemBase
{
    YCLASS(MatchingItemXPath,MatchingItemBase)
public:
    /**
     * Constructor
     * @param name Item name
     * @param value The XPath
     * @param match Optional matching used after XPath search
     * @param negated True if matching is negated (return the opposite of match in
     *  public methods), false otherwise
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     */
    inline MatchingItemXPath(const char* name, const char* value, MatchingItemBase* match = 0,
	bool negated = false, int missingMatch = 0, const char* id = 0)
	: MatchingItemBase(TypeXPath,name,negated,missingMatch,id), m_value(value), m_match(match)
	{ m_value.parse(); }

    /**
     * Constructor
     * @param name Item name
     * @param value The XPath
     * @param match Optional matching used after XPath search
     * @param negated True if matching is negated (return the opposite of match in
     *  public methods), false otherwise
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     */
    inline MatchingItemXPath(const char* name, const XPath& value, MatchingItemBase* match = 0,
	bool negated = false, int missingMatch = 0, const char* id = 0)
	: MatchingItemBase(TypeXPath,name,negated,missingMatch,id), m_value(value), m_match(match)
	{ m_value.parse(); }

    /**
     * Destructor
     */
    virtual ~MatchingItemXPath();

    /**
     * Retrieve the XPath used to match
     * @return XPath used to match
     */
    inline const XPath& value() const
	{ return m_value; }

    /**
     * Retrieve the optional matching used after XPath search
     * @return MatchingItemBase pointer, NULL if not set
     */
    inline const MatchingItemBase* match() const
	{ return m_match; }

    /**
     * Build a MatchingItemXPath from string
     * @param name Item name
     * @param str Value description
     * @param error String to set with error. Invalid item will returned if this parameter is set
     * @param validate Check for validity
     * @param match Optional matching used after XPath search. Will be consumed
     * @param negated Item negated flag
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     * @return MatchingItemXPath pointer, NULL on failure
     */
    static MatchingItemXPath* build(const char* name, const String& str, String* error = 0,
	bool validate = true, MatchingItemBase* match = 0, bool negated = false,
	int missingMatch = 0, const char* id = 0);

protected:
    /**
     * String match
     * @param str String to match
     * @param params Optional parameters used during match
     * @return True if matched, false otherwise
     */
    virtual bool runMatchString(const String& str, MatchingParams* params = 0) const;

    /**
     * NamedList parameter match
     * @param list List to search for parameter match
     * @param params Optional parameters used during match
     * @return True if matches, false otherwise
     */
    virtual bool runMatchListParam(const NamedList& list, MatchingParams* params = 0) const;

    /**
     * Copy this item
     * @return MatchingItemBase pointer
     */
    virtual MatchingItemBase* copyItem() const;

private:
    bool runMatch(MatchingParams* params, const NamedList* list,
	const String& str = String::empty()) const;
    XPath m_value;                       // XPath used for matching/searching
    MatchingItemBase* m_match;           // Matching for XPath result
};

/**
 * Match using a random number
 * Implements a matching of a reference value greater than RANDOM[0..MAX - 1]
 * @short Random number matching
 */
class YATE_API MatchingItemRandom : public MatchingItemBase
{
    YCLASS(MatchingItemRandom,MatchingItemBase)
public:
    /**
     * Constructor
     * Random percent match: val=[PERCENT] maxVal=100
     * @param val Reference value. 0: never match, 'maxVal' is ignored
     * @param maxVal Upper interval value. 0, 1, less than / equal to 'val': always match
     * @param negated True if matching is negated (return the opposite of match in
     *  public methods), false otherwise
     * @param name Item name
     * @param missingMatch Matching action when parameter is missing.
     *  This parameter is ignored if name is empty
     * @param id Item id
     */
    inline MatchingItemRandom(uint32_t val, uint32_t maxVal, bool negated = false,
	const char* name = 0, int missingMatch = 0, const char* id = 0)
	: MatchingItemBase(TypeRandom,name,negated,TelEngine::null(name) ? 0 : missingMatch,id),
	m_value(val), m_maxVal(maxVal) {
	    int check = checkMatchValues(m_value,m_maxVal);
	    if (check < 0) // Never match
		m_maxVal = 100;
	    else if (check > 0) // Always match. Avoid division by 0
		m_value = m_maxVal = 100;
	}

    /**
     * Retrieve the reference value used to make a decision
     * @return The reference value used to make a decision
     */
    inline uint32_t value() const
	{ return m_value; }

    /**
     * Retrieve the maximum value for random number
     * @return The maximum value for random number
     */
    inline uint32_t maxValue() const
	{ return m_maxVal; }

    /**
     * Dump this item's value
     * @param buf Destination buffer
     * @return Destination buffer reference
     */
    inline String& dumpValue(String& buf) const {
	    buf << value();
	    if (maxValue() == 100)
		return buf << '%';
	    return buf << '/' << maxValue();
	}

    /**
     * Build a MatchingItemRandom from string
     * @param str Value description. Format: val[/maxVal] or [0..100]%
     * @param valid Pointer to flag to be set to valid value flag. Invalid item will returned if this parameter is set
     * @param validate Check values
     * @param negated Item negated flag
     * @param name Item name
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     * @return MatchingItemRandom pointer, NULL on failure
     */
    static MatchingItemRandom* build(const String& str, bool* valid = 0, bool validate = true,
	bool negated = false, const char* name = 0, int missingMatch = 0, const char* id = 0);

    /**
     * Check values
     * @param val Reference value
     * @param maxVal Upper interval value
     * @return Negative: never match, strict positive: always match, 0:random match
     */
    static inline int checkMatchValues(uint32_t val, uint32_t maxVal) {
	    if (!val) // Never match
		return -1;
	    if (val >= maxVal) // Always match
		return 1;
	    return 0;
	}

protected:
    /**
     * String match
     * @param str String to match
     * @param params Optional parameters used during match
     * @return True if matched, false otherwise
     */
    virtual bool runMatchString(const String& str, MatchingParams* params = 0) const;

    /**
     * Copy this item
     * @return MatchingItemBase pointer
     */
    virtual MatchingItemBase* copyItem() const;

private:
    uint32_t m_value;                    // Reference value
    uint32_t m_maxVal;                   // Max value
};

/**
 * List of matching items
 * @short A list of matching items
 */
class YATE_API MatchingItemList : public MatchingItemBase
{
    YCLASS(MatchingItemList,MatchingItemBase)
    friend class MatchingItemLoad;
    friend class MatchingParams;
public:
    /**
     * Constructor
     * @param name Item name
     * @param matchAll True to match all items (logical AND), false to match any item (logical OR)
     * @param negated True if matching is negated (return the opposite of match in
     *  public methods), false otherwise
     * @param missingMatch Matching action when parameter is missing
     * @param id Item id
     */
    MatchingItemList(const char* name, bool matchAll = true, bool negated = false,
	int missingMatch = 0, const char* id = 0);
    
    /**
     * Check if all items must match
     * @return True if all items must match (logical AND), false if any item matches (logical OR)
     */
    inline bool matchAll() const
	{ return m_matchAll; }

    /**
     * Retrieve the list length
     * @return List length
     */
    inline unsigned int length() const
	{ return m_value.length(); }

    /**
     * Retrieve the index of an item found by name
     * @param name Item name
     * @return Index of found item, negative if not found
     */
    inline int indexOf(const String& name) const
	{ return m_value.index(name); }

    /**
     * Retrieve a pointer to item at given index
     * @param index Index to retrieve
     * @return MatchingItemBase pointer, NULL if not set or index is out of bounds
     */
    inline const MatchingItemBase* at(unsigned int index) const
	{ return static_cast<MatchingItemBase*>(m_value.at(index)); }

    /**
     * Find an item by name
     * @param name Item name
     * @return MatchingItemBase pointer, NULL if not found
     */
    inline const MatchingItemBase* find(const String& name) const
	{ return static_cast<const MatchingItemBase*>(m_value[name]); }

    /**
     * Append a list of items to the list
     * @param list Items list. Handled items will be consumed
     * @return True on success, false on failure (memory allocation error or empty list given)
     */
    bool append(ObjList& list);

    /**
     * Append an item to the list
     * @param item Item to append, pointer will be consumed
     * @return True on success, false on failure (memory allocation error or NULL pointer given)
     */
    inline bool append(MatchingItemBase* item)
	{ return change(item); }

    /**
     * Set an item at given position
     * Item is removed if given pointer is NULL
     * @param item Item to set, pointer will be consumed
     * @param pos Item position. Append if past list length
     * @return True on success, false on failure (memory allocation error or NULL pointer given)
     */
    inline bool set(MatchingItemBase* item, unsigned int pos)
	{ return change(item,pos); }

    /**
     * Optimize a MatchingItemList
     * Delete list if empty or there is only one item in it, return the first item in it any
     * @param list List to optimize
     * @param flags Optimize flags (see MatchingItemLoad flags)
     * @return MatchingItemBase pointer, may be the list itself if not optimized
     *  May be NULL if list is empty
     */
    static inline MatchingItemBase* optimize(MatchingItemList* list, uint64_t flags = 0)
	{ return doOptimize(list,flags); }

protected:
    /**
     * String match
     * @param str String to match
     * @param params Optional parameters used during match
     * @return True if matched, false otherwise
     */
    virtual bool runMatchString(const String& str, MatchingParams* params = 0) const;

    /**
     * NamedList parameter match
     * @param list List to search for parameter match
     * @param params Optional parameters used during match
     * @return True if matches, false otherwise
     */
    virtual bool runMatchListParam(const NamedList& list, MatchingParams* params = 0) const;

    /**
     * Copy this item
     * @return MatchingItemBase pointer
     */
    virtual MatchingItemBase* copyItem() const;

private:
    const MatchingItemBase* runMatch(MatchingParams* params, const NamedList* list,
	const String& str = String::empty()) const;
    bool change(MatchingItemBase* item, int pos = -1, bool ins = false);
    static MatchingItemBase* doOptimize(MatchingItemList* list, uint64_t flags,
	unsigned int depth = 0, const MatchingItemLoad* loader = 0);

    ObjVector m_value;                   // List of items to match
    bool m_matchAll;                     // Match all/any item(s)
};

/**
 * Custom match
 * @short Base class for custom matching item
 */
class YATE_API MatchingItemCustom : public MatchingItemBase
{
    YCLASS(MatchingItemCustom,MatchingItemBase)
public:
    /**
     * Destructor
     */
    virtual ~MatchingItemCustom();

    /**
     * Retrieve the custom type name
     * @return Custom name
     */
    inline const String& customType() const
	{ return m_type; }

    /**
     * Retrieve the custom display (dump) type name
     * @return Custom display name
     */
    inline const String& displayType() const
	{ return m_typeDisplay; }

    /**
     * Retrieve item type name
     * @return Type name
     */
    virtual const char* typeName() const;

    /**
     * Retrieve item string value
     * @return String pointer, NULL if not applicable
     */
    virtual const String* valueStr() const;

    /**
     * Dump item string value
     * @param dump Dumper calling this method
     * @param buf Destination buffer
     * @return Destination buffer reference
     */
    virtual const String& dumpValue(const MatchingItemDump& dump, String& buf) const;

    /**
     * Dump this item
     * @param dump Dumper calling this method
     * @param buf Destination buffer
     * @param indent Indent for each item (line)
     * @param addIndent Indent to be added when depth advances
     * @param depth Re-enter depth
     * @return Destination buffer reference
     */
    virtual String& dump(const MatchingItemDump& dump, String& buf,
	const String& indent, const String& addIndent, unsigned int depth) const;

    /**
     * Dump this item. This method is used when item implements full dump
     * @param dump Dumper calling this method
     * @param buf Destination buffer
     * @param indent Indent for each item (line)
     * @param addIndent Indent to be added when depth advances
     * @param depth Re-enter depth
     * @return Destination buffer reference
     */
    virtual String& dumpFull(const MatchingItemDump& dump, String& buf,
	const String& indent, const String& addIndent, unsigned int depth) const;

    /**
     * Fill item data in XML 
     * @param dump Dumper calling this method
     * @param xml XmlElement to fill
     * @param depth Re-enter depth
     */
    virtual void dumpXml(const MatchingItemDump& dump, XmlElement* xml, unsigned int depth) const;

    /**
     * Dump this item to parameters list
     * @param dump Dumper calling this method
     * @param list Destination list
     * @param prefix Optional parameters prefix
     * @param depth Re-enter depth
     * @param id Id to use when storing item
     * @return Number of saved item(s)
     */
    virtual unsigned int dumpList(const MatchingItemDump& dump, NamedList& list,
	const char* prefix = 0, unsigned int depth = 0, const char* id = 0) const;

    /**
     * Load matching item(s)
     * @param load Loader calling this method
     * @param params Parameters list
     * @param error Optional pointer to error string
     * @param prefix Optional parameters prefix
     * @return True on success, false on failure
     */
    virtual bool loadItem(const MatchingItemLoad& load, const NamedList& params, String* error,
	const char* prefix);

    /**
     * Load matching item(s) from XML description
     * @param load Loader calling this method
     * @param xml XmlElement object
     * @param error Optional pointer to error string
     * @return True on success, false on failure
     */
    virtual bool loadXml(const MatchingItemLoad& load, const XmlElement& xml, String* error);

protected:
    /**
     * Constructor
     * @param type Type name
     * @param name Item name
     * @param typeDisplay Type display (dump) name
     */
    MatchingItemCustom(const char* type, const char* name, const char* typeDisplay = 0);

    /**
     * Build an item copy. Fill base params
     * @return MatchingItemBase pointer, NULL if not implemented
     */
    virtual MatchingItemBase* copyItem() const;

    /**
     * Build a copy of this item
     * @return MatchingItemBase pointer, NULL if not implemented
     */
    virtual MatchingItemBase* customCopyItem() const = 0;

private:
    String m_type;
    String m_typeDisplay;
};

/**
 * This class holds matching item custom build factory
 * @short Matching item custom build factory
 */
class YATE_API MatchingItemCustomFactory : public GenObject
{
    YCLASS(MatchingItemCustomFactory,GenObject)
public:
    /**
     * Build an item
     * @param type Item type
     * @param name Item name
     * @param known Type known flag
     * @return MatchingItemCustom pointer, NULL on failure, not built, not found
     */
    static MatchingItemCustom* build(const String& type, const char* name, bool* known = 0);

    /**
     * Retrieve the type name
     * @return Type name
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Retrieve the type name
     * @return Type name
     */
    virtual const String& toString() const;

protected:
    /**
     * Constructor
     * @param name Type name
     */
    MatchingItemCustomFactory(const char* name);

    /**
     * Destructor
     */
    virtual ~MatchingItemCustomFactory();

    /**
     * Build an item
     * @param name Item name
     * @return MatchingItemCustom pointer, NULL on failure
     */
    virtual MatchingItemCustom* customBuild(const char* name) = 0;

private:
    String m_name;
    bool m_optimize;
};

/**
 * This class holds matching item load parameters
 * @short Matching item load parameters
 */
class YATE_API MatchingItemLoad : public String
{
    YCLASS(MatchingItemLoad,String)
public:
    /**
     * Load behaviour flags
     */
    enum LoadFlags {
	IgnoreFailed           = 0x00000001, // Ignore failed to load items. Continue loading
	LoadInvalid            = 0x00000002, // Load matching item(s) with invalid value
	LoadItemId             = 0x00000004, // Load matching item's id parameter
	RexValidate            = 0x00000010, // Validate regular expressions
	XPathValidate          = 0x00000020, // Validate XPath expressions
	RandomValidate         = 0x00000040, // Validate Random values
	NoOptimize             = 0x00000100, // Do not optmize lists
	NameReqList            = 0x00010000, // Name is required for lists
	NameReqSimple          = 0x00020000, // Request name of the parameter to match for simple items
	ListAny                = 0x01000000, // Default 'any' (not match all) parameter value. Used in load()
	RexBasic               = 0x02000000, // Load basic POSIX regular expressions
	RexDetect              = 0x04000000, // Detect regular expression if value starts with ^
                                             // Used when loading from parameters list
	RexDetectNegated       = 0x08000000, // Detect negated regular expression if value ends with ^
                                             // Used when loading from parameters list
	PrivateFlag            = 0x100000000,// Private flag to be used for derived classes
	Validate = RexValidate | XPathValidate | RandomValidate,
	DefaultFlags = RexDetect | RexDetectNegated | NameReqSimple,
    };

    /**
     * Item flags
     */
    enum ItemFlags {
	ItemNegated         = 0x00000001,// Matching is negated
	ItemCaseInsensitive = 0x00000002,// Matching is case insensitive
	ItemBasic           = 0x00000004,// Matching regexp: use basic POSIX
	ItemAny             = 0x00000008,// Matching list: match any
	ItemMissingMatch    = 0x00000010,// Item missing: match
	ItemMissingNoMatch  = 0x00000020,// Item missing: no match
	ItemPrivateFlag     = 0x00010000,// Private flag to be used for custom matching
    };

    /**
     * Constructor
     * @param flags Optional load flags
     * @param name Optional name
     */
    inline MatchingItemLoad(uint64_t flags = DefaultFlags, const char* name = 0)
	: String(name), m_flags(flags),
	m_ignoreName(0), m_allowName(0), m_ignoreType(0), m_allowType(0),
	m_dbg(0), m_warnLevel(0)
	{}

    /**
     * Load matching item(s)
     * @param params Parameters list
     * @param error Optional pointer to error string
     * @param prefix Optional parameters prefix
     * @return MatchingItemBase pointer, NULL if none loaded
     */
    MatchingItemBase* loadItem(const NamedList& params, String* error = 0,
	const char* prefix = 0) const;

    /**
     * Load matching item(s) from XML description
     * @param str XML string
     * @param error Optional pointer to error string
     * @return MatchingItemBase pointer, NULL if none loaded
     */
    MatchingItemBase* loadXml(const String& str, String* error = 0) const;

    /**
     * Load matching item(s) from XML description
     * @param xml XmlElement object
     * @param error Optional pointer to error string
     * @return MatchingItemBase pointer, NULL if none loaded
     */
    MatchingItemBase* loadXml(const XmlElement* xml, String* error = 0) const;

    /**
     * Load matching item(s)
     * Parameters prefix is formed from 'prefix' + ':' + our_string + ':' + 'suffix'
     * @param params Parameters list
     * @param error Optional pointer to error string
     * @param prefix Optional parameters prefix
     * @param suffix Optional parameters suffix
     * @return MatchingItemBase pointer, NULL if none loaded
     */
    MatchingItemBase* load(const NamedList& params, String* error = 0,
	const char* prefix = 0, const char* suffix = 0) const;

    /**
     * Check flag(s)
     * @param mask Mask to check
     * @return True if set, false otherwise
     */
    inline bool flagSet(uint64_t mask) const
	{ return 0 != (m_flags & mask); }

    /**
     * Retrieve load flags dictionary
     * @return TokenDict64 pointer
     */
    static const TokenDict64* loadFlags();

    /**
     * Retrieve item flags dictionary
     * @return TokenDict pointer
     */
    static const TokenDict* itemFlags();

    uint64_t m_flags;                    // Load flags
    const ObjList* m_ignoreName;         // List of matching name(s) to ignore (blacklist)
    const ObjList* m_allowName;          // List of matching name(s) to allow (whitelist)
    const ObjList* m_ignoreType;         // List of matching type(s) to ignore (blacklist)
    const ObjList* m_allowType;          // List of matching type(s) to allow (whitelist)
    DebugEnabler* m_dbg;                 // Optional pointer to DebugEnabler to be used
    int m_warnLevel;                     // Warn debug level for ignore name/type or other errors

private:
    MatchingItemBase* miLoadRetList(ObjList& items, const char* name, bool matchAll,
	bool negated = false, int missingMatch = 0, const char* id = 0) const;
    MatchingItemBase* miLoadItem(bool& fatal, String* error, void* data,
	const char* loc, const String& pName, const NamedList* params = 0, const char* prefix = 0,
	const XmlElement* xml = 0, const String* xmlFrag = 0, bool forceFail = false) const;
    MatchingItemBase* miLoadItemParam(const String& name, bool& fatal, String* error,
	const char* loc, const String& pName, const NamedList* params, const char* prefix,
	const XmlElement* xml) const;
};

/**
 * This class holds dump matching item parameters
 * @short Matching item dump parameters
 */
class YATE_API MatchingItemDump : public String
{
    YCLASS(MatchingItemDump,String)
public:
    /**
     * Dump behaviour flags
     */
    enum DumpFlags {
	ForceInitialListDesc = 0x00000001, // Force list description at depth 0 description
	DumpXmlStr           = 0x00000002, // Used in dump(): dump string in xml format
	IgnoreName           = 0x00000004, // Used in dump(): ignore item name
	DumpIgnoreEmpty      = 0x00000008, // Used in dumpXml() and dumpList(): ignore (do not dump) empty values
	DumpItemFlagsName    = 0x00000010, // Used in dump(): dump item flag names instead of configured replacements
	DumpItemId           = 0x00000020, // Used in dump(): dump item flag id
	DumpCustomFull       = 0x00000040, // Used in dump(): dump custom item using its full dump
	DumpPrivate          = 0x01000000  // Private flags not used by us 
    };

    /**
     * Constructor
     * @param params Optional parameters
     * @param name Optional name
     */
    MatchingItemDump(const NamedList* params = 0, const char* name = 0);

    /**
     * Initialize dumper data
     * @param params Parameters list
     */
    virtual void init(const NamedList& params);

    /**
     * Dump an item's value related data
     * @param mi Item to dump
     * @param buf Destination buffer
     * @return Destination buffer reference
     */
    virtual String& dumpValue(const MatchingItemBase* mi, String& buf) const;

    /**
     * Dump an item's value string
     * @param mi Item to dump
     * @param buf Destination buffer
     * @param typeInfo Add item type related data
     * @return Destination buffer reference
     */
    virtual String& dumpValueStr(const MatchingItemBase* mi, String& buf,
	bool typeInfo = false) const;

    /**
     * Dump an item
     * @param mi Item to dump
     * @param buf Destination buffer
     * @param indent Indent for each item (line)
     * @param addIndent Indent to be added when depth advances
     * @param depth Re-enter depth
     * @return Destination buffer reference
     */
    virtual String& dump(const MatchingItemBase* mi, String& buf,
	const String& indent = String::empty(), const String& addIndent = String::empty(),
	unsigned int depth = 0) const;

    /**
     * Dump an item in XML format
     * @param mi Item to dump
     * @param depth Re-enter depth
     * @return XmlElement pointer, NULL if not dumped
     */
    virtual XmlElement* dumpXml(const MatchingItemBase* mi, unsigned int depth = 0) const;

    /**
     * Dump an item in XML format. Add to parent if given
     * @param parent Element parent
     * @param mi Item to dump
     * @param childTag Optional tag for intermedia child
     * @param depth Re-enter depth
     * @return XmlElement pointer (already added to parent), NULL if not dumped
     */
    XmlElement* dumpXmlChild(XmlElement* parent, const MatchingItemBase* mi,
	const char* childTag = 0, unsigned int depth = 0) const;

    /**
     * Dump an item to parameters list
     * @param mi Item to store
     * @param list Destination list
     * @param prefix Optional parameters prefix
     * @param depth Re-enter depth
     * @param id Id to use when storing item
     * @return Number of saved item(s)
     */
    virtual unsigned int dumpList(const MatchingItemBase* mi, NamedList& list,
	const char* prefix = 0, unsigned int depth = 0, const char* id = 0) const;

    /**
     * Dump an item
     * @param mi Item to dump
     * @param buf Destination buffer
     * @param indent Indent for each item (line)
     * @param addIndent Indent to be added when depth advances
     * @param params Optional dumper parameters
     * @return Destination buffer reference
     */
    static inline String& dumpItem(const MatchingItemBase* mi, String& buf,
	const String& indent = String::empty(), const String& addIndent = String::empty(),
	const NamedList* params = 0) {
	    MatchingItemDump tmp(params);
	    return tmp.dump(mi,buf,indent,addIndent);
	}

    /**
     * Dump an item
     * @param mi Item to dump
     * @param params Optional dumper parameters parameters
     * @return XmlElement pointer, NULL if not dumped
     */
    static inline XmlElement* dumpItemXml(const MatchingItemBase* mi, const NamedList* params = 0) {
	    MatchingItemDump tmp(params);
	    return tmp.dumpXml(mi);
	}

    /**
     * Dump an item to parameters list
     * @param mi Item to store
     * @param list Destination list
     * @param prefix Optional parameters prefix
     * @param params Optional dumper parameters
     * @return Number of saved item(s)
     */
    static inline unsigned int dumpItemList(const MatchingItemBase* mi, NamedList& list,
	const char* prefix = 0, const NamedList* params = 0) {
	    MatchingItemDump tmp(params);
	    return tmp.dumpList(mi,list,prefix);
	}

    /**
     * Retrieve the dump flags dictionary
     * @return TokenDict pointer
     */
    static const TokenDict* flagsDict();

    unsigned int m_flags;                // Dump flags
    char m_rexEnclose;                   // Regexp enclose char
    char m_strEnclose;                   // String enclose char
    String m_nameValueSep;               // Separator to be set between name and value
    char m_negated;                      // Negated match value
    bool m_missingMatch;                 // Dump missing parameter match value
    char m_caseInsentive;                // Case insensitive match value
    char m_regexpBasic;                  // Basic POSIX regexp value
    char m_regexpExtended;               // Extended POSIX regexp value
};

}; // namespace TelEngine

#endif /* __YATEMATCHINGITEM_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
