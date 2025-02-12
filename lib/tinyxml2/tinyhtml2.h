//
// Author: xuwf
// Contributor: idolpx
// History:
//     2013-10-14 create.
//     2021-05-07 updated to tinyxml2 (https://github.com/idolpx/tinyhtml2)
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any
// damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any
// purpose, including commercial applications, and to alter it and
// redistribute it freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must
// not claim that you wrote the original software. If you use this
// software in a product, an acknowledgment in the product documentation
// would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and
// must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
// distribution.
//

#ifndef TINYHTML2_INCLUDED
#define TINYHTML2_INCLUDED

const int TIHTML_MAJOR_VERSION = 0;
const int TIHTML_MINOR_VERSION = 2;
const int TIHTML_PATCH_VERSION = 0;

namespace tinyhtml2
{

class HTMLResult;

class HTMLElement {
public:
    HTMLElement( void* internal );
    HTMLElement( HTMLElement* element );
    const char* Attribute( const char* name ) const;
    const char* GetText() const;

    /*
     * Query
     * -Support # and . same as jQuery;
     *  like:
     *  "album" that search "<album..." (Note: direct child only)
     *  "#album" that search "id=album"
     *  ".album" that search "class=album"
     *
     * -Support Multi-level use '/'
     *  like:
     *  "album/song" that search "<album ...><song>...</song></album>"
     */
    HTMLResult* Query( const char* query );

    void* internal;
};

class HTMLResult {
public:
    HTMLResult();
    virtual ~HTMLResult();

    int Count();
    HTMLElement* Element(int index);

    bool Append( HTMLElement* element );
    bool Append( HTMLResult* result );
    void Clear();
private:
    HTMLElement** elements;
    int capacity;
    int count;
};

class HTMLDocument {
public:
	HTMLDocument();
    virtual ~HTMLDocument();

    bool ParseFile( const char * file );
    bool ParseData( const char * data );

    /*
     * Query
     * -Support # and . same as jQuery;
     *  like:
     *  "album" that search "<album..." (Note: direct child only)
     *  "#album" that search "id=album"
     *  ".album" that search "class=album"
     *
     * -Support Multi-level use '/'
     *  like:
     *  "album/song" that search "<album ...><song>...</song></album>"
     */

    HTMLResult* Query( const char* query );
private:
    void* internal;
};

} // tinyhtml2


#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

#endif // TINYHTML2_INCLUDED