/**
 * FujiNet Microsoft OneDrive OAuth2 Relay Service
 *
 * Deploy this at https://auth.fujinet.online  (or any fixed HTTPS host).
 * The FujiNet project registers ONE redirect URI in its Microsoft Entra
 * (Azure AD) app registration (Web platform):
 *
 *   https://auth.fujinet.online/onedrive-callback
 *
 * The client_id and client_secret live here on the server — never in firmware.
 *
 * Flow:
 *  1. FujiNet calls GET /onedrive-auth → gets Microsoft auth URL with state=X
 *  2. Browser opens that URL, user signs in / consents
 *  3. Microsoft redirects to GET /onedrive-callback?code=CODE&state=X  (this service)
 *  4. This service exchanges the code for tokens using the stored client_secret
 *  5. FujiNet polls GET /onedrive-code?state=X and receives the finished tokens
 *  6. For future refreshes, FujiNet POSTs to /onedrive-refresh
 *
 * Note vs Google: the Microsoft token endpoint REQUIRES the `scope` parameter on
 * both the authorization-code and refresh_token grants, and rotates the
 * refresh_token on each refresh — so /onedrive-refresh returns the new
 * refresh_token as well, which FujiNet persists.
 *
 * Required env vars:
 *   ONEDRIVE_CLIENT_ID      – Application (client) ID from the Entra app
 *   ONEDRIVE_CLIENT_SECRET  – client secret value from the Entra app
 *
 * Optional env vars:
 *   ONEDRIVE_TENANT   (default "common")   – Entra tenant: common|organizations|consumers|<tenant-id>
 *   ONEDRIVE_SCOPE    (default "offline_access Files.ReadWrite")
 *   PORT              (default 3000)
 *   TTL_SECONDS       (default 120)
 *
 * Start:  node relay.js
 */

'use strict';

const http  = require('http');
const https = require('https');
const url   = require('url');

const PORT           = parseInt(process.env.PORT        || '3000',  10);
const TTL_SECONDS    = parseInt(process.env.TTL_SECONDS || '120',   10);
const CLIENT_ID      = process.env.ONEDRIVE_CLIENT_ID;
const CLIENT_SECRET  = process.env.ONEDRIVE_CLIENT_SECRET;
const TENANT         = process.env.ONEDRIVE_TENANT || 'common';
const SCOPE          = process.env.ONEDRIVE_SCOPE  || 'offline_access Files.ReadWrite';
const REDIRECT_URI   = 'https://auth.fujinet.online/onedrive-callback';
const TOKEN_URL      = `https://login.microsoftonline.com/${TENANT}/oauth2/v2.0/token`;

if (!CLIENT_ID || !CLIENT_SECRET) {
    console.error('FATAL: ONEDRIVE_CLIENT_ID and ONEDRIVE_CLIENT_SECRET must be set');
    process.exit(1);
}

// In-memory store: state → { tokens | pending, expires }
// tokens shape: { access_token, refresh_token, expires_in, error }
const store = new Map();

setInterval(() => {
    const now = Date.now();
    for (const [k, v] of store) {
        if (v.expires < now) store.delete(k);
    }
}, 30_000);

// ── helpers ───────────────────────────────────────────────────────────────────

function jsonResponse(res, statusCode, obj) {
    const body = JSON.stringify(obj);
    res.writeHead(statusCode, {
        'Content-Type':  'application/json',
        'Cache-Control': 'no-store',
        'Access-Control-Allow-Origin': '*',
    });
    res.end(body);
}

function htmlResponse(res, statusCode, html) {
    res.writeHead(statusCode, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(html);
}

function escHtml(s) {
    return String(s)
        .replace(/&/g,  '&amp;')
        .replace(/</g,  '&lt;')
        .replace(/>/g,  '&gt;')
        .replace(/"/g,  '&quot;');
}

function formEncode(obj) {
    return Object.entries(obj)
        .map(([k, v]) => encodeURIComponent(k) + '=' + encodeURIComponent(v))
        .join('&');
}

// Synchronous-style HTTPS POST returning { status, body }.
function httpsPost(urlStr, formBody) {
    return new Promise((resolve, reject) => {
        const u   = new URL(urlStr);
        const buf = Buffer.from(formBody, 'utf8');
        const req = https.request({
            hostname: u.hostname,
            path:     u.pathname,
            method:   'POST',
            headers: {
                'Content-Type':   'application/x-www-form-urlencoded',
                'Content-Length': buf.length,
            },
        }, res => {
            let data = '';
            res.on('data', d => { data += d; });
            res.on('end',  () => resolve({ status: res.statusCode, body: data }));
        });
        req.on('error', reject);
        req.write(buf);
        req.end();
    });
}

// ── request handler ───────────────────────────────────────────────────────────

const server = http.createServer(async (req, res) => {
    const parsed = url.parse(req.url, true);
    const path   = parsed.pathname;
    const query  = parsed.query;

    // ── GET /onedrive-callback?code=CODE&state=STATE ──────────────────────────
    // Microsoft redirects here after user consent.
    // We exchange the code for tokens immediately and store the result.
    if (path === '/onedrive-callback' && req.method === 'GET') {
        const code  = query.code;
        const state = query.state;
        const error = query.error;

        if (error) {
            return htmlResponse(res, 400,
                `<h2>Authorization denied</h2><p>Microsoft returned: ${escHtml(error)}</p>`);
        }
        if (!code || !state) {
            return htmlResponse(res, 400, '<h2>Bad request</h2><p>Missing code or state.</p>');
        }

        // Show success page immediately — token exchange happens in the background.
        htmlResponse(res, 200,
            `<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>FujiNet — Authorized</title></head>
<body style="font-family:sans-serif;text-align:center;padding:4em">
<h2>&#10003; Authorization complete</h2>
<p>You can close this tab. FujiNet will pick up the credentials automatically.</p>
</body></html>`);

        // Mark as pending while the exchange runs.
        const expires = Date.now() + TTL_SECONDS * 1000;
        store.set(state, { pending: true, expires });

        try {
            const { status, body } = await httpsPost(TOKEN_URL, formEncode({
                grant_type:    'authorization_code',
                code,
                redirect_uri:  REDIRECT_URI,
                client_id:     CLIENT_ID,
                client_secret: CLIENT_SECRET,
                scope:         SCOPE,
            }));

            const j = JSON.parse(body);
            if (status >= 200 && status < 300 && j.access_token) {
                store.set(state, {
                    access_token:  j.access_token,
                    refresh_token: j.refresh_token || '',
                    expires_in:    j.expires_in    || 3600,
                    expires,
                });
                console.log(`onedrive-callback: tokens stored for state=${state}`);
            } else {
                store.set(state, { error: j.error || 'token_exchange_failed', expires });
                console.error(`onedrive-callback: exchange failed HTTP ${status}: ${body}`);
            }
        } catch (e) {
            store.set(state, { error: 'relay_error', expires });
            console.error('onedrive-callback: exchange exception:', e.message);
        }
        return;
    }

    // ── GET /onedrive-code?state=STATE ────────────────────────────────────────
    // FujiNet polls here to receive the tokens once the exchange completes.
    if (path === '/onedrive-code' && req.method === 'GET') {
        const state = query.state;
        if (!state) return jsonResponse(res, 400, { error: 'missing state' });

        const entry = store.get(state);
        if (!entry) return jsonResponse(res, 200, { pending: true });

        if (entry.expires < Date.now()) {
            store.delete(state);
            return jsonResponse(res, 200, { expired: true });
        }
        if (entry.pending) return jsonResponse(res, 200, { pending: true });
        if (entry.error)   return jsonResponse(res, 200, { error: entry.error });

        // Deliver tokens once; delete to prevent replay.
        store.delete(state);
        return jsonResponse(res, 200, {
            access_token:  entry.access_token,
            refresh_token: entry.refresh_token,
            expires_in:    entry.expires_in,
        });
    }

    // ── POST /onedrive-refresh ────────────────────────────────────────────────
    // FujiNet sends its refresh_token here; relay exchanges it with Microsoft.
    // Body: application/x-www-form-urlencoded  refresh_token=TOKEN
    // Microsoft rotates the refresh_token, so we return the new one too.
    if (path === '/onedrive-refresh' && req.method === 'POST') {
        let body = '';
        req.on('data', d => { body += d; });
        req.on('end', async () => {
            const params = new URLSearchParams(body);
            const refresh_token = params.get('refresh_token');
            if (!refresh_token) return jsonResponse(res, 400, { error: 'missing refresh_token' });

            try {
                const { status, body: rb } = await httpsPost(TOKEN_URL, formEncode({
                    grant_type:    'refresh_token',
                    refresh_token,
                    client_id:     CLIENT_ID,
                    client_secret: CLIENT_SECRET,
                    scope:         SCOPE,
                }));

                const j = JSON.parse(rb);
                if (status >= 200 && status < 300 && j.access_token) {
                    return jsonResponse(res, 200, {
                        access_token:  j.access_token,
                        refresh_token: j.refresh_token || '',
                        expires_in:    j.expires_in || 3600,
                    });
                }
                return jsonResponse(res, 400, { error: j.error || 'refresh_failed' });
            } catch (e) {
                return jsonResponse(res, 500, { error: 'relay_error' });
            }
        });
        return;
    }

    // ── Health check ──────────────────────────────────────────────────────────
    if (path === '/health' && req.method === 'GET') {
        return jsonResponse(res, 200, { ok: true });
    }

    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not found');
});

server.listen(PORT, () => {
    console.log(`onedrive-relay listening on port ${PORT} (TTL ${TTL_SECONDS}s, tenant ${TENANT})`);
});
