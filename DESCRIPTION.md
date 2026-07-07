Pixel8 is a feature-rich watchface for Pebble Time 2. It shows the time and date, phone and watch battery levels, step count with heart rate, sleep duration, weather forecast, and upcoming calendar events — all on a customisable background.

Three major panels — Clock, Info, and Calendar — can be freely reordered by dragging in the settings page, or hidden entirely. The bottom panel is always anchored to the screen edge. The clock panel can be hidden for a minimal data-only layout.

The Info panel has four sortable, individually toggleable rows:

Battery — phone and watch bars with colour-coded gradients and a lightning bolt icon when your phone is charging.
Steps — today's steps, distance walked, and heart rate with a pixel heart icon when a reading is available. Set a step goal in settings; the row turns green when you reach it.
Weather — up to 4 forecast columns (current, a few hours ahead, tomorrow, day+2) with weather icons. Provided by Open-Meteo, no API key needed.
Sleep — last night's sleep shown as hours and minutes.

The order of rows can be changed by dragging in the settings page.

The Calendar panel shows your next 1 to 5 events from any iCal/ICS feed (Google Calendar, Nextcloud, Apple Calendar, etc.). Events also appear as Timeline pins with an Open Agenda action button. The "Today" and "Tomorrow" labels can be customised to anything you like (NOW, TMR, Idag, ...).

Style it your way: each panel's 3D card background can be toggled on or off (including a new card behind the clock), the text size of the Info and Calendar panels is selectable (14/18/24 px), the padding between panel borders and content is adjustable per panel, and the date can show the ISO week number (v.28).

The background can be a solid colour or a custom PNG loaded from a URL. A .list file URL rotates through a different image each day.

https://example.com/file.list with the following content
https://example.com/image1.png
https://example.com/image2.png

All panels and rows can be toggled on or off individually. Colours use the Pebble 64-colour palette.

How to convert an image for use as background:
convert image.png -resize 200x228! -colors 64 -type Palette new_image.png

This watchface is vibe coded together with Claude for my own personal liking.
