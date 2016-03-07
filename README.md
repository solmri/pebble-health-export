# Pebble Health Export

This is a simple application for Pebble Watch that extract all the raw
Pebble Health data gathered by the watch and POST it to a user-configured
HTTP endpoint.

Note that this involves streaming a lot of data from the watch to the phone
through the Bluetooth link, so it consumes a lot of battery. Hopefully this
will be offset by a high transfer rate, so that the high battery usage only
lasts for a short time.

Preliminary tests suggest that when running at full capacity, the app would
drain empty a fully-charged Pebble Time Round battery in about 2h30, but
transferring a full day worth of Pebble Health data would only take about
3 minutes. That means roughly 2% of battery per day, for the Pebble model
with the smallest battery.
