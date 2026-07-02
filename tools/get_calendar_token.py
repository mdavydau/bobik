#!/usr/bin/env python3
"""Mint a CALENDAR-ONLY OAuth refresh token for Bobik's firmware.

The board reads the Bobik calendar over HTTPS and must carry a refresh token in
its firmware image. That token must be scoped to calendar ONLY — never reuse a
token that also grants Gmail/Drive access.

Run this on a machine with a browser (e.g. your Mac):

    pip install google-auth-oauthlib
    python3 tools/get_calendar_token.py

It opens a browser, you log in as daff.nik@gmail.com and consent to a
read-only calendar scope, then it prints the three values to paste into
firmware/src/calendar_config.h:  GCAL_CLIENT_ID / GCAL_CLIENT_SECRET /
GCAL_REFRESH_TOKEN.
"""
import os
import sys

from google_auth_oauthlib.flow import InstalledAppFlow

# Read-only is all the board needs (writes happen from laptop-side scripts).
SCOPES = ["https://www.googleapis.com/auth/calendar.readonly"]

CLIENT_SECRET = os.environ.get(
    "GCAL_CLIENT_SECRET_FILE",
    os.path.join(os.path.dirname(__file__), "..", ".gcal_client_secret.json"),
)


def main() -> int:
    if not os.path.exists(CLIENT_SECRET):
        print(f"ERROR: client secret not found at {CLIENT_SECRET}", file=sys.stderr)
        print("Set GCAL_CLIENT_SECRET_FILE or place .gcal_client_secret.json at repo root.", file=sys.stderr)
        return 1

    flow = InstalledAppFlow.from_client_secrets_file(CLIENT_SECRET, SCOPES)
    # Loopback flow: opens a browser and captures the code on localhost.
    creds = flow.run_local_server(port=0, prompt="consent")

    print("\n--- paste into firmware/src/calendar_config.h ---")
    print(f'#define GCAL_CLIENT_ID      "{creds.client_id}"')
    print(f'#define GCAL_CLIENT_SECRET  "{creds.client_secret}"')
    print(f'#define GCAL_REFRESH_TOKEN  "{creds.refresh_token}"')
    print("-------------------------------------------------")
    if not creds.refresh_token:
        print("\nWARNING: no refresh_token returned. Revoke prior grant at", file=sys.stderr)
        print("https://myaccount.google.com/permissions and re-run.", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
