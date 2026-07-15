# FujiNet Microsoft OneDrive OAuth2 Relay

This tiny Node.js service is the shared relay that makes OneDrive
authorization work for FujiNets behind NAT/dynamic IPs. It mirrors the
[`gdrive-relay`](../gdrive-relay) service, targeting the Microsoft identity
platform and Microsoft Graph.

## How it works

The relay holds the `client_secret` — it never touches the firmware.

```
FujiNet web UI                     relay                       Microsoft
─────────────────                  ──────────────────          ──────────────────
GET /onedrive-auth  ────────────>  (not involved)
 ← {auth_url,state}

browser opens auth_url ──────────────────────────────────>  sign-in + consent
                                                            user approves
                        GET /onedrive-callback?code=C&state=S <────────────────
                         exchanges code for tokens ──────────────────────────>
                         stores {S→tokens}           ← {access_token,refresh_token,...}

GET /onedrive-poll?state=S ────>  GET /onedrive-code?state=S
                          <──────  returns {access_token,refresh_token,...}
 stores tokens locally

(later, token refresh)
 POST /onedrive-refresh ───────>  POST to Microsoft /token
 ← {access_token,refresh_token,expires_in}
```

### Differences from the Google relay

- Microsoft's token endpoint **requires the `scope` parameter** on both the
  authorization-code and refresh_token grants.
- Microsoft **rotates the refresh_token** on every refresh, so
  `/onedrive-refresh` returns the new `refresh_token` and the firmware persists
  it (see `NetworkProtocolONEDRIVE::refresh_access_token`).

## Deployment

```bash
# Required environment variables
export ONEDRIVE_CLIENT_ID="00000000-0000-0000-0000-000000000000"
export ONEDRIVE_CLIENT_SECRET="your-client-secret-value"
# Optional
export ONEDRIVE_TENANT="common"                       # or organizations|consumers|<tenant-id>
export ONEDRIVE_SCOPE="offline_access Files.ReadWrite"

# Stand-alone
PORT=3000 node relay.js

# Behind nginx / Caddy with TLS (recommended for production)
PORT=3001 node relay.js &
# then proxy HTTPS :443 → localhost:3001
```

The service is stateless (in-memory Map, 120 s TTL) — no database required.
For high availability, use sticky sessions or switch to Redis for the store.

## Microsoft Entra (Azure AD) setup (project maintainers only)

Register the relay's app once at https://entra.microsoft.com (App registrations):

1. **New registration** → supported account types: *Accounts in any organizational
   directory and personal Microsoft accounts* (for `common`).
2. Add a **Web** platform redirect URI:
   ```
   https://auth.fujinet.online/onedrive-callback
   ```
3. Under **Certificates & secrets**, create a **client secret** and copy its value.
4. Under **API permissions**, add Microsoft Graph **delegated** permissions:
   `Files.ReadWrite` and `offline_access`.
5. Set `ONEDRIVE_CLIENT_ID` (Application ID) and `ONEDRIVE_CLIENT_SECRET` on the
   relay server, and bake the same **Application ID** into the firmware
   (`ONEDRIVE_CLIENT_ID` in `lib/http/httpService.cpp` and `lib/http/mgHttpService.cpp`).

End users need no credentials — they just click **Authorize with Microsoft** in
the FujiNet web UI.

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/onedrive-callback?code=C&state=S` | Receives Microsoft redirect, exchanges code for tokens |
| `GET` | `/onedrive-code?state=S` | FujiNet polls — returns `{access_token,refresh_token,expires_in}`, `{pending:true}`, or `{expired:true}` |
| `POST` | `/onedrive-refresh` | FujiNet sends `refresh_token=TOKEN`, relay returns `{access_token,refresh_token,expires_in}` |
| `GET` | `/health` | Returns `{"ok":true}` |

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ONEDRIVE_CLIENT_ID` | *(required)* | Entra app Application (client) ID |
| `ONEDRIVE_CLIENT_SECRET` | *(required)* | Entra app client secret value |
| `ONEDRIVE_TENANT` | `common` | Entra tenant segment in the token URL |
| `ONEDRIVE_SCOPE` | `offline_access Files.ReadWrite` | OAuth2 scopes |
| `PORT` | `3000` | TCP port to listen on |
| `TTL_SECONDS` | `120` | Seconds to keep a code before declaring it expired |
