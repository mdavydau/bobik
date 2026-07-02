// Google Calendar schedule source — COPY this file to `calendar_config.h` and
// fill in your OAuth credentials + calendar id. When calendar_config.h exists,
// the firmware enables the calendar-driven schedule (TABBIE_CALENDAR):
//
//   cp calendar_config.example.h calendar_config.h   # then edit, then reflash
//
// calendar_config.h is gitignored so your tokens never get committed.
//
// ── HOW IT WORKS ────────────────────────────────────────────────────────────
// The board polls the "Bobik" calendar over HTTPS (Google Calendar API v3),
// figures out which event covers "now", and drives the matching face. It runs
// as an OVERLAY on top of the built-in ambientWindows[]/timedEvents[] schedule:
//
//   calendar event covers now   -> that face wins (calendar is authoritative)
//   no event / API unreachable  -> fall back to the built-in on-device schedule
//
// So if WiFi or Google is down, Bobik still shows sensible ambient faces.
//
// ── EVENT → FACE CONVENTION ──────────────────────────────────────────────────
// Event TITLE  = animation name (same words tabbie-pub accepts):
//                focus  coffee  sleepy  paused  love  idle  complete  break
//                pomodoro  sweat  mochi_happy  mochi_angry  mochi_love
//                upiir_big_smile  status_alert  startup
// Event DESC   = optional task text (shown under the face).
//
// ── SECURITY ─────────────────────────────────────────────────────────────────
// This refresh token is baked into the firmware image and can be extracted from
// a device. Use a token scoped to CALENDAR ONLY (calendar or calendar.readonly).
// NEVER embed a token that also grants Gmail/Drive/Sheets access.
#pragma once

// OAuth 2.0 installed-app credentials (Google Cloud > APIs & Services).
#define GCAL_CLIENT_ID      "xxxxxxxx.apps.googleusercontent.com"
#define GCAL_CLIENT_SECRET  "your-client-secret"
#define GCAL_REFRESH_TOKEN  "your-calendar-only-refresh-token"
#define GCAL_TOKEN_URI      "https://oauth2.googleapis.com/token"

// The dedicated "Bobik" calendar id (Calendar settings > Integrate calendar).
#define GCAL_CALENDAR_ID    "xxxxxxxx@group.calendar.google.com"

// How often to re-fetch events from Google, in seconds. Access tokens last ~1h
// and are cached; this only controls schedule freshness. 60–300 is reasonable.
#define GCAL_POLL_SECONDS   120
