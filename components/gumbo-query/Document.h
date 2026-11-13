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

#ifndef DOCUMENT_H_
#define DOCUMENT_H_

#include <gumbo.h>
#include <string>
#include "Selection.h"

class CDocument: public CObject
{
	public:

		CDocument();

		void parse(const std::string& aInput);

		virtual ~CDocument();

		CSelection find(std::string aSelector);

		// FujiNet: expose the parsed tree's root so callers that need to walk it
		// directly (e.g. FNPretty's layout engine) don't have to reparse it.
		GumboNode* root() const { return mpOutput == NULL ? NULL : mpOutput->root; }

	private:

		void reset();

	private:

		GumboOutput* mpOutput;
};

#endif /* DOCUMENT_H_ */

/* vim: set ts=4 sw=4 sts=4 tw=100 noet: */
