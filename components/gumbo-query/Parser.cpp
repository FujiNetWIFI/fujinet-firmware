/***************************************************************************
 *
 * $Id$
 *
 **************************************************************************/

/**
 * @file $HeadURL$
 * @author $Author$(hoping@baimashi.com)
 * @date $Date$
 * @version $Revision$
 * @brief
 *
 **/

// FujiNet: converted to be exception-free. The ESP firmware disables C++
// exceptions, so every original `throw error(msg)` became `{ error(msg);
// return <sentinel>; }` (NULL for CSelector*, "" for std::string, 0 for int),
// and callers bail via `if (mError)` after each sub-parse, releasing owned
// selectors so the error path neither dereferences NULL nor leaks.

#include "Parser.h"
#include "Selector.h"
#include "QueryUtil.h"

CParser::CParser(std::string aInput)
{
	mInput = aInput;
	mOffset = 0;
	mError = false;
}

CParser::~CParser()
{
}

CSelector* CParser::create(std::string aInput)
{
	CParser parser(aInput);
	CSelector* ret = parser.parseSelectorGroup();
	if (parser.mError)
	{
		if (ret != NULL)
		{
			ret->release();
		}
		return NULL;
	}
	return ret;
}

CSelector* CParser::parseSelectorGroup()
{
	CSelector* ret = parseSelector();
	if (mError)
	{
		if (ret != NULL) ret->release();
		return NULL;
	}
	while (mOffset < mInput.size())
	{
		if (mInput[mOffset] != ',')
		{
			return ret;
		}

		mOffset++;

		CSelector* sel = parseSelector();
		if (mError)
		{
			if (sel != NULL) sel->release();
			if (ret != NULL) ret->release();
			return NULL;
		}
		CSelector* oldRet = ret;
		ret = new CBinarySelector(CBinarySelector::EUnion, ret, sel);
		sel->release();
		oldRet->release();
	}

	return ret;
}

CSelector* CParser::parseSelector()
{
	skipWhitespace();
	CSelector* ret = parseSimpleSelectorSequence();
	if (mError)
	{
		if (ret != NULL) ret->release();
		return NULL;
	}
	while (true)
	{
		char combinator = 0;
		if (skipWhitespace())
		{
			combinator = ' ';
		}

		if (mOffset >= mInput.size())
		{
			return ret;
		}

		char c = mInput[mOffset];
		if (c == '+' || c == '>' || c == '~')
		{
			combinator = c;
			mOffset++;
			skipWhitespace();
		}
		else if (c == ',' || c == ')')
		{
			return ret;
		}

		if (combinator == 0)
		{
			return ret;
		}

		CSelector* oldRet = ret;
		CSelector* sel = parseSimpleSelectorSequence();
		if (mError)
		{
			if (sel != NULL) sel->release();
			if (oldRet != NULL) oldRet->release();
			return NULL;
		}
		bool isOk = true;
		if (combinator == ' ')
		{
			ret = new CBinarySelector(CBinarySelector::EDescendant, oldRet, sel);
		}
		else if (combinator == '>')
		{
			ret = new CBinarySelector(CBinarySelector::EChild, oldRet, sel);
		}
		else if (combinator == '+')
		{
			ret = new CBinarySelector(oldRet, sel, true);
		}
		else if (combinator == '~')
		{
			ret = new CBinarySelector(oldRet, sel, true);
		}
		else
		{
			isOk = false;
		}
		oldRet->release();
		sel->release();
		if(!isOk)
		{
			error("impossible");
			return NULL;
		}

	}
}

CSelector* CParser::parseSimpleSelectorSequence()
{
	CSelector* ret = NULL;
	if (mOffset >= mInput.size())
	{
		error("expected selector, found EOF instead");
		return NULL;
	}

	char c = mInput[mOffset];
	if (c == '*')
	{
		mOffset++;
	}
	else if (c == '#' || c == '.' || c == '[' || c == ':')
	{

	}
	else
	{
		ret = parseTypeSelector();
		if (mError)
		{
			if (ret != NULL) ret->release();
			return NULL;
		}
	}

	while (mOffset < mInput.size())
	{
		char c = mInput[mOffset];
		CSelector* sel = NULL;
		if (c == '#')
		{
			sel = parseIDSelector();
		}
		else if (c == '.')
		{
			sel = parseClassSelector();
		}
		else if (c == '[')
		{
			sel = parseAttributeSelector();
		}
		else if (c == ':')
		{
			sel = parsePseudoclassSelector();
		}
		else
		{
			break;
		}

		if (mError)
		{
			if (sel != NULL) sel->release();
			if (ret != NULL) ret->release();
			return NULL;
		}

		if (ret == NULL)
		{
			ret = sel;
		}
		else
		{
			CSelector* oldRet = ret;
			ret = new CBinarySelector(CBinarySelector::EIntersection, ret, sel);
			sel->release();
			oldRet->release();
		}
	}

	if (ret == NULL)
	{
		ret = new CSelector();
	}

	return ret;
}

void CParser::parseNth(int& aA, int& aB)
{

	if (mOffset >= mInput.size())
	{
		goto eof;
	}

	{
		char c = mInput[mOffset];
		if (c == '-')
		{
			mOffset++;
			goto negativeA;
		}
		else if (c == '+')
		{
			mOffset++;
			goto positiveA;
		}
		else if (c >= '0' && c <= '9')
		{
			goto positiveA;
		}
		else if (c == 'n' || c == 'N')
		{
			goto readN;
		}
		else if (c == 'o' || c == 'O' || c == 'e' || c == 'E')
		{
			std::string id = parseName();
			if (mError) return;
			id = CQueryUtil::tolower(id);
			if (id == "odd")
			{
				aA = 2;
				aB = 1;
			}
			else if (id == "even")
			{
				aA = 2;
				aB = 0;
			}
			else
			{
				error("expected 'odd' or 'even', invalid found");
				return;
			}
			return;
		}
		else
		{
			goto invalid;
		}
	}

	positiveA:
	{
		if (mOffset >= mInput.size())
		{
			goto eof;
		}
		char c = mInput[mOffset];
		if (c >= '0' && c <= '9')
		{
			aA = parseInteger();
			if (mError) return;
			goto readA;
		}
		else if (c == 'n' || c == 'N')
		{
			aA = 1;
			mOffset++;
			goto readN;
		}
		else
		{
			goto invalid;
		}
	}

	negativeA:
	{
		if (mOffset >= mInput.size())
		{
			goto eof;
		}
		char c = mInput[mOffset];
		if (c >= '0' && c <= '9')
		{
			aA = -parseInteger();
			if (mError) return;
			goto readA;
		}
		else if (c == 'n' || c == 'N')
		{
			aA = -1;
			mOffset++;
			goto readN;
		}
		else
		{
			goto invalid;
		}
	}

	readA:
	{
		if (mOffset >= mInput.size())
		{
			goto eof;
		}

		char c = mInput[mOffset];
		if (c == 'n' || c == 'N')
		{
			mOffset++;
			goto readN;
		}
		else
		{
			aB = aA;
			aA = 0;
			return;
		}

	}

	readN:
	{
		skipWhitespace();
		if (mOffset >= mInput.size())
		{
			goto eof;
		}

		char c = mInput[mOffset];
		if (c == '+')
		{
			mOffset++;
			skipWhitespace();
			aB = parseInteger();
			return;
		}
		else if (c == '-')
		{
			mOffset--;
			skipWhitespace();
			aB = -parseInteger();
			return;
		}
		else
		{
			aB = 0;
			return;
		}
	}

	eof:
	{
		error("unexpected EOF while attempting to parse expression of form an+b");
		return;
	}

	invalid:
	{
		error("unexpected character while attempting to parse expression of form an+b");
		return;
	}

}

int CParser::parseInteger()
{
	size_t offset = mOffset;
	int i = 0;
	for (; offset < mInput.size(); offset++)
	{
		char c = mInput[offset];
		if (c < '0' || c > '9')
		{
			break;
		}
		i = i * 10 + c - '0';
	}

	if (offset == mOffset)
	{
		error("expected integer, but didn't find it.");
		return 0;
	}
	mOffset = offset;

	return i;
}

CSelector* CParser::parsePseudoclassSelector()
{
	if (mOffset >= mInput.size() || mInput[mOffset] != ':')
	{
		error("expected pseudoclass selector (:pseudoclass), found invalid char");
		return NULL;
	}
	mOffset++;
	std::string name = parseIdentifier();
	if (mError) return NULL;
	name = CQueryUtil::tolower(name);
	if (name == "not" || name == "has" || name == "haschild")
	{
		if (!consumeParenthesis())
		{
			error("expected '(' but didn't find it");
			return NULL;
		}

		CSelector* sel = parseSelectorGroup();
		if (mError)
		{
			if (sel != NULL) sel->release();
			return NULL;
		}
		if (!consumeClosingParenthesis())
		{
			sel->release();
			error("expected ')' but didn't find it");
			return NULL;
		}

		CUnarySelector::TOperator op;
		if (name == "not")
		{
			op = CUnarySelector::ENot;
		}
		else if (name == "has")
		{
			op = CUnarySelector::EHasDescendant;
		}
		else if (name == "haschild")
		{
			op = CUnarySelector::EHasChild;
		}
		else
		{
			sel->release();
			error("impossbile");
			return NULL;
		}
		CSelector* ret = new CUnarySelector(op, sel);
		sel->release();
		return ret;
	}
	else if (name == "contains" || name == "containsown")
	{
		if (!consumeParenthesis() || mOffset >= mInput.size())
		{
			error("expected '(' but didn't find it");
			return NULL;
		}

		std::string value;
		char c = mInput[mOffset];
		if (c == '\'' || c == '"')
		{
			value = parseString();
		}
		else
		{
			value = parseIdentifier();
		}
		if (mError) return NULL;
		value = CQueryUtil::tolower(value);
		skipWhitespace();

		if (!consumeClosingParenthesis())
		{
			error("expected ')' but didn't find it");
			return NULL;
		}

		CTextSelector::TOperator op;
		if (name == "contains")
		{
			op = CTextSelector::EContains;
		}
		else if (name == "containsown")
		{
			op = CTextSelector::EOwnContains;
		}
		else
		{
			error("impossibile");
			return NULL;
		}
		return new CTextSelector(op, value);
	}
	else if (name == "matches" || name == "matchesown")
	{
		//TODO
		error("unsupported regex");
		return NULL;
	}
	else if (name == "nth-child" || name == "nth-last-child" || name == "nth-of-type"
			|| name == "nth-last-of-type")
	{
		if (!consumeParenthesis())
		{
			error("expected '(' but didn't find it");
			return NULL;
		}

		int a, b;
		parseNth(a, b);
		if (mError) return NULL;

		if (!consumeClosingParenthesis())
		{
			error("expected ')' but didn't find it");
			return NULL;
		}

		return new CSelector(a, b, name == "nth-last-child" || name == "nth-last-of-type",
				name == "nth-of-type" || name == "nth-last-of-type");
	}
	else if (name == "first-child")
	{
		return new CSelector(0, 1, false, false);
	}
	else if (name == "last-child")
	{
		return new CSelector(0, 1, true, false);
	}
	else if (name == "first-of-type")
	{
		return new CSelector(0, 1, false, true);
	}
	else if (name == "last-of-type")
	{
		return new CSelector(0, 1, true, true);
	}
	else if (name == "only-child")
	{
		return new CSelector(false);
	}
	else if (name == "only-of-type")
	{
		return new CSelector(true);
	}
	else if (name == "empty")
	{
		return new CSelector(CSelector::EEmpty);
	}
	else
	{
		error("unsupported op:" + name);
		return NULL;
	}
}

CSelector* CParser::parseAttributeSelector()
{
	if (mOffset >= mInput.size() || mInput[mOffset] != '[')
	{
		error("expected attribute selector ([attribute]), found invalid char");
		return NULL;
	}
	mOffset++;
	skipWhitespace();
	std::string key = parseIdentifier();
	if (mError) return NULL;
	skipWhitespace();
	if (mOffset >= mInput.size())
	{
		error("unexpected EOF in attribute selector");
		return NULL;
	}

	if (mInput[mOffset] == ']')
	{
		mOffset++;
		return new CAttributeSelector(CAttributeSelector::EExists, key);
	}

	if (mOffset + 2 > mInput.size())
	{
		error("unexpected EOF in attribute selector");
		return NULL;
	}

	std::string op = mInput.substr(mOffset, 2);
	if (op[0] == '=')
	{
		op = "=";
	}
	else if (op[1] != '=')
	{
		error("expected equality operator, found invalid char");
		return NULL;
	}

	mOffset += op.size();
	skipWhitespace();
	if (mOffset >= mInput.size())
	{
		error("unexpected EOF in attribute selector");
		return NULL;
	}

	std::string value;
	if (op == "#=")
	{
		//TODo
		error("unsupported regex");
		return NULL;
	}
	else
	{
		char c = mInput[mOffset];
		if (c == '\'' || c == '"')
		{
			value = parseString();
		}
		else
		{
			value = parseIdentifier();
		}
		if (mError) return NULL;
	}
	skipWhitespace();
	if (mOffset >= mInput.size() || mInput[mOffset] != ']')
	{
		error("expected attribute selector ([attribute]), found invalid char");
		return NULL;
	}
	mOffset++;

	CAttributeSelector::TOperator aop;
	if (op == "=")
	{
		aop = CAttributeSelector::EEquals;
	}
	else if (op == "~=")
	{
		aop = CAttributeSelector::EIncludes;
	}
	else if (op == "|=")
	{
		aop = CAttributeSelector::EDashMatch;
	}
	else if (op == "^=")
	{
		aop = CAttributeSelector::EPrefix;
	}
	else if (op == "$=")
	{
		aop = CAttributeSelector::ESuffix;
	}
	else if (op == "*=")
	{
		aop = CAttributeSelector::ESubString;
	}
	else if (op == "#=")
	{
		//TODO
		error("unsupported regex");
		return NULL;
	}
	else
	{
		error("unsupported op:" + op);
		return NULL;
	}
	return new CAttributeSelector(aop, key, value);
}

CSelector* CParser::parseClassSelector()
{
	if (mOffset >= mInput.size() || mInput[mOffset] != '.')
	{
		error("expected class selector (.class), found invalid char");
		return NULL;
	}
	mOffset++;
	std::string clazz = parseIdentifier();
	if (mError) return NULL;

	return new CAttributeSelector(CAttributeSelector::EIncludes, "class", clazz);
}

CSelector* CParser::parseIDSelector()
{
	if (mOffset >= mInput.size() || mInput[mOffset] != '#')
	{
		error("expected id selector (#id), found invalid char");
		return NULL;
	}
	mOffset++;
	std::string id = parseName();
	if (mError) return NULL;

	return new CAttributeSelector(CAttributeSelector::EEquals, "id", id);
}

CSelector* CParser::parseTypeSelector()
{
	std::string tag = parseIdentifier();
	if (mError) return NULL;
	return new CSelector(gumbo_tag_enum(tag.c_str()));
}

bool CParser::consumeClosingParenthesis()
{
	size_t offset = mOffset;
	skipWhitespace();
	if (mOffset < mInput.size() && mInput[mOffset] == ')')
	{
		mOffset++;
		return true;
	}
	mOffset = offset;
	return false;
}

bool CParser::consumeParenthesis()
{
	if (mOffset < mInput.size() && mInput[mOffset] == '(')
	{
		mOffset++;
		skipWhitespace();
		return true;
	}
	return false;
}

bool CParser::skipWhitespace()
{
	size_t offset = mOffset;
	while (offset < mInput.size())
	{
		char c = mInput[offset];
		if (c == ' ' || c == '\r' || c == '\t' || c == '\n' || c == '\f')
		{
			offset++;
			continue;
		}
		else if (c == '/')
		{
			if (mInput.size() > offset + 1 && mInput[offset + 1] == '*')
			{
				size_t pos = mInput.find("*/", offset + 2);
				if (pos != std::string::npos)
				{
					offset = pos + 2;
					continue;
				}
			}
		}
		break;
	}

	if (offset > mOffset)
	{
		mOffset = offset;
		return true;
	}

	return false;
}

std::string CParser::parseString()
{
	size_t offset = mOffset;
	if (mInput.size() < offset + 2)
	{
		error("expected string, found EOF instead");
		return "";
	}

	char quote = mInput[offset];
	offset++;
	std::string ret;

	while (offset < mInput.size())
	{
		char c = mInput[offset];
		if (c == '\\')
		{
			if (mInput.size() > offset + 1)
			{
				char c = mInput[offset + 1];
				if (c == '\r')
				{
					if (mInput.size() > offset + 2 && mInput[offset + 2] == '\n')
					{
						offset += 3;
						continue;
					}
				}

				if (c == '\r' || c == '\n' || c == '\f')
				{
					offset += 2;
					continue;
				}
			}
			mOffset = offset;
			ret += parseEscape();
			if (mError) return "";
			offset = mOffset;
		}
		else if (c == quote)
		{
			break;
		}
		else if (c == '\r' || c == '\n' || c == '\f')
		{
			error("unexpected end of line in string");
			return "";
		}
		else
		{
			size_t start = offset;
			while (offset < mInput.size())
			{
				char c = mInput[offset];
				if (c == quote || c == '\\' || c == '\r' || c == '\n' || c == '\f')
				{
					break;
				}
				offset++;
			}
			ret += mInput.substr(start, offset - start);
		}
	}

	if (offset >= mInput.size())
	{
		error("EOF in string");
		return "";
	}

	offset++;
	mOffset = offset;

	return ret;
}

std::string CParser::parseName()
{
	size_t offset = mOffset;
	std::string ret;
	while (offset < mInput.size())
	{
		char c = mInput[offset];
		if (nameChar(c))
		{
			size_t start = offset;
			while (offset < mInput.size() && nameChar(mInput[offset]))
			{
				offset++;
			}
			ret += mInput.substr(start, offset - start);
		}
		else if (c == '\\')
		{
			mOffset = offset;
			ret += parseEscape();
			if (mError) return "";
			offset = mOffset;
		}
		else
		{
			break;
		}
	}

	if (ret == "")
	{
		error("expected name, found EOF instead");
		return "";
	}

	mOffset = offset;
	return ret;
}

std::string CParser::parseIdentifier()
{
	bool startingDash = false;
	if (mInput.size() > mOffset && mInput[mOffset] == '-')
	{
		startingDash = true;
		mOffset++;
	}

	if (mInput.size() <= mOffset)
	{
		error("expected identifier, found EOF instead");
		return "";
	}

	char c = mInput[mOffset];
	if (!nameStart(c) && c != '\\')
	{
		error("expected identifier, found invalid char");
		return "";
	}

	std::string name = parseName();
	if (mError) return "";
	if (startingDash)
	{
		name = "-" + name;
	}

	return name;
}

bool CParser::nameChar(char c)
{
	return nameStart(c) || (c == '-') || (c >= '0' && c <= '9');
}

bool CParser::nameStart(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_') || (c > 127);
}

bool CParser::hexDigit(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::string CParser::parseEscape()
{
	if (mInput.size() < mOffset + 2 || mInput[mOffset] != '\\')
	{
		error("invalid escape sequence");
		return "";
	}

	size_t start = mOffset + 1;
	char c = mInput[start];
	if (c == '\r' || c == '\n' || c == '\f')
	{
		error("escaped line ending outside string");
		return "";
	}

	if (!hexDigit(c))
	{
		std::string ret = mInput.substr(start, 1);
		mOffset += 2;
		return ret;
	}

	size_t i = 0;
	std::string ret;
	c = 0;
	for (i = start; i < mOffset + 6 && i < mInput.size() && hexDigit(mInput[i]); i++)
	{
		unsigned int d = 0;
		char ch = mInput[i];
		if (ch >= '0' && ch <= '9')
		{
			d = ch - '0';
		}
		else if (ch >= 'a' && ch <= 'f')
		{
			d = ch - 'a' + 10;
		}
		else if (ch >= 'A' && ch <= 'F')
		{
			d = ch - 'A' + 10;
		}
		else
		{
			error("impossible");
			return "";
		}

		if ((i - start) % 2)
		{
			c += d;
			ret.push_back(c);
			c = 0;
		}
		else
		{
			c += (d << 4);
		}
	}

	if (ret.size() == 0 || c != 0)
	{
		error("invalid hex digit");
		return "";
	}

	if (mInput.size() > i)
	{
		switch (mInput[i])
		{
			case '\r':
				i++;
				if (mInput.size() > i && mInput[i] == '\n')
				{
					i++;
				}
				break;
			case ' ':
			case '\t':
			case '\n':
			case '\f':
				i++;
				break;
		}
	}

	mOffset = i;
	return ret;
}

std::string CParser::error(std::string message)
{
	mError = true;

	size_t d = mOffset;
	std::string ds;
	if (d == 0)
	{
		ds = '0';
	}

	while (d)
	{
		ds.push_back(d % 10 + '0');
		d /= 10;
	}

	std::string ret = message + " at:";
	for (std::string::reverse_iterator rit = ds.rbegin(); rit != ds.rend(); ++rit) {
		ret.push_back(*rit);
	}

	return ret;
}

/* vim: set ts=4 sw=4 sts=4 tw=100 noet: */
