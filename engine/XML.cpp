/**
 * XML.cpp
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

#include <yatexml.h>
#include <string.h>

using namespace TelEngine;


const String XmlElement::s_ns = "xmlns";
const String XmlElement::s_nsPrefix = "xmlns:";
static const String s_type("type");
static const String s_name("name");


// Return a replacement char for the given string
static inline char replace(const char* str, const XmlEscape* esc)
{
    if (!str)
	return 0;
    if (esc) {
	for (; esc->value; esc++)
	    if (!::strcmp(str,esc->value))
		return esc->replace;
    }
    return 0;
}

// Return a replacement string for the given char
static inline const char* replace(char replace, const XmlEscape* esc)
{
    if (esc) {
	for (; esc->value; esc++)
	    if (replace == esc->replace)
		return esc->value;
    }
    return 0;
}

// XmlEscape a string or replace it if found in a list of restrictions
static inline void addAuth(String& buf, const String& comp, const String& value,
    bool esc, const String* auth)
{
    if (auth) {
	for (; !auth->null(); auth++)
	    if (*auth == comp) {
		buf << "***";
		return;
	    }
    }
    if (esc)
	XmlSaxParser::escape(buf,value);
    else
	buf << value;
}


/*
 * XmlSaxParser
 */
const TokenDict XmlSaxParser::s_errorString[] = {
	{"No error",                      NoError},
	{"Error",                         Unknown},
	{"Not well formed",               NotWellFormed},
	{"I/O error",                     IOError},
	{"Error parsing Element",         ElementParse},
	{"Failed to read Element name",   ReadElementName},
	{"Bad element name",              InvalidElementName},
	{"Error reading Attributes",      ReadingAttributes},
	{"Error reading end tag",         ReadingEndTag},
	{"Error parsing Comment",         CommentParse},
	{"Error parsing Declaration",     DeclarationParse},
	{"Error parsing Definition",      DefinitionParse},
	{"Error parsing CDATA",           CDataParse},
	{"Incomplete",                    Incomplete},
	{"Invalid encoding",              InvalidEncoding},
	{"Unsupported encoding",          UnsupportedEncoding},
	{"Unsupported version",           UnsupportedVersion},
	{0,0}
};

const XmlEscape XmlSaxParser::s_escape[] = {
	{"&lt;",   '<'},
	{"&gt;",   '>'},
	{"&amp;",  '&'},
	{"&quot;", '\"'},
	{"&apos;", '\''},
	{0,0}
};


XmlSaxParser::XmlSaxParser(const char* name)
    : m_offset(0), m_row(1), m_column(1), m_error(NoError),
    m_parsed(""), m_unparsed(None)
{
    debugName(name);
}

XmlSaxParser::~XmlSaxParser()
{
}

// Parse a given string
bool XmlSaxParser::parse(const char* text)
{
    if (TelEngine::null(text))
	return m_error == NoError;
#ifdef XDEBUG
    String tmp;
    m_parsed.dump(tmp," ");
    if (tmp)
	tmp = " parsed=" + tmp;
    XDebug(this,DebugAll,"XmlSaxParser::parse(%s) unparsed=%u%s buf=%s [%p]",
	text,unparsed(),tmp.safe(),m_buf.safe(),this);
#endif
    char car;
    setError(NoError);
    String auxData;
    m_buf << text;
    if (m_buf.lenUtf8() == -1) {
	//FIXME this should not be here in case we have a different encoding
	DDebug(this,DebugNote,"Request to parse invalid utf-8 data [%p]",this);
	return setError(Incomplete);
    }
    if (unparsed()) {
	if (unparsed() != Text) {
	    if (!auxParse())
		return false;
	}
	else
	    auxData = m_parsed;
	resetParsed();
	setUnparsed(None);
    }
    unsigned int len = 0;
    while (m_buf.at(len) && !error()) {
	car = m_buf.at(len);
	if (car != '<' ) { // We have a new child check what it is
	    if (car == '>' || !checkDataChar(car)) {
		Debug(this,DebugNote,"XML text contains unescaped '%c' character [%p]",
		    car,this);
		return setError(Unknown);
	    }
	    len++; // Append xml Text
	    continue;
	}
	if (len > 0) {
	    auxData << m_buf.substr(0,len);
	}
	if (auxData.c_str()) {  // We have an end of tag or another child is riseing
	    if (!processText(auxData))
		return false;
	    m_buf = m_buf.substr(len);
	    len = 0;
	    auxData = "";
	}
	char auxCar = m_buf.at(1);
	if (!auxCar)
	    return setError(Incomplete);
	if (auxCar == '?') {
	    m_buf = m_buf.substr(2);
	    if (!parseInstruction())
		return false;
	    continue;
	}
	if (auxCar == '!') {
	    m_buf = m_buf.substr(2);
	    if (!parseSpecial())
		return false;
	    continue;
	}
	if (auxCar == '/') {
	    m_buf = m_buf.substr(2);
	    if (!parseEndTag())
		return false;
	    continue;
	}
	// If we are here mens that we have a element
	// process an xml element
	m_buf = m_buf.substr(1);
	if (!parseElement())
	    return false;
    }
    // Incomplete text
    if ((unparsed() == None || unparsed() == Text) && (auxData || m_buf)) {
	if (!auxData)
	    m_parsed.assign(m_buf);
	else {
	    auxData << m_buf;
	    m_parsed.assign(auxData);
	}
	m_buf = "";
	setUnparsed(Text);
	return setError(Incomplete);
    }
    if (error()) {
	DDebug(this,DebugNote,"Got error while parsing %s [%p]",getError(),this);
	return false;
    }
    m_buf = "";
    resetParsed();
    setUnparsed(None);
    return true;
}

// Process incomplete text
bool XmlSaxParser::completeText()
{
    if (!completed() || unparsed() != Text || error() != Incomplete)
	return error() == NoError;
    String tmp = m_parsed;
    return processText(tmp);
}

// Parse an unfinished xml object
bool XmlSaxParser::auxParse()
{
    switch (unparsed()) {
	case Element:
	    return parseElement();
	case CData:
	    return parseCData();
	case Comment:
	    return parseComment();
	case Declaration:
	    return parseDeclaration();
	case Instruction:
	    return parseInstruction();
	case EndTag:
	    return parseEndTag();
	case Special:
	    return parseSpecial();
	default:
	    return false;
    }
}

// Set the error code and destroys a child if error code is not NoError
bool XmlSaxParser::setError(Error error, XmlChild* child)
{
    m_error = error;
    if (child && error)
	TelEngine::destruct(child);
    return m_error == XmlSaxParser::NoError;
}

// Parse an endtag form the main buffer
bool XmlSaxParser::parseEndTag()
{
    bool aux = false;
    String* name = extractName(aux);
    // We don't check aux flag because we don't look for attributes here
    if (!name) {
	if (error() && error() == Incomplete)
	    setUnparsed(EndTag);
	return false;
    }
    if (!aux || m_buf.at(0) == '/') { // The end tag has attributes or contains / char at the end of name
	setError(ReadingEndTag);
	Debug(this,DebugNote,"Got bad end tag </%s/> [%p]",name->c_str(),this);
	setUnparsed(EndTag);
	m_buf = *name + m_buf;
	return false;
    }
    resetError();
    endElement(*name);
    if (error()) {
	setUnparsed(EndTag);
	m_buf = *name + ">";
	TelEngine::destruct(name);
	return false;
    }
    m_buf = m_buf.substr(1);
    TelEngine::destruct(name);
    return true;
}

// Parse an instruction form the main buffer
bool XmlSaxParser::parseInstruction()
{
    XDebug(this,DebugAll,"XmlSaxParser::parseInstruction() buf len=%u [%p]",m_buf.length(),this);
    setUnparsed(Instruction);
    if (!m_buf.c_str())
	return setError(Incomplete);
    // extract the name
    String name;
    char c;
    int len = 0;
    if (!m_parsed) {
	bool nameComplete = false;
	bool endDecl = false;
	while (0 != (c = m_buf.at(len))) {
	    nameComplete = blank(c);
	    if (!nameComplete) {
		// Check for instruction end: '?>'
		if (c == '?') {
		    char next = m_buf.at(len + 1);
		    if (!next)
			return setError(Incomplete);
		    if (next == '>') {
			nameComplete = endDecl = true;
			break;
		    }
		}
		if (checkNameCharacter(c)) {
		    len++;
		    continue;
		}
		Debug(this,DebugNote,"Instruction name contains bad character '%c' [%p]",c,this);
		return setError(InvalidElementName);
	    }
	    // Blank found
	    if (len)
	        break;
	    Debug(this,DebugNote,"Instruction with empty name [%p]",this);
	    return setError(InvalidElementName);
	}
	if (!len) {
	    if (!endDecl)
		return setError(Incomplete);
	    // Remove instruction end from buffer
	    m_buf = m_buf.substr(2);
	    Debug(this,DebugNote,"Instruction with empty name [%p]",this);
	    return setError(InvalidElementName);
	}
	if (!nameComplete)
	    return setError(Incomplete);
	name = m_buf.substr(0,len);
	m_buf = m_buf.substr(!endDecl ? len : len + 2);
	if (name == YSTRING("xml")) {
	    if (!endDecl)
		return parseDeclaration();
	    resetParsed();
	    resetError();
	    setUnparsed(None);
	    gotDeclaration(NamedList::empty());
	    return error() == NoError;
	}
	// Instruction name can't be xml case insensitive
	if (name.length() == 3 && name.startsWith("xml",false,true)) {
	    Debug(this,DebugNote,"Instruction name '%s' reserved [%p]",name.c_str(),this);
	    return setError(InvalidElementName);
	}
    }
    else {
	name = m_parsed;
	resetParsed();
    }
    // Retrieve instruction content
    skipBlanks();
    len = 0;
    while (0 != (c = m_buf.at(len))) {
	if (c != '?') {
	    if (c == 0x0c) {
		setError(Unknown);
		Debug(this,DebugNote,"Xml instruction with unaccepted character '%c' [%p]",
		    c,this);
		return false;
	    }
	    len++;
	    continue;
	}
	char ch = m_buf.at(len + 1);
	if (!ch)
	    break;
	if (ch == '>') { // end of instruction
	    NamedString inst(name,m_buf.substr(0,len));
	    // Parsed instruction: remove instruction end from buffer and reset parsed
	    m_buf = m_buf.substr(len + 2);
	    resetParsed();
	    resetError();
	    setUnparsed(None);
	    gotProcessing(inst);
	    return error() == NoError;
	}
	len ++;
    }
    // If we are here mens that text has reach his bounds is an error or we need to receive more data
    m_parsed.assign(name);
    return setError(Incomplete);
}

// Parse a declaration form the main buffer
bool XmlSaxParser::parseDeclaration()
{
    XDebug(this,DebugAll,"XmlSaxParser::parseDeclaration() buf len=%u [%p]",m_buf.length(),this);
    setUnparsed(Declaration);
    if (!m_buf.c_str())
	return setError(Incomplete);
    NamedList dc("xml");
    if (m_parsed.count()) {
	dc.copyParams(m_parsed);
	resetParsed();
    }
    char c;
    skipBlanks();
    int len = 0;
    while (m_buf.at(len)) {
	c = m_buf.at(len);
	if (c != '?') {
	    skipBlanks();
	    NamedString* s = getAttribute();
	    if (!s) {
		if (error() == Incomplete)
		    m_parsed = dc;
		return false;
	    }
	    len = 0;
	    if (dc.getParam(s->name())) {
		Debug(this,DebugNote,"Duplicate attribute '%s' in declaration [%p]",
		    s->name().c_str(),this);
		TelEngine::destruct(s);
		return setError(DeclarationParse);
	    }
	    dc.addParam(s);
	    char ch = m_buf.at(len);
	    if (ch && !blank(ch) && ch != '?') {
		Debug(this,DebugNote,"No blanks between attributes in declaration [%p]",this);
		return setError(DeclarationParse);
	    }
	    skipBlanks();
	    continue;
	}
	if (!m_buf.at(++len))
	    break;
	char ch = m_buf.at(len);
	if (ch == '>') { // end of declaration
	    // Parsed declaration: remove declaration end from buffer and reset parsed
	    resetError();
	    resetParsed();
	    setUnparsed(None);
	    m_buf = m_buf.substr(len + 1);
	    gotDeclaration(dc);
	    return error() == NoError;
	}
	Debug(this,DebugNote,"Invalid declaration ending char '%c' [%p]",ch,this);
	return setError(DeclarationParse);
    }
    m_parsed.copyParams(dc);
    setError(Incomplete);
    return false;
}

// Parse a CData section form the main buffer
bool XmlSaxParser::parseCData()
{
    if (!m_buf.c_str()) {
	setUnparsed(CData);
	setError(Incomplete);
	return false;
    }
    String cdata = "";
    if (m_parsed.c_str()) {
	cdata = m_parsed;
	resetParsed();
    }
    char c;
    int len = 0;
    while (m_buf.at(len)) {
	c = m_buf.at(len);
	if (c != ']') {
	    len ++;
	    continue;
	}
	if (m_buf.substr(++len,2) == "]>") { // End of CData section
	    cdata += m_buf.substr(0,len - 1);
	    resetError();
	    gotCdata(cdata);
	    resetParsed();
	    if (error())
		return false;
	    m_buf = m_buf.substr(len + 2);
	    return true;
	}
    }
    cdata += m_buf;
    m_buf = "";
    setUnparsed(CData);
    int length = cdata.length();
    m_buf << cdata.substr(length - 2);
    if (length > 1)
	m_parsed.assign(cdata.substr(0,length - 2));
    setError(Incomplete);
    return false;
}

// Helper method to classify the Xml objects starting with "<!" sequence
bool XmlSaxParser::parseSpecial()
{
    if (m_buf.length() < 2) {
	setUnparsed(Special);
	return setError(Incomplete);
    }
    if (m_buf.startsWith("--")) {
	m_buf = m_buf.substr(2);
	if (!parseComment())
	    return false;
	return true;
    }
    if (m_buf.length() < 7) {
	setUnparsed(Special);
	return setError(Incomplete);
    }
    if (m_buf.startsWith("[CDATA[")) {
	m_buf = m_buf.substr(7);
	if (!parseCData())
	    return false;
	return true;
    }
    if (m_buf.startsWith("DOCTYPE")) {
	m_buf = m_buf.substr(7);
	if (!parseDoctype())
	    return false;
	return true;
    }
    Debug(this,DebugNote,"Can't parse unknown special starting with '%s' [%p]",
	m_buf.c_str(),this);
    setError(Unknown);
    return false;
}


// Extract from the given buffer an comment and check if is valid
bool XmlSaxParser::parseComment()
{
    String comment;
    if (m_parsed.c_str()) {
	comment = m_parsed;
	resetParsed();
    }
    char c;
    int len = 0;
    while (m_buf.at(len)) {
	c = m_buf.at(len);
	if (c != '-') {
	    if (c == 0x0c) {
		Debug(this,DebugNote,"Xml comment with unaccepted character '%c' [%p]",c,this);
		return setError(NotWellFormed);
	    }
	    len++;
	    continue;
	}
	if (m_buf.at(len + 1) == '-' && m_buf.at(len + 2) == '>') { // End of comment
	    comment << m_buf.substr(0,len);
	    m_buf = m_buf.substr(len + 3);
#ifdef DEBUG
	    if (comment.at(0) == '-' || comment.at(comment.length() - 1) == '-')
		DDebug(this,DebugInfo,"Comment starts or ends with '-' character [%p]",this);
	    if (comment.find("--") >= 0)
		DDebug(this,DebugInfo,"Comment contains '--' char sequence [%p]",this);
#endif
	    gotComment(comment);
	    resetParsed();
	    // The comment can apear anywhere sow SaxParser never
	    // sets an error when receive a comment
	    return true;
	}
	len++;
    }
    // If we are here we haven't detect the end of comment
    comment << m_buf;
    int length = comment.length();
    // Keep the last 2 charaters in buffer because if the input buffer ends
    // between "--" and ">"
    m_buf = comment.substr(length - 2);
    setUnparsed(Comment);
    if (length > 1)
	m_parsed.assign(comment.substr(0,length - 2));
    return setError(Incomplete);
}

// Parse an element form the main buffer
bool XmlSaxParser::parseElement()
{
    XDebug(this,DebugAll,"XmlSaxParser::parseElement() buf len=%u [%p]",m_buf.length(),this);
    if (!m_buf.c_str()) {
	setUnparsed(Element);
	return setError(Incomplete);
    }
    bool empty = false;
    if (!m_parsed.c_str()) {
	String* name = extractName(empty);
	if (!name) {
	    if (error() == Incomplete)
		setUnparsed(Element);
	    return false;
	}
#ifdef XML_STRICT
	// http://www.w3.org/TR/REC-xml/
	// Names starting with 'xml' (case insensitive) are reserved
	if (name->startsWith("xml",false,true)) {
	    Debug(this,DebugNote,"Element tag starts with 'xml' [%p]",this);
	    TelEngine::destruct(name);
	    return setError(ReadElementName);
	}
#endif
	m_parsed.assign(*name);
	TelEngine::destruct(name);
    }
    if (empty) { // empty flag means that the element does not have attributes
	// check if the element is empty
	bool aux = m_buf.at(0) == '/';
	if (!processElement(m_parsed,aux))
	    return false;
	if (aux)
	    m_buf = m_buf.substr(2); // go back where we were
	else
	    m_buf = m_buf.substr(1); // go back where we were
	return true;
    }
    char c;
    skipBlanks();
    int len = 0;
    while (m_buf.at(len)) {
	c = m_buf.at(len);
	if (c == '/' || c == '>') { // end of element declaration
	    if (c == '>') {
		if (!processElement(m_parsed,false))
		    return false;
		m_buf = m_buf.substr(1);
		return true;
	    }
	    if (!m_buf.at(++len))
		break;
	    char ch = m_buf.at(len);
	    if (ch != '>') {
		Debug(this,DebugNote,"Element attribute name contains '/' character [%p]",this);
		return setError(ReadingAttributes);
	    }
	    if (!processElement(m_parsed,true))
		return false;
	    m_buf = m_buf.substr(len + 1);
	    return true;
	}
	NamedString* ns = getAttribute();
	if (!ns) { // Attribute is invalid
	    if (error() == Incomplete)
		break;
	    return false;
	}
	if (m_parsed.getParam(ns->name())) {
	    Debug(this,DebugNote,"Duplicate attribute '%s' [%p]",ns->name().c_str(),this);
	    TelEngine::destruct(ns);
	    return setError(NotWellFormed);
	}
	XDebug(this,DebugAll,"Parser adding attribute %s='%s' to '%s' [%p]",
	    ns->name().c_str(),ns->c_str(),m_parsed.c_str(),this);
	m_parsed.setParam(ns);
	char ch = m_buf.at(len);
	if (ch && !blank(ch) && (ch != '/' && ch != '>')) {
	    Debug(this,DebugNote,"Element without blanks between attributes [%p]",this);
	    return setError(NotWellFormed);
	}
	skipBlanks();
    }
    setUnparsed(Element);
    return setError(Incomplete);
}

// Parse a doctype form the main buffer
bool XmlSaxParser::parseDoctype()
{
    if (!m_buf.c_str()) {
	setUnparsed(Doctype);
	setError(Incomplete);
	return false;
    }
    unsigned int len = 0;
    skipBlanks();
    while (m_buf.at(len) && !blank(m_buf.at(len)))
	len++;
    // Use a while() to break to the end
    while (m_buf.at(len)) {
	while (m_buf.at(len) && blank(m_buf.at(len)))
	    len++;
	if (len >= m_buf.length())
	   break;
	if (m_buf[len++] == '[') {
	    while (len < m_buf.length()) {
		if (m_buf[len] != ']') {
		    len ++;
		    continue;
		}
		if (m_buf.at(++len) != '>')
		    continue;
		gotDoctype(m_buf.substr(0,len));
		resetParsed();
		m_buf = m_buf.substr(len + 1);
		return true;
	    }
	    break;
	}
	while (len < m_buf.length()) {
	    if (m_buf[len] != '>') {
		len++;
		continue;
	    }
	    gotDoctype(m_buf.substr(0,len));
	    resetParsed();
	    m_buf = m_buf.substr(len + 1);
	    return true;
	}
	break;
    }
    setUnparsed(Doctype);
    return setError(Incomplete);
}

// Extract the name of tag
String* XmlSaxParser::extractName(bool& empty)
{
    skipBlanks();
    unsigned int len = 0;
    bool ok = false;
    empty = false;
    while (len < m_buf.length()) {
	char c = m_buf[len];
	if (blank(c)) {
	    if (checkFirstNameCharacter(m_buf[0])) {
		ok = true;
		break;
	    }
	    Debug(this,DebugNote,"Element tag starting with invalid char %c [%p]",
		m_buf[0],this);
	    setError(ReadElementName);
	    return 0;
	}
	if (c == '/' || c == '>') { // end of element declaration
	    if (c == '>') {
		if (checkFirstNameCharacter(m_buf[0])) {
		    empty = true;
		    ok = true;
		    break;
		}
		Debug(this,DebugNote,"Element tag starting with invalid char %c [%p]",
		    m_buf[0],this);
		setError(ReadElementName);
		return 0;
	    }
	    char ch = m_buf.at(len + 1);
	    if (!ch)
		break;
	    if (ch != '>') {
		Debug(this,DebugNote,"Element tag contains '/' character [%p]",this);
		setError(ReadElementName);
		return 0;
	    }
	    if (checkFirstNameCharacter(m_buf[0])) {
		empty = true;
		ok = true;
		break;
	    }
	    Debug(this,DebugNote,"Element tag starting with invalid char %c [%p]",
		m_buf[0],this);
	    setError(ReadElementName);
	    return 0;
	}
	if (checkNameCharacter(c))
	    len++;
	else {
	    Debug(this,DebugNote,"Element tag contains invalid char %c [%p]",c,this);
	    setError(ReadElementName);
	    return 0;
	}
    }
    if (ok) {
	String* name = new String(m_buf.substr(0,len));
	m_buf = m_buf.substr(len);
	if (!empty) {
	    skipBlanks();
	    empty = (m_buf && m_buf[0] == '>') ||
		(m_buf.length() > 1 && m_buf[0] == '/' && m_buf[1] == '>');
	}
	return name;
    }
    setError(Incomplete);
    return 0;
}

// Extract an attribute
NamedString* XmlSaxParser::getAttribute()
{
    String name = "";
    skipBlanks();
    char c,sep = 0;
    unsigned int len = 0;

    while (len < m_buf.length()) { // Circle until we find attribute value startup character (["]|['])
	c = m_buf[len];
	if (blank(c) || c == '=') {
	    if (!name.c_str())
		name = m_buf.substr(0,len);
	    len++;
	    continue;
	}
	if (!name.c_str()) {
	    if (!checkNameCharacter(c)) {
		Debug(this,DebugNote,"Attribute name contains %c character [%p]",c,this);
		setError(ReadingAttributes);
		return 0;
	    }
	    len++;
	    continue;
	}
	if (c != '\'' && c != '\"') {
	    Debug(this,DebugNote,"Unenclosed attribute value [%p]",this);
	    setError(ReadingAttributes);
	    return 0;
	}
	sep = c;
	break;
    }

    if (!sep) {
	setError(Incomplete);
	return 0;
    }
    if (!checkFirstNameCharacter(name[0])) {
	Debug(this,DebugNote,"Attribute name starting with bad character %c [%p]",
	    name.at(0),this);
	setError(ReadingAttributes);
	return 0;
    }
    int pos = ++len;

    while (len < m_buf.length()) {
	c = m_buf[len];
	if (c != sep && !badCharacter(c)) {
	    len ++;
	    continue;
	}
	if (badCharacter(c)) {
	    Debug(this,DebugNote,"Attribute value with unescaped character '%c' [%p]",
		c,this);
	    setError(ReadingAttributes);
	    return 0;
	}
	NamedString* ns = new NamedString(name,m_buf.substr(pos,len - pos));
	m_buf = m_buf.substr(len + 1);
	// End of attribute value
	unEscape(*ns);
	if (error()) {
	    TelEngine::destruct(ns);
	    return 0;
	}
	return ns;
    }

    setError(Incomplete);
    return 0;
}

// Reset this parser
void XmlSaxParser::reset()
{
    m_offset = 0;
    m_row = 1;
    m_column = 1;
    m_error = NoError;
    m_buf.clear();
    resetParsed();
    m_unparsed = None;
}

// Check if the given character is in the range allowed for an xml char
bool XmlSaxParser::checkDataChar(unsigned char c)
{
    return  c == 0x9 || c == 0xA || c == 0xD || (c >= 0x20);
}

// Remove blank characters from the beginning of the buffer
void XmlSaxParser::skipBlanks()
{
    unsigned int len = 0;
    while (len < m_buf.length() && blank(m_buf[len]))
	len++;
    if (len != 0)
	m_buf = m_buf.substr(len);
}

// Obtain a char from an ascii decimal char declaration
inline unsigned char getDec(String& dec)
{
    if (dec.length() > 6) {
	DDebug(DebugNote,"Decimal number '%s' too long",dec.c_str());
	return 0;
    }
    int num = dec.substr(2,dec.length() - 3).toInteger(-1);
    if (num > 0 && num < 256)
	return num;
    DDebug(DebugNote,"Invalid decimal number '%s'",dec.c_str());
    return 0;
}

// Unescape the given text
void XmlSaxParser::unEscape(String& text)
{
    String error;
    if (unEscape(text,&error))
	return;
    Debug(this,DebugNote,"Unescape. %s [%p]",error.c_str(),this);
    setError(NotWellFormed);
}

// Check if a given string is a valid xml tag name
bool XmlSaxParser::validTag(const String& buf)
{
    if (!(buf && checkFirstNameCharacter(buf[0])))
	return false;
    for (unsigned int i = 1; i < buf.length(); i++)
	if (!checkNameCharacter(buf[i]))
	    return false;
    return true;
}

// XmlEscape the given text
String& XmlSaxParser::escape(String& buf, const String& text)
{
    const char* str = text.c_str();
    if (!str)
	return buf;
    const char* accum = str;
    unsigned int aLen = 0;
    while (*str) {
	const char* rep = replace(*str++,XmlSaxParser::s_escape);
	if (!rep) {
	    aLen++;
	    continue;
	}
	if (aLen)
	    buf.append(accum,aLen);
	accum = str;
	aLen = 0;
	buf += rep;
    }
    if (aLen)
	return buf.append(accum,aLen);
    return buf;
}

// Unescape the given text
bool XmlSaxParser::unEscape(String& text, const char* str, unsigned int n, String* error,
    bool inText, bool* escFound)
{
    if (escFound)
	*escFound = false;
    if (!(str && n))
	return true;
    inText = inText && (str != text.c_str());
    String tmp;
    String& buf = inText ? text : tmp;
    String aux = "&";
    unsigned int len = 0;
    int found = -1;
    for (unsigned int i = 0; i < n; ++i) {
	if (str[len] == '&' && found < 0) {
	    found = len++;
	    continue;
	}
	if (found < 0) {
	    len++;
	    continue;
	}
	if (str[len] == '&') {
	    if (error)
		*error = "Duplicate '&' in expression";
	    return false;
	}
	if (str[len] != ';')
	    len++;
	else { // We have a candidate for escaping
	    len += 1; // Append ';' character
	    String aux(str + found,len - found);
	    char re = 0;
	    if (aux.startsWith("&#")) {
		if (aux.at(2) == 'x') {
		    if (aux.length() > 4 && aux.length() <= 12) {
			int esc = aux.substr(3,aux.length() - 4).toInteger(-1,16);
			if (esc != -1) {
			    UChar uc(esc);
			    buf.append(str,found) << uc.c_str();
			    str += len;
			    len = 0;
			    found = -1;
			    continue;
			}
		    }
		} else
		    re = getDec(aux);
	    }
	    if (re == '&') {
		if (str[len] == '#') {
		    aux.assign(str + len,4);
		    if (aux == "#60;") {
			re = '<';
			len += 4;
		    }
		    if (aux == "#38;") {
			re = '&';
			len += 4;
		    }
		}
	    }
	    else if (!re)
		re = replace(aux,s_escape);
	    if (!re) {
		if (error)
		    error->printf("No replacement found for '%s'",(String(str + found,len - found)).c_str());
		return false;
	    }
	    if (escFound)
		*escFound = true;
	    // We have an valid escape character
	    buf.append(str,found) << re;
	    str += len;
	    len = 0;
	    found = -1;
	}
    }
    if (found >= 0) {
	if (error)
	    *error = "Unexpected end of expression";
	return false;
    }
    if (inText) {
	if (len)
	    buf.append(str,len);
    }
    else if (len) {
	if (str != text.c_str()) {
	    buf.append(str,len);
	    text = buf;
	}
    }
    else
	text = buf;
    return true;
}

// Calls gotElement(). Reset parsed if ok
bool XmlSaxParser::processElement(NamedList& list, bool empty)
{
    gotElement(list,empty);
    if (error() == XmlSaxParser::NoError) {
	resetParsed();
	return true;
    }
    return false;
}

// Calls gotText() and reset parsed on success
bool XmlSaxParser::processText(String& text)
{
    resetError();
    unEscape(text);
    if (!error())
	gotText(text);
    else
	setUnparsed(Text);
    if (!error()) {
	resetParsed();
	setUnparsed(None);
    }
    return error() == NoError;
}


/*
 * XmlDomPareser
 */
XmlDomParser::XmlDomParser(const char* name, bool fragment)
    : XmlSaxParser(name),
    m_current(0), m_data(0), m_ownData(true)
{
    if (fragment)
	m_data = new XmlFragment();
    else
	m_data = new XmlDocument();
}

XmlDomParser::XmlDomParser(XmlParent* fragment, bool takeOwnership)
    : m_current(0), m_data(0), m_ownData(takeOwnership)
{
    m_data = fragment;
}

XmlDomParser::~XmlDomParser()
{
    if (m_ownData) {
	reset();
	if (m_data)
	    delete m_data;
    }
}

// Create a new xml comment and append it in the xml three
void XmlDomParser::gotComment(const String& text)
{
    XmlComment* com = new XmlComment(text);
    if (m_current)
	setError(m_current->addChild(com),com);
    else
	setError(m_data->addChild(com),com);

}

// Append a new xml doctype to main xml parent
void XmlDomParser::gotDoctype(const String& doc)
{
    m_data->addChild(new XmlDoctype(doc));
}

// TODO implement it see what to do
void XmlDomParser::gotProcessing(const NamedString& instr)
{
    DDebug(this,DebugStub,"gotProcessing(%s=%s) not implemented [%p]",
	instr.name().c_str(),instr.safe(),this);
}

// Create a new xml declaration, verifies the version and encoding
// and append it in the main xml parent
void XmlDomParser::gotDeclaration(const NamedList& decl)
{
    if (m_current) {
	setError(DeclarationParse);
	Debug(this,DebugNote,"Received declaration inside element bounds [%p]",this);
	return;
    }
    Error err = NoError;
    while (true) {
	String* version = decl.getParam("version");
	if (version) {
	    int ver = version->substr(0,version->find('.')).toInteger();
	    if (ver != 1) {
		err = UnsupportedVersion;
		break;
	    }
	}
	String* enc = decl.getParam("encoding");
	if (enc && !(*enc &= "utf-8")) {
	    err = UnsupportedEncoding;
	    break;
	}
	break;
    }
    if (err == NoError) {
	XmlDeclaration* dec = new XmlDeclaration(decl);
	setError(m_data->addChild(dec),dec);
    }
    else {
	setError(err);
	Debug(this,DebugNote,
	    "Received unacceptable declaration version='%s' encoding='%s' error '%s' [%p]",
	    decl.getValue("version"),decl.getValue("encoding"),getError(),this);
    }
}

// Create a new xml text and append it in the xml tree
void XmlDomParser::gotText(const String& text)
{
    XmlText* tet = new XmlText(text);
    if (m_current)
	m_current->addChild(tet);
    else
	setError(m_data->addChild(tet),tet);
}

// Create a new xml Cdata and append it in the xml tree
void XmlDomParser::gotCdata(const String& data)
{
    XmlCData* cdata = new XmlCData(data);
    if (!m_current) {
	if (m_data->document()) {
	    Debug(this,DebugNote,"Document got CDATA outside element [%p]",this);
	    setError(NotWellFormed);
	    TelEngine::destruct(cdata);
	    return;
	}
	setError(m_data->addChild(cdata),cdata);
	return;
    }
    setError(m_current->addChild(cdata),cdata);
}

// Create a new xml element and append it in the xml tree
void XmlDomParser::gotElement(const NamedList& elem, bool empty)
{
    XmlElement* element = 0;
    if (!m_current) {
	// If we don't have curent element menns that the main fragment
	// should hold it
	element = new XmlElement(elem,empty);
	setError(m_data->addChild(element),element);
	if (!empty && error() == XmlSaxParser::NoError)
	    m_current = element;
    }
    else {
	if (empty) {
	    element = new XmlElement(elem,empty);
	    setError(m_current->addChild(element),element);
	}
	else {
	    element = new XmlElement(elem,empty,m_current);
	    setError(m_current->addChild(element),element);
	    if (error() == XmlSaxParser::NoError)
		m_current = element;
	}
    }
}

// Verify if is the closeing tag for the current element
// Complete th current element and make current the current parent
void XmlDomParser::endElement(const String& name)
{
    if (!m_current) {
	setError(ReadingEndTag);
	Debug(this,DebugNote,"Unexpected element end tag %s [%p]",name.c_str(),this);
	return;
    }
    if (m_current->getName() != name) {
	setError(ReadingEndTag);
	Debug(this,DebugNote,
	    "Received end element for %s, but the expected one is for %s [%p]",
	    name.c_str(),m_current->getName().c_str(),this);
	return;
    }
    m_current->setCompleted();
    XDebug(this,DebugInfo,"End element for %s [%p]",m_current->getName().c_str(),this);
    m_current = static_cast<XmlElement*>(m_current->getParent());
}

// Reset this parser
void XmlDomParser::reset()
{
    m_data->reset();
    m_current = 0;
    XmlSaxParser::reset();
}


/*
 * XmlDeclaration
 */
// Create a new XmlDeclaration from version and encoding
XmlDeclaration::XmlDeclaration(const char* version, const char* enc)
    : m_declaration("")
{
    XDebug(DebugAll,"XmlDeclaration::XmlDeclaration(%s,%s) [%p]",version,enc,this);
    if (!TelEngine::null(version))
	m_declaration.addParam("version",version);
    if (!TelEngine::null(enc))
	m_declaration.addParam("encoding",enc);
}

// Constructor
XmlDeclaration::XmlDeclaration(const NamedList& decl)
    : m_declaration(decl)
{
    XDebug(DebugAll,"XmlDeclaration::XmlDeclaration(%s) [%p]",m_declaration.c_str(),this);
}

// Copy Constructor
XmlDeclaration::XmlDeclaration(const XmlDeclaration& decl)
    : m_declaration(decl.getDec())
{
}

// Destructor
XmlDeclaration::~XmlDeclaration()
{
    XDebug(DebugAll,"XmlDeclaration::~XmlDeclaration() ( %s| %p )",
	m_declaration.c_str(),this);
}

// Create a String from this Xml Declaration
void XmlDeclaration::toString(String& dump, bool esc) const
{
    dump << "<?" << "xml";
    int n = m_declaration.count();
    for (int i = 0;i < n;i ++) {
	NamedString* ns = m_declaration.getParam(i);
	if (!ns)
	    continue;
	dump += " ";
	dump += ns->name();
	dump << "=\"";
	if (esc)
	    XmlSaxParser::escape(dump,*ns);
	else
	    dump += *ns;
	dump << "\"";
    }
    dump << "?>";
}


/*
 * XmlFragment
 */
// Constructor
XmlFragment::XmlFragment()
    : m_list()
{
    XDebug(DebugAll,"XmlFragment::XmlFragment() ( %p )",this);
}

// Copy Constructor
XmlFragment::XmlFragment(const XmlFragment& orig)
{
    copy(orig);
}

// Destructor
XmlFragment::~XmlFragment()
{
    m_list.clear();
    XDebug(DebugAll,"XmlFragment::~XmlFragment() ( %p )",this);
}

// Reset. Clear children list
void XmlFragment::reset()
{
    m_list.clear();
}

// Append a new child
XmlSaxParser::Error XmlFragment::addChild(XmlChild* child)
{
    if (child)
	m_list.append(child);
    return XmlSaxParser::NoError;
}

// Remove the first XmlElement from list and returns it if completed
XmlElement* XmlFragment::popElement()
{
    for (ObjList* o = m_list.skipNull(); o; o = o->skipNext()) {
	XmlChild* c = static_cast<XmlChild*>(o->get());
	XmlElement* x = c->xmlElement();
	if (x) {
	     if (x->completed()) {
		o->remove(false);
		return x;
	     }
	     return 0;
	}
    }
    return 0;
}

// Remove a child
XmlChild* XmlFragment::removeChild(XmlChild* child, bool delObj)
{
    XmlChild* ch = static_cast<XmlChild*>(m_list.remove(child,delObj));
    if (ch && ch->xmlElement())
	ch->xmlElement()->setParent(0);
    return ch;
}

// Copy other fragment into this one
void XmlFragment::copy(const XmlFragment& other, XmlParent* parent)
{
    for (ObjList* o = other.getChildren().skipNull(); o; o = o->skipNext()) {
	XmlChild* ch = static_cast<XmlChild*>(o->get());
	if (ch->xmlElement())
	    ch = new XmlElement(*(ch->xmlElement()));
	else if (ch->xmlCData())
	    ch = new XmlCData(*(ch->xmlCData()));
	else if (ch->xmlText())
	    ch = new XmlText(*(ch->xmlText()));
	else if (ch->xmlComment())
	    ch = new XmlComment(*(ch->xmlComment()));
	else if (ch->xmlDeclaration())
	    ch = new XmlDeclaration(*(ch->xmlDeclaration()));
	else if (ch->xmlDoctype())
	    ch = new XmlDoctype(*(ch->xmlDoctype()));
	else
	    continue;
	ch->setParent(parent);
	addChild(ch);
    }
}

// Create a String from this XmlFragment
void XmlFragment::toString(String& dump, bool escape, const String& indent,
    const String& origIndent, bool completeOnly, const String* auth,
    const XmlElement* parent) const
{
    ObjList* ob = m_list.skipNull();
    if (!ob)
	return;
    ObjList buffers;
    for (;ob;ob = ob->skipNext()) {
	String* s = new String;
	XmlChild* obj = static_cast<XmlChild*>(ob->get());
	if (obj->xmlElement())
	    obj->xmlElement()->toString(*s,escape,indent,origIndent,completeOnly,auth);
	else if (obj->xmlText())
	    obj->xmlText()->toString(*s,escape,indent,auth,parent);
	else if (obj->xmlCData())
	    obj->xmlCData()->toString(*s,indent);
	else if (obj->xmlComment())
	    obj->xmlComment()->toString(*s,indent);
	else if (obj->xmlDeclaration())
	    obj->xmlDeclaration()->toString(*s,escape);
	else if (obj->xmlDoctype())
	    obj->xmlDoctype()->toString(*s,origIndent);
	else
	    Debug(DebugStub,"XmlFragment::toString() unhandled element type!");
	if (!TelEngine::null(s))
	    buffers.append(s);
	else
	    TelEngine::destruct(s);
    }
    dump.append(buffers);
}

XmlElement* XmlFragment::getElement(ObjList*& lst, const String* name, const String* ns,
    bool noPrefix)
{
    for (; lst; lst = lst->skipNext()) {
	XmlElement* x = (static_cast<XmlChild*>(lst->get()))->xmlElement();
	if (!(x && x->completed()))
	    continue;
	if (name || ns) {
	    if (!ns) {
		// Compare tag
		if (noPrefix) {
		    if (*name != x->unprefixedTag())
			continue;
		}
		else if (*name != x->toString())
		    continue;
	    }
	    else if (name) {
		// Compare tag and namespace
		const String* t = 0;
		const String* n = 0;
		if (!(x->getTag(t,n) && *t == *name && n && *n == *ns))
		    continue;
	    }
	    else {
		// Compare namespace
		const String* n = x->xmlns();
		if (!n || *n != *ns)
		    continue;
	    }
	}
	lst = lst->skipNext();
	return x;
    }
    return 0;
}

// Replaces all ${paramname} in fragment's children with the corresponding parameters
void XmlFragment::replaceParams(const NamedList& params)
{
    for (ObjList* o = m_list.skipNull(); o; o = o->skipNext())
	static_cast<XmlChild*>(o->get())->replaceParams(params);
}


/*
 * XmlDocument
 */
// Constructor
XmlDocument::XmlDocument()
    : m_root(0)
{

}

// Destructor
XmlDocument::~XmlDocument()
{
    reset();
}

// Append a new child to this document
// Set the root to an XML element if not already set. If we already have a completed root
//  the element will be added to the root, otherwise an error will be returned.
// If we don't have a root non xml elements (other then text) will be added the list
//  of elements before root
XmlSaxParser::Error XmlDocument::addChild(XmlChild* child)
{
    if (!child)
	return XmlSaxParser::NoError;

    XmlElement* element = child->xmlElement();
    if (!m_root) {
	if (element) {
	    m_root = element;
	    return XmlSaxParser::NoError;
	}
	XmlDeclaration* decl = child->xmlDeclaration();
	if (decl && declaration()) {
	    DDebug(DebugNote,"XmlDocument. Request to add duplicate declaration [%p]",this);
	    return XmlSaxParser::NotWellFormed;
	}
	// Text outside root: ignore empty, raise error otherwise
	XmlText* text = child->xmlText();
	if (text) {
	    if (text->onlySpaces()) {
		m_beforeRoot.addChild(text);
		return XmlSaxParser::NoError;
	    }
	    Debug(DebugNote,"XmlDocument. Got text outside element [%p]",this);
	    return XmlSaxParser::NotWellFormed;
	}
	return m_beforeRoot.addChild(child);
    }
    // We have a root
    if (element) {
	if (m_root->completed())
	    return m_root->addChild(child);
	DDebug(DebugStub,"XmlDocument. Request to add xml element child to incomplete root [%p]",this);
	return XmlSaxParser::NotWellFormed;
    }
    if ((child->xmlText() && child->xmlText()->onlySpaces()) || child->xmlComment())
	return m_afterRoot.addChild(child);
    // TODO: check what xml we can add after the root or if we can add
    //  anything after an incomplete root
    Debug(DebugStub,"XmlDocument. Request to add non element while having a root [%p]",this);
    return XmlSaxParser::NotWellFormed;
}

// Retrieve the document declaration
XmlDeclaration* XmlDocument::declaration() const
{
    for (ObjList* o = m_beforeRoot.getChildren().skipNull(); o; o = o->skipNext()) {
	XmlDeclaration* d = (static_cast<XmlChild*>(o->get()))->xmlDeclaration();
	if (d)
	    return d;
    }
    return 0;
}

// Obtain root element completed ot not
XmlElement* XmlDocument::root(bool completed) const
{
    return (m_root && (m_root->completed() || !completed)) ? m_root : 0;
}

void XmlDocument::toString(String& dump, bool escape, const String& indent, const String& origIndent) const
{
    m_beforeRoot.toString(dump,escape,indent,origIndent);
    if (m_root) {
	dump << origIndent;
	m_root->toString(dump,escape,indent,origIndent);
    }
    m_afterRoot.toString(dump,escape,indent,origIndent);
}

// Reset this XmlDocument. Destroys root and clear the others xml objects
void XmlDocument::reset()
{
    TelEngine::destruct(m_root);
    m_beforeRoot.clearChildren();
    m_afterRoot.clearChildren();
    m_file.clear();
}

// Load this document from data stream and parse it
XmlSaxParser::Error XmlDocument::read(Stream& in, int* error)
{
    XmlDomParser parser(static_cast<XmlParent*>(this),false);
    char buf[8096];
    bool start = true;
    while (true) {
	int rd = in.readData(buf,sizeof(buf) - 1);
	if (rd > 0) {
	    buf[rd] = 0;
	    const char* text = buf;
	    if (start) {
		String::stripBOM(text);
		start = false;
	    }
	    if (parser.parse(text) || parser.error() == XmlSaxParser::Incomplete)
		continue;
	    break;
	}
	break;
    }
    parser.completeText();
    if (parser.error() != XmlSaxParser::NoError) {
	DDebug(DebugNote,"XmlDocument error loading stream. Parser error %d '%s' [%p]",
	    parser.error(),parser.getError(),this);
	return parser.error();
    }
    if (in.error()) {
	if (error)
	    *error = in.error();
#ifdef DEBUG
	String tmp;
	Thread::errorString(tmp,in.error());
	Debug(DebugNote,"XmlDocument error loading stream. I/O error %d '%s' [%p]",
	    in.error(),tmp.c_str(),this);
#endif
	return XmlSaxParser::IOError;
    }
    return XmlSaxParser::NoError;
}

// Write this document to a data stream
int XmlDocument::write(Stream& out, bool escape, const String& indent,
    const String& origIndent, bool completeOnly) const
{
    String dump;
    m_beforeRoot.toString(dump,escape,indent,origIndent);
    if (m_root)
	m_root->toString(dump,escape,indent,origIndent,completeOnly);
    m_afterRoot.toString(dump,escape,indent,origIndent);
    return out.writeData(dump);
}

// Load a file and parse it
XmlSaxParser::Error XmlDocument::loadFile(const char* file, int* error)
{
    reset();
    if (TelEngine::null(file))
	return XmlSaxParser::NoError;
    m_file = file;
    File f;
    if (f.openPath(file))
	return read(f,error);
    if (error)
	*error = f.error();
#ifdef DEBUG
    String tmp;
    Thread::errorString(tmp,f.error());
    Debug(DebugNote,"XmlDocument error opening file '%s': %d '%s' [%p]",
	file,f.error(),tmp.c_str(),this);
#endif
    return XmlSaxParser::IOError;
}

// Save this xml document in a file
int XmlDocument::saveFile(const char* file, bool esc, const String& indent,
    bool completeOnly, const char* eoln) const
{
    if (!file)
	file = m_file;
    if (!file)
	return 0;
    File f;
    int err = 0;
    if (f.openPath(file,true,false,true,false)) {
	String eol(eoln);
	if (eoln && !eol)
	    eol = "\r\n";
	write(f,esc,eol,indent,completeOnly);
	err = f.error();
	// Add an empty line
	if (err >= 0 && eol)
	    f.writeData((void*)eol.c_str(),eol.length());
    }
    else
	err = f.error();
    if (!err) {
	XDebug(DebugAll,"XmlDocument saved file '%s' [%p]",file,this);
	return 0;
    }
#ifdef DEBUG
    String error;
    Thread::errorString(error,err);
    Debug(DebugNote,"Error saving XmlDocument to file '%s'. %d '%s' [%p]",
	file,err,error.c_str(),this);
#endif
    return f.error();
}

// Replaces all ${paramname} in document's components with the corresponding parameters
void XmlDocument::replaceParams(const NamedList& params)
{
    if (m_root)
        m_root->replaceParams(params);
    m_beforeRoot.replaceParams(params);
    m_afterRoot.replaceParams(params);
}


/*
 * XmlChild
 */
XmlChild::XmlChild()
{
}


/*
 * XmlElement
 */
XmlElement::XmlElement(const NamedList& element, bool empty, XmlParent* parent)
    : m_element(element), m_prefixed(0),
    m_parent(0), m_inheritedNs(0),
    m_empty(empty), m_complete(empty)
{
    XDebug(DebugAll,"XmlElement::XmlElement(%s,%u,%p) [%p]",
	element.c_str(),empty,parent,this);
    setPrefixed();
    setParent(parent);
}

// Copy constructor
XmlElement::XmlElement(const XmlElement& el)
    : m_element(el.getElement()), m_prefixed(0),
    m_parent(0), m_inheritedNs(0),
    m_empty(el.empty()), m_complete(el.completed())
{
    setPrefixed();
    setInheritedNs(&el,true);
    m_children.copy(el.m_children,this);
}

// Create an empty xml element
XmlElement::XmlElement(const char* name, bool complete)
    : m_element(name), m_prefixed(0),
    m_parent(0), m_inheritedNs(0),
    m_empty(true), m_complete(complete)
{
    setPrefixed();
    XDebug(DebugAll,"XmlElement::XmlElement(%s) [%p]",
	m_element.c_str(),this);
}

// Create a new element with a text child
XmlElement::XmlElement(const char* name, const char* value, bool complete)
    : m_element(name), m_prefixed(0),
    m_parent(0), m_inheritedNs(0),
    m_empty(true), m_complete(complete)
{
    setPrefixed();
    addText(value);
    XDebug(DebugAll,"XmlElement::XmlElement(%s) [%p]",
	m_element.c_str(),this);
}

// Destructor
XmlElement::~XmlElement()
{
    setInheritedNs();
    TelEngine::destruct(m_prefixed);
    XDebug(DebugAll,"XmlElement::~XmlElement() ( %s| %p )",
	m_element.c_str(),this);
}

// Set element's unprefixed tag, don't change namespace prefix
void XmlElement::setUnprefixedTag(const String& s)
{
    if (!s || s == unprefixedTag())
	return;
    if (TelEngine::null(m_prefixed))
	m_element.assign(s);
    else
	m_element.assign(*m_prefixed + ":" + s);
    setPrefixed();
}

// Set inherited namespaces from a given element. Reset them anyway
void XmlElement::setInheritedNs(const XmlElement* xml, bool inherit)
{
    XDebug(DebugAll,"XmlElement(%s) setInheritedNs(%p,%s) [%p]",
	tag(),xml,String::boolText(inherit),this);
    TelEngine::destruct(m_inheritedNs);
    if (!xml)
	return;
    addInheritedNs(xml->attributes());
    if (!inherit)
	return;
    XmlElement* p = xml->parent();
    bool xmlAdd = (p == 0);
    while (p) {
	addInheritedNs(p->attributes());
	const NamedList* i = p->inheritedNs();
	p = p->parent();
	if (!p && i)
	    addInheritedNs(*i);
    }
    if (xmlAdd && xml->inheritedNs())
	addInheritedNs(*xml->inheritedNs());
}

// Add inherited namespaces from a list
void XmlElement::addInheritedNs(const NamedList& list)
{
    XDebug(DebugAll,"XmlElement(%s) addInheritedNs(%s) [%p]",tag(),list.c_str(),this);
    unsigned int n = list.count();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = list.getParam(i);
	if (!(ns && isXmlns(ns->name())))
	    continue;
	// Avoid adding already overridden namespaces
	if (m_element.getParam(ns->name()))
	    continue;
	if (m_inheritedNs && m_inheritedNs->getParam(ns->name()))
	    continue;
	// TODO: Check if attribute names are unique after adding the namespace
	//       See http://www.w3.org/TR/xml-names/ Section 6.3
	if (!m_inheritedNs)
	    m_inheritedNs = new NamedList("");
	XDebug(DebugAll,"XmlElement(%s) adding inherited %s=%s [%p]",
	    tag(),ns->name().c_str(),ns->c_str(),this);
	m_inheritedNs->addParam(ns->name(),*ns);
    }
}

// Obtain the first text of this xml element
const String& XmlElement::getText() const
{
    const XmlText* txt = 0;
    for (ObjList* ob = getChildren().skipNull(); ob && !txt; ob = ob->skipNext())
	txt = (static_cast<XmlChild*>(ob->get()))->xmlText();
    return txt ? txt->getText() : String::empty();
}

XmlChild* XmlElement::getFirstChild()
{
    if (!m_children.getChildren().skipNull())
	return 0;
    return static_cast<XmlChild*>(m_children.getChildren().skipNull()->get());
}

XmlText* XmlElement::setText(const char* text)
{
    XmlText* txt = 0;
    for (ObjList* o = getChildren().skipNull(); o; o = o->skipNext()) {
	txt = (static_cast<XmlChild*>(o->get()))->xmlText();
	if (txt)
	    break;
    }
    if (txt) {
	if (!text)
	    return static_cast<XmlText*>(removeChild(txt));
	txt->setText(text);
    }
    else if (text) {
	txt = new XmlText(text);
	addChild(txt);
    }
    return txt;
}

// Add a text child
void XmlElement::addText(const char* text)
{
    if (!TelEngine::null(text))
	addChild(new XmlText(text));
}

// Retrieve the element's tag (without prefix) and namespace
bool XmlElement::getTag(const String*& tag, const String*& ns) const
{
    if (!m_prefixed) {
	tag = &m_element;
	ns = xmlns();
	return true;
    }
    // Prefixed element
    tag = &m_prefixed->name();
    ns = xmlns();
    return ns != 0;
}

// Append a new child
XmlSaxParser::Error XmlElement::addChild(XmlChild* child)
{
    if (!child)
	return XmlSaxParser::NoError;
    // TODO: Check if a child element's attribute names are unique in the new context
    //       See http://www.w3.org/TR/xml-names/ Section 6.3
    XmlSaxParser::Error err = m_children.addChild(child);
    if (err == XmlSaxParser::NoError)
	child->setParent(this);
    return err;
}

// Remove a child
XmlChild* XmlElement::removeChild(XmlChild* child, bool delObj)
{
    return m_children.removeChild(child,delObj);
}

// Set this element's parent. Update inherited namespaces
void XmlElement::setParent(XmlParent* parent)
{
    XDebug(DebugAll,"XmlElement(%s) setParent(%p) element=%s [%p]",
	tag(),parent,String::boolText(parent != 0),this);
    if (m_parent && m_parent->element()) {
	// Reset inherited namespaces if the new parent is an element
	// Otherwise set them from the old parent
	if (parent && parent->element())
	    setInheritedNs(0);
	else
	    setInheritedNs(m_parent->element());
    }
    m_parent = parent;
}

// Obtain a string from this xml element
void XmlElement::toString(String& dump, bool esc, const String& indent,
    const String& origIndent, bool completeOnly, const String* auth) const
{
    XDebug(DebugAll,"XmlElement(%s) toString(%u,%s,%s,%u,%p) complete=%u [%p]",
	tag(),esc,indent.c_str(),origIndent.c_str(),completeOnly,auth,m_complete,this);
    if (!m_complete && completeOnly)
	return;
    String auxDump;
    auxDump << indent << "<" << m_element;
    int n = m_element.count();
    for (int i = 0; i < n; i++) {
	NamedString* ns = m_element.getParam(i);
	if (!ns)
	    continue;
	auxDump << " " << ns->name() << "=\"";
	addAuth(auxDump,ns->name(),*ns,esc,auth);
	auxDump << "\"";
    }
    int m = getChildren().count();
    if (m_complete && !m)
	auxDump << "/";
    auxDump << ">";
    if (m) {
	// Avoid adding text on new line when text is the only child
	XmlText* text = 0;
	if (m == 1)
	    text = static_cast<XmlChild*>(getChildren().skipNull()->get())->xmlText();
	if (!text)
	    m_children.toString(auxDump,esc,indent + origIndent,origIndent,completeOnly,auth,this);
	else
	    text->toString(auxDump,esc,String::empty(),auth,this);
	if (m_complete)
	    auxDump << (!text ? indent : String::empty()) << "</" << getName() << ">";
    }
    dump << auxDump;
}

// Copy element attributes to a list of parameters
unsigned int XmlElement::copyAttributes(NamedList& list, const String& prefix) const
{
    unsigned int copy = 0;
    unsigned int n = m_element.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = m_element.getParam(i);
	if (!(ns && ns->name()))
	    continue;
	list.addParam(prefix + ns->name(),*ns);
	copy++;
    }
    return copy;
}

void XmlElement::setAttributes(NamedList& list, const String& prefix, bool skipPrefix)
{
    if (prefix)
	m_element.copySubParams(list,prefix,skipPrefix);
    else
	m_element.copyParams(list);
}

// Retrieve a namespace attribute. Search in parent or inherited for it
String* XmlElement::xmlnsAttribute(const String& name) const
{
    String* tmp = getAttribute(name);
    if (tmp)
	return tmp;
    XmlElement* p = parent();
    if (p)
	return p->xmlnsAttribute(name);
    return m_inheritedNs ? m_inheritedNs->getParam(name) : 0;
}

// Set the element's namespace
bool XmlElement::setXmlns(const String& name, bool addAttr, const String& value)
{
    const String* cmp = name ? &name : &s_ns;
    XDebug(DebugAll,"XmlElement(%s)::setXmlns(%s,%u,%s) [%p]",
	tag(),cmp->c_str(),addAttr,value.c_str(),this);
    if (*cmp == s_ns) {
	if (m_prefixed) {
	    m_element.assign(m_prefixed->name());
	    setPrefixed();
	    // TODO: remove children and attributes prefixes
	}
    }
    else if (!m_prefixed || *m_prefixed != cmp) {
	if (!m_prefixed)
	    m_element.assign(*cmp + ":" + tag());
	else
	    m_element.assign(*cmp + ":" + m_prefixed->name());
	setPrefixed();
	// TODO: change children and attributes prefixes
    }
    if (!(addAttr && value))
	return true;
    String attr;
    if (*cmp == s_ns)
	attr = s_ns;
    else
	attr << s_nsPrefix << *cmp;
    NamedString* ns = m_element.getParam(attr);
    if (!ns && m_inheritedNs && m_inheritedNs->getParam(attr))
	m_inheritedNs->clearParam(attr);
    // TODO: Check if attribute names are unique after adding the namespace
    //       See http://www.w3.org/TR/xml-names/ Section 6.3
    if (!ns)
	m_element.addParam(attr,value);
    else
	*ns = value;
    return true;
}

// Replaces all ${paramname} in element's attributes and children with the
//  corresponding parameters
void XmlElement::replaceParams(const NamedList& params)
{
    m_children.replaceParams(params);
    for (ObjList* o = m_element.paramList()->skipNull(); o; o = o->skipNext())
	params.replaceParams(*static_cast<String*>(o->get()));
}

// Build an XML element from a list parameter
XmlElement* XmlElement::param2xml(NamedString* param, const String& tag, bool copyXml)
{
    if (!(param && param->name() && tag))
	return 0;
    XmlElement* xml = new XmlElement(tag);
    xml->setAttribute(s_name,param->name());
    xml->setAttributeValid(YSTRING("value"),*param);
    NamedPointer* np = YOBJECT(NamedPointer,param);
    if (!(np && np->userData()))
	return xml;
    DataBlock* db = YOBJECT(DataBlock,np->userData());
    if (db) {
	xml->setAttribute(s_type,"DataBlock");
	Base64 b(db->data(),db->length(),false);
	String tmp;
	b.encode(tmp);
	b.clear(false);
	xml->addText(tmp);
	return xml;
    }
    XmlElement* element = YOBJECT(XmlElement,np->userData());
    if (element) {
	xml->setAttribute(s_type,"XmlElement");
	if (!copyXml) {
	    np->takeData();
	    xml->addChild(element);
	}
	else
	    xml->addChild(new XmlElement(*element));
	return xml;
    }
    NamedList* list = YOBJECT(NamedList,np->userData());
    if (list) {
	xml->setAttribute(s_type,"NamedList");
	xml->addText(list->c_str());
	unsigned int n = list->length();
	for (unsigned int i = 0; i < n; i++)
	    xml->addChild(param2xml(list->getParam(i),tag,copyXml));
	return xml;
    }
    return xml;
}

// Build a list parameter from xml element
NamedString* XmlElement::xml2param(XmlElement* xml, const String* tag, bool copyXml)
{
    const char* name = xml ? xml->attribute(s_name) : 0;
    if (TelEngine::null(name))
	return 0;
    GenObject* gen = 0;
    String* type = xml->getAttribute(s_type);
    if (type) {
	if (*type == YSTRING("DataBlock")) {
	    gen = new DataBlock;
	    const String& text = xml->getText();
	    Base64 b((void*)text.c_str(),text.length(),false);
	    b.decode(*(static_cast<DataBlock*>(gen)));
	    b.clear(false);
	}
	else if (*type == YSTRING("XmlElement")) {
	    if (!copyXml)
		gen = xml->pop();
	    else {
		XmlElement* tmp = xml->findFirstChild();
		if (tmp)
		    gen = new XmlElement(*tmp);
	    }
	}
	else if (*type == YSTRING("NamedList")) {
	    gen = new NamedList(xml->getText());
	    xml2param(*(static_cast<NamedList*>(gen)),xml,tag,copyXml);
	}
	else
	    Debug(DebugStub,"XmlElement::xml2param: unhandled type=%s",type->c_str());
    }
    if (!gen)
	return new NamedString(name,xml->attribute(YSTRING("value")));
    return new NamedPointer(name,gen,xml->attribute(YSTRING("value")));
}

// Build and add list parameters from XML element children
void XmlElement::xml2param(NamedList& list, XmlElement* parent, const String* tag,
    bool copyXml)
{
    if (!parent)
	return;
    XmlElement* ch = 0;
    while (0 != (ch = parent->findNextChild(ch,tag))) {
	NamedString* ns = xml2param(ch,tag,copyXml);
	if (ns)
	    list.addParam(ns);
    }
}


/*
 * XmlComment
 */
// Constructor
XmlComment::XmlComment(const String& comm)
    : m_comment(comm)
{
    XDebug(DebugAll,"XmlComment::XmlComment(const String& comm) ( %s| %p )",
	m_comment.c_str(),this);
}

// Copy Constructor
XmlComment::XmlComment(const XmlComment& comm)
    : m_comment(comm.getComment())
{
}

// Destructor
XmlComment::~XmlComment()
{
    XDebug(DebugAll,"XmlComment::~XmlComment() ( %s| %p )",
	m_comment.c_str(),this);
}

// Obtain string representation of this xml comment
void XmlComment::toString(String& dump, const String& indent) const
{
    dump << indent << "<!--" << getComment() << "-->";
}


/*
 * XmlCData
 */
// Constructor
XmlCData::XmlCData(const String& data)
    : m_data(data)
{
    XDebug(DebugAll,"XmlCData::XmlCData(const String& data) ( %s| %p )",
	m_data.c_str(),this);
}

// Copy Constructor
XmlCData::XmlCData(const XmlCData& data)
    : m_data(data.getCData())
{
}

// Destructor
XmlCData::~XmlCData()
{
    XDebug(DebugAll,"XmlCData::~XmlCData() ( %s| %p )",
	m_data.c_str(),this);
}

// Obtain string representation of this xml Cdata
void XmlCData::toString(String& dump, const String& indent) const
{
    dump << indent << "<![CDATA[" << getCData() << "]]>";
}


/*
 * XmlText
 */
// Constructor
XmlText::XmlText(const String& text)
    : m_text(text)
{
    XDebug(DebugAll,"XmlText::XmlText(%s) [%p]",m_text.c_str(),this);
}

// Copy Constructor
XmlText::XmlText(const XmlText& text)
    : m_text(text.getText())
{
    XDebug(DebugAll,"XmlText::XmlText(%p,%s) [%p]",
	&text,TelEngine::c_safe(text.getText()),this);
}

// Destructor
XmlText::~XmlText()
{
    XDebug(DebugAll,"XmlText::~XmlText [%p]",this);
}

// Obtain string representation of this xml text
void XmlText::toString(String& dump, bool esc, const String& indent,
    const String* auth, const XmlElement* parent) const
{
    dump << indent;
    if (auth)
	addAuth(dump,parent ? parent->toString() : String::empty(),m_text,esc,auth);
    else if (esc)
        XmlSaxParser::escape(dump,m_text);
    else
	dump << m_text;
}

bool XmlText::onlySpaces()
{
    if (!m_text)
	return true;
    const char *s = m_text;
    unsigned int i = 0;
    for (;i < m_text.length();i++) {
	if (s[i] == ' ' || s[i] == '\t' || s[i] == '\v' || s[i] == '\f' || s[i] == '\r' || s[i] == '\n')
	    continue;
	return false;
    }
    return true;
}

// Replaces all ${paramname} in text with the corresponding parameters
void XmlText::replaceParams(const NamedList& params)
{
    params.replaceParams(m_text);
}


/*
 * XmlDoctype
 */
// Constructor
XmlDoctype::XmlDoctype(const String& doctype)
    : m_doctype(doctype)
{
    XDebug(DebugAll,"XmlDoctype::XmlDoctype(const String& doctype) ( %s| %p )",
	m_doctype.c_str(),this);
}

// Copy Constructor
XmlDoctype::XmlDoctype(const XmlDoctype& doctype)
    : m_doctype(doctype.getDoctype())
{
}

// Destructor
XmlDoctype::~XmlDoctype()
{
    XDebug(DebugAll,"XmlDoctype::~XmlDoctype() ( %s| %p )",
	m_doctype.c_str(),this);
}

// Obtain string representation of this xml doctype
void XmlDoctype::toString(String& dump, const String& indent) const
{
    dump << indent << "<!DOCTYPE " << m_doctype << ">";
}


/*
 * XPath
 */
// Maximul number of predicates in step
#ifndef XPATH_MAX_PREDICATES
#define XPATH_MAX_PREDICATES 5
#endif

#ifdef XDEBUG
#define XPATH_DEBUG_PARSE
#define XPATH_XDEBUG_PARSE
#define XPATH_DEBUG_FIND
#define XPATH_XDEBUG_FIND
#else
//#define XPATH_DEBUG_PARSE
//#define XPATH_XDEBUG_PARSE
//#define XPATH_DEBUG_FIND
//#define XPATH_XDEBUG_FIND
#endif

#ifdef XPATH_DEBUG_PARSE
#define XPathDebugParse Debug
#ifdef XPATH_XDEBUG_PARSE
#define XPathXDebugParse XPathDebugParse
#endif
#else
#define XPathDebugParse XDebug
#define XPathXDebugParse XDebug
#endif

#ifdef XPATH_DEBUG_FIND
#define XPathDebugFind Debug
#else
#define XPathDebugFind XDebug
#endif

static const TokenDict s_xpathErrors[] = {
    {"Empty item",               XPath::EEmptyItem},
    {"Syntax error",             XPath::ESyntax},
    {"Semantic error",           XPath::ESemantic},
    {"Value out of range",       XPath::ERange},
    {"Always empty result",      XPath::EEmptyResult},
    {0,0}
};

/**
 * Internal processing action
 */
enum XPathProcessAction {
    // Not matched
    XPathProcStop = -1,                  // Don't handle current element. Stop further handling
    XPathProcCont = 0,                   // Don't handle current element. Continue handling
    // Matched
    XPathProcHandleCont = 1,             // Handle curent element. Continue handling other elements in the same list
    XPathProcHandleStop = 2,             // Handle curent element. Stop handling other elements
};

static const TokenDict s_xpathProcAct[] = {
    {"Stop",           XPathProcStop},
    {"Continue",       XPathProcCont},
    {"HandleContinue", XPathProcHandleCont},
    {"HandleStop",     XPathProcHandleStop},
    {0,0}
};

namespace TelEngine {
class XPathParseData
{
public:
    enum Opc {
	OpcNone = 0,
	OpcEq,
	OpcNotEq,
    };
    inline XPathParseData(const char* b, unsigned int l, unsigned int flags)
	: strictParse(0 != (flags & XPath::StrictParse)),
	checkEmptyRes(0 == (flags & XPath::IgnoreEmptyResult)),
	checkXmlName(0 == (flags & XPath::NoXmlNameCheck)),
	m_step(0), m_buf(b), m_idx(0), m_length(b ? l : 0)
	{}

    inline unsigned int step() const
	{ return m_step; }
    // Retrieve original buffer length
    inline unsigned int origLength() const
	{ return m_length; }
    // Return index in original buffer
    inline unsigned int index() const
	{ return m_idx; }
    // Retrieve current buffer
    inline const char* c_str() const
	{ return m_buf; }
    // Retrieve current buffer length
    inline unsigned int length() const
	{ return (m_length > m_idx) ? m_length - m_idx : 0; }
    // Retrieve char in current buffer
    inline char crt() const
	{ return *m_buf; }
    // Retrieve char in current buffer at index (not original !)
    inline char at(unsigned int i) const
	{ return m_buf[i]; }
    // Advance 1 char in buffer
    inline void advance() { m_buf++; m_idx++; }
    // Advance 1 char in buffer. Return char before advance
    inline char getCrtAdvance() { char c = *m_buf++; m_idx++; return c; }
    // Advance n char(s) in buffer
    inline void skip(unsigned int n) { m_buf += n; m_idx += n; }
    // Advance step and buffer
    inline void advanceStep() { m_step++; if (haveData()) advance(); }
    inline bool haveData()
	{ return index() < origLength(); }
    inline bool ended()
	{ return index() >= origLength(); }
    inline bool isChar(char c)
	{ return c == crt(); }
    inline bool isSep()
	{ return isSep(crt()); }
    inline bool isBlank()
	{ return XmlSaxParser::blank(crt()); }
    inline bool isDigit()
	{ return isDigit(crt()); }
    inline bool isStepEnd()
	{ return ended() || isSep(); }
    inline bool isPredicatedEnd()
	{ return isStepEnd() || isChar(']'); }
    // Skip blanks in buffer. Return false if buffer ended
    inline bool skipBlanks() {
	    while (haveData() && isBlank())
		advance();
	    return haveData();
	}
    // Parse an operator. Return it and skip data if found
    inline int parseOperator() {
	    if (ended())
		return 0;
	    if (isChar('=')) {
		advance();
		return OpcEq;
	    }
	    if (isChar('!')) {
		if (length() < 2 || at(1) != '=')
		    return 0;
		skip(2);
		return OpcNotEq;
	    }
	    return 0;
	}
    // Parse a string literal
    inline const char* parseStringLiteral(const char*& start, unsigned int& n,
	char& delimiter, bool& esc, bool req = true) {
	    delimiter = isStringLiteralDelimiter(crt()) ? getCrtAdvance() : 0;
	    if (!delimiter)
		return req ? "Expecting string literal" : 0;
	    start = c_str();
	    n = 0;
	    for (; haveData(); ++n, advance()) {
		if (!isChar(delimiter))
		    continue;
		advance();
		// Check for escaped delimiter (char repeated)
		// See https://www.w3.org/TR/xpath-30/#prod-xpath30-EscapeQuot
		if (!isChar(delimiter))
		    return 0;
		n++;
		esc = true;
	    }
	    start = 0;
	    n = 0;
	    return "Unexpected end of data while parsing string literal";
	}
    // Parse an XML string (possible escapes!)
    inline const char* parseStringXml(const char*& start, unsigned int& n,
	char& delimiter, bool& esc, bool req = true) {
	    delimiter = isStringLiteralDelimiter(crt()) ? getCrtAdvance() : 0;
	    if (!delimiter)
		return req ? "Expecting string" : 0;
	    esc = true;
	    start = c_str();
	    n = 0;
	    for (; haveData(); ++n, advance()) {
		if (!isChar(delimiter))
		    continue;
		advance();
		return 0;
	    }
	    start = 0;
	    n = 0;
	    return "Unexpected end of data while parsing string";
	}
    // Check a buffer for a valid XML name if check is enabled
    // Return 0 on success, offending char otherwise
    inline char validXmlName(const char* b, unsigned int n) {
	    if (!(checkXmlName && b && n))
		return 0;
	    if (!XmlSaxParser::checkFirstNameCharacter(*b))
		return *b;
	    for (unsigned int i = 1; i < n; ++b, ++i)
		if (!XmlSaxParser::checkNameCharacter(*b))
		    return *b;
	    return 0;
	}
    // Check if current buffer char equals a given char
    inline bool operator==(char c) const
	{ return crt() == c; }
    // Check if current buffer char id different than a given char
    inline bool operator!=(char c) const
	{ return crt() != c; }

    static inline bool isSep(char c)
	{ return '/' == c; }
    static inline bool isDigit(char c)
	{ return c >= '0' && c <= '9'; }
    static inline bool isStringLiteralDelimiter(char c)
	{ return c == '\'' || c == '"'; }
    // Unescape an XPath string literal
    static inline bool unEscapeLiteral(String& buf,
	const char* b, unsigned int n, char esc, String* error) {
	    if (!(esc && b && n)) {
		buf.append(b,n);
		return true;
	    }
	    const char* accum = b;
	    unsigned int aLen = 0;
	    for (unsigned int i = 0; i < n; ++i) {
		aLen++;
		if (*b++ != esc)
		    continue;
		if (*b != esc) {
		    if (error)
			error->printf("Invalid char '%c' following escape",*b);;
		    return false;
		}
		// Append current buffer. Update accumulator. Skip second escape
		buf.append(accum,aLen);
		accum = ++b;
		aLen = 0;
		++i;
	    }
	    if (aLen)
		buf.append(accum,aLen);
	    return true;
	}
    // Escape an XPath string literal
    static inline String& escapeStringLiteral(String& buf, const String& str, char esc)
	{ return escapeStringLiteral(buf,str.c_str(),str.length(),esc); }
    // Escape an XPath string literal
    static inline String& escapeStringLiteral(String& buf,
	const char* b, unsigned int n, char esc) {
	    if (!(esc && b && n))
		return buf.append(b,n);
	    const char* accum = b;
	    unsigned int aLen = 0;
	    for (unsigned int i = 0; i < n; ++i) {
		aLen++;
		if (*b++ != esc)
		    continue;
		buf.append(accum,aLen);
		buf << esc;
		accum = b;
		aLen = 0;
	    }
	    if (aLen)
		return buf.append(accum,aLen);
	    return buf;
	}

    bool strictParse;                    // String buffer parsing
    bool checkEmptyRes;                  // Check if a path will always produce empty result
    bool checkXmlName;                   // Validate XML names

protected:
    unsigned int m_step;                 // Current path step index
    const char* m_buf;                   // Current buffer pointer
    unsigned int m_idx;                  // Current index in original buffer
    unsigned int m_length;               // Original buffer length
};
}; // namespace TelEngine

// Utility. Use when parsing to accumulate and store parsed item info
class XPathParseItem
{
public:
    inline XPathParseItem(const char* b = 0, unsigned int n = 0)
	: buf(b), len(n), delimiter(0), esc(false)
	{}
    inline const char* c_str() const
	{ return buf; }
    inline unsigned int length() const
	{ return len; }
    inline void advance()
	{ len++; }
    inline void set(const char* b = 0, unsigned int n = 0)
	{ buf = b; len = n; }
    inline String& appendTo(String& s) const
	{ return length() ? s.append((const char*)c_str(),length()) : s; }
    inline String& assignTo(String& s) const {
	    if (length())
		s.assign((const char*)c_str(),length());
	    else
		s.clear();
	    return s;
	}
    inline char operator[](unsigned int idx) const
	{ return c_str()[idx]; }
    inline const String& value() const
	{ return assignTo(m_value); }

    const char* buf;
    unsigned int len;
    char delimiter;
    bool esc;

protected:
    mutable String m_value;              // Temporary string
};

class XPathEscapedString
{
public:
    inline XPathEscapedString(String* str, bool literal = false)
	: m_delimiter(0), m_esc(false), m_literal(literal), m_str(str)
	{}
    inline void setLiteral(bool on)
	{ m_literal = on; }
    inline char delimiter() const
	{ return m_delimiter; }
    inline bool setString(const XPathParseItem& b, String* error)
	{ return setString(b.c_str(),b.length(),b.delimiter,b.esc,error); }
    inline bool setString(const char* b, unsigned int n, char delim, bool esc, String* error) {
	    if (m_str)
		return setString(*m_str,b,n,delim,esc,error);
	    if (error)
		*error = "Internal. No destination string";
	    return false;
	}
    inline bool setString(String& s, const char* b, unsigned int n, char delim, bool esc,
	String* error) {
	    m_delimiter = delim;
	    if (!m_delimiter)
		return true;
	    m_esc = esc;
	    if (!(esc && b && n))
		s.assign(b,n);
	    else if (!(m_literal ? XPathParseData::unEscapeLiteral(s,b,n,m_delimiter,error) :
		XmlSaxParser::unEscape(s,b,n,error,true,&m_esc))) {
		s.clear();
		return false;
	    }
	    return true;
	}
    inline String& dumpString(String& buf, bool escape = false) const
	{ return m_str ? dumpString(buf,*m_str,escape) : buf; }
    inline String& dumpString(String& buf, const String& str, bool escape = false) const {
	    if (!m_delimiter)
		return buf;
	    buf << m_delimiter;
	    if (!(escape && m_esc && str.length()))
		return buf << str << m_delimiter;
	    if (!m_literal)
		return XmlSaxParser::escape(buf,str) << m_delimiter;
	    return XPathParseData::escapeStringLiteral(buf,str,m_delimiter) << m_delimiter;
	}

protected:
    char m_delimiter;                    // Delimiter. Non 0 indicates data is used
    bool m_esc;                          // String contains escaped chars
    bool m_literal;                      // String is an XPATH literal
    String* m_str;                       // String to handle
};

// XPath string literal
class XPathString : public String, public XPathEscapedString
{
public:
    inline XPathString(bool literal = false)
	: XPathEscapedString(this,literal)
	{}
    inline String& dump(String& buf, bool escape = false) const
	{ return dumpString(buf,escape); }
};

// XPath regexp literal
class XPathRegexp : public Regexp, public XPathEscapedString
{
public:
    inline XPathRegexp()
	: XPathEscapedString(this),
	m_match(true)
	{}
    inline bool matches(const char* value) const
	{ return m_match == Regexp::matches(value); }
    inline const XPathString& flags() const
	{ return m_flags; }
    inline bool set(bool match, const XPathParseItem& rex, XPathParseItem& flags, String* error) {
	    if (!setString(rex,error))
		return false;
	    if (!m_flags.setString(flags,error))
		return false;
	    m_match = match;
	    bool insensitive = false;
	    bool extended = true;
	    for (unsigned int i = 0; i < m_flags.length(); i++) {
		switch (m_flags[i]) {
		    case 'i': insensitive = true; continue;
		    case 'b': extended = false; continue;
		}
	    }
	    setFlags(extended,insensitive);
	    if (compile())
		return true;
	    if (error)
		*error = Regexp::length() ? "Invalid regexp" : "Empty regexp";
	    return false;
	}
    inline String& dump(String& buf, bool escape) const {
	    char sep = ',';
	    buf << sep;
	    dumpString(buf,escape);
	    if (m_flags) {
		buf << sep;
		m_flags.dumpString(buf,escape);
	    }
	    return buf;
	}

protected:
    bool m_match;                        // (Reverse) match
    XPathString m_flags;                 // Regexp flags
};

namespace TelEngine {
class XPathPredicate
{
public:
    // Predicate type
    // Used to select input for operator
    enum Type {
	// Values
	None = 0,
	Index,                           // Node index in list
	Text,                            // Xml element text
	XmlName = 0x10,
	Attribute,                       // XML element attribute match
	Child,                           // XML child match (presence or text),
    };
    // Operators
    enum Opc {
	OpcEq = XPathParseData::OpcEq,       // Equality operator
	OpcNotEq = XPathParseData::OpcNotEq, // Inequality operator
	OpcFunc = 0x10,
	OpcMatch,                        // Regexp match operator
	OpcMatchNot,                     // Regexp match NOT operator
    };
    inline XPathPredicate()
	: m_type(0), m_opc(0)
	{}
    inline int type() const
	{ return m_type; }
    inline unsigned int opc() const
	{ return m_opc; }
    inline const char* opcName() const
	{ return opcName(opc()); }
    inline const char* typeName() const
	{ return lookup(m_type,s_typeName); }
    inline bool valid() const
	{ return 0 != type(); }
    inline bool isPosition() const
	{ return Index == type(); }
    inline int check(unsigned int index, const XmlElement* xml = 0, const NamedString* attr = 0) const {
	    if (Index == m_type) {
		if (index == m_opc)
		    return XPathProcHandleStop;
		return index < m_opc ? XPathProcCont : XPathProcStop;
	    }
	    if (Text == m_type || Child == m_type) {
		const String* txt = xml ?
		    ((Child == m_type) ? xml->childText(m_name) : &(xml->getText())) : 0;
		return ((!m_opc && txt) || (txt && runOpc(*txt))) ?
		    XPathProcHandleCont : XPathProcCont;
	    }
	    if (Attribute == m_type) {
		const ObjList* o = xml ? xml->attributes().paramList()->skipNull() : 0;
		for (; o; o = o->skipNext()) {
		    NamedString* ns = static_cast<NamedString*>(o->get());
		    if (m_name && m_name != ns->name())
			continue;
		    if (!m_opc || runOpc(*ns))
			return XPathProcHandleCont;
		    if (m_name)
			break;
		}
		return XPathProcCont;
	    }
	    if (m_type)
		Debug("XPath",DebugStub,"Predicate type %u '%s' not handled in check",m_type,typeName());
	    return XPathProcHandleCont;
	}
    inline bool runOpc(const String& value) const {
	    switch (m_opc) {
		case OpcEq:       return m_value == value;
		case OpcNotEq:    return m_value != value;
		case OpcMatch:    return m_regexp.matches(value);
		case OpcMatchNot: return m_regexp.matches(value);
	    }
	    Debug("XPath",DebugStub,"Operator %u not handled in operator check",m_opc);
	    return false;
	}
    inline String& dump(String& buf, bool escape = false) const {
	    if (!valid())
		return buf;
	    buf << "[";
	    if (Index == m_type)
		buf << m_opc;
	    else {
		bool func = 0 != (m_opc & OpcFunc);
		dumpType(buf,func);
		dumpOpc(buf,escape,func);
	    }
	    return buf << "]";
	}
    inline void dumpType(String& buf, bool opcFunc) const {
	    if (opcFunc)
		buf << opcName() << '(';
	    if (Attribute == m_type)
		buf << '@' << m_name.safe("*");
	    else if (Child == m_type)
		buf << m_name.safe("*");
	    else
		buf << typeName() << "()";
	}
    inline void dumpOpc(String& buf, bool escape, bool func, bool fin = true) const {
	    if (func) {
		m_regexp.dump(buf,escape);
		if (fin)
		    buf << ')';
	    }
	    else if (m_opc) {
		buf << opcName();
		m_value.dump(buf,escape);
	    }
	}

    static inline const char* opcName(int opc)
	{ return lookup(opc,s_opcAll); }

    static const TokenDict s_opcAll[];
    static const TokenDict s_opcFunc[];
    static const TokenDict s_opcBin[];
    static const TokenDict s_typeName[];

    unsigned int m_type;                 // Predicate type
    unsigned int m_opc;                  // Node index value to check or operator
    String m_name;                       // Name of the item to check
    XPathString m_value;                 // Value to check
    XPathRegexp m_regexp;                // Regexp value to use in functions
};
}; // namespace TelEngine

#define XPATH_PREDICATE_DECLARE_OPC_FUNC \
    {"matches",    OpcMatch}, \
    {"notMatches", OpcMatchNot},

#define XPATH_PREDICATE_DECLARE_OPC_BIN \
    {"=",  OpcEq}, \
    {"!=", OpcNotEq},

const TokenDict XPathPredicate::s_opcAll[] = {
    XPATH_PREDICATE_DECLARE_OPC_BIN
    XPATH_PREDICATE_DECLARE_OPC_FUNC
    {0,0},
};

const TokenDict XPathPredicate::s_opcFunc[] = {
    XPATH_PREDICATE_DECLARE_OPC_FUNC
    {0,0},
};

const TokenDict XPathPredicate::s_opcBin[] = {
    XPATH_PREDICATE_DECLARE_OPC_BIN
    {0,0},
};

#undef XPATH_PREDICATE_DECLARE_OPC_FUNC
#undef XPATH_PREDICATE_DECLARE_OPC_BIN

const TokenDict XPathPredicate::s_typeName[] = {
    {"index",     Index},
    {"attribute", Attribute},
    {"child",     Child},
    {"text",      Text},
    {0,0},
};


class XPathPredicateList
{
public:
    inline XPathPredicateList()
	: m_indexPredicate(0), m_stopProc(false)
	{}
    inline bool valid() const
	{ return m_predicates[0].valid(); }
    inline XPathPredicate* first()
	{ return m_predicates; }
    inline const XPathPredicate* first() const
	{ return m_predicates; }
    inline int check(unsigned int& index, const XmlElement* xml = 0,
	const NamedString* attr = 0) const {
	    if (!valid())
		return XPathProcHandleCont;
	    index++;
	    int rProc = XPathProcHandleCont;
	    if (!m_stopProc) {
		// Always evalute predicates with position first. May lead to fast return
		if (m_indexPredicate)
		    check(rProc,true,*m_indexPredicate,index,xml,attr);
		const XPathPredicate* f = first();
		for (unsigned int i = 0; rProc > 0 && i < XPATH_MAX_PREDICATES && f->valid(); ++i, ++f) {
		    if (!f->isPosition())
			check(rProc,!(m_indexPredicate || i),*f,index,xml,attr);
		}
	    }
	    else
		rProc = XPathProcStop;
    	    #ifdef XPATH_DEBUG_FIND
	    if (rProc <= 0)
		Debug("XPath",DebugAll,
		    "Checked %s '%s' idx=%u. Predicate(s) not matched proc=%s",
		    (xml ? "xml" : (attr ? "attribute" : "???")),
		    (xml ? xml->tag() : ((attr ? attr->name().c_str() : ""))),
		    index,lookup(rProc,s_xpathProcAct));
	    #endif
	    return rProc;
	}
    inline bool check(int& rProc, bool first, const XPathPredicate& pred,
	unsigned int index, const XmlElement* xml, const NamedString* attr) const {
	    #ifdef XPATH_XDEBUG_FIND
	    String tmp;
	    pred.dump(tmp) << " index=" << index;
	    #endif
	    if (first) {
		rProc = pred.check(index,xml,attr);
		#ifdef XPATH_XDEBUG_FIND
		Debug("XPath",DebugAll,"Predicate %s check returned %s",
		    tmp.safe(),lookup(rProc,s_xpathProcAct));
		#endif
	    }
	    else {
		int proc = pred.check(index,xml,attr);
		rProc = filterProc(rProc,proc);
		#ifdef XPATH_XDEBUG_FIND
		Debug("XPath",DebugAll,"Predicate %s check returned %s. Filtered: %s",
		    tmp.safe(),lookup(proc,s_xpathProcAct),lookup(rProc,s_xpathProcAct));
		#endif
	    }
	    return rProc > 0;
	}
    inline String& dump(String& buf, bool escape = false) const {
	    const XPathPredicate* f = first();
	    for (unsigned int i = 0; i < XPATH_MAX_PREDICATES && f->valid(); ++i, ++f)
		f->dump(buf,escape);
	    return buf;
	}

    // Filter curent predicate evaluation result agains accumulated result
    // NOTE: Assume previous predicate(s) check is handle (stop or continue)
    static inline int filterProc(int prev, int crt) {
	    // Predicate matched. Handle current item. Do not handle susequent items
	    if (XPathProcHandleStop == crt)
		return crt;
	    // Predicate matched. Handle current and subsequent item(s)
	    // Previous check indicated handle stop/continue: honor it
	    if (XPathProcHandleCont == crt)
		return prev;
	    // Predicate not matched indicating Stop: honor it
	    if (XPathProcStop == crt)
		return crt;
	    // Predicate not matched indicating continue processing next items
	    // Check previous
	    if (XPathProcHandleStop == prev)
		return XPathProcStop;
	    return XPathProcCont;
	}

    XPathPredicate m_predicates[XPATH_MAX_PREDICATES]; // Predicate expression(s)
    XPathPredicate* m_indexPredicate;
    bool m_stopProc;
};

namespace TelEngine {
class YATE_API XPathStep : public String
{
    YCLASS(XPathStep,String)
public:
    /**
     * Path item type
     */
    enum Type {
	// Mask(s)
	ElementNode = 0x1000,            // Step is an XML element nod
	XmlName = 0x2000,                // Step name is subject to XML name validity check
	// Values
	Unknown = 0,
	Xml = ElementNode | XmlName | 1, // XML Element
	Attribute = XmlName | 2,         // Node attribute(s)
	Text = 2,                        // XML Text
	ChildText = 3,                   // XML Element child text
    };
    inline XPathStep(int nodeType, const char* value = 0)
	: String(value),
	m_nodeType(nodeType)
	{}
    inline XPathStep(const XPathStep& other)
	: String(other.c_str()),
	m_nodeType(other.m_nodeType)
	{}
    inline int nodeType() const
	{ return m_nodeType; }
    inline const char* nodeTypeName() const
	{ return lookup(nodeType(),s_xpathNodeType,"Unknown"); }
    inline int isElementNode() const
	{ return isElementNode(nodeType()); }
    inline bool valueMatchAny() const
	{ return 0 == String::length(); }
    inline const String* valueMatch() const
	{ return valueMatchAny() ? 0 : (const String*)this; }
    inline const XPathPredicateList* predicates() const
	{ return m_predicates.valid() ? &m_predicates : 0; }
    inline String& dump(String& buf, bool escape = false) {
	    switch (m_nodeType) {
		case Xml:
		    buf << String::safe("*");
		    break;
		case Attribute:
		    buf << "@" << String::safe("*");
		    break;
		default:
		    const char* f = lookup(m_nodeType,s_xpathNodeSelFunction);
		    if (f)
			buf << f << "()";
		    else
			buf << "unk_function(" << m_nodeType << ")";
	    }
	    return m_predicates.dump(buf,escape);
	}
    // Check if a given item should be added to result set
    inline int checkHandle(const XPath* path, unsigned int& resultIdx,
	const XmlElement* xml = 0, const NamedString* attr = 0,
	const String& name = String::empty(), const String* nameCheck = 0) const {
	    if (nameCheck && name != *nameCheck) {
		XPathDebugFind("XPath",DebugAll,"Checked %s '%s': not matched [%p]",
		    (xml ? "xml" : "attribute"),name.c_str(),path);
		return XPathProcCont;
	    }
	    return m_predicates.check(resultIdx,xml,attr);
	}

    static inline bool matchAny(const char* buf, unsigned int len)
	{ return 1 == len && '*' == *buf; }
    static inline bool isElementNode(int type)
	{ return 0 != (ElementNode & type); }

    // Utility used when processing path items in find
    // Filter action
    static inline int filterProc(int upperProc, int proc) {
	    if (upperProc < 0 || XPathProcHandleStop == upperProc)
		return upperProc;
	    return proc;
	}

    static const TokenDict s_xpathNodeType[];
    static const TokenDict s_xpathNodeSelFunction[];

    int m_nodeType;                      // Node selector type
    XPathPredicateList m_predicates;     // Step predicates
};
}; // namespace TelEngine

#define XPATH_DECLARE_NODE_SEL_FUNC \
    {"text",               Text}, \
    {"child::text",        ChildText}, \

const TokenDict XPathStep::s_xpathNodeType[] = {
    {"element" ,  Xml},
    {"attribute", Attribute},
    XPATH_DECLARE_NODE_SEL_FUNC
    {0,0}
};

const TokenDict XPathStep::s_xpathNodeSelFunction[] = {
    XPATH_DECLARE_NODE_SEL_FUNC
    {0,0}
};

#undef XPATH_DECLARE_NODE_SEL_FUNC

XPath::XPath(const char* value, unsigned int flags)
    : String(value),
    m_flags(flags & ~FInternal),
    m_status(NotParsed),
    m_errorItem(0)
{
    XDebug(DebugAll,"XPath(%s,0x%x) [%p]",c_str(),m_flags,this);
    if (0 == (m_flags & LateParse))
	changed();
}

XPath::XPath(const XPath& other)
    : String(other.c_str()),
    m_flags(other.m_flags),
    m_status(other.m_status),
    m_errorItem(other.m_errorItem),
    m_error(other.m_error)
{
    XDebug(DebugAll,"XPath(%s,0x%x) [%p]",c_str(),m_flags,this);
    ObjList* itAppend = &m_items;
    for (ObjList* o = other.m_items.skipNull(); o; o = o->skipNext())
	itAppend->append(new XPathStep(*static_cast<XPathStep*>(o->get())));
}

XPath::~XPath()
{
    reset();
}

static inline bool addFindResult(const GenObject* gen, ObjList*& list)
{
    if (!list)
	return false;
    list = list->append(gen);
    list->setDelete(false);
    return true;
}

static inline bool setFindResult(const XmlElement* xml, ObjList*& list,
    const XmlElement** xmlReq, const GenObject** anyReq)
{
    if (xmlReq) {
	if (!*xmlReq)
	    *xmlReq = xml;
    }
    else if (anyReq) {
	if (!*anyReq)
	    *anyReq = xml;
    }
    return addFindResult(xml,list);
}

static inline bool setFindResult(const String* str, ObjList*& list,
    const String** textReq, const GenObject** anyReq)
{
    if (textReq) {
	if (!*textReq)
	    *textReq = str;
    }
    else if (anyReq) {
	if (!*anyReq)
	    *anyReq = str;
    }
    return addFindResult(str,list);
}

static inline bool xpathAddResult(const XPath* ptr, const GenObject* item,
    const GenObject*& res, ObjList*& list)
{
#ifdef XPATH_DEBUG_FIND
    String tmp;
    XmlElement* xml = YOBJECT(XmlElement,item);
    NamedString* attr = YOBJECT(NamedString,item);
    String* text = YOBJECT(String,item);
    if (xml)
	tmp.printf("XML (%p) '%s'",xml,xml->tag());
    else if (attr)
	tmp.printf("ATTR (%p) '%s'='%s'",attr,attr->name().c_str(),attr->safe());
    else if (text)
	tmp.printf("TEXT %s(%p) '%s'",(text == &String::empty() ? "EMPTY " : ""),text,text->safe());
    else
	tmp.printf("??? (%p) '%s'",item,item->toString().safe());
    Debug("XPath",(xml || attr || text) ? DebugAll : DebugFail,"FIND adding result %s [%p]",tmp.c_str(),ptr);
#endif
    if (!res)
	res = item;
    if (!list)
	return false;
    list = list->append(item);
    list->setDelete(false);
    return true;
}

int XPath::find(unsigned int& total, const XmlElement& src, const GenObject*& res, ObjList* list,
    unsigned int what, ObjList* crtItem, unsigned int step, bool absolute) const
{
#ifdef XPATH_DEBUG_FIND
    String stepInfo, tmp;
    if (!step) {
	dump(tmp,false," ");
	stepInfo << "items=" << m_items.count() << " ";
    }
    String req;
    if (what != FindAny) {
	if (what & FindXml)
	    req.append("XML","_");
	if (what & FindText)
	    req.append("TEXT","_");
	if (what & FindAttr)
	    req.append("ATTR","_");
    }
    Debugger debug(step ? DebugInfo : DebugCall,"XPath FIND"," %sstep=%u req=%s%s [%p]%s",
	stepInfo.safe(),step,req.safe("ANY"),(list ? "_LIST" : ""),
	this,tmp.safe());
#endif

    if (!crtItem) {
	crtItem = m_items.skipNull();
	if (!crtItem) {
	    XPathDebugFind("XPath",DebugInfo,"FIND step=%u res_count=0 returning proc %s [%p]",
		step,lookup(XPathProcStop,s_xpathProcAct),this);
	    return XPathProcStop;
	}
    }
    XPathStep& it = *static_cast<XPathStep*>(crtItem->get());
    ObjList* nextItem = crtItem->skipNext();

    ObjList* lstAppend = list;
    unsigned int n = 0;
    bool stop = false;
    unsigned int resultIdx = 0;
    while (true) {
	if (it.isElementNode()) {
	    ObjList* o = 0;
	    XmlElement* x = 0;
	    if (absolute)
		x = (XmlElement*)&src;
	    else {
		o = src.getChildren().skipNull();
		x = XmlFragment::getElement(o);
	    }
	    bool xmlReq = 0 != (what & FindXml);
	    // Last item but no XML/TEXT requested ?
	    if (!nextItem && !xmlReq && 0 == (what & FindText)) {
		stop = true;
		break;
	    }
	    const String* tag = it.valueMatch();
	    for ( ; x; x = XmlFragment::getElement(o)) {
		int proc = it.checkHandle(this,resultIdx,x,0,x->getTag(),tag);
		if (proc > 0) {
		    if (nextItem)
			proc = XPathStep::filterProc(proc,find(n,*x,res,list,what,nextItem,step + 1));
		    else if (xmlReq) {
			n++;
			if (!xpathAddResult(this,x,res,lstAppend))
			    proc = XPathProcStop;
		    }
		    else
			// Last item pointing to an XML but XML was not requested
			proc = XPathStep::filterProc(proc,getText(n,*x,0,resultIdx,res,list));
		}
		if (proc < 0 || XPathProcHandleStop == proc)
		    break;
	    }
	    break;
	}
	
	if (XPathStep::Text == it.m_nodeType || XPathStep::Text == it.m_nodeType) {
	    // No need to check anything if the requested result set contains other data
	    //  type (non XML Text) or we have a next item
	    // If next item is present there is nothing there to match (XmlText has no attributes, children ...)
	    // NOTE: This won't be true if we are going to implement other node type selector
	    //       that may be possible (e.g. parent)
	    if (nextItem || 0 == (FindText & what)) {
		stop = true;
		break;
	    }
	    if (XPathStep::Text == it.m_nodeType)
		getText(n,src,&it,resultIdx,res,list);
	    else {
		ObjList* o = src.getChildren().skipNull();
		for (XmlElement* x = XmlFragment::getElement(o); x; x = XmlFragment::getElement(o)) {
		    int proc = getText(n,*x,&it,resultIdx,res,list);
		    if (proc < 0 || XPathProcHandleStop == proc)
			break;
		}
	    }
	    break;
	}

	if (XPathStep::Attribute == it.m_nodeType) {
	    // If next item is present there is nothing there to match
	    // (an attribute has no relatinship with other data)
	    if (nextItem || 0 == (FindAttr & what)) {
		stop = true;
		break;
	    }
	    const String* name = it.valueMatch();
	    for (const ObjList* o = src.attributes().paramList()->skipNull(); o; o = o->skipNext()) {
		NamedString* ns = static_cast<NamedString*>(o->get());
		int proc = it.checkHandle(this,resultIdx,0,ns,ns->name(),name);
		if (proc > 0) {
		    n++;
		    if (!xpathAddResult(this,ns,res,lstAppend))
			proc = XPathProcStop;
		}
		if (proc < 0 || XPathProcHandleStop == proc)
		    break;
	    }
	    break;
	}

	Debug("XPath",DebugStub,"Node type selector %d '%s' not handled [%p]",
	    it.m_nodeType,it.nodeTypeName(),this);
	stop = true;
	break;
    }

    total += n;
    int rProc = XPathProcCont;
    if (stop || (n && !list))
	rProc = XPathProcStop;
    XPathDebugFind("XPath",DebugInfo,"FIND step=%u res_count=%u returning proc %s [%p]",
	step,n,lookup(rProc,s_xpathProcAct),this);
    return rProc;
}

int XPath::getText(unsigned int& total, const XmlElement& src, const XPathStep* step,
    unsigned int& resultIdx, const GenObject*& res, ObjList* list) const
{
    XPathDebugFind("XPath",DebugAll,"Get text xml '%s' multi=%s step=(%p) [%p]",
	src.getTag().c_str(),String::boolText(list),step,this);
    unsigned int n = 0;
    int proc = XPathProcHandleCont;
    ObjList* o = src.getChildren().skipNull();
    for (XmlText* t = XmlFragment::getText(o); t; t = XmlFragment::getText(o)) {
	if (step)
	    proc = step->checkHandle(this,resultIdx);
	if (proc > 0) {
	    n++;
	    if (!xpathAddResult(this,&t->getText(),res,list))
		proc = XPathProcStop;
	}
	if (proc < 0 || XPathProcHandleStop == proc)
	    break;
    }
    total += n;
    XPathDebugFind("XPath",DebugAll,"Get text found=%u returning proc %s [%p]",
	n,lookup(proc,s_xpathProcAct),this);
    return proc;
}

void XPath::changed()
{
    parsePath();
}

#define XPATH_SET_STATUS_BREAK(code,str) { setStatus(code,data.step(),str); break; }
#define XPATH_SET_STATUS_RET(code,str) { return setStatus(code,data.step(),str); }
#define XPATH_SET_SYNTAX_BREAK(str) XPATH_SET_STATUS_BREAK(ESyntax,str)
#define XPATH_SET_SYNTAX_RET(str) XPATH_SET_STATUS_RET(ESyntax,str)

#define XPATH_PARSE_EOB(str) ((String("Unexpected end of buffer ") + str).c_str())
#define XPATH_CHECK_END_BREAK(str) if (data.ended()) XPATH_SET_SYNTAX_BREAK(XPATH_PARSE_EOB(str));
#define XPATH_CHECK_END_RET(str) if (data.ended()) XPATH_SET_SYNTAX_RET(XPATH_PARSE_EOB(str));
#define XPATH_PARSE_SKIP_BLANKS_BREAK(str) if (!data.skipBlanks()) XPATH_SET_SYNTAX_BREAK(XPATH_PARSE_EOB(str));
#define XPATH_PARSE_SKIP_BLANKS_RET(str) if (!data.skipBlanks()) XPATH_SET_SYNTAX_RET(XPATH_PARSE_EOB(str));

#define XPATH_PARSE_STRICT_BLANK_BREAK(str) { \
    XPATH_CHECK_END_BREAK(str); \
    if (data.isBlank()) { \
	if (data.strictParse) \
	    { XPATH_SET_SYNTAX_BREAK((String("Unexpected space ") + str).c_str()); } \
	else \
	    XPATH_PARSE_SKIP_BLANKS_BREAK(str); \
    } \
}
#define XPATH_PARSE_STRICT_BLANK_RET(str) { \
    XPATH_CHECK_END_RET(str); \
    if (data.isBlank()) { \
	if (data.strictParse) \
	    { XPATH_SET_SYNTAX_RET((String("Unexpected space ") + str).c_str()); } \
	else \
	    XPATH_PARSE_SKIP_BLANKS_RET(str); \
    } \
}
#define XPATH_PARSE_STRICT_BLANK_PREDICATE XPATH_PARSE_STRICT_BLANK_RET("while parsing predicate")

void XPath::parsePath()
{
    reset();

    m_flags = m_flags & ~(unsigned int)FAbsolute;
    m_status = 0;
    XPathParseData data(c_str(),String::length(),m_flags);
    String tmp;
#ifdef XPATH_DEBUG_PARSE
    Debugger dbg(DebugCall,"XPath PARSE"," flags=0x%x len=%u '%s' [%p]",
	m_flags,data.origLength(),data.c_str(),this);
#endif
    XPathStep* step = 0;
    XPathStep* prevStep = 0;
    XPathParseItem stepStart;
    while (true) {
	// Path step start
	XPathDebugParse("XPath",DebugAll,"Parsing step %u idx=%u '%s' [%p]",
	    data.step(),data.index(),data.c_str(),this);
	stepStart.set(data.c_str(),data.index());
	if (data.haveData() && data.isBlank()) {
	    if (data.strictParse)
		XPATH_SET_SYNTAX_BREAK("Unexpected space at step start");
	    if (!data.skipBlanks())
		XPATH_SET_STATUS_BREAK(EEmptyItem,"");
	}

	if (data.isStepEnd()) {
	    if (data.step() || data.ended())
		XPATH_SET_STATUS_BREAK(EEmptyItem,"");
	    XPathDebugParse("XPath",DebugAll,"Processed empty step %u idx=%u '%s' [%p]",
		data.step(),data.index(),data.c_str(),this);
	    // Empty first step: set absolute path flag
	    m_flags |= FAbsolute;
	    data.advance();
	    continue;
	}

#ifdef XPATH_DEBUG_PARSE
	Debugger dbgStep(DebugCall,"XPath PARSE STEP"," %u idx=%u '%s' [%p]",
	    data.step(),data.index(),data.c_str(),this);
#endif
	// Retrieve step expression
	// https://www.w3.org/TR/xpath-30/#doc-xpath30-StepExpr
	const char* name = data.c_str();
	unsigned int n = data.index();
	while (data.haveData()
	    && !(data.isSep() || (data == '(') || (data == '[') || data.isBlank()))
	    data.advance();
	n = data.index() - n;
	if (!n)
	    XPATH_SET_SYNTAX_BREAK("Empty step expression");
	if (data.haveData())
	    XPATH_PARSE_STRICT_BLANK_BREAK("while parsing step expression");
	if (data.isStepEnd() || data == '[') {
	    // Handle element tag or attribute
	    if ('@' == *name) {
		// Attribute(s) ...
		if (n < 2)
		    XPATH_SET_SYNTAX_BREAK("Empty attribute match in step");
		step = new XPathStep(XPathStep::Attribute);
		name++;
		n--;
	    }
	    else
		step = new XPathStep(XPathStep::Xml);
	    if (!XPathStep::matchAny(name,n)) {
		char c = data.validXmlName(name,n);
		if (c)
		    XPATH_SET_SYNTAX_BREAK(tmp.printf("Invalid char '%c' in %s name",c,step->nodeTypeName()));
		step->assign(name,n);
	    }
	}
	else if (data == '(') {
	    // Node selector. Function call: selector()
	    String fn(name,n);
	    int type = lookup(fn,XPathStep::s_xpathNodeSelFunction);
	    if (!type)
		XPATH_SET_SYNTAX_BREAK(tmp.printf("Unknown node selector '%s'",fn.c_str()));
	    data.advance();
	    XPATH_PARSE_STRICT_BLANK_BREAK("while parsing node selector");
	    // Node selector can't have parameters. Expect end of function call
	    if (data != ')')
		XPATH_SET_SYNTAX_BREAK("Non empty node selector");
	    data.advance();
	    if (!data.strictParse)
		data.skipBlanks();
	    step = new XPathStep(type);
	}
	if (data.checkEmptyRes) {
	    // Previous selector is not XML
	    // We are only handling XML element or text nodes
	    // If previous is a text node there is nothing else following it
	    // Searching something using this path will alsways produce an empty result
	    if (prevStep && !prevStep->isElementNode())
		XPATH_SET_STATUS_BREAK(EEmptyResult,"Path step after a final selector step");
	    prevStep = step;
	}

	XPathPredicate* p = step->m_predicates.first();
	for (unsigned int i = 0; ; ++p, ++i) {
	    if (!data.strictParse)
		data.skipBlanks();
	    if (data.isStepEnd())
		break;
	    if (data != '[') {
		if (i)
		    XPATH_SET_SYNTAX_BREAK("Unexpected char after step predicate");
		XPATH_SET_SYNTAX_BREAK("Unexpected char after step selector");
	    }
	    if (i == XPATH_MAX_PREDICATES)
		XPATH_SET_STATUS_BREAK(ERange,"Too many predicates");
	    if (!parseStepPredicate(data,p))
		break;
	    if (!checkStepPredicate(data,step,p))
		break;
	}
	if (m_status)
	    break;

#ifdef XPATH_DEBUG_PARSE
	String tmpStep;
	XPathDebugParse("XPath",DebugInfo,"Parsed step %u type=%d (%s) '%s' value='%s' [%p]",
	    data.step(),step->nodeType(),step->nodeTypeName(),step->dump(tmpStep).c_str(),
	    step->c_str(),this);
#endif
	m_items.append(step);
	step = 0;
	if (data.ended())
	    break;
	data.advanceStep();
    }
    TelEngine::destruct(step);

#ifdef XPATH_DEBUG_PARSE
    tmp = "";
    String tmp2;
    if (m_status)
	Debug("XPath",DebugNote,"Parse failed step=%u offset=%u '%s': %s [%p]",
	    data.step(),data.index() - stepStart.length(),stepStart.value().c_str(),
	    describeError(tmp).c_str(),this);
    else {
	Debug("XPath",DebugCall,"Parsed%s [%p]\r\n-----\r\n%s\r\n%s\r\n-----",
	    (absolute() ? " absolute" : ""),this,
	    dump(tmp2,true,"/",absolute()).c_str(),dump(tmp).safe());
    }
#endif
}

// Parse predicate (filter expression) [...]
bool XPath::parseStepPredicate(XPathParseData& data, XPathPredicate* pred)
{
#ifdef XPATH_DEBUG_PARSE
    Debugger dbg(DebugAll,"XPath PARSE predicate"," len=%u '%s' [%p]",
	data.length(),data.c_str(),this);
#endif
    // Skip over starting [ and spaces
    data.advance();
    // Allow spaces in predicate start
    XPATH_PARSE_STRICT_BLANK_PREDICATE;
    if (data.isPredicatedEnd()) {
	if (data.isStepEnd())
	    XPATH_SET_SYNTAX_RET("Expectind predicate contents");
	XPATH_SET_SYNTAX_RET("Empty predicate");
    }

    String tmp;
    XPathParseItem selector(data.c_str());

    // Nothing can start with decimal digits except for Index 
    if (data.isDigit()) {
	for (; data.haveData() && data.isDigit(); data.advance())
	    selector.advance();
	XPATH_PARSE_STRICT_BLANK_PREDICATE;
	if (data.isStepEnd())
	    XPATH_SET_SYNTAX_RET("Unexpected end of step while parsing predicate");
	if (data != ']')
	    XPATH_SET_SYNTAX_RET(tmp.printf("Unexpected char '%c' while parsing index predicate",data.crt()));
	data.advance();
	uint64_t val = (selector.length() && '0' != selector[0]) ?
	    selector.value().toUInt64() : 0;
	if (!val || val > 0xffffffff)
	    XPATH_SET_SYNTAX_RET("Predicate index value invalid or out of range");
	pred->m_type = XPathPredicate::Index;
	pred->m_opc = (unsigned int)val;

	XPathDebugParse("XPath",DebugInfo,"Parsed predicate %u '%s' value=%u [%p]",
	    pred->type(),pred->typeName(),pred->opc(),this);
	return true;
    }

    pred->m_type = XPathPredicate::None;
    unsigned int selMin = 1;
    if (data == '@') {
	data.advance();
	if (data.isPredicatedEnd())
	    XPATH_SET_SYNTAX_RET("Unexpected end of predicate attribute selector");
	pred->m_type = XPathPredicate::Attribute;
	selector.advance();
	selMin = 2;
    }
    unsigned int opc = 0;
    String fn;
    int funcParam = 0;
    int reqParams = 0;
    int maxParams = -1;
    XPathParseItem op1; // Second predicate operand or second function parameter
    XPathParseItem op2; // Third function parameter

    // Parse input from XML
    // Predicate may be an XML selector: attribute, child, text() or function call
    // Non function may be followed by a binary operator
    // Function first parameter MUST be an XML selector
    while (!data.isPredicatedEnd()) {
	if (fn) {
	    // We are in function call
	    if (funcParam) {
		XPATH_PARSE_STRICT_BLANK_PREDICATE;
		if (funcParam <= maxParams) {
		    XPathParseItem& op = (funcParam == 1) ? op1 : op2;
		    const char* e = data.parseStringXml(op.buf,op.len,op.delimiter,op.esc);
		    if (e)
			XPATH_SET_SYNTAX_RET(String(e) + " in predicate function parameter");
		    XPathXDebugParse("XPath",DebugAll,"Parsed function param %u '%s' [%p]",
			funcParam,op.value().safe(),this);
		    XPATH_PARSE_STRICT_BLANK_PREDICATE;
		}
		if (data == ')') {
		    if (funcParam < reqParams)
			XPATH_SET_SYNTAX_RET("Missing function parameter");
		    data.advance();
		    break;
		}
		if (data != ',')
		    XPATH_SET_SYNTAX_RET("Expecting function parameters separator");
		funcParam++;
		if (funcParam > maxParams)
		    XPATH_SET_SYNTAX_RET("Too many predicate function parameters");
		data.advance();
		continue;
	    }
	    if (data == ',' || data == '(' || data == ')') {
		// Enf of selector
		if (!selector.length()) {
		    if (data == '(')
			XPATH_SET_SYNTAX_RET("Unexpected '(' in function parameter");
		    XPATH_SET_SYNTAX_RET("Missing function parameter");
		}
		XPathXDebugParse("XPath",DebugAll,"Parsed function selector '%s' [%p]",
		    selector.value().c_str(),this);
		// Check name
		if (data == '(') {
		    const String& f = selector.value();
		    unsigned int func = lookup(f,XPathPredicate::s_typeName);
		    switch (func) {
			case XPathPredicate::Text:
			    // Empty parameter list allowed
			    if (!data.ended())
				data.advance();
			    if (data.ended() || data != ')')
				XPATH_SET_SYNTAX_RET("Expecting ')' after predicate input selector");
			    pred->m_type = XPathPredicate::Text;
			    break;
			default:
			    if (func)
				tmp.printf("Predicate function '%s' not implemented",f.c_str());
			    else
				tmp.printf("Unknown function '%s' in predicate",f.c_str());
			    XPATH_SET_SYNTAX_RET(tmp);
		    }
		    data.advance();
		}
		else if ('@' == selector[0]) {
		    if (selector.length() < 2)
			XPATH_SET_SYNTAX_RET("Empty attribute name in function parameter");
		    pred->m_type = XPathPredicate::Attribute;
		}
		if (data != ')')
		    data.advance();
		XPATH_PARSE_STRICT_BLANK_PREDICATE;
		funcParam = 1;
	    }
	    else {
		if (!selector.length()) {
		    XPATH_PARSE_STRICT_BLANK_PREDICATE;
		    selector.set(data.c_str());
		}
		selector.advance();
		data.advance();
	    }
	    continue;
	}

	if (data.isBlank()) {
	    if (selector.length() < selMin)
		XPATH_SET_SYNTAX_RET("Unexpected space in predicate operand");
	    data.advance();
	    // Next blanks will be skipped if allowed
	    XPATH_PARSE_SKIP_BLANKS_RET("while parsing predicate");
	    if (!op1.c_str())
		XPathXDebugParse("XPath",DebugAll,"Parsed selector '%s' [%p]",
		    selector.value().c_str(),this);
	    // Prepare second operand
	    op1.set(data.c_str());
	    continue;
	}
	if (!opc) {
	    opc = data.parseOperator();
	    if (opc) {
		if (selector.length() < selMin)
		    XPATH_SET_SYNTAX_RET("Unexpected operator while parsing predicate");
		if (!data.ended() && data.isBlank()) {
		    data.advance();
		    // Next blanks will be skipped if allowed
		    XPATH_PARSE_SKIP_BLANKS_RET("while parsing predicate");
		}
		if (!op1.c_str())
		    XPathXDebugParse("XPath",DebugAll,"Parsed selector '%s' [%p]",
			selector.value().c_str(),this);
		// Prepare second operand
		XPathXDebugParse("XPath",DebugAll,"Parsed operator %u '%s' [%p]",
		    opc,XPathPredicate::opcName(opc),this);
		op1.set(data.c_str());
		continue;
	    }
	    if (data == '(') {
		if (selector.length() < selMin)
		    XPATH_SET_SYNTAX_RET("Unexpected operator while parsing predicate");
		// Function call
		if (pred->type()) {
		    tmp.printf("Unexpected '(' after %s operand",pred->typeName());
		    XPATH_SET_SYNTAX_RET(tmp);
		}
		selector.assignTo(fn);
		data.advance();
		selector.advance();
		unsigned int func = lookup(fn,XPathPredicate::s_opcFunc);
		switch (func) {
		    case XPathPredicate::OpcMatch:
		    case XPathPredicate::OpcMatchNot:
			maxParams = 2;
			reqParams = 1;
			break;
		    case 0:
			// Function not found. Check for selector type function
			func = lookup(fn,XPathPredicate::s_typeName);
			switch (func) {
			    case XPathPredicate::Text:
				if (data.ended() || data != ')')
				    XPATH_SET_SYNTAX_RET("Expecting ')' after predicate input selector");
				pred->m_type = XPathPredicate::Text;
				func = 0;
				selector.advance();
				data.advance();
				break;
			    default:
				if (func)
				    tmp.printf("Predicate function '%s' not implemented",fn.c_str());
				else
				    tmp.printf("Unknown function '%s' in predicate",fn.c_str());
				XPATH_SET_SYNTAX_RET(tmp);
			}
			break;
		    default:
			tmp.printf("Predicate function '%s' not implemented",fn.c_str());
			XPATH_SET_SYNTAX_RET(tmp);
		}
		if (func) {
		    XPathXDebugParse("XPath",DebugAll,"Parsed function %u '%s' [%p]",
			func,XPathPredicate::opcName(func),this);
		    opc = func;
		    // Reset selector: parse input from XML
		    selector.set();
		}
		else {
		    // Prepare second operand
		    XPathXDebugParse("XPath",DebugAll,"Parsed selector '%s' [%p]",
			selector.value().c_str(),this);
		    op1.set(data.c_str());
		    fn.clear();
		}
		continue;
	    }
	    // Operator not matched after first operand ?
	    if (op1.c_str())
		XPATH_SET_SYNTAX_RET("Expecting operator");
	}
	if (!op1.c_str()) {
	    selector.advance();
	    data.advance();
	    continue;
	}
	const char* e = data.parseStringLiteral(op1.buf,op1.len,op1.delimiter,op1.esc);
	if (e)
	    XPATH_SET_SYNTAX_RET(String(e) + " in predicate operand");
	XPathXDebugParse("XPath",DebugAll,"Parsed operand '%s' [%p]",op1.value().safe(),this);
	XPATH_PARSE_STRICT_BLANK_PREDICATE;
	break;
    }
    if (!data.isPredicatedEnd())
	XPATH_PARSE_STRICT_BLANK_PREDICATE;
    if (data.isStepEnd())
	XPATH_SET_SYNTAX_RET("Unexpected end of step while parsing predicate");
    if (data != ']')
	XPATH_SET_SYNTAX_RET(tmp.printf("Unexpected char '%c' while parsing predicate",data.crt()));
    data.advance();

    if (!pred->type())
	pred->m_type = XPathPredicate::Child;
    pred->m_opc = opc;

#if 0
#ifdef XPATH_XDEBUG_PARSE
    String xd;
    xd << tmp.printf("\r\nselector: %u '%s'",selector.length(),selector.value().c_str());
    xd << tmp.printf("\r\nop1: %u '%s'",op1.length(),op1.value().c_str());
    xd << tmp.printf("\r\nop2: %u '%s'",op2.length(),op2.value().c_str());
    Debug("XPath",DebugAll,"Processing predicate %u opc=%u [%p]\r\n-----%s\r\n-----",
	pred->type(),pred->opc(),this,xd.c_str());
#endif
#endif

    if (XPathPredicate::Attribute == pred->type()) {
	if (selector.length() < 2)
	    XPATH_SET_SYNTAX_RET("Empty attribute name in predicate operand");
	if (XPathStep::matchAny(selector.c_str() + 1,selector.length() - 1))
	    selector.set();
	else
	    selector.set(selector.c_str() + 1,selector.length() - 1);
    }
    if (selector.length()) {
	if (0 != (pred->type() & XPathPredicate::XmlName)) {
	    char c = data.validXmlName(selector.c_str(),selector.length());
	    if (c)
		XPATH_SET_SYNTAX_RET(
		    tmp.printf("Invalid char '%c' in %s name predicate",c,pred->typeName()));
	}
	selector.assignTo(pred->m_name);
    }
    if (op1.c_str()) {
	bool ok = true;
	switch (pred->opc()) {
	    case XPathPredicate::OpcMatch:    ok = pred->m_regexp.set(true,op1,op2,&tmp); break;
	    case XPathPredicate::OpcMatchNot: ok = pred->m_regexp.set(false,op1,op2,&tmp); break;
	    default:
		pred->m_value.setLiteral(true);
		ok = pred->m_value.setString(op1,&tmp);
	}
	if (!ok)
	    XPATH_SET_SYNTAX_RET(tmp + " in predicate function parameter");
    }

#ifdef XPATH_DEBUG_PARSE
    String pInfo;
    if (XPathPredicate::Index == pred->type())
	pInfo << "value " << pred->opc();
    else {
	pInfo << "name='" << pred->m_name << "'";
	if (pred->opc()) {
	    pInfo << " opc=" << pred->opc() << " (" << pred->opcName() << ")";
	    if (pred->m_value.delimiter()) {
		pInfo << " value=";
		pred->m_value.dump(pInfo);
	    }
	    if (pred->m_regexp.delimiter()) {
		pInfo << " regexp=";
		pred->m_regexp.dumpString(pInfo);
		if (pred->m_regexp.flags()) {
		    pInfo << " flags=";
		    pred->m_regexp.flags().dumpString(pInfo);
		}
	    }
	}
    }
    Debug("XPath",DebugInfo,"Parsed predicate %u '%s' %s [%p]",
	pred->type(),pred->typeName(),pInfo.c_str(),this);
#endif

    return true;
}

bool XPath::checkStepPredicate(XPathParseData& data, XPathStep* step, XPathPredicate* pred)
{
    if (pred->type() == XPathPredicate::Index) {
	XPathPredicateList& lst = step->m_predicates;
	if (!lst.m_indexPredicate)
	    lst.m_indexPredicate = pred;
	else {
	    if (data.strictParse)
		XPATH_SET_STATUS_RET(ESemantic,"Repeated index predicate in step");
	    if (pred->opc() != lst.m_indexPredicate->opc())
		if (data.checkEmptyRes)
		    XPATH_SET_STATUS_RET(EEmptyResult,"Path step with different index value in predicate");
		lst.m_stopProc = true;
	}
    }
    else {
	if (data.checkEmptyRes) {
	    switch (pred->type()) {
		case XPathPredicate::Attribute:
		case XPathPredicate::Child:
		case XPathPredicate::Text:
		    // Only XML Element can have children or attributes
		    // For all other types there will be no match
		    if (!step->isElementNode()) {
			String tmp;
			tmp.printf("Found %s predicate for '%s' selector step",
			    pred->typeName(),step->nodeTypeName());
			XPATH_SET_STATUS_RET(EEmptyResult,tmp);
		    }
		    break;
		case XPathPredicate::Index:
		    break;
		default:
		    Debug("XPath",DebugStub,
			"Predicate type %d (%s) not handled in step empty result check [%p]",
			pred->type(),pred->typeName(),this);
	    }
	}
    }
    return true;
}

String& XPath::dump(String& buf, bool escape, const char* itemSep, bool sepFirst) const
{
    for (ObjList* o = m_items.skipNull(); o; o = o->skipNext()) {
	String tmp;
	(static_cast<XPathStep*>(o->get()))->dump(tmp,escape);
	if (sepFirst)
	    buf << itemSep << tmp;
	else {
	    buf << tmp;
	    sepFirst = true;
	}
    }
    return buf;
}

void XPath::dump(ObjList& lst, bool escape) const
{
    ObjList* a = &lst;
    for (ObjList* o = m_items.skipNull(); o; o = o->skipNext()) {
	String* tmp = new String;
	(static_cast<XPathStep*>(o->get()))->dump(*tmp,escape);
	a = a->append(tmp);
    }
}

String& XPath::escape(String& buf, const String& str, char quot, bool literal)
{
    if (quot != '"' && quot != '\'')
	quot = '"';
    if (!str)
	return buf << quot << quot;
    buf << quot;
    if (literal)
	return XPathParseData::escapeStringLiteral(buf,str,quot) << quot;
    return XmlSaxParser::escape(buf,str) << quot;
}

unsigned int XPath::maxStepPredicates()
{
    return XPATH_MAX_PREDICATES;
}

void XPath::reset()
{
    setStatus(NotParsed);
    m_items.clear();
}

const TokenDict* XPath::dictErrors()
{
    return s_xpathErrors;
}

bool XPath::setStatus(unsigned int code, unsigned int itemIdx, const char* error,
    XPathParseData* data)
{
    m_status = code;
    m_errorItem = itemIdx;
    m_error = error;
#ifdef XPATH_DEBUG_PARSE
    if (data && m_status) {
	String s;
	Debug("XPath",DebugNote,"Status %s data: index=%u '%s' [%p]",
	    describeError(s).c_str(),data->index(),data->c_str(),this);
    }
#endif
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
