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

#include "tinyhtml2.h"
#include <string.h>
#include <stdlib.h>
#include "tinyxml2.h"

#define MIN_CAPACITY (64)

#define TIXML_FOR_EACH(__rootElement, __nodeElement, __name)\
    for (__nodeElement = __rootElement->FirstChildElement(__name);__nodeElement;__nodeElement = __nodeElement->NextSiblingElement(__name))

#define TIXML_FOR_EACH_ALL(__rootElement, __nodeElement)\
    for (__nodeElement = __rootElement->FirstChildElement();__nodeElement;__nodeElement = __nodeElement->NextSiblingElement())

#ifndef MIN
#define MIN(a, b) (((a) > (b)) ? (b) : (a))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif


char* strtrim(char* str) {
    if (!str) return str;

    int len = strlen(str);
    int loff = 0, roff = len-1;

    /* Left offset */
    while (loff<len && str[loff]==' ') loff++;
    loff = MIN(loff, roff);

    /* Right offset*/
    while (roff>=0 && str[roff]==' ') roff--;
    roff = MAX(roff, loff);

    len = roff-loff+1;
    strncpy(str, str+loff, roff-loff+1);
    str[len] = '\0';
    return str;
}

namespace tinyhtml2
{

////////////////////////////////////////////////////////////////////////////////
HTMLResult::HTMLResult() {
    elements = (HTMLElement** )calloc(sizeof(HTMLElement* )*MIN_CAPACITY, 1);
    if (elements) capacity = MIN_CAPACITY;
    count = 0;
}

HTMLResult::~HTMLResult() {
    if (elements) {

        while (count > 0)  delete(elements[--count]);

        free(elements);
        elements = NULL;
        capacity = 0;
        count = 0;
    }
}

bool HTMLResult::Append( HTMLElement* element ) {
    if (capacity <= 0 || !element) return false;

    if (count >= capacity) {
        elements = (HTMLElement** )realloc(elements, capacity*2*sizeof(HTMLElement* ));
        if (elements) capacity = capacity*2;
    }

    elements[count++] = element;
    return true;
}

bool HTMLResult::Append( HTMLResult* result ) {
    int count = result->Count();
    int i = 0;
    while (i < count) {
        Append(result->Element(i++));
    }
    return true;
}

void HTMLResult::Clear() {
    if (elements) {
        while (count > 0) delete(elements[--count]);

        memset(elements, 0, sizeof(HTMLElement* )*capacity);
        count = 0;
    }
}

int HTMLResult::Count() {
    return count;
}

HTMLElement* HTMLResult::Element(int index) {
    return (index >= 0 && index < count)?elements[index]:NULL;
}

////////////////////////////////////////////////////////////////////////////////
HTMLElement::HTMLElement( void* internal ) {
    this->internal = internal;
}

HTMLElement::HTMLElement( HTMLElement* element ) {
    this->internal = element->internal;
}

const char* HTMLElement::Attribute( const char* name ) const {
    tinyxml2::XMLElement* element = (tinyxml2::XMLElement* )internal;
    return element ? element->Attribute(name):NULL;
}

const char* HTMLElement::GetText() const {
    tinyxml2::XMLElement* element = (tinyxml2::XMLElement* )internal;
    return element ? element->GetText():NULL;
}

HTMLResult* FindByTag( HTMLElement* htmlElement, const char* name,  bool deep ) {
    tinyxml2::XMLElement* element = (tinyxml2::XMLElement* )htmlElement->internal;
    tinyxml2::XMLElement* node = NULL;
    HTMLResult* result = new HTMLResult();

    int arrayCount = 0;
    int arrayCapacity = 0;
    tinyxml2::XMLElement** array = (tinyxml2::XMLElement** )calloc(MIN_CAPACITY, sizeof(tinyxml2::XMLElement* ));
    if (array) arrayCapacity = MIN_CAPACITY;
    int i = 0;

    /* 广度优先遍历 */
    array[i] = element;
    do {
        element = array[i];
        TIXML_FOR_EACH(element, node, name) {
            result->Append(new HTMLElement(node));

            //add to array
            if (deep && array) {
                if (arrayCount >= arrayCapacity) {
                    array = (tinyxml2::XMLElement** )realloc(array, sizeof(tinyxml2::XMLElement* )*arrayCapacity*2);
                    if (array) arrayCapacity *= 2;
                }

                if (array) {
                    array[arrayCount++] = node;
                }
            }
        }

        i++;
    } while(deep && element && i < arrayCount);

    if (array) free(array);

    return result;
}

HTMLResult* FindByAttribute( HTMLElement* htmlElement, const char* name, const char* value, bool deep ) {
    tinyxml2::XMLElement* element = (tinyxml2::XMLElement* )htmlElement->internal;
    tinyxml2::XMLElement* node = NULL;
    HTMLResult* result = new HTMLResult();

    int arrayCount = 0;
    int arrayCapacity = 0;
    tinyxml2::XMLElement** array = (tinyxml2::XMLElement** )calloc(MIN_CAPACITY, sizeof(tinyxml2::XMLElement* ));
    if (array) arrayCapacity = MIN_CAPACITY;
    int i = 0;

    /* 广度优先遍历 */
    array[i] = element;
    do {
        element = array[i];
        TIXML_FOR_EACH_ALL(element, node) {
            const char* aValue = node->Attribute(name);

            //add to array
            if (deep && array) {
                if (arrayCount >= arrayCapacity) {
                    array = (tinyxml2::XMLElement** )realloc(array, sizeof(tinyxml2::XMLElement* )*arrayCapacity*2);
                    if (array) arrayCapacity *= 2;
                }

                if (array) {
                    array[arrayCount++] = node;
                }
            }

            if (aValue && 0 == strcasecmp(aValue, value)) {
                result->Append(new HTMLElement(node));
            }
        }

        i++;
    } while(deep && element && i < arrayCount);

    if (array) free(array);

    return result;
}


HTMLResult* HTMLElement::Query( const char* query ) {
    if (!query) return NULL;

    HTMLResult* results[2] = { new HTMLResult(), new HTMLResult()};
    char aQuery[512] = {0};
    strncpy(aQuery, query, sizeof(aQuery));
    const char* token = strtok(aQuery, "/");

    int index = 0;
    int aIndex = 0;
    results[index]->Append(new HTMLElement(this));
    while (token) {
        char name[256] = {0};
        strncpy(name, token, sizeof(name));
        token = strtok(NULL, "/");
        aIndex = index ? 0 : 1;

        results[aIndex]->Clear();

        /* trim */
        strtrim(name);
        if (name[0]) {
            int count = results[index]->Count();
            for (int i = 0; i < count; i++) {
                HTMLElement* element = results[index]->Element(i);
                HTMLResult* aResult = NULL;
                switch(name[0]) {
                    case '#':
                        aResult = FindByAttribute(element, "id", &name[1], true);
                        break;
                    case '.':
                        aResult = FindByAttribute(element, "class", &name[1], true);
                        break;
                    default:
                        aResult = FindByTag(element, name, false);
                        break;
                }
                results[aIndex]->Append(aResult);
            }
        }

        index = aIndex;
    }

    aIndex = index ? 0 : 1;
    delete(results[aIndex]);
    return results[index];
}

////////////////////////////////////////////////////////////////////////////////
HTMLDocument::HTMLDocument() {
    tinyxml2::XMLDocument* doc = new tinyxml2::XMLDocument();
    internal = doc;
}

bool HTMLDocument::ParseFile( const char * file ) {
    tinyxml2::XMLDocument* doc = (tinyxml2::XMLDocument* )internal;
    return doc ? !!doc->LoadFile(file):false;
}

bool HTMLDocument::ParseData( const char * data ) {
    if (data){
        tinyxml2::XMLDocument* doc = (tinyxml2::XMLDocument* )internal;
        doc->Parse(data);
        if (doc->Error()) return false;
        return true;
    }

    return false;
}

HTMLDocument::~HTMLDocument() {
    if (internal) delete((tinyxml2::XMLDocument* )internal);
}

HTMLResult* HTMLDocument::Query( const char* query ) {
    tinyxml2::XMLDocument* doc = (tinyxml2::XMLDocument* )internal;
    tinyxml2::XMLElement* rootElement = doc ? doc ->RootElement():NULL;
    HTMLElement* element = new HTMLElement(rootElement);
    HTMLResult* result = element->Query(query);
    delete(element);
    return result;
}

} // tinyhtml2
