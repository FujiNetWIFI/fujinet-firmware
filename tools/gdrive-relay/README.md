# FujiNet Google Drive OAuth2 Relay

This tiny Node.js service is the shared relay that makes Google Drive
authorization work for FujiNets behind NAT/dynamic IPs.

## How it works

The relay holds the `client_secret` — it never touches the firmware.

```
FujiNet web UI                     relay                       Google
─────────────────                  ──────────────────          ──────────────────
GET /gdrive-auth  ──────────────>  (not involved)
 ← {auth_url,state}

browser opens auth_url ──────────────────────────────────>  OAuth consent page
                                                            user approves
                        GET /gdrive-callback?code=C&state=S <──────────────────
                         exchanges code for tokens ──────────────────────────>
                         stores {S→tokens}           ← {access_token,...}

GET /gdrive-poll?state=S ──────>  GET /gdrive-code?state=S
                          <──────  returns {access_token,refresh_token,...}
 stores tokens locally

(later, token refresh)
 POST /gdrive-refresh ─────────>  POST to Google /token
 ← {access_token,expires_in}
```

## Deployment

```bash
# Required environment variables
export GDRIVE_CLIENT_ID="your-client-id.apps.googleusercontent.com"
export GDRIVE_CLIENT_SECRET="your-client-secret"

# Stand-alone
PORT=3000 node relay.js

# Behind nginx / Caddy with TLS (recommended for production)
PORT=3001 node relay.js &
# then proxy HTTPS :443 → localhost:3001
```

The service is stateless (in-memory Map, 120 s TTL) — no database required.
For high availability, use sticky sessions or switch to Redis for the store.

## Google Console setup (project maintainers only)

Register the relay's credential once at console.cloud.google.com:

1. Create an OAuth2 **Web application** credential
2. Add as **Authorized Redirect URI**:
   ```
   https://auth.fujinet.online/gdrive-callback
   ```
3. Set `GDRIVE_CLIENT_ID` and `GDRIVE_CLIENT_SECRET` env vars on the relay server

End users need no credentials — they just click **Authorize with Google** in the FujiNet web UI.

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/gdrive-callback?code=C&state=S` | Receives Google redirect, exchanges code for tokens |
| `GET` | `/gdrive-code?state=S` | FujiNet polls — returns `{access_token,refresh_token,expires_in}`, `{pending:true}`, or `{expired:true}` |
| `POST` | `/gdrive-refresh` | FujiNet sends `refresh_token=TOKEN`, relay returns `{access_token,expires_in}` |
| `GET` | `/health` | Returns `{"ok":true}` |

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | `3000` | TCP port to listen on |
| `TTL_SECONDS` | `60` | Seconds to keep a code before declaring it expired |
