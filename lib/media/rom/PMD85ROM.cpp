#include "PMD85ROM.h"

error_is_true PMD85ROM::attach()
{
    if (!matchExtension(_path, extensions).has_value())
        RETURN_ERROR_AS_TRUE();

    if (readSize().is_error() || _size == 0 || _size > PAGE_SIZE * MAX_PAGES)
        RETURN_ERROR_AS_TRUE();

    RETURN_SUCCESS_AS_FALSE();
}
