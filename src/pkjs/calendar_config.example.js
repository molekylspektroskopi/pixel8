/**
 * Calendar configuration for the Pixel8 agenda panel.
 *
 * Copy this file to `calendar_config.js` (which is git-ignored) and fill in
 * your calendar's iCal/ICS URL. The companion JS fetches this URL, parses the
 * next 3 upcoming events, and sends them to the watch.
 *
 *   CALENDAR_URL : direct .ics URL (CalDAV export or a read-only share link)
 *   CALENDAR_USER / CALENDAR_PASS : only if the URL needs HTTP Basic auth.
 *                                   Leave empty for a public/secret share link.
 *
 * Examples:
 *   Nextcloud private export : https://host/remote.php/dav/calendars/USER/CAL/?export   (+ user/pass)
 *   Nextcloud secret share   : https://host/remote.php/dav/public-calendars/TOKEN/?export
 *   SOGo / generic ICS       : https://host/SOGo/dav/USER/Calendar/personal/events.ics (+ user/pass)
 */
module.exports = {
  CALENDAR_URL: '',
  CALENDAR_USER: '',
  CALENDAR_PASS: ''
  // Optional: override the timeline pin endpoint (defaults to Rebble's):
  // , TIMELINE_API: 'https://timeline-api.rebble.io'
};
