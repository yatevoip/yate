/**
 * yatescript.h
 * Yet Another (Java)script library
 * This file is part of the YATE Project http://YATE.null.ro
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

#ifndef __YATESCRIPT_H
#define __YATESCRIPT_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYSCRIPT_EXPORTS
#define YSCRIPT_API __declspec(dllexport)
#else
#ifndef LIBYSCRIPT_STATIC
#define YSCRIPT_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YSCRIPT_API
#define YSCRIPT_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class ExpEvaluator;
class ExpOperation;
class ScriptMutex;
class JsArray;

/**
 * This class holds a JSON Pointer as specified in RFC 6901
 * @short JSON path
 */
class YSCRIPT_API JPath : public String
{
    YCLASS(JPath,String)
public:
    /**
     * Constructor
     * @param value Initial value
     */
    JPath(const char* value = 0);

    /**
     * Copy constructor
     * @param other Object to copy
     */
    JPath(const JPath& other);

    /**
     * Destructor
     */
    ~JPath();

    /**
     * Check if path is valid
     * @return True if valid, false otherwise
     */
    inline bool valid() const
	{ return m_data || !c_str(); }

    /**
     * Retrieve the number of items in path
     * @return The number of items in path
     */
    inline unsigned int count() const
	{ return m_count; }

    /**
     * Retrieve path item at a index
     * @param idx Index to retrieve
     * @return Path item at requested index, empty string if not found
     */
    inline const String& at(unsigned int idx) const
	{ return idx < count() ? m_data[idx] : String::empty(); }

    /**
     * Retrieve path item at a index
     * @param idx Index to retrieve
     * @return Path item at requested index, empty string if not found
     */
    inline const String& at(unsigned int idx)
	{ return idx < count() ? m_data[idx] : String::empty(); }

    /**
     * Retrieve path item at a index
     * @param idx Index to retrieve
     * @return Path item at requested index, empty string if not found
     */
    inline const String& operator[](unsigned int idx) const
	{ return at(idx); }

    /**
     * Retrieve path item at a index
     * @param idx Index to retrieve
     * @return Path item at requested index, empty string if not found
     */
    inline const String& operator[](unsigned int idx)
	{ return at(idx); }

    /**
     * Add a path item to path
     * @param path Destination string
     * @param value Item value to add
     * @return Given 'path' string reference
     */
    static inline String& addItem(String& path, const char* value) {
	    if (!value)
		return path;
	    String tmp;
	    char* s = (char*)value;
	    for (unsigned int i = 0; *s; ++i, ++s) {
		char c = escapeChar(*s);
		if (!c)
		    continue;
		if (!tmp)
		    tmp = value;
		tmp.insert(i,'~');
		s = (char*)(tmp.c_str() + (++i));
		*s = c;
	    }
	    return path << '/' << tmp.safe(value);
	}

    /**
     * Check if an item is a valid Array index
     * A valid array index is a decimal string with no leading/trailing spaces or leading 0
     * @param str String to check
     * @return Array index, negative if invalid
     */
    static inline int validArrayIndex(const String& str)
	{ return str.toInteger(-1,10); }

    /**
     * Retrieve escape char
     * @param value Requested character
     * @return Escape character, 0 if given character doesn't need to be escaped
     */
    static inline char escapeChar(char value) {
	    if (value == '~')
		return '0';
	    if (value == '/')
		return '1';
	    return 0;
	}

    /**
     * Retrieve unescape char
     * @param value Requested character
     * @return Unescape character, 0 if given character is not an escaped one
     */
    static inline char unescapeChar(char value) {
	    if (value == '0')
		return '~';
	    if (value == '1')
		return '/';
	    return 0;
	}

protected:
    /**
     * Called whenever the String value changed.
     * Reset data, parse the path
     */
    virtual void changed();

    /**
     * Parse the path
     */
    void parse();

    /**
     * Reset data
     */
    inline void reset() {
	    m_count = 0;
	    if (m_data) {
		delete[] m_data;
		m_data = 0;
	    }
	}

    String* m_data;
    unsigned int m_count;
};

/**
 * This class allows extending ExpEvaluator to implement custom fields and functions
 * @short ExpEvaluator extending interface
 */
class YSCRIPT_API ExpExtender
{
public:
    /**
     * Destructor
     */
    virtual ~ExpExtender()
	{ }

    /**
     * Retrieve the reference counted object owning this interface
     * @return Pointer to object owning the extender, NULL if no ownership
     */
    virtual RefObject* refObj();

    /**
     * Check if a certain field is assigned in extender
     * @param stack Evaluation stack in use
     * @param name Name of the field to test
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if the field is present
     */
    virtual bool hasField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Get a pointer to a field in extender
     * @param stack Evaluation stack in use
     * @param name Name of the field to retrieve
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return Pointer to field, NULL if not present
     */
    virtual NamedString* getField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Try to evaluate a single function
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to evaluate a single field
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to assign a value to a single field
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

/**
 * A class used to keep a parsing context
 * @short The parsing context
 */
class YSCRIPT_API ParsePoint
{
public:
    /**
     * Constructor
     * @param expr Expression to be parsed
     * @param eval ExpEvaluator associated with this parsing context
     * @param lineNo The line number that is currently parsed
     * @param fileName File name associated with this context
     * @param seps Searched separator during parsing
     */
    explicit inline ParsePoint(const char*& expr, ExpEvaluator* eval = 0, unsigned int lineNo = 0,
			const char* fileName = 0, const char* seps = 0)
	: m_expr(expr), m_searchedSeps(seps), m_count(0), m_foundSep(0), m_lineNo(lineNo), m_eval(eval), m_fileName(fileName)
	{ }

    /**
     * Assignment operator
     * @param parsePoint Parsing context which is to be assigned to this parsing context
     */
    ParsePoint& operator=(ParsePoint& parsePoint);

    /**
     * Line number assignment
     * @param line New line number
     */
    ParsePoint& operator=(unsigned int line);

    /**
     * Cast operator to const char*&
     */
    inline operator const char*&()
	{ return m_expr; }

    /**
     * Assignement from const char*
     */
    inline ParsePoint& operator=(const char* newExpr)
    {
	m_expr = newExpr;
	return *this;
    }

    /**
     * Prefix incrementation operator. Incrementes the internal expression
     */
    inline ParsePoint& operator++() // prefix
    {
	++m_expr;
	return *this;
    }

    /**
     * Postfix incrementation operator. Incrementes the internal expression
     */
    inline ParsePoint& operator++(int unused) // postfix
    {
	++m_expr;
	return *this;
    }

    /**
     * Get first char in the parsed expression
     * @return First char in the expression to be parsed
     */
    inline char firstChar()
	{ return *m_expr; }

    /**
     * Get line number of the parsed expression
     * @return Line number stored in parse point
     */
    inline unsigned lineNumber() const
	{ return m_lineNo; }

    /**
     * Expression to be parsed
     */
    const char* m_expr;

    /**
     * Searched instruction separators
     */
    const char* m_searchedSeps;

    /**
     * Number of how many times the parser must encouter a separator
     */
    unsigned int m_count;

    /**
     * Separator that the parser encountered
     */
    char m_foundSep;

    /**
     * Line numbet at which parsing is taking place
     */
    unsigned int m_lineNo;

    /**
     * ExpEvaluator associated with this parsing context
     */
    ExpEvaluator* m_eval;

    /**
    * File name associated for this context
    */
    String m_fileName;
};

/**
 * A class used to build stack based (posifix) expression parsers and evaluators
 * @short An expression parser and evaluator
 */
class YSCRIPT_API ExpEvaluator : public DebugEnabler
{
    friend class ParsePoint;
public:
    /**
     * Parsing styles
     */
    enum Parser {
	C,
	SQL,
    };

    /**
     * Operation codes
     */
    enum Opcode {
	// FORTH style notation of effect on stack, C-syntax expression
	OpcNone = 0,// ( --- )
	OpcNull,    // ( --- A)
	OpcPush,    // ( --- A)
	OpcDrop,    // (A --- )
	OpcDup,     // (A --- A A)
	OpcSwap,    // (A B --- B A)
	OpcRot,     // (A B C --- B C A)
	OpcOver,    // (A B --- A B A)
	// Arithmetic operators
	OpcAdd,     // (A B --- A+B)
	OpcSub,     // (A B --- A-B)
	OpcMul,     // (A B --- A*B)
	OpcDiv,     // (A B --- A/B)
	OpcMod,     // (A B --- A%B)
	OpcNeg,     // (A --- -A)
	OpcIncPre,  // (A --- ++A)
	OpcDecPre,  // (A --- --A)
	OpcIncPost, // (A --- A++)
	OpcDecPost, // (A --- A--)
	// Bitwise logic operators
	OpcAnd,     // (A B --- A&B)
	OpcOr,      // (A B --- A|B)
	OpcXor,     // (A B --- A^B)
	OpcNot,     // (A --- ~A)
	OpcShl,     // (A B --- A<<B)
	OpcShr,     // (A B --- A>>B)
	// Boolean logic operators
	OpcLAnd,    // (A B --- A&&B)
	OpcLOr,     // (A B --- A||B)
	OpcLXor,    // (A B --- A^^B)
	OpcLNot,    // (A --- !A)
	// String concatenation
	OpcCat,     // (A B --- A.B)
	// String matching
	OpcReM,     // (A B --- Amatch/B/)
	OpcReIM,    // (A B --- Amatch_insensitive/B/)
	OpcReNm,    // (A B --- A!match/B/)
	OpcReINm,   // (A B --- A!match_insensitive/B/)
	OpcLike,    // (A B --- AlikeB)
	OpcILike,   // (A B --- Alike_insensitiveB)
	OpcNLike,   // (A B --- A!likeB)
	OpcNIlike,  // (A B --- A!like_insensitiveB)
	// Comparation operators
	OpcEq,      // (A B --- A==B)
	OpcNe,      // (A B --- A!=B)
	OpcGt,      // (A B --- A>B)
	OpcLt,      // (A B --- A<B)
	OpcGe,      // (A B --- A>=B)
	OpcLe,      // (A B --- A<=B)
	// Ternary conditional operator
	OpcCond,    // (A B C --- A?B:C)
	// Field naming operator
	OpcAs,      // (A B --- A[name=B])
	// Field replacement
	OpcField,   // (A --- A)
	// Call of function with N parameters
	OpcFunc,    // (... funcN --- func(...))
	// Label for a jump
	OpcLabel,   // ( --- )
	// Push with deep copy
	OpcCopy,    // ( --- CopiedA)
	// Nullish coalescing
	OpcNullish, // (A B --- A??B)
	// Field assignment - can be ORed with other binary operators
	OpcAssign  = 0x0100, // (A B --- B,(&A=B))
	// Private extension area for derived classes
	OpcPrivate = 0x1000
    };

    /**
     * Constructs an evaluator from an operator dictionary
     * @param operators Pointer to operator dictionary, longest strings first
     * @param unaryOps Pointer to unary operators dictionary, longest strings first
     */
    explicit ExpEvaluator(const TokenDict* operators = 0, const TokenDict* unaryOps = 0);

    /**
     * Constructs an evaluator from a parser style
     * @param style Style of parsing to use
     */
    explicit ExpEvaluator(Parser style);

    /**
     * Copy constructor
     * @param original Evaluator to copy the operation list from
     */
    ExpEvaluator(const ExpEvaluator& original);

    /**
     * Destructor
     */
    virtual ~ExpEvaluator();

    /**
     * Parse and compile an expression
     * @param expr Parsing context to compile
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Number of expressions compiled, zero on error
     */
    int compile(ParsePoint& expr, GenObject* context = 0);

    /**
     * Evaluate the expression, optionally return results
     * @param results List to fill with results row
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if expression evaluation succeeded, false on failure
     */
    bool evaluate(ObjList* results, GenObject* context = 0) const;

    /**
     * Evaluate the expression, return computed results
     * @param results List to fill with results row
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if expression evaluation succeeded, false on failure
     */
    inline bool evaluate(ObjList& results, GenObject* context = 0) const
	{ return evaluate(&results,context); }

    /**
     * Evaluate the expression, return computed results
     * @param results List of parameters to populate with results row
     * @param index Index of result row, zero to not include an index
     * @param prefix Prefix to prepend to parameter names
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Number of result columns, -1 on failure
     */
    int evaluate(NamedList& results, unsigned int index = 0, const char* prefix = 0, GenObject* context = 0) const;

    /**
     * Evaluate the expression, return computed results
     * @param results Array of result rows to populate
     * @param index Index of result row, zero to just set column headers
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Number of result columns, -1 on failure
     */
    int evaluate(Array& results, unsigned int index, GenObject* context = 0) const;

    /**
     * Simplify the expression, performs constant folding
     * @return True if the expression was simplified
     */
    inline bool simplify()
	{ return trySimplify(); }

    /**
     * Check if a parse or compile error was encountered
     * @return True if the evaluator encountered an error
     */
    inline bool inError() const
	{ return m_inError; }

    /**
     * Retrieve the number of line currently being parsed
     * @return Number of current parsed line, 1 is the first line
     */
    inline unsigned int lineNumber() const
	{ return m_lineNo; }

    /**
     * Check if the expression is empty (no operands or operators)
     * @return True if the expression is completely empty
     */
    virtual bool null() const;

    /**
     * Dump a list of operations according to current operators dictionary
     * @param codes List of operation codes
     * @param res Result string representation of operations
     * @param lineNo True to include line numbers
     */
    void dump(const ObjList& codes, String& res, bool lineNo = false) const;

    /**
     * Dump the postfix expression according to current operators dictionary
     * @param res Result string representation of operations
     * @param lineNo True to include line numbers
     */
    virtual void dump(String& res, bool lineNo = false) const;

    /**
     * Dump a list of operations according to current operators dictionary
     * @param codes List of operation codes
     * @param lineNo True to include line numbers
     * @return String representation of operations
     */
    inline String dump(const ObjList& codes, bool lineNo = false) const
	{ String s; dump(codes,s,lineNo); return s; }

    /**
     * Dump the postfix expression according to current operators dictionary
     * @param lineNo True to include line numbers
     * @return String representation of operations
     */
    inline String dump(bool lineNo = false) const
	{ String s; dump(s,lineNo); return s; }

    /**
     * Retrieve the internally used operator dictionary
     * @return Pointer to operators dictionary in use
     */
    inline const TokenDict* operators() const
	{ return m_operators; }

    /**
     * Retrieve the internally used unary operators dictionary
     * @return Pointer to unary operators dictionary in use
     */
    inline const TokenDict* unaryOps() const
	{ return m_unaryOps; }

    /**
     * Retrieve the internally used expression extender
     * @return Pointer to the extender in use, NULL if none
     */
    inline ExpExtender* extender() const
	{ return m_extender; }

    /**
     * Set the expression extender to use in evaluation
     * @param ext Pointer to the extender to use, NULL to remove current
     */
    void extender(ExpExtender* ext);

    /**
     * Retrieve the line number from one to three operands
     * @param op1 First operand
     * @param op2 Optional second operand
     * @param op3 Optional third operand
     * @return Line number at compile time, zero if not found
     */
    static unsigned int getLineOf(ExpOperation* op1, ExpOperation* op2 = 0, ExpOperation* op3 = 0);

    /**
     * Push an operand on an evaluation stack
     * @param stack Evaluation stack to remove the operand from
     * @param oper Operation to push on stack, NULL will not be pushed
     */
    static void pushOne(ObjList& stack, ExpOperation* oper);

    /**
     * Pops an operand off an evaluation stack, does not pop a barrier
     * @param stack Evaluation stack to remove the operand from
     * @return Operator removed from stack, NULL if stack underflow
     */
    static ExpOperation* popOne(ObjList& stack);

    /**
     * Pops any operand (including barriers) off an evaluation stack
     * @param stack Evaluation stack to remove the operand from
     * @return Operator removed from stack, NULL if stack underflow
     */
    static ExpOperation* popAny(ObjList& stack);

    /**
     * Pops and evaluate the value of an operand off an evaluation stack, does not pop a barrier
     * @param stack Evaluation stack to remove the operand from
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Value removed from stack, NULL if stack underflow or field not evaluable
     */
    virtual ExpOperation* popValue(ObjList& stack, GenObject* context = 0) const;

    /**
     * Try to evaluate a single operation
     * @param stack Evaluation stack in use, operands are popped off this stack
     *  and results are pushed back on stack
     * @param oper Operation to execute
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runOperation(ObjList& stack, const ExpOperation& oper, GenObject* context = 0) const;

    /**
     * Convert all fields on the evaluation stack to their values
     * @param stack Evaluation stack to evaluate fields from
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if all fields on the stack were evaluated properly
     */
    virtual bool runAllFields(ObjList& stack, GenObject* context = 0) const;

protected:
    /**
     * Method to skip over whitespaces, count parsed lines too
     * @param expr Current parsing context, advances on expression to be compiled
     * @return First character after whitespaces where expr points
     */
    virtual char skipWhites(ParsePoint& expr);

    /**
     * Helper method to conditionally convert to lower case
     * @param chr Character to convert
     * @param makeLower True to convert chr to lower case
     * @return Converted character or original if conversion not requested
     */
    inline static char condLower(char chr, bool makeLower)
	{ return (makeLower && ('A' <= chr) && (chr <= 'Z')) ? (chr + ('a' - 'A')) : chr; }

    /**
     * Helper method to return next operator in the parsed text
     * @param expr Pointer to text to parse, gets advanced if succeeds
     * @param operators Pointer to operators table to use
     * @param caseInsensitive Match case-insensitive if set
     * @return Operator code, OpcNone on failure
     */
    Opcode getOperator(const char*& expr, const TokenDict* operators, bool caseInsensitive = false) const;

    /**
     * Check if a character can be a letter character in a keyword or identifier
     * @param c Character to check
     * @return True if the character can be part of a keyword or identifier
     */
    virtual bool keywordLetter(char c) const;

    /**
     * Check if a character can be can be a digit character in a keyword or identifier
     * @param c Character to check
     * @return True if the character can be part of a keyword or identifier
     */
    virtual bool keywordDigit(char c) const;

    /**
     * Check if a character can be part of a keyword or identifier
     * @param c Character to check
     * @return True if the character can be part of a keyword or identifier
     */
    virtual bool keywordChar(char c) const;

    /**
     * Helper method to count characters making a keyword
     * @param str Pointer to text without whitespaces in front
     * @return Length of the keyword, 0 if a valid keyword doesn't follow
     */
    virtual int getKeyword(const char* str) const;

    /**
     * Helper method to display debugging errors internally
     * @param error Text of the error
     * @param text Optional text that caused the error
     * @param line Number of line generating the error, zero for parsing errors
     * @return Always returns false
     */
    bool gotError(const char* error = 0, const char* text = 0, unsigned int line = 0) const;

    /**
     * Helper method to set error flag and display debugging errors internally
     * @param error Text of the error
     * @param text Optional text that caused the error
     * @param line Number of line generating the error, zero for parsing errors
     * @return Always returns false
     */
    bool gotError(const char* error = 0, const char* text = 0, unsigned int line = 0);

    /**
     * Helper method to display debugging errors internally
     * @param error Text of the error
     * @param line Number of line generating the error, zero for parsing errors
     * @return Always returns false
     */
    inline bool gotError(const char* error, unsigned int line) const
	{ return gotError(error, 0, line); }

    /**
     * Helper method to set error flag and display debugging errors internally
     * @param error Text of the error
     * @param line Number of line generating the error, zero for parsing errors
     * @return Always returns false
     */
    inline bool gotError(const char* error, unsigned int line)
	{ return gotError(error, 0, line); }

    /**
     * Formats a line number to display in error messages
     * @param buf String buffer used to return the value
     * @param line Line number to format
     */
    virtual void formatLineNo(String& buf, unsigned int line) const;

    /**
     * Runs the parser and compiler for one (sub)expression
     * @param expr Current parsing context, advances on expression to be compiled
     * @param stop Optional character expected after the expression
     * @param nested User defined object to pass for nested parsing
     * @return True if one expression was compiled and a separator follows
     */
    bool runCompile(ParsePoint& expr, char stop, GenObject* nested = 0);

    /**
     * Runs the parser and compiler for one (sub)expression
     * @param expr Current parsing context, advances on expression to be compiled
     * @param stop Optional list of possible characters expected after the expression
     * @param nested User defined object to pass for nested parsing
     * @return True if one expression was compiled and a separator follows
     */
    virtual bool runCompile(ParsePoint& expr, const char* stop = 0, GenObject* nested = 0);

    /**
     * Skip over comments and whitespaces
     * @param expr Current parsing context, advances on expression to be compiled
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return First character after comments or whitespaces where expr points
     */
    virtual char skipComments(ParsePoint& expr, GenObject* context = 0);

    /**
     * Process top-level preprocessor directives
     * @param expr Current parsing context, advances on expression to be compiled
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Number of expressions compiled, negative if no more directives
     */
    virtual int preProcess(ParsePoint& expr, GenObject* context = 0);

    /**
     * Returns next operator in the parsed text
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @return Operator code, OpcNone on failure
     */
    virtual Opcode getOperator(ParsePoint& expr);

    /**
     * Returns next unary operator in the parsed text
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @return Operator code, OpcNone on failure
     */
    virtual Opcode getUnaryOperator(ParsePoint& expr);

    /**
     * Returns next unary postfix operator in the parsed text
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @param precedence The precedence of the previous operator
     * @return Operator code, OpcNone on failure
     */
    virtual Opcode getPostfixOperator(ParsePoint& expr, int precedence = 0);

    /**
     * Helper method to get the canonical name of an operator
     * @param oper Operator code
     * @return name of the operator, NULL if it doesn't have one
     */
    virtual const char* getOperator(Opcode oper) const;

    /**
     * Get the precedence of an operator
     * @param oper Operator code
     * @return Precedence of the operator, zero (lowest) if unknown
     */
    virtual int getPrecedence(Opcode oper) const;

    /**
     * Get the associativity of an operator
     * @param oper Operator code
     * @return True if the operator is right-to-left associative, false if left-to-right
     */
    virtual bool getRightAssoc(Opcode oper) const;

    /**
     * Check if we are at an expression separator and optionally skip past it
     * @param expr Current parsing context to check, advances on expression to be compiled if asked to remove separator
     * @param remove True to skip past the found separator
     * @return True if a separator was found
     */
    virtual bool getSeparator(ParsePoint& expr, bool remove);

    /**
     * Get an instruction or block, advance parsing pointer past it
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @param stop Optional character expected after the instruction
     * @param nested User defined object passed from nested parsing
     * @return True if succeeded, must add the operands internally
     */
    virtual bool getInstruction(ParsePoint& expr, char stop = 0, GenObject* nested = 0);

    /**
     * Get an operand, advance parsing pointer past it
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @param endOk Consider reaching the end of string a success
     * @param precedence The precedence of the previous operator
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getOperand(ParsePoint& expr, bool endOk = true, int precedence = 0);

    /**
     * Get an inline simple type, usually string or number
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @param constOnly Return only inline constants
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getSimple(ParsePoint& expr, bool constOnly = false);

    /**
     * Get a numerical operand, advance parsing pointer past it
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getNumber(ParsePoint& expr);

    /**
     * Get a string operand, advance parsing pointer past it
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getString(ParsePoint& expr);

    /**
     * Get a function call, advance parsing pointer past it
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getFunction(ParsePoint& expr);

    /**
     * Helper method - get a string, advance parsing pointer past it
     * @param expr Pointer to string separator, gets advanced on success
     * @param str String in which the result is returned
     * @return True if succeeded
     */
    virtual bool getString(const char*& expr, String& str);

    /**
     * Helper method - get an escaped component of a string
     * @param expr Pointer past escape character, gets advanced on success
     * @param str String in which the result is returned
     * @param sep String separator character
     * @return True if succeeded
     */
    virtual bool getEscape(const char*& expr, String& str, char sep);

    /**
     * Get a field keyword, advance parsing pointer past it
     * @param expr Current parsing context, advances on expression to be compiled if it succeeds
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getField(ParsePoint& expr);

    /**
     * Add an aready built operation to the expression and set its line number
     * @param oper Operation to add
     * @param line Line number where operation was compiled, zero to used parsing point
     */
    void addOpcode(ExpOperation* oper, unsigned int line = 0);

    /**
     * Add a simple operator to the expression
     * @param oper Operator code to add
     * @param barrier True to create an evaluator stack barrier
     * @param line Line number where operation was compiled, zero to used parsing point
     * @return Newly added operation
     */
    ExpOperation* addOpcode(Opcode oper, bool barrier = false, unsigned int line = 0);

    /**
     * Add a simple operator to the expression
     * @param oper Operator code to add
     * @param value 64 bit nteger value to add
     * @param barrier True to create an evaluator stack barrier
     * @return Newly added operation
     */
    ExpOperation* addOpcode(Opcode oper, int64_t value, bool barrier = false);

    /**
     * Add a string constant to the expression
     * @param value String value to add, will be pushed on execution
     * @return Newly added operation
     */
    ExpOperation* addOpcode(const String& value);

    /**
     * Add an integer constant to the expression
     * @param value Integer value to add, will be pushed on execution
     * @return Newly added operation
     */
    ExpOperation* addOpcode(int64_t value);

    /**
     * Add a boolean constant to the expression
     * @param value Boolean value to add, will be pushed on execution
     * @return Newly added operation
     */
    ExpOperation* addOpcode(bool value);

    /**
     * Add a function or field to the expression
     * @param oper Operator code to add, must be OpcField or OpcFunc
     * @param name Name of the field or function, case sensitive
     * @param value Numerical value used as parameter count to functions
     * @param barrier True to create an exavuator stack barrier
     * @param line Line number where operation was compiled, zero to used parsing point
     * @return Newly added operation
     */
    ExpOperation* addOpcode(Opcode oper, const String& name, int64_t value = 0,
	bool barrier = false, unsigned int line = 0);

    /**
     * Remove from the code and return the last operation
     * @return Operation removed from end of code, NULL if no operations remaining
     */
    ExpOperation* popOpcode();

    /**
     * Try to apply simplification to the expression
     * @return True if the expression was simplified
     */
    virtual bool trySimplify();

    /**
     * Try to evaluate a list of operation codes
     * @param opcodes List of operation codes to evaluate
     * @param stack Evaluation stack in use, results are left on stack
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runEvaluate(const ObjList& opcodes, ObjList& stack, GenObject* context = 0) const;

    /**
     * Try to evaluate a vector of operation codes
     * @param opcodes ObjVector of operation codes to evaluate
     * @param stack Evaluation stack in use, results are left on stack
     * @param context Pointer to arbitrary object to be passed to called methods
     * @param index Index in operation codes to start evaluation from
     * @return True if evaluation succeeded
     */
    virtual bool runEvaluate(const ObjVector& opcodes, ObjList& stack, GenObject* context = 0, unsigned int index = 0) const;

    /**
     * Try to evaluate the expression
     * @param stack Evaluation stack in use, results are left on stack
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runEvaluate(ObjList& stack, GenObject* context = 0) const;

    /**
     * Try to evaluate a single function
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context = 0) const;

    /**
     * Try to evaluate a single field
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context = 0) const;

    /**
     * Try to assign a value to a single field
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context = 0) const;

    /**
     * Dump a single operation according to current operators dictionary
     * @param oper Operation to dump
     * @param res Result string representation of operations
     * @param lineNo True to include line numbers
     */
    virtual void dump(const ExpOperation& oper, String& res, bool lineNo = false) const;

    /**
     * Internally used operator dictionary
     */
    const TokenDict* m_operators;

    /**
     * Internally used unary operators dictionary
     */
    const TokenDict* m_unaryOps;

    /**
     * Internally used list of operands and operator codes
     */
    ObjList m_opcodes;

    /**
     * Internally used for faster appending to the operator codes list
     */
    ObjList* m_lastOpcode;

    /**
     * Flag that we encountered a parse or compile error
     */
    bool m_inError;

    /**
     * Current line index
     */
    unsigned int m_lineNo;

private:
    bool getOperandInternal(ParsePoint& expr, bool endOk, int precedence);
    ExpExtender* m_extender;
};

/**
 * This class describes a single operation in an expression evaluator
 * @short A single operation in an expression
 */
class YSCRIPT_API ExpOperation : public NamedString
{
    friend class ExpEvaluator;
    YCLASS(ExpOperation,NamedString)
public:
    /**
     * Special value that is not recognized as an integer value
     * @return A value that indicates a non-integer value
     */
    inline static int64_t nonInteger()
	{ return LLONG_MIN; }

    /**
     * Copy constructor
     * @param original Operation to copy
     */
    inline ExpOperation(const ExpOperation& original)
	: NamedString(original.name(),original),
	  m_opcode(original.opcode()), m_number(original.number()), m_bool(original.isBoolean()),
	  m_isNumber(original.isNumber()), m_lineNo(original.lineNumber()), m_barrier(original.barrier())
	{ }

    /**
     * Copy constructor with renaming, to be used for named results
     * @param original Operation to copy
     * @param name Name of the newly created operation
     * @param copyType True to copy operation type, false to create an OpcPush
     */
    inline ExpOperation(const ExpOperation& original, const char* name, bool copyType = true)
	: NamedString(name,original),
	  m_opcode(copyType ? original.opcode() : ExpEvaluator::OpcPush),
	  m_number(original.number()), m_bool(original.isBoolean()),
	  m_isNumber(original.isNumber()), m_lineNo(original.lineNumber()), m_barrier(original.barrier())
	{ }

    /**
     * Push String constructor
     * @param value String constant to push on stack on execution
     * @param name Optional of the newly created constant
     * @param autoNum Automatically convert to number if possible
     */
    inline explicit ExpOperation(const String& value, const char* name = 0, bool autoNum = false)
	: NamedString(name,value),
	  m_opcode(ExpEvaluator::OpcPush),
	  m_number(autoNum ? value.toInt64(nonInteger()) : nonInteger()),
	  m_bool(autoNum && value.isBoolean()),
	  m_isNumber(autoNum && (value == YSTRING("NaN") || m_number != nonInteger())),
	  m_lineNo(0), m_barrier(false)
	{ if (m_bool) { m_number = value.toBoolean() ? 1 : 0; m_isNumber = true;} }

    /**
     * Push literal string constructor
     * @param value String constant to push on stack on execution
     * @param name Optional of the newly created constant
     */
    inline explicit ExpOperation(const char* value, const char* name = 0)
	: NamedString(name,value),
	  m_opcode(ExpEvaluator::OpcPush), m_number(nonInteger()), m_bool(false),
	  m_isNumber(false), m_lineNo(0), m_barrier(false)
	{ }

    /**
     * Push 64 bit Number constructor
     * @param value Integer constant to push on stack on execution
     * @param name Optional of the newly created constant
     */
    inline explicit ExpOperation(int64_t value, const char* name = 0)
	: NamedString(name,"NaN"),
	  m_opcode(ExpEvaluator::OpcPush),
	  m_number(value), m_bool(false), m_isNumber(true), m_lineNo(0), m_barrier(false)
	{ if (value != nonInteger()) String::operator=(value); }

    /**
     * Push Boolean constructor
     * @param value Boolean constant to push on stack on execution
     * @param name Optional of the newly created constant
     */
    inline explicit ExpOperation(bool value, const char* name = 0)
	: NamedString(name,String::boolText(value)),
	  m_opcode(ExpEvaluator::OpcPush),
	  m_number(value ? 1 : 0), m_bool(true), m_isNumber(true),
	  m_lineNo(0), m_barrier(false)
	{ }

    /**
     * Constructor from components
     * @param oper Operation code
     * @param name Optional name of the operation or result
     * @param value Optional integer constant used as function parameter count
     * @param barrier True if the operation is an expression barrier on the stack
     */
    inline ExpOperation(ExpEvaluator::Opcode oper, const char* name = 0, int64_t value = nonInteger(), bool barrier = false)
	: NamedString(name,""),
	  m_opcode(oper), m_number(value), m_bool(false), m_isNumber(false),
	  m_lineNo(0), m_barrier(barrier)
	{ }

    /**
     * Constructor of non-integer operation from components
     * @param oper Operation code
     * @param name Name of the operation or result
     * @param value String value of operation
     * @param barrier True if the operation is an expression barrier on the stack
     */
    inline ExpOperation(ExpEvaluator::Opcode oper, const char* name, const char* value, bool barrier = false)
	: NamedString(name,value),
	  m_opcode(oper), m_number(nonInteger()), m_bool(false), m_isNumber(false),
	  m_lineNo(0), m_barrier(barrier)
	{ }

    /**
     * Constructor from components
     * @param oper Operation code
     * @param name Optional name of the operation or result
     * @param value String value of operation
     * @param number Integer value
     * @param barrier True if the operation is an expression barrier on the stack
     */
    inline ExpOperation(ExpEvaluator::Opcode oper, const char* name, const char* value, int64_t number, bool barrier)
	: NamedString(name,value),
	  m_opcode(oper), m_number(number), m_bool(false), m_isNumber(true),
	  m_lineNo(0), m_barrier(barrier)
	{ }

    /**
     * Retrieve the code of this operation
     * @return Operation code as declared in the expression evaluator
     */
    inline ExpEvaluator::Opcode opcode() const
	{ return m_opcode; }

    /**
     * Check if an integer value is stored
     * @return True if an integer value is stored
     */
    inline bool isInteger() const
	{ return m_number != nonInteger(); }

    /**
     * Retrieve the number stored in this operation
     * @return Stored number
     */
    inline int64_t number() const
	{ return m_number; }

    /**
     * Check if a boolean value is stored
     * @return True if a boolean value is stored
     */
    inline bool isBoolean() const
	{ return m_bool; }

    /**
     * Check if a number type value is stored
     * @return True if a number type value is stored
     */
    inline bool isNumber() const
	{ return m_isNumber; }

    /**
     * Check if this operation acts as an evaluator barrier on the stack
     * @return True if an expression should not pop this operation off the stack
     */
    inline bool barrier() const
	{ return m_barrier; }

    /**
     * Retrieve the line number where the operation was compiled from
     * @return Line number, zero if unknown
     */
    inline unsigned int lineNumber() const
	{ return m_lineNo; }

    /**
     * Set the line number where the operation was compiled from
     * @param line Number of the compiled line
     */
    inline void lineNumber(unsigned int line)
	{ m_lineNo = line; }

    /**
     * Number assignment operator
     * @param num Numeric value to assign to the operation
     * @return Assigned number
     */
    inline int64_t operator=(int64_t num)
	{ m_number = num; String::operator=(num); m_isNumber = true; return num; }

    /**
     * Retrieve the numeric value of the operation
     * @param defVal Default to return if not a number
     * @return Number contained in operation
     */
    virtual int64_t valInteger(int64_t defVal = 0) const;

    /**
     * Convert to number
     * @return Value converted to number, NaN if not possible of if stored value is NaN
     */
    virtual int64_t toNumber() const;

    /**
     * Retrieve the boolean value of the operation
     * @param defVal Default to return if not a boolean
     * @return True if the operation is to be interpreted as true value
     */
    virtual bool valBoolean(bool defVal = false) const;

    /**
     * Retrieve the name of the type of the value of this operation
     * @return Name of the type of the value
     */
    virtual const char* typeOf() const;

    /**
     * Clone and rename method
     * @param name Name of the cloned operation
     * @return New operation instance
     */
    virtual ExpOperation* clone(const char* name) const;

    /**
     * Clone method
     * @return New operation instance, may keep a reference to the old instance
     */
    inline ExpOperation* clone() const
	{ return clone(name()); }

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied object
     * @return New operation instance
     */
    virtual ExpOperation* copy(ScriptMutex* mtx) const
	{ return clone(); }

private:
    ExpEvaluator::Opcode m_opcode;
    int64_t m_number;
    bool m_bool;
    bool m_isNumber;
    unsigned int m_lineNo;
    bool m_barrier;
};

/**
 * This class implements an ExpOperation vector
 * @short An ExpOperation vector
 */
class YSCRIPT_API ExpOperVector : public String
{
    YCLASS(ExpOperVector,String)
public:
    /**
     * Constructor
     * @param len Initial length
     * @param name Optional vector name
     */
    inline ExpOperVector(unsigned int len = 0, const char* name = 0)
	: String(name), m_data(len)
	{}

    /**
     * Init from other  constructor
     * Copy references from given vector
     * @param other Vector to copy
     */
    inline ExpOperVector(ExpOperVector& other)
	: m_data(other.length())
	{
	    for (unsigned int i = 0; i < length(); ++i) {
		ExpOperation* op = other[i];
		if (op)
		    set(op->clone(),i);
	    }
	}

    /**
     * Retrieve vector length
     * @return Vector length
     */
    inline unsigned int length() const
	{ return m_data.length(); }

    /**
     * Retrieve the number of non NULL objects in vector
     * @return The number of non NULL objects in vector
     */
    inline unsigned int count() const
	{ return m_data.count(); }

    /**
     * Retrieve an item at given index
     * @param idx Index to retrieve
     * @return ExpOperation pointer, NULL if not set or index is out of bounds
     */
    inline ExpOperation* at(unsigned int idx) const
	{ return static_cast<ExpOperation*>(m_data.at(idx)); }

    /**
     * Retrieve an item at given index
     * @param idx Index to retrieve
     * @return ExpOperation pointer, NULL if not set or index is out of bounds
     */
    inline ExpOperation* operator[](unsigned int idx) const
	{ return at(idx); }

    /**
     * Take item at given index
     * @param idx Index to take
     * @return ExpOperation pointer, NULL if not set or index is out of bounds
     */
    inline ExpOperation* take(unsigned int idx)
	{ return static_cast<ExpOperation*>(m_data.take(idx)); }

    /**
     * Replace item at given index
     * @param op New item to set
     * @param idx Index to set
     * @param consume Consume the item if failed to be set
     */
    inline void set(ExpOperation* op, unsigned int idx, bool consume = true) {
	    if (!m_data.set(op,idx) && consume)
		TelEngine::destruct(op);
	}

    /**
     * Clear the vector
     */
    inline void clear()
	{ m_data.clear(); }

    /**
     * Resize the vector. No changes are made if length is the same as current
     * @param len New length
     * @param keepData Keep old data (append). Default: false
     * @return True on success, false on failure (memory allocation error)
     */
    inline bool resize(unsigned int len, bool keepData = false)
	{ return len == m_data.resize(len,keepData); }

    /**
     * Clone vector items to ObjList
     * @param list Destination list
     */
    inline void cloneTo(ObjList& list) const {
	    for (unsigned int i = 0; i < length(); ++i) {
		ExpOperation* op = at(i);
		if (op)
		    list.append(op->clone());
	    }
	}

    /**
     * Move vector items to ObjList. Clear the vector
     * @param list Destination list
     */
    inline void moveTo(ObjList& list) {
	    for (unsigned int i = 0; i < length(); ++i) {
		ExpOperation* op = take(i);
		if (op)
		    list.append(op);
	    }
	    clear();
	}

    /**
     * Clone another vector into this one into this vector
     * @param other Vector to clone
     * @param offs Offset in input vector to start from
     * @param keepData Keep old data (append)
     * @param count Optional number of elements to clone, negative for all available
     * @return Reference of this vector
     */
    inline ExpOperVector& cloneFrom(const ExpOperVector& other, unsigned int offs = 0,
	bool keepData = false, int count = -1) {
	    if (!count || offs >= other.length()) {
		if (!keepData)
		    clear();
		return *this;
	    }
	    unsigned int start = keepData ? length() : 0;
	    resize(other.length() - offs,count,start);
	    for (; start < length(); ++start) {
		ExpOperation* op = other[offs++];
		if (op)
		    set(op->clone(),start);
	    }
	    return *this;
	}

    /**
     * Take other vector's data into this one into this vector
     * @param other Vector to clone
     * @param offs Offset in input vetor to start from
     * @param keepData Keep old data (append). Default: false
     * @param count Optional number of elements to clone, negative for all available
     * @return Reference of this vector
     */
    inline ExpOperVector& takeFrom(ExpOperVector& other, unsigned int offs = 0,
	bool keepData = false, int count = -1) {
	    if (!count || offs >= other.length()) {
		if (!keepData)
		    clear();
		return *this;
	    }
	    unsigned int start = keepData ? length() : 0;
	    resize(other.length() - offs,count,start);
	    for (; start < length(); ++start)
		set(other.take(offs++),start);
	    return *this;
	}

protected:
    inline void resize(unsigned int len, int count, unsigned int keepData = 0) {
	    if (count < 0 || count > (int)len)
		resize(keepData + len,0 != keepData);
	    else
		resize(keepData + count,0 != keepData);
	}
    ObjVector m_data;
};

/**
 * Small helper class that simplifies declaring native functions
 * @short Helper class to declare a native function
 */
class YSCRIPT_API ExpFunction : public ExpOperation
{
    YCLASS(ExpFunction,ExpOperation)
public:
    /**
     * Constructor
     * @param name Name of the function
     * @param argc Number of arguments expected by function
     * @param barrier True if the function is an expression barrier on the stack
     */
    inline ExpFunction(const char* name, long int argc = 0, bool barrier = false)
	: ExpOperation(ExpEvaluator::OpcFunc,name,argc,barrier)
	{ if (name) (*this) << "[function " << name << "()]"; }

    /**
     * Retrieve the boolean value of the function (not of its result)
     * @param defVal Parameter ignored
     * @return Always true
     */
    virtual bool valBoolean(bool defVal = false) const
	{ return true; }

    /**
     * Clone and rename method
     * @param name Name of the cloned operation
     * @return New operation instance
     */
    virtual ExpOperation* clone(const char* name) const;
};

/**
 * Helper class that allows wrapping entire objects in an operation
 * @short Object wrapper for evaluation
 */
class YSCRIPT_API ExpWrapper : public ExpOperation
{
public:
    /**
     * Constructor
     * @param object Pointer to the object to wrap
     * @param name Optional name of the wrapper
     * @param barrier True if the operation is an expression barrier on the stack
     */
    inline ExpWrapper(GenObject* object, const char* name = 0, bool barrier = false)
	: ExpOperation(ExpEvaluator::OpcPush,name,
	    object ? object->toString().c_str() : (const char*)0,barrier),
	  m_object(object)
	{ }

    /**
     * Constructor with special operation
     * @param opcode Operation code of the wrapper
     * @param object Pointer to the object to wrap
     * @param name Optional name of the wrapper
     */
    inline ExpWrapper(ExpEvaluator::Opcode opcode, GenObject* object, const char* name = 0)
	: ExpOperation(opcode,name,object ? object->toString().c_str() : (const char*)0,false),
	  m_object(object)
	{ }

    /**
     * Destructor, deletes the held object
     */
    virtual ~ExpWrapper()
	{ TelEngine::destruct(m_object); }

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Retrieve the boolean value of the operation
     * @param defVal Parameter ignored
     * @return True if the wrapped object is to be interpreted as true value
     */
    virtual bool valBoolean(bool defVal = false) const;

    /**
     * Retrieve the name of the type of the value of this operation
     * @return Name of the type of the value
     */
    virtual const char* typeOf() const;

    /**
     * Clone and rename method
     * @param name Name of the cloned operation
     * @return New operation instance
     */
    virtual ExpOperation* clone(const char* name) const;

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied object
     * @return New operation instance
     */
    virtual ExpOperation* copy(ScriptMutex* mtx) const;

    /**
     * Object access method
     * @return Pointer to the held object
     */
    GenObject* object() const
	{ return m_object; }

    /**
     * Replace held object if given object is not NULL and different from held one
     * @param gen Pointer to the new held object
     */
    inline void setObject(GenObject* gen) {
	    if (!gen || gen == m_object)
		return;
	    TelEngine::destruct(m_object);
	    m_object = gen;
	}

private:
    GenObject* m_object;
};

/**
 * An evaluator for multi-row (tables like in SQL) expressions
 * @short An SQL-like table evaluator
 */
class YSCRIPT_API TableEvaluator
{
public:
    /**
     * Copy constructor, duplicates current state of original
     * @param original Evaluator to copy
     */
    TableEvaluator(const TableEvaluator& original);

    /**
     * Constructor from a parser synatx style
     * @param style Style of evaluator to create
     */
    TableEvaluator(ExpEvaluator::Parser style);

    /**
     * Constructor from operator description table
     * @param operators Pointer to operators synatx table
     * @param unaryOps Pointer to unary operators dictionary
     */
    TableEvaluator(const TokenDict* operators, const TokenDict* unaryOps);

    /**
     * Destructor
     */
    virtual ~TableEvaluator();

    /**
     * Evaluate the WHERE (selector) expression
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if the current row is part of selection
     */
    virtual bool evalWhere(GenObject* context = 0);

    /**
     * Evaluate the SELECT (results) expression
     * @param results List to fill with results row
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool evalSelect(ObjList& results, GenObject* context = 0);

    /**
     * Evaluate the LIMIT expression and cache the result
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Desired maximum number or result rows
     */
    virtual unsigned int evalLimit(GenObject* context = 0);

    /**
     * Set the expression extender to use in all evaluators
     * @param ext Pointer to the extender to use, NULL to remove current
     */
    void extender(ExpExtender* ext);

protected:
    ExpEvaluator m_select;
    ExpEvaluator m_where;
    ExpEvaluator m_limit;
    unsigned int m_limitVal;
};

class ScriptRun;

/**
 * A mutex that serializes object access
 * @short Script context serialization mutex
 */
class YSCRIPT_API ScriptMutex : public Mutex
{
public:
    /**
     * Constructor
     * @param recursive True if the mutex has to be recursive (reentrant),
     *  false for a normal fast mutex
     * @param name Static name of the mutex (for debugging purpose only)
     */
    inline ScriptMutex(bool recursive, const char* name)
	: Mutex(recursive,name), m_objTrack(false)
	{ }

    /**
     * Notification that an object was created in context serialized by this mutex
     * @param obj Created object
     */
    virtual void objCreated(GenObject* obj) = 0;

    /**
     * Notification that an object was destroyed in context serialized by this mutex
     * @param obj Destroyed object
     */
    virtual void objDeleted(GenObject* obj) = 0;

    /**
     * Check if object tracking is active
     * @return True if it's active, false otherwise
     */
    inline bool objTrack() const
	{ return m_objTrack; }

protected:
    bool m_objTrack;
};

/**
 * A script execution context, holds global variables and objects
 * @short Script execution context
 */
class YSCRIPT_API ScriptContext : public RefObject, public ExpExtender
{
public:
    /**
     * Constructor
     * @param name Name of the context
     */
    inline explicit ScriptContext(const char* name = 0)
	: m_params(name), m_instIdx(0), m_instCount(1), m_terminated(false)
	{ }

    /**
     * Access to the NamedList operator
     * @return Reference to the internal named list
     */
    inline NamedList& params()
	{ return m_params; }

    /**
     * Const access to the NamedList operator
     * @return Reference to the internal named list
     */
    inline const NamedList& params() const
	{ return m_params; }

    /**
     * Access any native NamedList hold by the context
     * @return Pointer to a native named list
     */
    virtual NamedList* nativeParams() const
	{ return 0; }

    /**
     * Override GenObject's method to return the internal name of the named list
     * @return A reference to the context name
     */
    virtual const String& toString() const
	{ return m_params; }

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Retrieve the reference counted object owning this interface
     * @return Pointer to this script context
     */
    virtual RefObject* refObj()
	{ return this; }

    /**
     * Retrieve the Mutex object used to serialize object access, if any
     * @return Pointer to the mutex or NULL if none applies
     */
    virtual ScriptMutex* mutex() = 0;

    /**
     * Check if a certain field is assigned in context
     * @param stack Evaluation stack in use
     * @param name Name of the field to test
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if the field is present
     */
    virtual bool hasField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Get a pointer to a field in the context
     * @param stack Evaluation stack in use
     * @param name Name of the field to retrieve
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return Pointer to field, NULL if not present
     */
    virtual NamedString* getField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Fill a list with the unique names of all fields
     * @param names List to which key names must be added
     */
    virtual void fillFieldNames(ObjList& names);

    /**
     * Fill a list with the unique names of all fields
     * @param names List to which key names must be added
     * @param list List of parameters whose names to be added
     * @param checkDupl True to ignore duplicates from the given list
     * @param skip Parameters starting with this prefix will not be added
     */
    static void fillFieldNames(ObjList& names, const NamedList& list, bool checkDupl = true, const char* skip = 0);

    /**
     * Fill a list with the unique names from a Hash list
     * @param names List to which key names must be added
     * @param list Hash list whose names are to be added
     */
    static void fillFieldNames(ObjList& names, const HashList& list);

    /**
     * Try to evaluate a single function in the context
     * @param stack Evaluation stack in use, parameters are popped off this stack and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to context data passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to evaluate a single field in the context
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to context data passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to assign a value to a single field
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to context data passed from evaluation methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Copy all fields from another context
     * @param stack Evaluation stack in use
     * @param original Script context to copy from
     * @param context Pointer to context data passed from evaluation methods
     * @return True if all fields were copied
     */
    virtual bool copyFields(ObjList& stack, const ScriptContext& original, GenObject* context);

    /**
     * Add string parameters from list
     * @param list Parameters list
     * @param skipPrefix Skip parameters whose name start with specified prefix
     */
    virtual void addFields(const NamedList& list, const char* skipPrefix = "__");

    /**
     * Try to evaluate a single field searching for a matching context
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to context data passed from evaluation methods
     * @return True if evaluation succeeded
     */
    bool runMatchingField(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Notification that an object was created in this context
     * Used for object tracking purposes
     * @param obj The created object
     */
    virtual void createdObj(GenObject* obj)
	{  }

    /**
     * Notification that an object was destroyed in this context
     * Used for object tracking purposes
     * @param obj The destroyed object
     */
    virtual void deletedObj(GenObject* obj)
	{  }

    /**
     * Activate object tracking
     * @param track O for not enabled, non-zero for enabling it
     */
    virtual void trackObjs(unsigned int track = 0)
	{  }

    /**
     * Retrieve a list of how many objects were allocated at each line
     * @return The list of counters for allocations, 0 if tracking is not active
     */
    virtual ObjList* countAllocations()
	{ return 0; }

    virtual void setInstance(unsigned int idx, unsigned int count)
    {
        m_instIdx = idx;
        m_instCount = count;
    }

    virtual unsigned int instanceIndex() const
        { return m_instIdx; }

    virtual unsigned int instanceCount() const
        { return m_instCount; }

    /**
     * Cleanup the context. Remove variables
     */
    virtual void cleanup() {
	    Lock lck(mutex());
	    m_terminated = true;
	    params().clearParams();
	}

    /**
     * Check if the context was terminated
     */
    inline bool terminated() const
	{ return m_terminated; }

private:
    NamedList m_params;
    unsigned int m_instIdx; // instance index
    unsigned int m_instCount; // total number of instances
    bool m_terminated;                   // Context was terminated. Variables were cleared
};

/**
 * Preparsed script code fragment ready to be executed
 * @short Script parsed code
 */
class YSCRIPT_API ScriptCode : public RefObject
{
    YCLASS(ScriptCode,RefObject)
public:
    /**
     * Context initializer for language specific globals
     * @param context Pointer to the context to initialize
     * @return True if context was properly populated with globals
     */
    virtual bool initialize(ScriptContext* context) const = 0;

    /**
     * Evaluation of a single code expression
     * @param runner Reference to the runtime to use in evaluation
     * @param results List to fill with expression results
     */
    virtual bool evaluate(ScriptRun& runner, ObjList& results) const = 0;

    /**
     * Create a runner adequate for this block of parsed code
     * @param context Script context, must not be NULL
     * @param title An optional name for the runner
     * @return A new script runner, NULL if context is NULL or feature is not supported
     */
    virtual ScriptRun* createRunner(ScriptContext* context, const char* title = 0)
	{ return 0; }

    /**
     * Get the file name and the file line from which this code line was interpreted
     * @param line Code line
     * @param fileName On output, it contains the file name associated with code line
     * @param fileLine On output, it contains the file line associated with code line
     * @param wholePath If true, file name contains the whole file path, otherwise just the filename
     */
    virtual void getFileLine(unsigned int line, String& fileName, unsigned int& fileLine, bool wholePath = true)
	{ }
};

/**
 * A stack for a script running instance
 * @short Script runtime stack
 */
class YSCRIPT_API ScriptStack : public ObjList
{
    YCLASS(ScriptStack,ObjList)
    YNOCOPY(ScriptStack);
public:
    /**
     * Constructor
     * @param owner The script running instance that will own this stack
     */
    ScriptStack(ScriptRun* owner)
	: m_runner(owner)
	{ }

    /**
     * Retrieve the script running instance that owns this stack
     * @return Pointer to owner script instance
     */
    inline ScriptRun* runner()
	{ return m_runner; }

private:
    ScriptRun* m_runner;
};

/**
 * Operation that is to be executed by the script runtime before current operation
 * @short Asynchronous execution support
 */
class YSCRIPT_API ScriptAsync : public GenObject
{
    YCLASS(ScriptAsync,GenObject)
public:
    /**
     * Constructor
     * @param owner The script running instance that will own this operation
     */
    ScriptAsync(ScriptRun* owner)
	: m_runner(owner)
	{ }

    /**
     * Destructor
     */
    virtual ~ScriptAsync()
	{ }

    /**
     * Retrieve the script running instance that owns this stack
     * @return Pointer to owner script instance
     */
    inline ScriptRun* runner()
	{ return m_runner; }

    /**
     * Execute the aynchronous operation with context unlocked if the script is paused
     * @return True if the operation should be removed (was one-shot)
     */
    virtual bool run() = 0;

private:
    ScriptRun* m_runner;
};

/**
 * A script runner user data to be set by upper layer
 * @short Script runner upper layer data
 */
class YSCRIPT_API ScriptRunData : public RefObject
{
    YCLASS(ScriptRunData,RefObject)
public:
    /**
     * Constructor
     * @param name Name of the context
     */
    inline ScriptRunData()
	{}
};

/**
 * An instance of script code and data, status machine run by a single thread at a time
 * @short Script runtime execution
 */
class YSCRIPT_API ScriptRun : public GenObject, public ScriptMutex
{
    friend class ScriptCode;
    YCLASS(ScriptRun,GenObject)
    YNOCOPY(ScriptRun);
public:
    /**
     * Runtime states
     */
    enum Status {
	Invalid,
	Running,
	Incomplete,
	Succeeded,
	Failed,
    };

    /**
     * Constructor
     * @param code Code fragment to execute
     * @param context Script context, an empty one will be allocated if NULL
     */
    ScriptRun(ScriptCode* code, ScriptContext* context = 0);

    /**
     * Destructor, disposes the code and context
     */
    virtual ~ScriptRun();

    /**
     * Retrieve the parsed code being executed
     * @return Pointer to ScriptCode object
     */
    inline ScriptCode* code() const
	{ return m_code; }

    /**
     * Retrieve the execution context associated with the runtime
     * @return Pointer to ScriptContext object
     */
    inline ScriptContext* context() const
	{ return m_context; }

    /**
     * Current state of the runtime
     */
    inline Status state() const
	{ return m_state; }

    /**
     * Get the text description of a runtime state
     * @param state State to describe
     * @return Description of the runtime state
     */
    static const char* textState(Status state);

    /**
     * Get the text description of the current runtime state
     * @return Description of the runtime state
     */
    inline const char* textState() const
	{ return textState(m_state); }

    /**
     * Access the runtime execution stack
     * @return The internal execution stack
     */
    inline ObjList& stack()
	{ return m_stack; }

    /**
     * Const access the runtime execution stack
     * @return The internal execution stack
     */
    inline const ObjList& stack() const
	{ return m_stack; }

    /**
     * Create a duplicate of the runtime with its own stack and state
     * @return New clone of the runtime
     */
    inline ScriptRun* clone() const
	{ return new ScriptRun(code(),context()); }

    /**
     * Resets code execution to the beginning, does not clear context
     * @param init Initialize context
     * @return Status of the runtime after reset
     */
    virtual Status reset(bool init = false);

    /**
     * Execute script from where it was left, may stop and return Incomplete state
     * @return Status of the runtime after code execution
     */
    virtual Status execute();

    /**
     * Execute script from the beginning until it returns a final state
     * @param init Initialize context
     * @return Final status of the runtime after code execution
     */
    virtual Status run(bool init = true);

    /**
     * Pause the script, make it return Incomplete state
     * @return True if pausing the script succeeded or was already paused
     */
    virtual bool pause();

    /**
     * Call a script function or method
     * @param name Name of the function to call
     * @param args Values to pass as actual function arguments
     * @param thisObj Object to pass as "this" if applicable
     * @param scopeObj Optional object to be used for scope resolution inside the call
     * @return Final status of the runtime after function call
     */
    virtual Status call(const String& name, ObjList& args,
	ExpOperation* thisObj = 0, ExpOperation* scopeObj = 0);

    /**
     * Check if a script has a certain function or method
     * @param name Name of the function to check
     * @return True if function exists in code
     */
    virtual bool callable(const String& name);

    /**
     * Insert an asynchronous operation to be executed
     * @param oper Operation to be inserted, will be owned by the runtime instance
     * @return True if the operation was added
     */
    virtual bool insertAsync(ScriptAsync* oper);

    /**
     * Append an asynchronous operation to be executed
     * @param oper Operation to be appended, will be owned by the runtime instance
     * @return True if the operation was added
     */
    virtual bool appendAsync(ScriptAsync* oper);

    /**
     * Retrieve current file line being executed
     * @return The file line being evaluated
     */
    virtual unsigned int currentLineNo() const
	{ return 0; }

    /**
     * Retrieve the name of the source file from which code is being executed
     * @param wholePath Retrieve name including path
     * @return The file name
     */
    virtual const String& currentFileName(bool wholePath = false) const
	{ return String::empty(); }

    /**
     * Get the trace ID associated with this instance
     * @return The trace ID
     */
    virtual const String& traceId() const
        { return m_traceId; }

    /**
     * Set an associated trace ID for this instance
     * @param tid Trace ID to associate
     */
    virtual void setTraceId(const String& tid)
        { m_traceId = tid; }

    /**
     * Try to assign a value to a single field in the script context
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if assignment succeeded
     */
    bool runAssign(const ExpOperation& oper, GenObject* context = 0);

    void objCreated(GenObject* obj)
	{ if (m_context) m_context->createdObj(obj); };
    void objDeleted(GenObject* obj)
	{ if (m_context) m_context->deletedObj(obj); };

    /**
     * Retrieve runner user data
     * @return ScriptRunData pointer, NULL if not set
     */
    inline ScriptRunData* userData() const
	{ return m_data; }

    /**
     * Set runner user data if not already set
     * The caller still owns the given pointer (runner holds a reference)
     * @param data Data to set
     * @return True on success, false otherwise (NULL pointer given or already set)
     */
    inline bool userData(ScriptRunData* data) {
	    if (!data)
		return false;
	    if (!m_data)
		m_data = data;
	    return data == m_data;
	}

protected:
    /**
     * Resume script from where it was left, may stop and return Incomplete state
     * @return Status of the runtime after code execution
     */
    virtual Status resume();

private:
    ScriptCode* m_code;
    ScriptContext* m_context;
    Status m_state;
    ObjList m_stack;
    ObjList m_async;
    String m_traceId;
    RefPointer<ScriptRunData> m_data;
};

/**
 * Abstract parser, base class for each language parser
 * @short Abstract script parser
 */
class YSCRIPT_API ScriptParser : public GenObject
{
    YCLASS(ScriptParser,GenObject)
public:
    /**
     * Destructor, releases code
     */
    virtual ~ScriptParser();

    /**
     * Parse a string as script source code
     * @param text Source code text
     * @param fragment True if the code is just an included fragment
     * @param file Name of the file that is being parsed
     * @param len Length of text, negative if unknown
     * @return True if the text was successfully parsed
     */
    virtual bool parse(const char* text, bool fragment = false, const char* file = 0, int len = -1) = 0;

    /**
     * Parse a file as script source code
     * @param name Source file name
     * @param fragment True if the code is just an included fragment
     * @return True if the file was successfully parsed
     */
    virtual bool parseFile(const char* name, bool fragment = false);

    /**
     * Clear any existing parsed code
     */
    inline void clear()
	{ setCode(0); }

    /**
     * Retrieve the currently stored parsed code
     * @return Parsed code block, may be NULL
     */
    inline ScriptCode* code() const
	{ return m_code; }

    /**
     * Set the maximum loaded file length
     * @param len New maximum file length
     */
    inline void setMaxFileLen(unsigned int len)
	{ m_maxFileLen = len; }

    /**
     * Retrieve the maximum loadable file size
     * @return The maximum number of octets that will be loaded from a file
     */
    inline unsigned int maxFileLen() const
	{ return m_maxFileLen; }

    /**
     * Create a context adequate for the parsed code
     * @return A new script context
     */
    virtual ScriptContext* createContext(unsigned int instIdx = 0, unsigned int maxInst = 1) const;

    /**
     * Create a runner adequate for a block of parsed code
     * @param code Parsed code block
     * @param context Script context, an empty one will be allocated if NULL
     * @param title An optional name for the runner
     * @param instIdx Javascript context instance
     * @param maxInst Number of context instances
     * @return A new script runner, NULL if code is NULL
     */
    virtual ScriptRun* createRunner(ScriptCode* code, ScriptContext* context = 0, const char* title = 0,
                            unsigned int instIdx = 0, unsigned int maxInst = 1) const;

    /**
     * Create a runner adequate for the parsed code
     * @param context Script context, an empty one will be allocated if NULL
     * @param title An optional name for the runner
     * @param instIdx Javascript context instance
     * @param maxInst Number of context instances
     * @return A new script runner, NULL if code is not yet parsed
     */
    inline ScriptRun* createRunner(ScriptContext* context = 0, const char* title = 0, 
                            unsigned int instIdx = 0, unsigned int maxInst = 1) const
	{ return createRunner(code(),context,title,instIdx,maxInst); }

    /**
     * Check if a script has a certain function or method
     * @param name Name of the function to check
     * @return True if function exists in code
     */
    virtual bool callable(const String& name);

protected:
    /**
     * Default constructor for derived classes
     */
    inline ScriptParser()
	: m_code(0), m_maxFileLen(500000)
	{ }

    /**
     * Set the just parsed block of code
     * @param code Parsed code block, may be NULL
     */
    void setCode(ScriptCode* code);

private:
    ScriptCode* m_code;
    unsigned int m_maxFileLen;
};

class JsFunction;

/**
 * Javascript Object class, base for all JS objects
 * @short Javascript Object
 */
class YSCRIPT_API JsObject : public ScriptContext
{
    friend class JsFunction;
    YCLASS(JsObject,ScriptContext)
public:
    /**
     * Dump object flags
     */
    enum DumpFlags {
	DumpFunc = 0x01,                 // Dump functions
	DumpProp = 0x02,                 // Dump non functions (data)
	DumpRecursive = 0x10,            // Dump recursive (stop on root if not set)
	DumpType = 0x20,                 // Dump type (apply to functions also),
	DumpProto = 0x40,                // Dump prototype
	DumpPropObjType = 0x80,          // Dump non basic type for DumpPropOnly whithout DumpType
	DumpInternals = 0x1000,
	// Masks
	DumpFuncOnly = DumpRecursive | DumpProto | DumpFunc,
	DumpPropOnly = DumpRecursive | DumpPropObjType | DumpProp,
    };

    /**
     * Copy properties flags
     */
    enum AssignFlags {
	AssignSkipPrefix = 0x01,           // Skip prefix if given when copying
	AssignSkipNull = 0x02,             // Do not copy properties with 'null' value
	AssignSkipUndefined = 0x04,        // Do not copy properties with 'undefined' value
	AssignSkipEmpty = 0x08,            // Do not copy properties evaluating to empty strings
	AssignSkipObject = 0x10,           // Do not copy Object properties
	AssignSkipArrayProps = 0x20,       // Do not copy Array properties
	AssignSkipArrayIndex = 0x40,       // Do not copy Array indexes
	AssignDeepCopy = 0x80,             // Make a deep copy of objects
	AssignFreezeCopy = 0x100,          // Freeze destination after copy
	AssignSkipExist = 0x200,           // Skip (ignore) existing (non undefined) properties in destination
	// Masks
	AssignFilled = AssignSkipNull | AssignSkipUndefined | AssignSkipEmpty,
	AssignFilledSkipObject = AssignFilled | AssignSkipObject,
    };

    /**
     * Keys/Values/Entries build flags
     */
    enum ArrayPropsFlags {
	ArrayPropsForceBasicVal = 0x01,  // Force basic, non object, type for values (ignored if building keys)
	ArrayPropsAutoNum = 0x02,        // Automatically convert to number/boolean if possible
	                                 // Values/entries: ignored if ArrayPropsForceBasicVal is not set
	ArrayPropsEmptyNull = 0x04,      // Return 'null' if result is empty
	ArrayPropsSkipNull = 0x08,       // Do not process properties with 'null' value
	ArrayPropsSkipUndefined = 0x10,  // Do not process properties with 'undefined' value
	ArrayPropsSkipObject = 0x20,     // Do not process properties with object value (non null/undefined)
	ArrayPropsSkipEmpty = 0x40,      // Do not process properties evaluating to empty strings
	ArrayPropsNameValObj = 0x80,     // Build object array entries (name/value).
	                                 // Not set: build map array entries (array with 2 values)
	                                 // Ignored if building keys or values
    };

    /**
     * Constructor
     * @param name Name of the object
     * @param mtx Pointer to the mutex that serializes this object
     * @param frozen True if the object is to be frozen from creation
     */
    JsObject(const char* name = "Object", ScriptMutex* mtx = 0, bool frozen = false);

    /**
     * Constructor for an empty object
     * @param mtx Pointer to the mutex that serializes this object
     * @param name Full name of the object
     * @param line Code line where this object was created
     * @param frozen True if the object is to be frozen from creation
     */
    JsObject(ScriptMutex* mtx, const char* name, unsigned int line, bool frozen = 0);

    /**
     * Constructor for an empty object with prototype
     * @param context Script context from which Object prototype is obtained
     * @param line Code line where this object was created
     * @param mtx Pointer to the mutex that serializes this object
     * @param frozen True if the object is to be frozen from creation
     */
    JsObject(GenObject* context, unsigned int line, ScriptMutex* mtx = 0, bool frozen = false);

    /**
     * Destructor
     */
    virtual ~JsObject();

    /**
     * Retrieve the Mutex object used to serialize object access
     * @return Pointer to the mutex of the context this object belongs to
     */
    virtual ScriptMutex* mutex()
	{ return m_mutex; }

    /**
     * Clone and rename method
     * @param name Name of the cloned object
     * @param oper ExpOperation that required the clone
     * @return New object instance
     */
    virtual JsObject* clone(const char* name, const ExpOperation& oper ) const
	{ return new JsObject(m_mutex,name,oper.lineNumber()); }

    /**
     * Clone method
     * @param oper ExpOperation that required the clone
     * @return New object instance
     */
    inline JsObject* clone(const ExpOperation& oper) const
	{ return clone(toString(),oper); }

    /**
     * Clone and rename method. Used to prepare a new object for copy
     * @param context Script context from which Object prototype is obtained
     * @param mtx Optional mutex, use our mutex if not given
     * @param line Line number
     * @return New object instance
     */
    virtual JsObject* cloneForCopy(GenObject* context, ScriptMutex** mtx = 0,
	unsigned int line = 0) const
	{ return new JsObject(context,line,mtx ? *mtx : m_mutex); }

    /**
     * Set the object prototype
     * @param context  Pointer to arbitrary object passed from evaluation methods
     * @param objName Name of the object prototype to set the this object
     * @return True on success, false if not found
     */
    bool setPrototype(GenObject* context, const String& objName);

    /**
     * Set the object prototype
     * @param params Parameters list to search for prototype
     * @param objName Name of the object prototype to set the this object
     * @return True on success, false if not found
     */
    bool setPrototype(NamedList& params, const String& objName);

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied object
     * @param oper Caller of copying operation
     * @return New object instance, does not keep references to old object
     */
    virtual JsObject* copy(ScriptMutex* mtx, const ExpOperation& oper) const;

    /**
     * Fill a list with the unique names of all fields
     * @param names List to which key names must be added
     */
    virtual void fillFieldNames(ObjList& names);

    /**
     * Retrieve enclosed hashed list paramsters
     * @return HashList pointer, NULL if this object don't hold one
     */
    virtual const HashList* getHashListParams() const;

    /**
     * Check if a certain field is assigned in the object or its prototype
     * @param stack Evaluation stack in use
     * @param name Name of the field to test
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if the field is present
     */
    virtual bool hasField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Get a pointer to a field in the object or its prototype
     * @param stack Evaluation stack in use
     * @param name Name of the field to retrieve
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return Pointer to field, NULL if not present
     */
    virtual NamedString* getField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Native constructor initialization, called by addConstructor on the prototype
     * @param construct Function that has this object as prototype
     */
    virtual void initConstructor(JsFunction* construct)
	{ }

    /**
     * Native object constructor, it's run on the prototype
     * @param stack Evaluation stack in use
     * @param oper Constructor function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return New created and populated Javascript object
     */
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to evaluate a single method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to evaluate a single field
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to assign a value to a single field if object is not frozen
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Pops and evaluate the value of an operand off an evaluation stack, does not pop a barrier
     * @param stack Evaluation stack to remove the operand from
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Value removed from stack, NULL if stack underflow or field not evaluable
     */
    virtual ExpOperation* popValue(ObjList& stack, GenObject* context = 0);

    /**
     * Retrieve object parameters list (check native first)
     * @return Object parameters list
     */
    virtual const NamedList* getObjParams() {
	    const NamedList* p = nativeParams();
	    return p ? p : &params();
	}

    /**
     * Delete a field of the object
     * @param name Name of field to remove
     */
    virtual void clearField(const String& name)
	{ params().clearParam(name); }

    /**
     * Set a ExpOperation field in this object
     * @param oper Data to set
     * @return True if set was successful
     */
    inline bool setField(ExpOperation* oper) {
	    if (!oper)
		return false;
	    params().setParam(oper);
	    return true;
	}

    /**
     * Set a integer field in this object
     * @param name Name of field to set
     * @param val Integer value to set
     * @return True if set was successful
     */
    inline bool setIntField(const char* name, int64_t val)
	{ return name && setField(new ExpOperation(val,name)); }

    /**
     * Set a boolean field in this object
     * @param name Name of field to set
     * @param val Boolean value to set
     * @return True if set was successful
     */
    inline bool setBoolField(const char* name, bool val)
	{ return name && setField(new ExpOperation(val,name)); }

    /**
     * Set a string field in this object
     * @param name Name of field to set
     * @param val String value to set
     * @param autoNum Automatically convert to number if possible
     * @return True if set was successful
     */
    inline bool setStringField(const char* name, const char* val, bool autoNum = false)
	{ return name && setField(new ExpOperation(val,name,autoNum)); }

    /**
     * Set string fields from parameters list
     * @param list Parameters list
     * @return Number of set fields
     */
    inline unsigned int setStringFields(const NamedList& list)
    {
	unsigned int n = 0;
	for (ObjList* o = list.paramList()->skipNull(); o; o = o->skipNext()) {
	    const NamedString* ns = static_cast<NamedString*>(o->get());
	    if (ns->name() != protoName() && setStringField(ns->name(),*ns))
		n++;
	}
	return n;
    }

    /**
     * Set a object field in this object
     * @param name Name of field to set
     * @param obj Object value to set
     * @return True if set was successful
     */
    inline bool setObjField(const char* name, JsObject* obj)
	{ return name && obj && setField(new ExpWrapper(obj,name)); }

    /**
     * Get the integer value of the field with the given name
     * @param name Name of field to retrieve
     * @param val Field where to put the retrieved value
     * @return True if field was retrieved, false if not found or not the right type
     */
    bool getIntField(const String& name, int64_t& val);

    /**
     * Get the boolean value of the field with the given name
     * @param name Name of field to retrieve
     * @param val Field where to put the retrieved value
     * @return True if field was retrieved, false if not found or not the right type
     */
    bool getBoolField(const String&  name, bool& val);

    /**
     * Get the string value of the field with the given name
     * @param name Name of field to retrieve
     * @param val Field where to put the retrieved value
     * @return True if field was retrieved, false if not found or not the right type
     */
    bool getStringField(const String& name, String& val);

    /**
     * Get the object associated with the field with the given name
     * @param name Name of field to retrieve
     * @param obj Field where to put the retrieved value
     * @return True if field was retrieved, false if not found or not the right type
     */
    bool getObjField(const String& name, JsObject*& obj);

    /**
     * Retrieve the object frozen status (cannot modify attributes or methods)
     * @return True if the object is frozen
     */
    inline bool frozen() const
	{ return m_frozen; }

    /**
     * Freeze the Javascript object preventing external changes to it
     */
    inline void freeze()
	{ m_frozen = true; }

    /**
     * Set the script line number at which this object was created
     * @param line Line number
     */
    inline void lineNo(unsigned int line)
	{ m_lineNo = line; }

    /**
     * Get the script line number at which this object was created;
     * @return The line number from the script where this object was created.
     */
    inline unsigned int lineNo() const
	{ return m_lineNo; }

    /**
     * Copy object properties
     * @param src Source object
     * @param flags Copy flags (see AssignFlags)
     * @param props Optional list of properties to copy
     * @param prefix Optional prefix.
     *   'props' given: Skip this prefix in handled properties starting with it if requested in flags
     *   'props' not given: Copy only properties starting with it. Skip this prefix if requested in flags
     * @param addPrefix Optional prefix to add to properties when copied
     * @param context Pointer to an execution context
     * @param origContext Optional original context
     *  (used for debug purposes when object is not copied using a context, i.e. when given context is NULL)
     * @return Number of copied properties, negative on error (object is frozen on first assign attempt or
     *  recursivity found during copy)
     */
    int assignProps(JsObject* src, unsigned int flags = 0, ObjList* props = 0,
	const String& prefix = String::empty(), const String& addPrefix = String::empty(),
	GenObject* context = 0, GenObject* origContext = 0);

    /**
     * Helper static method that adds an object to a parent
     * @param params List of parameters where to add the object
     * @param name Name of the new parameter
     * @param obj Pointer to the object to add
     */
    static void addObject(NamedList& params, const char* name, JsObject* obj);

    /**
     * Helper static method that adds a constructor to a parent
     * @param params List of parameters where to add the constructor
     * @param name Name of the new parameter
     * @param obj Pointer to the prototype object to add
     */
    static void addConstructor(NamedList& params, const char* name, JsObject* obj);

    /**
     * Helper static method that pops arguments off a stack to a list in proper order
     * @param obj Pointer to the object to use when popping each argument
     * @param stack Evaluation stack in use, parameters are popped off this stack
     * @param oper Function that is being evaluated
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param arguments List where the arguments are added in proper order
     * @return Number of arguments popped off stack
     */
    static int extractArgs(JsObject* obj, ObjList& stack, const ExpOperation& oper,
	GenObject* context, ObjList& arguments);

    /**
     * Helper static method that pops arguments off a stack to a list in proper order
     * @param obj Pointer to the object to use when popping each argument
     * @param stack Evaluation stack in use, parameters are popped off this stack
     * @param oper Function that is being evaluated
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param arguments List where the arguments are set in proper order
     *   Will be reset to reflect the number of arguments
     * @return Number of arguments popped off stack
     */
    static int extractArgs(JsObject* obj, ObjList& stack, const ExpOperation& oper,
	GenObject* context, ExpOperVector& arguments);

    /**
     * Helper static method that pops arguments off a stack to a list in proper order
     * @param obj Pointer to the object to use when popping each argument
     * @param stack Evaluation stack in use, parameters are popped off this stack
     * @param oper Function that is being evaluated
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param arguments List where the arguments are set in proper order
     *   Will be reset to reflect the number of arguments
     * @param minArgc Minimum number of arguments (required arguments)
     * @param checkValid Check valid (not NULL) arguments. negative (check required argiments only: 0..minArgc-1),
     *  0 (no check), positive (check 0..checkValid-1)
     * @param maxArgc Maximum number of arguments to accept. Set it to negative to ignore checking
     * @return True on success, false on failure (object not given, incorrect number of arguments
     *  or missing arguments)
     */
    static bool extractArgs(JsObject* obj, ObjList& stack, const ExpOperation& oper,
	GenObject* context, ExpOperVector& arguments,
	unsigned int minArgc, int checkValid = -1, int maxArgc = -1);

    /**
     * Helper method that pops arguments off a stack to a list in proper order
     * @param stack Evaluation stack in use, parameters are popped off this stack
     * @param oper Function that is being evaluated
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param arguments List where the arguments are added in proper order
     * @return Number of arguments popped off stack
     */
    inline int extractArgs(ObjList& stack, const ExpOperation& oper, GenObject* context,
	ObjList& arguments)
	{ return extractArgs(this,stack,oper,context,arguments); }

    /**
     * Helper method that pops arguments off a stack to a list in proper order
     * @param stack Evaluation stack in use, parameters are popped off this stack
     * @param oper Function that is being evaluated
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param arguments List where the arguments are set in proper order
     *   Will be reset to reflect the number of arguments
     * @return Number of arguments popped off stack
     */
    inline int extractArgs(ObjList& stack, const ExpOperation& oper, GenObject* context,
	ExpOperVector& arguments)
	{ return extractArgs(this,stack,oper,context,arguments); }

    /**
     * Helper static method that pops arguments off a stack to a list in proper order
     * @param stack Evaluation stack in use, parameters are popped off this stack
     * @param oper Function that is being evaluated
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param arguments List where the arguments are set in proper order
     *   Will be reset to reflect the number of arguments
     * @param minArgc Minimum number of arguments (required arguments)
     * @param checkValid Check valid (not NULL) arguments. negative (check required argiments only: 0..minArgc-1),
     *  0 (no check), positive (check 0..checkValid-1)
     * @param maxArgc Maximum number of arguments to accept. Set it to negative to ignore checking
     * @return True on success, false on failure (object not given, incorrect number of arguments
     *  or missing arguments)
     */
    bool extractArgs(ObjList& stack, const ExpOperation& oper, GenObject* context,
	ExpOperVector& arguments, unsigned int minArgc, int checkValid = -1, int maxArgc = -1)
	{ return extractArgs(this,stack,oper,context,arguments,minArgc,maxArgc,checkValid); }

    /**
     * Create an empty function call context
     * @param mtx Pointer to the mutex that serializes this object
     * @param thisObj Optional object that will be set as "this"
     * @return New empty object usable as call context
     */
    static JsObject* buildCallContext(ScriptMutex* mtx, JsObject* thisObj = 0);

    /**
     * Initialize the standard global objects in a context
     * @param context Script context to initialize
     */
    static void initialize(ScriptContext* context);

    /**
     * Set the creation line for this object and its properties
     * @param obj Object t set the line for
     * @param lineNo The line number to set
     * @param recursive True to set it to its sub-objects, false otherwise
     */
    static void setLineForObj(JsObject* obj,unsigned int lineNo, bool recursive);

    /**
     * Get the name of the internal property used to track prototypes
     * @return The "__proto__" constant string
     */
    inline static const String& protoName()
	{ return s_protoName; }

    /**
     * Static helper method that deep copies all parameters
     * @param dst Destination parameters
     * @param src Source parameters
     * @param mtx Mutex to be used to synchronize all new objects
     */
    static void deepCopyParams(NamedList& dst, const NamedList& src, ScriptMutex* mtx);

    /**
     * Helper method to return the hierarchical structure of an object
     * @param obj Object to dump structure
     * @param buf String to which the structure is added
     * @param flags Flags indicating what to dump
     */
    static void dumpRecursive(const GenObject* obj, String& buf, unsigned int flags = 0xffffffff & ~DumpInternals);

    /**
     * Helper method to display the hierarchical structure of an object
     * @param obj Object to display
     * @param flags Flags indicating what to dump (display)
     */
    static void printRecursive(const GenObject* obj, unsigned int flags = 0xffffffff & ~DumpInternals);

    /**
     * Static method to obtain a JSON representation of the given object
     * @param oper Object to represent in JSON format
     * @param spaces Number of spaces used for one indentation level
     * @return String represention of the object
     */
    static ExpOperation* toJSON(const ExpOperation* oper, int spaces);

    /**
     * Resolve JSON references (replace objects with '#ref' properties)
     * '#ref' is handled as JSON path relative to given object
     * @param oper Object to handle
     * @return True if all references were resolved, false if at least one failed
     */
    static bool resolveReferences(ExpOperation* oper);

    /**
     * Find a value in object by path
     * @param oper Object to handle
     * @param path Path to use
     * @return Found property value, NULL if not found
     */
    static ExpOperation* find(ExpOperation* oper, const JPath& path);

    /**
     * Copy an object
     * @param res Number of copied properties, negative on error (NULL pointr returned)
     * @param src Object to copy, returns error if missing/null/undefined
     * @param flags Flags for properties assign
     * @param context Context owning the prototypes of the new object
     * @param mtx Optional mutex, use our mutex if not given
     * @param line Line number
     * @param origContext Optional original context
     *  (used for debug purposes when object is not copied using a context, i.e. when given context is NULL)
     * @return JsObject pointer, NULL on failure
     */
    static JsObject* copy(int& res, JsObject* src, unsigned int flags, GenObject* context = 0,
	ScriptMutex** mtx = 0, unsigned int line = 0, GenObject* origContext = 0);

    /**
     * Build an array of data from object parameters (properties)
     * @param proc 0: build entries, negative: build keys, positive: build values
     * @param obj Source object
     * @param context Script context from which Array prototype is obtained
     * @param line Code line where this object was created
     * @param mtx Pointer to the mutex that serializes this object
     * @param flags Process properties flags
     * @param filterName Optional parameter name filter (process only names matching it). RegExp or MatchingItem
     * @param filterValue Optional parameter value filter (process only values matching it). RegExp or MatchingItem
     * @param jsa Optional existing array to append to
     * @return JsArray pointer, NULL if empty is not requested and no parameter found
     *  Array item:
     *  Entries: object with name/value properties or array with 2 entries (0:name, 1:value)
     *  Keys: property names
     *  Values: property values
     */
    static JsArray* arrayProps(int proc, const GenObject* obj, GenObject* context,
	unsigned int line, ScriptMutex* mtx = 0, unsigned int flags = 0,
	const GenObject* filterName = 0, const GenObject* filterValue = 0, JsArray* jsa = 0);

    /**
     * Retrieve object parameters list (check native first)
     * @param obj Object to retrieve from
     * @param emptyOk Set it to true to force returning a valid NamedList pointer
     * @return Object parameters list, NULL if missing
     */
    static inline const NamedList* getObjParams(GenObject* obj, bool emptyOk = false) {
	    JsObject* jso = YOBJECT(JsObject,obj);
	    return jso ? jso->getObjParams() : (emptyOk ? &NamedList::empty() : 0);
	}

protected:
    /**
     * Try to evaluate a single native method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Retrieve the Mutex object used to serialize object access
     * @return Pointer to the mutex of the context this object belongs to
     */
    inline ScriptMutex* mutex() const
	{ return m_mutex; }

    /**
     * Set the Mutex used to serialize this object, set to 0 to reset it
     * @param mtx Mutex to set
     */
    inline void setMutex(ScriptMutex* mtx)
	{ m_mutex = mtx; }

    /**
     * Static method to obtain a JSON representation of the given object
     * @param ns Object to represent in JSON format
     * @param buf String used as output for the JSON represantion
     * @param spaces Number of spaces used for one indentation level
     * @param indent Current number of spaces used for indentation
     * @param data Internal data used for various purposes
     * @param path Current path if any
     * @param crtProp Current property if any
     */
    static inline void toJSON(const NamedString* ns, String& buf, int spaces, int indent = 0,
	void* data = 0, const String& path = String::empty(), const String& crtProp = String::empty())
	{ internalToJSON(ns,true,buf,spaces,indent,data,path,crtProp); }

    /**
     * Static helper method for escaping special characters when JSON stringifying
     * @param str String to escape
     * @return Escaped string
     */
    static String strEscape(const char* str);

private:
    static bool recursiveToJSON(String& newPath, JsObject* jso, String& buf, int spaces, int indent,
	void* data, const String& path, const String& crtProp);
    static void internalToJSON(const GenObject* obj, bool isStr, String& buf, int spaces,
	int indent = 0, void* data = 0, const String& path = String::empty(),
	const String& crtProp = String::empty());
    static int internalAssignProps(JsObject* dest, JsObject* src, unsigned int flags, ObjList* props = 0,
	const String& prefix = String::empty(), const String& addPrefix = String::empty(),
	GenObject* context = 0, GenObject* origContext = 0,
	void* data = 0, const String& path = String::empty());
    static inline JsObject* jsCopy(int& res, JsObject* src, unsigned int flags, GenObject* context,
	ScriptMutex** mtx, unsigned int line, GenObject* origContext = 0,
	void* data = 0, const String& path = String::empty(),
	bool ignoreCloneFail = false);

    static const String s_protoName;
    bool m_frozen;
    ScriptMutex* m_mutex;
    unsigned int m_lineNo; // creation line for this object;
};

/**
 * Javascript Function class, implements user defined functions
 * @short Javascript Function
 */
class YSCRIPT_API JsFunction : public JsObject
{
    YCLASS(JsFunction,JsObject)
public:
    /**
     * Constructor
     * @param mtx Pointer to the mutex that serializes this object
     */
    JsFunction(ScriptMutex* mtx = 0);

    /**
     * Constructor with function name
     * @param mtx Pointer to the mutex that serializes this object
     * @param name Name of the function
     * @param line Code line where this object was created
     * @param args Optional list of formal parameter names, will be emptied
     * @param lbl Number of the entry point label
     * @param code The script code to be used while running the function
     */
    JsFunction(ScriptMutex* mtx, const char* name, unsigned int line, ObjList* args = 0, long int lbl = 0,
	ScriptCode* code = 0);

    /**
     * Try to evaluate a single user defined method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param thisObj Object that should act as "this" for the function call
     * @return True if evaluation succeeded
     */
    virtual bool runDefined(ObjList& stack, const ExpOperation& oper, GenObject* context, JsObject* thisObj = 0);

    /**
     * Function constructor initialization
     * @param construct The Function function
     */
    virtual void initConstructor(JsFunction* construct);

    /**
     * Retrieve the ExpFunction matching this Javascript function
     * @return Pointer to ExpFunction representation
     */
    inline const ExpFunction* getFunc() const
	{ return &m_func; }

    /**
     * Set the name of this function if still empty
     * @param name Name to set as first assigned name
     */
    inline void firstName(const char* name)
	{ if (m_func.name().null()) const_cast<String&>(m_func.name()) = name; }

    /**
     * Retrieve the name of the N-th formal argument
     * @param index Index of the formal argument
     * @return Pointer to formal argument name, NULL if index too large
     */
    inline const String* formalName(unsigned int index) const
	{ return static_cast<const String*>(m_formal[index]); }

    /**
     * Retrieve the entry label of the code for this function
     * @return Number of the entry point label, zero if no code defined
     */
    inline long int label() const
	{ return m_label; }

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied array
     * @param oper ExpOperation that required the copy
     * @return New object instance, does not keep references to old array
     */
    inline JsObject* copy(ScriptMutex* mtx, const ExpOperation& oper) const
	{ return copy(mtx,0,oper); }

    /**
     * Deep copy method with given name
     * @param mtx Pointer to the mutex that serializes the copied array
     * @param name Name for the copied function
     * @param oper ExpOperation that required the copy
     * @return New object instance, does not keep references to old array
     */
    virtual JsObject* copy(ScriptMutex* mtx, const char* name, const ExpOperation& oper) const;

    /**
     * Clone function
     * @param oper ExpOperation that required the clone
     * @param mtx Pointer to the mutex that serializes the cloning
     * @return A copy of JsFunction if not already cloned, a reference to itself is function is already a clone
     */
    virtual JsFunction* cloneFunction(const ExpOperation& oper, ScriptMutex* mtx = 0);

protected:
    /**
     * Try to evaluate a single native method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

private:
    void init();
    ObjList m_formal;
    long int m_label;
    ScriptCode* m_code;
    ExpFunction m_func;
};

/**
 * Javascript Array class, implements arrays of items
 * @short Javascript Array
 */
class YSCRIPT_API JsArray : public JsObject
{
    friend class JsObject;
    YCLASS(JsArray,JsObject)
public:

    /**
     * Constructor for an empty array with prototype
     * @param context Script context from which Array prototype is obtained
     * @param line Code line where this object was created
     * @param mtx Pointer to the mutex that serializes this object
     */
    JsArray(GenObject* context, unsigned int line, ScriptMutex* mtx = 0);

    /**
     * Constructor for an empty array
     * @param mtx Pointer to the mutex that serializes this object
     * @param name Full name of the object
     * @param line Code line where this object was created
     * @param frozen True if the object is to be frozen from creation
     */
    inline JsArray(ScriptMutex* mtx, const char* name, unsigned int line, bool frozen = false)
	: JsObject(mtx,name,line,frozen), m_length(0)
	{ }

    /**
     * Retrieve the length of the array
     * @return Number of numerically indexed objects in array
     */
    inline int32_t length() const
	{ return m_length; }

    /**
     * Set the internal length to a specific value
     * @param len Length of array to set
     */
    inline void setLength(int32_t len)
	{ m_length = len; }

    /**
     * Retrieve item at index
     * @param idx Index to search for
     * @return Pointer to item, NULL if not set
     */
    inline GenObject* at(int32_t idx)
	{ return params().getParam(String(idx)); }

    /**
     * Retrieve item at index
     * @param idx Index to search for
     * @return Pointer to item, NULL if not set
     */
    inline const GenObject* at(int32_t idx) const
	{ return params().getParam(String(idx)); }

    /**
     * Add an item at the end of the array
     * @param item Item to add to array
     */
    void push(ExpOperation* item);

    /**
     * Push a string into array
     * @param val String value to push
     * @param autoNum Automatically convert to number if possible
     * @param name Optional name of parameter
     */
    inline void push(const char* val, bool autoNum = false, const char* name = 0)
	{ push(new ExpOperation(val,name,autoNum)); }

    /**
     * Add string items at the end of the array
     * @param lst List with items to push
     */
    inline void push(const ObjList& lst) {
	    for (const ObjList* o = lst.skipNull(); o; o = o->skipNext())
		push(o->get()->toString());
	}

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied array
     * @param oper ExpOperation that required the copy operation
     * @return New object instance, does not keep references to old array
     */
    virtual JsObject* copy(ScriptMutex* mtx, const ExpOperation& oper) const;

    /**
     * Try to assign a value to a single field if object is not frozen and update array length.
     * Reimplemented from JsObject
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to evaluate a single field
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Array constructor initialization
     * @param construct The Array function
     */
    virtual void initConstructor(JsFunction* construct);

    /**
     * Array object constructor, it's run on the prototype
     * @param stack Evaluation stack in use
     * @param oper Constructor function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return New created and populated Javascript Array object
     */
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Add values to a string list
     * @param list Destination list
     * @param emptyOk Add empty strings
     * @return The number of added items
     */
    unsigned int toStringList(ObjList& list, bool emptyOk = true);

protected:
    /**
     * Clone and rename method
     * @param name Name of the cloned object
     * @param oper ExpOperation that requested the cloning
     * @return New object instance
     */
    virtual JsObject* clone(const char* name, const ExpOperation& oper) const
	{ return new JsArray(mutex(),name,oper.lineNumber()); }

    /**
     * Clone and rename method. Used to prepare a new object for copy
     * @param context Script context from which Array prototype is obtained
     * @param mtx Optional mutex, use our mutex if not given
     * @param line Line number
     * @return New object instance
     */
    virtual JsObject* cloneForCopy(GenObject* context, ScriptMutex** mtx = 0,
	unsigned int line = 0) const
	{ return new JsArray(context,line,mtx ? *mtx : mutex()); }

    /**
     * Try to evaluate a single native method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

private:

    /**
     * Private constructor
     * @param mtx Pointer to the mutex that serializes this object
     */
    JsArray(ScriptMutex* mtx = 0);

    bool runNativeSlice(ObjList& stack, const ExpOperation& oper, GenObject* context);
    bool runNativeSplice(ObjList& stack, const ExpOperation& oper, GenObject* context);
    bool runNativeSort(ObjList& stack, const ExpOperation& oper, GenObject* context);
    int32_t m_length;
};

/**
 * Javascript RegExp class, implements regular expression matching
 * @short Javascript RegExp
 */
class YSCRIPT_API JsRegExp : public JsObject
{
    YCLASS(JsRegExp,JsObject)
public:
    /**
     * Constructor for a RegExp constructor
     * @param mtx Pointer to the mutex that serializes this object
     */
    JsRegExp(ScriptMutex* mtx = 0);

    /**
     * Constructor for a RegExp object
     * @param mtx Pointer to the mutex that serializes this object
     * @param name Full name of the object
     * @param line Code line where this object was created
     * @param rexp Regular expression text
     * @param insensitive True to not differentiate case
     * @param extended True to use POSIX Extended Regular Expression syntax
     * @param frozen True to create an initially frozen object
     */
    JsRegExp(ScriptMutex* mtx, const char* name, unsigned int line, const char* rexp = 0, bool insensitive = false,
	bool extended = true, bool frozen = false);

    /**
     * Constructor from existing Regexp
     * @param mtx Pointer to the mutex that serializes this object
     * @param line Code line where this object was created
     * @param rexp Regular expression to copy
     * @param frozen True to create an initially frozen object
     */
    JsRegExp(ScriptMutex* mtx, unsigned int line, const Regexp& rexp, bool frozen = false);

    /**
     * Access the internal Regexp object that does the matching
     * @return Const reference to the internal Regexp object
     */
    inline const Regexp& regexp() const
	{ return m_regexp; }

    /**
     * Access the internal Regexp object that does the matching
     * @return Reference to the internal Regexp object
     */
    inline Regexp& regexp()
	{ return m_regexp; }

    /**
     * Try to assign a value to a single field
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * RegExp object constructor, it's run on the prototype
     * @param stack Evaluation stack in use
     * @param oper Constructor function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return New created and populated Javascript RegExp object
     */
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied regexp
     * @param oper ExpOperation that required the copy
     * @return New object instance, does not keep references to old regexp
     */
    virtual JsObject* copy(ScriptMutex* mtx, const ExpOperation& oper) const;

protected:
    /**
     * Constructor for a RegExp object
     * @param other RegExp to copy
     * @param context Context containing the prototype
     * @param mtx Script mutex to use
     * @param line Code line where this object was created
     * @param frozen True to create an initially frozen object
     */
    JsRegExp(const JsRegExp& other, GenObject* context, ScriptMutex* mtx,
	unsigned int line = 0, bool frozen = false);

    /**
     * Clone and rename method
     * @param name Name of the cloned object
     * @param oper ExpOperation that required the clone
     * @return New object instance
     */
    virtual JsObject* clone(const char* name, const ExpOperation& oper) const
	{ return new JsRegExp(mutex(),name,oper.lineNumber(),m_regexp.c_str(),
	    m_regexp.isCaseInsensitive(),m_regexp.isExtended()); }

    /**
     * Clone and rename method. Used to prepare a new object for copy
     * @param context Script context from which RegExp prototype is obtained
     * @param mtx Optional mutex, use our mutex if not given
     * @param line Line number
     * @return New object instance
     */
    virtual JsObject* cloneForCopy(GenObject* context, ScriptMutex** mtx = 0,
	unsigned int line = 0) const
	{ return new JsRegExp(*this,context,mtx ? *mtx : mutex(),line); }

    /**
     * Try to evaluate a single native method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

private:
    Regexp m_regexp;
};

/**
 * Javascript JSON path class
 * @short Javascript JSON path
 */
class JsJPath : public JsObject
{
public:
    /**
     * Constructor
     * @param mtx Pointer to the mutex that serializes this object
     */
    JsJPath(ScriptMutex* mtx);

    /**
     * JPath object constructor, it's run on the prototype
     * @param stack Evaluation stack in use
     * @param oper Constructor function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return Newly created and populated Javascript JSON path
     */
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Retrieve held path
     * @return Held path reference
     */
    virtual const JPath& path() const
	{ return m_path; }

    /**
     * Retrieve path string
     * @return Held path value
     */
    virtual const String& toString() const
	{ return m_path; }

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

protected:
    /**
     * Constructor from existing path
     * @param mtx Pointer to the mutex that serializes this object
     * @param line Code line where this object was created
     * @param path Path to copy
     */
    inline JsJPath(ScriptMutex* mtx, unsigned int line, const char* path)
	: JsObject(mtx,path,line),
	  m_path(path)
	{ }

    /**
     * Constructor for a JPath object
     * @param mtx Pointer to the mutex that serializes this object
     * @param name Full name of the object
     * @param line Code line where this object was created
     * @param path JSON path
     */
    inline JsJPath(ScriptMutex* mtx, const char* name, unsigned int line, const JPath& path)
	: JsObject(mtx,name,line),
	  m_path(path)
	{ }

    /**
     * Constructor for a JPath object
     * @param path Path to copy
     * @param context Context containing the prototype
     * @param mtx Script mutex to use
     * @param line Code line where this object was created
     * @param frozen True to create an initially frozen object
     */
    inline JsJPath(const JPath& path, GenObject* context, ScriptMutex* mtx,
	unsigned int line = 0, bool frozen = false)
	: JsObject(mtx,path,line,frozen),
	  m_path(path)
	{ setPrototype(context,YSTRING("JPath")); }

    /**
     * Clone and rename method
     * @param name Name of the cloned object
     * @param oper ExpOperation that required the clone
     * @return New object instance
     */
    virtual JsObject* clone(const char* name, const ExpOperation& oper) const
	{ return new JsJPath(mutex(),name,oper.lineNumber(),m_path); }

    /**
     * Clone and rename method. Used to prepare a new object for copy
     * @param context Script context from which JsPath prototype is obtained
     * @param mtx Optional mutex, use our mutex if not given
     * @param line Line number
     * @return New object instance
     */
    virtual JsObject* cloneForCopy(GenObject* context, ScriptMutex** mtx = 0,
	unsigned int line = 0) const
	{ return new JsJPath(path(),context,mtx ? *mtx : mutex(),line); }

    /**
     * Try to evaluate a single native method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

private:
    JPath m_path;                        // Held JPath
};

/**
 * Javascript parser, takes source code and generates preparsed code
 * @short Javascript parser
 */
class YSCRIPT_API JsParser : public ScriptParser
{
    YCLASS(JsParser,ScriptParser)
public:
    /**
     * Constructor
     * @param allowLink True to allow linking of the code, false otherwise.
     * @param allowTrace True to allow the script to enable performance tracing
     */
    inline JsParser(bool allowLink = true, bool allowTrace = false)
	: m_allowLink(allowLink), m_allowTrace(allowTrace)
	{ }

    /**
     * Parse a string as Javascript source code
     * @param text Source code text
     * @param fragment True if the code is just an included fragment
     * @param file Name of the file that is being parsed
     * @param len Length of text, negative if unknown
     * @return True if the text was successfully parsed
     */
    virtual bool parse(const char* text, bool fragment = false, const char* file = 0, int len = -1);

    /**
     * Create a context adequate for Javascript code
     * @return A new Javascript context
     */
    virtual ScriptContext* createContext(unsigned int instIdx = 0, unsigned int maxInst = 1) const;

    /**
     * Create a runner adequate for a block of parsed Javascript code
     * @param code Parsed code block
     * @param context Javascript context, an empty one will be allocated if NULL
     * @param title An optional name for the runner
     * @param instIdx Javascript context instance
     * @param maxInst Number of context instances
     * @return A new Javascript runner, NULL if code is NULL
     */
    virtual ScriptRun* createRunner(ScriptCode* code, ScriptContext* context = 0, const char* title = 0, 
                            unsigned int instIdx = 0, unsigned int maxInst = 1) const;

    /**
     * Create a runner adequate for the parsed Javascript code
     * @param context Javascript context, an empty one will be allocated if NULL
     * @param title An optional name for the runner
     * @param instIdx Javascript context instance
     * @param maxInst Number of context instances
     * @return A new Javascript runner, NULL if code is not yet parsed
     */
    inline ScriptRun* createRunner(ScriptContext* context = 0, const char* title = 0, 
                            unsigned int instIdx = 0, unsigned int maxInst = 1) const
	{ return createRunner(code(),context,title,instIdx,maxInst); }

    /**
     * Check if a script has a certain function or method
     * @param name Name of the function to check
     * @return True if function exists in code
     */
    virtual bool callable(const String& name);

    /**
     * Adjust a file script path to include default if needed
     * @param script File path to adjust
     * @param extraInc True to check the extra include path first
     */
    void adjustPath(String& script, bool extraInc = false) const;

    /**
     * Retrieve the base script path
     * @return Base path added to relative script paths
     */
    inline const String& basePath() const
	{ return m_basePath; }

    /**
     * Retrieve the extra include script path
     * @return Include path added to relative script paths
     */
    inline const String& includePath() const
	{ return m_includePath; }

    /**
     * Set the base script path
     * @param path Base path to add to relative script paths
     * @param incPath Extra include path to add to relative script paths
     */
    inline void basePath(const char* path, const char* incPath = 0)
	{ m_basePath = path; m_includePath = incPath; }

    /**
     * Retrieve the last parsed file name
     * @return Name of the successfully parsed file or an empty String
     */
    inline const String& parsedFile() const
	{ return m_parsedFile; }

    /**
     * Check if the script or any includes have changed
     * @param file Name of the file to check
     * @return True if the script may have changed, false if not changed
     */
    bool scriptChanged(const char* file) const;

    /**
     * Check if the script or any includes have changed
     * @param file Name of the file to check
     * @param path New base path to check
     * @param incPath New extra include path to check
     * @return True if the script may have changed, false if not changed
     */
    inline bool scriptChanged(const char* file, const String& path, const String& incPath = String::empty()) const
	{ return (path != m_basePath) || (incPath != m_includePath) || scriptChanged(file); }

    /**
     * Set whether the Javascript code should be linked or not
     * @param allowed True to allow linking, false otherwise
     */
    inline void link(bool allowed = true)
	{ m_allowLink = allowed; }

    /**
     * Set whether the Javascript code can be traced or not
     * @param allowed True to allow tracing, false otherwise
     */
    inline void trace(bool allowed = true)
	{ m_allowTrace = allowed; }

    /**
     * Parse and run a piece of Javascript code
     * @param text Source code fragment to execute
     * @param result Pointer to an optional pointer to store returned value
     * @param context Script context, an empty one will be allocated if NULL
     * @return Status of the runtime after code execution
     */
    static ScriptRun::Status eval(const String& text, ExpOperation** result = 0, ScriptContext* context = 0);

    /**
     * Parse a complete block of JSON text
     * @param text JSON text to parse
     * @param mtx Pointer to the mutex that serializes this object
     * @param stack Pointer to an execution stack, required for adding prototypes
     * @param context Pointer to an execution context, required for adding prototypes
     * @param op ExpOperation that determined calling of this method
     * @return ExpOperation holding the content of JSON, must be dereferenced after use, NULL if parse error
     */
    static ExpOperation* parseJSON(const char* text, ScriptMutex* mtx = 0, ObjList* stack = 0,
	    GenObject* context = 0, const ExpOperation* op = 0);

    /**
     * Get a "null" object wrapper that will identity match another "null"
     * @param name Name of the new wrapper, "null" if empty
     * @return ExpWrapper for the "null" object
     */
    static ExpOperation* nullClone(const char* name = 0);

    /**
     * Get an "undefined" object wrapper that will identity match another "undefined"
     * @param name Name of the new wrapper
     * @return ExpWrapper pointer
     */
    static inline ExpOperation* undefinedClone(const char* name = 0)
	{ return new ExpWrapper((GenObject*)0,name); }

    /**
     * Obtain the "null" object
     * @return Referenced "null" object (0 if ref() fails)
     */
    static JsObject* nullObject();

    /**
     * Check if an operation holds a null value
     * @return True if the operation holds a null object
     */
    static bool isNull(const ExpOperation& oper);

    /**
     * Check if an operation holds an undefined value
     * @return True if the operation holds an undefined value
     */
    static bool isUndefined(const ExpOperation& oper);

    /**
     * Check if an operation is null or undefined
     * @return True if the operation holds an undefined value or a null object
     */
    static bool isMissing(const ExpOperation& oper);

    /**
     * Check if an operation is missing, holds a null or undefined
     * @return True if the operation is null or holds an undefined value or a null object
     */
    inline static bool isMissing(const ExpOperation* oper)
	{ return !oper || isMissing(*oper); }

    /**
     * Check if an operation is not null or undefined
     * @return True if the operation holds a value or non-null object
     */
    inline static bool isPresent(const ExpOperation& oper)
	{ return !isMissing(oper); }

    /**
     * Check if an operation is present and not null or undefined
     * @return True if the operation holds a value or non-null object
     */
    inline static bool isPresent(const ExpOperation* oper)
	{ return oper && !isMissing(*oper); }

    /**
     * Check if an operation holds a null or undefined value or empty string
     * @return True if the operation is an undefined value or a null object or empty string
     */
    inline static bool isEmpty(const ExpOperation& oper)
	{ return oper.null() || isMissing(oper); }

    /**
     * Check if an operation is missing, holds a null or undefined value or empty string
     * @return True if the operation is an undefined value or a null object or empty string
     */
    inline static bool isEmpty(const ExpOperation* oper)
	{ return TelEngine::null(oper) || isMissing(*oper); }

    /**
     * Check if an operation is not null or undefined or empty string
     * @return True if the operation holds a non-empty value or non-null object
     */
    inline static bool isFilled(const ExpOperation& oper)
	{ return oper && !isMissing(oper); }

    /**
     * Check if an operation is not null or undefined or empty string
     * @return True if the operation holds a non-empty value or non-null object
     */
    inline static bool isFilled(const ExpOperation* oper)
	{ return !isEmpty(oper); }

    /**
     * Check if an operation is present and holds an object
     * @param oper Operation to check
     * @return JsObject pointer, 0 if not
     */
    inline static JsObject* objPresent(const ExpOperation& oper)
	{ return isPresent(oper) ? YOBJECT(JsObject,&oper) : 0; }

    /**
     * Check if an operation is present and holds an object
     * @param oper Operation to check
     * @return JsObject pointer, 0 if not
     */
    inline static JsObject* objPresent(const ExpOperation* oper)
	{ return oper ? objPresent(*oper) : 0; }

    /**
     * Check an ExpOperation. Return it or 'null' or 'undefined'
     * @param oper ExpOperation pointer to check
     * @param name null/undefined ExpOperation name when given oper is NULL
     * @param null True to build 'null', false to build 'undefined' when given oper is NULL
     * @return ExpOperation pointer
     */
    static inline ExpOperation* validExp(ExpOperation* oper, const char* name = 0,
	bool null = true) {
	    if (oper)
		return oper;
	    if (null)
		return nullClone(name);
	    return undefinedClone(name);
	}

    /**
     * Check a JsObject. Return it or 'null' or 'undefined'
     * @param oper JsObject pointer to check
     * @param name null/undefined ExpOperation name when given oper is NULL
     * @param null True to build 'null', false to build 'undefined' when given oper is NULL
     * @return ExpOperation pointer
     */
    static inline ExpOperation* validExp(JsObject* jso, const char* name = 0,
	bool null = true) {
	    if (jso)
		return new ExpWrapper(jso,name);
	    if (null)
		return nullClone(name);
	    return undefinedClone(name);
	}

    /**
     * Retrieve a string from ExpOperation
     * @param oper Operation to check
     * @return Empty string if oper is undefined/null, operation value otherwise
     */
    static inline const String& getString(const ExpOperation* oper)
	{ return isMissing(oper) ? String::empty() : *oper; }

    /**
     * Set a string from ExpOperation if not 'undefined'. Clear if 'null'
     * @param buf The string to set
     * @param oper Operation to check
     * @return String reference
     */
    static inline String& setString(String& buf, const ExpOperation* oper) {
	    if (!oper || isUndefined(*oper))
		return buf;
	    if (isNull(*oper))
		return (buf = "");
	    return buf.assign(*oper);
	}

private:
    String m_basePath;
    String m_includePath;
    String m_parsedFile;
    bool m_allowLink;
    bool m_allowTrace;
};

}; // namespace TelEngine

#endif /* __YATESCRIPT_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
