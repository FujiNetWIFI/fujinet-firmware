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

#include "Object.h"

CObject::CObject()
{
	mReferences = 1;
}

CObject::~CObject()
{
	// FujiNet: exception-free build - was a refcount sanity throw; now a no-op.
}

void CObject::retain()
{
	mReferences++;
}

void CObject::release()
{
	// FujiNet: exception-free build - removed a refcount sanity throw
	// (mReferences is unsigned, so the check was dead code anyway).
	if (mReferences == 1)
	{
		delete this;
	}
	else
	{
		mReferences--;
	}
}

unsigned int CObject::references()
{
	return mReferences;
}

/* vim: set ts=4 sw=4 sts=4 tw=100 noet: */

