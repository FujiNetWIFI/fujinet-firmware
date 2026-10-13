#ifndef AFP_STATUS_H
#define AFP_STATUS_H

#include "status_error_codes.h"

// 0 or a negative errno from lib/afp, as the status an N: device reports.
nDevStatus_t afp_errno_to_status(int error);

#endif /* AFP_STATUS_H */
