/*
 * Copyright (c) 2016, Natacha Porté
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <inttypes.h>
#include <pebble.h>

#include "dict_tools.h"
#include "progress_layer.h"

#define MSG_KEY_LAST_SENT	110
#define MSG_KEY_MODAL_MESSAGE	120
#define MSG_KEY_UPLOAD_DONE	130
#define MSG_KEY_UPLOAD_START	140
#define MSG_KEY_DATA_KEY	210
#define MSG_KEY_DATA_LINE	220

static Window *window;
static TextLayer *modal_text_layer;
static char modal_text[256];
static HealthMinuteData minute_data[1440];
static HealthActivityMask minute_activity[1440];
static uint16_t minute_data_size = 0;
static uint16_t minute_index = 0;
static time_t minute_first = 0, minute_last = 0;
static bool modal_displayed = false;
static bool display_dirty = false;
static char global_buffer[1024];

static struct widget {
	char		label[64];
	char		rate[32];
	TextLayer	*label_layer;
	TextLayer	*rate_layer;
	ProgressLayer	*progress_layer;
	uint32_t	first_key;
	uint32_t	current_key;
	time_t		start_time;
} phone, web;

static void
set_modal_mode(bool is_modal) {
	if (is_modal == modal_displayed) return;
	layer_set_hidden(text_layer_get_layer(modal_text_layer), !is_modal);
	layer_set_hidden(text_layer_get_layer(phone.label_layer), is_modal);
	layer_set_hidden(text_layer_get_layer(phone.rate_layer), is_modal);
	layer_set_hidden(phone.progress_layer, is_modal);
	layer_set_hidden(text_layer_get_layer(web.label_layer), is_modal);
	layer_set_hidden(text_layer_get_layer(web.rate_layer), is_modal);
	layer_set_hidden(web.progress_layer, is_modal);
	modal_displayed = is_modal;
}

static void
update_half_progress(struct widget *widget) {
	if (!widget || !widget->current_key) return;

	time_t t, now = time(0);
	struct tm *tm;
	int32_t last_key = (now + 59) / 60;
	int32_t key_span = last_key - widget->first_key;
	int32_t keys_done = widget->current_key - widget->first_key + 1;
	int32_t running_time = widget->start_time
	    ? now - widget->start_time : 0;

	progress_layer_set_progress(widget->progress_layer,
	    (keys_done * 100 + key_span / 2) / key_span);

	t = widget->current_key * 60;
	tm = localtime(&t);
	strftime(widget->label, sizeof widget->label, "%F %H:%M", tm);

	if (running_time > 0) {
		int32_t i = ((widget->current_key - widget->first_key) * 60
		    + running_time / 2) / running_time;
		snprintf(widget->rate, sizeof widget->rate,
		    "%" PRIi32 " /min", i);
	}
}

static void
update_progress(void) {
	update_half_progress(&phone);
	update_half_progress(&web);
	display_dirty = false;
}

#define PROGRESS_HEIGHT 10
#define PROGRESS_MARGIN 8
#define LABEL_MARGIN (PROGRESS_HEIGHT + PROGRESS_MARGIN)
#define LABEL_HEIGHT 22

static void
window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	modal_text_layer = text_layer_create(GRect(0, bounds.size.h / 3,
	    bounds.size.w, bounds.size.h / 3));
	text_layer_set_text(modal_text_layer, modal_text);
	text_layer_set_text_alignment(modal_text_layer, GTextAlignmentCenter);
	text_layer_set_font(modal_text_layer,
	    fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
	layer_add_child(window_layer, text_layer_get_layer(modal_text_layer));

	phone.rate_layer = text_layer_create(GRect(0,
	    bounds.size.h / 2 - LABEL_MARGIN - 2 * LABEL_HEIGHT,
	    bounds.size.w,
	    LABEL_HEIGHT));
	text_layer_set_text(phone.rate_layer, phone.rate);
	text_layer_set_text_alignment(phone.rate_layer, GTextAlignmentCenter);
	text_layer_set_font(phone.rate_layer,
	    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
	layer_add_child(window_layer, text_layer_get_layer(phone.rate_layer));

	phone.label_layer = text_layer_create(GRect(0,
	    bounds.size.h / 2 - LABEL_HEIGHT - LABEL_MARGIN,
	    bounds.size.w,
	    LABEL_HEIGHT));
	text_layer_set_text(phone.label_layer, phone.label);
	text_layer_set_text_alignment(phone.label_layer, GTextAlignmentCenter);
	text_layer_set_font(phone.label_layer,
	    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
	layer_add_child(window_layer, text_layer_get_layer(phone.label_layer));

	phone.progress_layer = progress_layer_create(GRect(bounds.size.w / 4,
	    bounds.size.h / 2 - PROGRESS_HEIGHT - PROGRESS_MARGIN / 2,
	    bounds.size.w / 2,
	    PROGRESS_HEIGHT));
	progress_layer_set_progress(phone.progress_layer, 0);
	progress_layer_set_corner_radius(phone.progress_layer, 3);
	progress_layer_set_foreground_color(phone.progress_layer, GColorBlack);
	progress_layer_set_background_color(phone.progress_layer,
	    GColorLightGray);
	layer_add_child(window_layer, phone.progress_layer);

	web.progress_layer = progress_layer_create(GRect(bounds.size.w / 4,
	    bounds.size.h / 2 + PROGRESS_MARGIN / 2,
	    bounds.size.w / 2,
	    PROGRESS_HEIGHT));
	progress_layer_set_progress(web.progress_layer, 0);
	progress_layer_set_corner_radius(web.progress_layer, 3);
	progress_layer_set_foreground_color(web.progress_layer, GColorBlack);
	progress_layer_set_background_color(web.progress_layer,
	    GColorLightGray);
	layer_add_child(window_layer, web.progress_layer);

	web.label_layer = text_layer_create(GRect(0,
	    bounds.size.h / 2 + LABEL_MARGIN,
	    bounds.size.w,
	    LABEL_HEIGHT));
	text_layer_set_text(web.label_layer, web.label);
	text_layer_set_text_alignment(web.label_layer, GTextAlignmentCenter);
	text_layer_set_font(web.label_layer,
	    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
	layer_add_child(window_layer, text_layer_get_layer(web.label_layer));

	web.rate_layer = text_layer_create(GRect(0,
	    bounds.size.h / 2 + LABEL_MARGIN + LABEL_HEIGHT,
	    bounds.size.w,
	    LABEL_HEIGHT));
	text_layer_set_text(web.rate_layer, web.rate);
	text_layer_set_text_alignment(web.rate_layer, GTextAlignmentCenter);
	text_layer_set_font(web.rate_layer,
	    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
	layer_add_child(window_layer, text_layer_get_layer(web.rate_layer));

	set_modal_mode(true);
}

static void
window_unload(Window *window) {
	text_layer_destroy(modal_text_layer);
	text_layer_destroy(phone.label_layer);
	text_layer_destroy(web.label_layer);
	progress_layer_destroy(phone.progress_layer);
	progress_layer_destroy(web.progress_layer);
}

static void
set_modal_message(const char *msg) {
	strncpy(modal_text, msg, sizeof modal_text);
	modal_text[sizeof modal_text - 1] = 0;
	layer_mark_dirty(text_layer_get_layer(modal_text_layer));
}

/* minute_data_image - fill a buffer with CSV data without line terminator */
/*    format: RFC-3339 time, step count, yaw, pitch, vmc, ambient light */
static uint16_t
minute_data_image(char *buffer, size_t size,
    HealthMinuteData *data, HealthActivityMask activity_mask, time_t key) {
	struct tm *tm;
	size_t ret;
	if (!buffer || !data) return 0;

	tm = gmtime(&key);
	if (!tm) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "Unable to get UTC time for %" PRIi32, key);
		return 0;
	}

	ret = strftime(buffer, size, "%FT%TZ", tm);
	if (!ret) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "Unable to build RFC-3339 representation of %" PRIi32,
		    key);
		return 0;
	}

	if (ret >= size) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "Unexpected return value %zu of strftime on buffer %zu",
		    ret, size);
		return 0;
	}

	if (data->is_invalid) {
		int i = snprintf(buffer + ret, size - ret, ",,,,,,");

		if (i <= 0) {
			APP_LOG(APP_LOG_LEVEL_ERROR, "minute_data_image: "
			    "Unexpected return value %d of snprintf (i)", i);
			return 0;
		}
		return ret + i;
	}

	uint16_t yaw = data->orientation & 0xF;
	uint16_t pitch = data->orientation >> 4;

	int i = snprintf(buffer + ret, size - ret,
	    ",%" PRIu8 ",%" PRIu16 ",%" PRIu16 ",%" PRIu16 ",%d,%" PRIu32,
	    data->steps,
	    yaw,
	    pitch,
	    data->vmc,
	    (int)data->light,
	    activity_mask);

	if (i <= 0) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "minute_data_image: "
		    "Unexpected return value %d of snprintf", i);
		return 0;
	}

	return ret + i;
}

/* send_minute_data - use AppMessage to send the given minute data to phone */
static void
send_minute_data(HealthMinuteData *data, HealthActivityMask activity_mask,
    time_t key) {
	int32_t int_key = key / 60;

	if (key % 60 != 0) {
		APP_LOG(APP_LOG_LEVEL_WARNING,
		    "Discarding %" PRIi32 " second from time key %" PRIi32,
		    key % 60, int_key);
	}

	uint16_t size = minute_data_image(global_buffer, sizeof global_buffer,
	    data, activity_mask, key);
	if (!size) return;

	AppMessageResult msg_result;
	DictionaryIterator *iter;
	msg_result = app_message_outbox_begin(&iter);

	if (msg_result) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "send_minute_data: app_message_outbox_begin returned %d",
		    (int)msg_result);
		return;
	}

	DictionaryResult dict_result;
	dict_result = dict_write_int(iter, MSG_KEY_DATA_KEY,
	    &int_key, sizeof int_key, true);
	if (dict_result != DICT_OK) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "send_minute_data: [%d] unable to add data key %" PRIi32,
		    (int)dict_result, int_key);
	}

	dict_result = dict_write_cstring(iter,
	    MSG_KEY_DATA_LINE, global_buffer);
	if (dict_result != DICT_OK) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "send_minute_data: [%d] unable to add data line \"%s\"",
		    (int)dict_result, global_buffer);
	}

	msg_result = app_message_outbox_send();
	if (msg_result) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "send_minute_data: app_message_outbox_send returned %d",
		    (int)msg_result);
	}

	if (!phone.first_key) phone.first_key = int_key;
	phone.current_key = int_key;
	display_dirty = true;
}

static bool
record_activity(HealthActivity activity, time_t start_time, time_t end_time,
    void *context) {
	uint16_t first_index, last_index;
	(void)context;

	if (start_time <= minute_first) {
		first_index = 0;
	} else {
		first_index = (start_time - minute_first) / 60;
	}
	if (first_index >= minute_data_size) return true;

	last_index = (end_time - minute_first + 59) / 60;
	if (last_index > minute_data_size) {
		last_index = minute_data_size;
	}

	for (uint16_t i = first_index; i < last_index; i += 1) {
		minute_activity[i] |= activity;
	}

	return true;
}

static bool
load_minute_data_page(time_t start) {
	minute_first = start;
	minute_last = time(0);
	minute_data_size = health_service_get_minute_history(minute_data,
	    ARRAY_LENGTH(minute_data),
	    &minute_first, &minute_last);
	minute_index = 0;

	memset(minute_activity, 0, sizeof minute_activity);
	if (health_service_any_activity_accessible(HealthActivityMaskAll,
	    minute_first, minute_last)
	    == HealthServiceAccessibilityMaskAvailable) {
		health_service_activities_iterate(HealthActivityMaskAll,
		    minute_first, minute_last,
		    HealthIterationDirectionFuture,
		    &record_activity,
		    0);
	}

	if (!minute_data_size) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "health_service_get_minute_history returned 0");
		minute_first = minute_last = 0;
		return false;
	}

	return true;
}

static void
send_next_line(void) {
	if (minute_index >= minute_data_size
	    && !load_minute_data_page(minute_last)) {
		return;
	}

	send_minute_data(minute_data + minute_index,
	    minute_activity[minute_index],
	    minute_first + 60 * minute_index);
	minute_index += 1;
}

static void
handle_last_sent(Tuple *tuple) {
	uint32_t ikey = 0;
	if (tuple->length == 4 && tuple->type == TUPLE_UINT)
		ikey = tuple->value->uint32;
	else if (tuple->length == 4 && tuple->type == TUPLE_INT)
		ikey = tuple->value->int32;
	else {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "Unexpected type %d or length %" PRIu16
		    " for MSG_KEY_LAST_SENT",
		    (int)tuple->type, tuple->length);
		return;
	}
	APP_LOG(APP_LOG_LEVEL_INFO, "received LAST_SENT %" PRIu32, ikey);

	phone.start_time = time(0);
	minute_index = 0;
	minute_data_size = 0;
	minute_last = ikey ? (ikey + 1) * 60 : 0;
	set_modal_mode(false);
	send_next_line();
}

static void
inbox_received_handler(DictionaryIterator *iterator, void *context) {
	Tuple *tuple;
	(void)context;

	tuple = dict_find(iterator, MSG_KEY_LAST_SENT);
	if (tuple) handle_last_sent (tuple);

	tuple = dict_find(iterator, MSG_KEY_MODAL_MESSAGE);
	if (tuple) {
		if (tuple->type != TUPLE_CSTRING) {
			APP_LOG(APP_LOG_LEVEL_ERROR,
			    "Unexpected type %d for MSG_KEY_MODAL_MESSAGE",
			    (int)tuple->type);
		} else {
			set_modal_mode(true);
			set_modal_message(tuple->value->cstring);
		}
	}

	tuple = dict_find(iterator, MSG_KEY_UPLOAD_DONE);
	if (tuple) {
		web.current_key = tuple_uint(tuple);
		if (!web.first_key) web.first_key = web.current_key;
		display_dirty = true;
	}

	tuple = dict_find(iterator, MSG_KEY_UPLOAD_START);
	if (tuple) {
		web.first_key = tuple_uint(tuple);
		web.start_time = time(0);
	}
}

static void
outbox_sent_handler(DictionaryIterator *iterator, void *context) {
	(void)iterator;
	(void)context;
	send_next_line();
}

static void
outbox_failed_handler(DictionaryIterator *iterator, AppMessageResult reason,
    void *context) {
	(void)iterator;
	(void)context;
	APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: 0x%x", (unsigned)reason);
}

static void
tick_handler(struct tm *tick_time, TimeUnits units_changed) {
	(void)tick_time;
	(void)units_changed;
	if (display_dirty) update_progress();
}

static void
init(void) {
	app_message_register_inbox_received(inbox_received_handler);
	app_message_register_outbox_failed(outbox_failed_handler);
	app_message_register_outbox_sent(outbox_sent_handler);
	app_message_open(256, 2048);

	strncpy(modal_text, "Waiting for JS part", sizeof modal_text);
	window = window_create();
	window_set_window_handlers(window, (WindowHandlers) {
	    .load = window_load,
	    .unload = window_unload,
	});
	window_stack_push(window, true);
	tick_timer_service_subscribe(SECOND_UNIT, &tick_handler);
}

static void deinit(void) {
	window_destroy(window);
}

int
main(void) {
	init();
	app_event_loop();
	deinit();
}
