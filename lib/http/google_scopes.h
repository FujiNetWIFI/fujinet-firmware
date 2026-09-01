/**
 * Scopes requested by the single shared Google grant.
 *
 * One authorization covers Drive, Gmail and Calendar; the tokens live in
 * fnConfig [GoogleDrive] and GDRIVE, GMAIL and GCAL all refresh against them.
 * The list is defined once here because it is used by both the ESP32 web service
 * (httpService.cpp) and the FujiNet-PC one (mgHttpService.cpp), and a scope added
 * to only one of them would leave half the builds unable to reach the API.
 *
 * Adding a scope invalidates nothing, but existing grants will not carry it -
 * users must re-authorize before the new API answers, and until they do the
 * affected adapter sees HTTP 403 ACCESS_TOKEN_SCOPE_INSUFFICIENT.
 */

#ifndef GOOGLE_SCOPES_H
#define GOOGLE_SCOPES_H

#define GOOGLE_OAUTH_SCOPES                              \
    "https://www.googleapis.com/auth/drive"              \
    " https://www.googleapis.com/auth/gmail.readonly"    \
    " https://www.googleapis.com/auth/gmail.send"        \
    " https://www.googleapis.com/auth/calendar.readonly" \
    " https://www.googleapis.com/auth/calendar.events"

#endif /* GOOGLE_SCOPES_H */
