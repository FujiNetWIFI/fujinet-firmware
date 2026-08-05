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

#include "Document.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

CDocument::CDocument()
{
	mpOutput = NULL;
}

// FujiNet: on ESP, route Gumbo's (large) DOM allocations to PSRAM, falling back
// to internal DRAM, so parsing a big page does not exhaust the ~300 KB of
// internal RAM. The same options must be used for parse and destroy so the
// matching deallocator frees the tree. Non-ESP builds keep the malloc defaults.
namespace {
#ifdef ESP_PLATFORM
void* gq_psram_alloc(void* /*userdata*/, size_t size)
{
	return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT);
}
void gq_psram_free(void* /*userdata*/, void* ptr)
{
	heap_caps_free(ptr);
}
#endif
const GumboOptions* gq_options()
{
	static GumboOptions opts = [] {
		GumboOptions o = kGumboDefaultOptions;
#ifdef ESP_PLATFORM
		o.allocator = gq_psram_alloc;
		o.deallocator = gq_psram_free;
#endif
		return o;
	}();
	return &opts;
}
} // namespace

void CDocument::parse(const std::string& aInput)
{
	reset();
	mpOutput = gumbo_parse_with_options(gq_options(), aInput.c_str(), aInput.length());
}

CDocument::~CDocument()
{
	reset();
}

CSelection CDocument::find(std::string aSelector)
{
	if (mpOutput == NULL)
	{
		// FujiNet: exception-free - an unparsed document yields no matches.
		return CSelection(std::vector<GumboNode*>());
	}

	CSelection sel(mpOutput->root);
	return sel.find(aSelector);
}

void CDocument::reset()
{
	if (mpOutput != NULL)
	{
		gumbo_destroy_output(gq_options(), mpOutput);
		mpOutput = NULL;
	}
}

/* vim: set ts=4 sw=4 sts=4 tw=100 noet: */

