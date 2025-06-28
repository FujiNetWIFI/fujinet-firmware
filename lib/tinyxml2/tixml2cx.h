/*
tinyxml2ex - a set of add-on classes and helper functions bringing C++11/14 features, such as iterators, strings and exceptions, to tinyxml2
https://github.com/stanthomas/tinyxml2-ex

tixml2cx.h implements the copy operations of tinyxml2ex
these copy a branch of an xml document to another document or another location within the same document,
optionally substituting element parameter values to allow customisation of templated xml snippets
it is separate from the base tinyxml2 extensions because it uses an additional collection class (unordered_map)


Copyright (c) 2017 Stan Thomas

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.


tinyxml2 is the work of Lee Thomason (www.grinninglizard.com) and others.
It can be found here: https://github.com/leethomason/tinyxml2 and has it's own licensing terms.

*/

#pragma once

#include <unordered_map>
#ifndef __TINYXML_EX__
#include <tixml2ex.h>
#endif // !__TINYXML_EX__

namespace tinyxml2
{
	inline namespace tixml2ex
	{
		class XMLCopy : public XMLVisitor
		{
		public:
			XMLCopy (XMLElement * target) : _target(target) { _newDoc = target->GetDocument(); }

			virtual bool VisitEnter (const XMLElement & element, const XMLAttribute * attribute) override
			{
				auto e = _newDoc->NewElement (element.Name());
				_target->InsertEndChild (e);
				while (attribute)
				{
					e->SetAttribute (attribute->Name(), attribute->Value());
					attribute = attribute->Next();
				}
				_target = e;
				return true;
			}

			virtual bool VisitExit (const XMLElement & element) override
			{
				_target = const_cast <XMLElement *> (_target->Parent()->ToElement());
				return true;
			}

			virtual bool Visit (const XMLDeclaration & declaration) override
			{
				auto d = declaration.ShallowClone (_newDoc);
				_target->InsertEndChild (d);
				return true;
			}

			virtual bool Visit (const XMLText & txt) override
			{
				auto t = txt.ShallowClone (_newDoc);
				_target->InsertEndChild (t);
				return true;
			}

			virtual bool Visit (const XMLComment & comment) override
			{
				auto c = comment.ShallowClone (_newDoc);
				_target->InsertEndChild (c);
				return true;
			}

		protected:
			XMLElement * _target;
			XMLDocument * _newDoc;
		};	// XMLCopy


		class XMLCopyAndReplace : public XMLCopy
		{
		public:
			XMLCopyAndReplace (XMLElement * target, const std::unordered_map<std::string, std::string> & params, char openDelim, char closeDelim)
				: XMLCopy(target), _params(params), _openDelim(openDelim), _closeDelim(closeDelim) {}

			bool VisitEnter (const XMLElement & element, const XMLAttribute * attribute) override
			{
				if (XMLCopy::VisitEnter (element, attribute))
				{
					auto a = const_cast <XMLAttribute *> (_target->FirstAttribute());
					while (a)
					{
						auto subst = substitute (a->Value());
						if (subst.first)
							a->SetAttribute (subst.second.c_str());
						a = const_cast <XMLAttribute *> (a->Next());
					}
					return true;
				}
				else
					return false;
			}

			virtual bool Visit (const XMLText & txt) override
			{
				if (XMLCopy::Visit (txt))
				{
					auto t = _target->LastChild()->ToText();	// it's the element text 'cos that the last one we added
					if (!t->CData())	// we don't substitute CDATA
					{
						auto subst = substitute (t->Value());
						if (subst.first)
							t->SetValue (subst.second.c_str());
					}
					return true;
				}
				else
					return false;
			}

		private:
			std::pair <bool, std::string> substitute (const std::string & val)
			{
				bool substituted{ false };
				std::string newValue;
				std::string::size_type cursor = 0;
				auto ps = val.find (_openDelim);
				while (ps != std::string::npos)
				{
					newValue += val.substr (cursor, ps - cursor);
					auto pe = val.find (_closeDelim, ps);
					if (pe != std::string::npos)
					{
						cursor = pe + 1;
						auto px = _params.find (val.substr (ps + 1, pe - ps - 1));
						if (px != _params.end())
						{
							newValue += px->second;
						}
						else
							throw XmlException ("no value for parameter " + (val.substr (ps + 1, pe - ps - 1)));
						substituted = true;
						ps = val.find (_openDelim, pe);
					}
					else
					{
						ps = std::string::npos;
						break;
					}
				}
				if (substituted)
					return std::make_pair (true, newValue + val.substr (cursor, ps - cursor));
				else
					return std::make_pair (false, val);
			}

		private:
			const std::unordered_map<std::string, std::string> & _params;
			const char _openDelim;
			const char _closeDelim;

		};	// XMLCopyAndReplace


		inline void xcopy (const XMLElement * source, XMLElement * destinationParent)
		{
			XMLCopy copier (destinationParent);
			source->Accept (&copier);
		}


		inline void xcopy (const XMLElement * source, XMLElement * destinationParent, const std::unordered_map<std::string, std::string> & params, char openDelim = '{', char closeDelim = '}')
		{
			XMLCopyAndReplace copier (destinationParent, params, openDelim, closeDelim);
			source->Accept (&copier);
		}
	}
}
