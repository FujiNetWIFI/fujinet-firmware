/* Shared markup for the password-protected area of the FujiNet web server.

Only one of httpService.cpp (ESP32) / mgHttpService.cpp (FujiNet-PC) is
compiled for a given build, so plain file-scope arrays are fine here.
*/
#ifndef PRIVATEPAGE_H
#define PRIVATEPAGE_H

// Stub content served once Basic Auth succeeds.
static const char PRIVATE_PAGE_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>FujiNet - Private</title></head><body>"
    "<h1>Private</h1>"
    "<p>You are authenticated with the FujiNet device password.</p>"
    "<p>This page is a placeholder for password-protected functionality.</p>"
    "<p><a href=\"/\">Back to the FujiNet web UI</a></p>"
    "</body></html>";

// Sent instead of a Basic Auth challenge when no device password exists, so
// the page fails closed rather than becoming publicly readable.
static const char PRIVATE_NO_PASSWORD_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>FujiNet - Private</title></head><body>"
    "<h1>No device password set</h1>"
    "<p>This page is protected by the FujiNet device password, and no password "
    "has been set yet.</p>"
    "<p>Set one under <strong>Security</strong> in the "
    "<a href=\"/\">FujiNet web UI</a>, then reload this page.</p>"
    "</body></html>";

static const char PRIVATE_UNAUTHORIZED_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<title>401 Unauthorized</title></head><body>"
    "<h1>401 Unauthorized</h1>"
    "<p>The FujiNet device password is required to view this page.</p>"
    "</body></html>";

#define PRIVATE_AUTH_REALM "Basic realm=\"FujiNet\", charset=\"UTF-8\""

#endif // PRIVATEPAGE_H
