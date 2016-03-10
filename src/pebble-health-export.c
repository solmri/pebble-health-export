#include <inttypes.h>
#include <pebble.h>

#define MSG_KEY_LAST_SENT	110
#define MSG_KEY_DATA_KEY	210
#define MSG_KEY_DATA_LINE	220

static Window *window;
static TextLayer *text_layer;
static char buffer[256];
static HealthMinuteData minute_data[1440];
static time_t minute_first = 0, minute_last = 0;
static unsigned sent = 0;
static char global_buffer[1024];

static void
window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	text_layer = text_layer_create((GRect) { .origin = { 0, bounds.size.h / 3 }, .size = { bounds.size.w, bounds.size.h / 3 } });
	text_layer_set_text(text_layer, buffer);
	text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(text_layer));
}

static void
window_unload(Window *window) {
	text_layer_destroy(text_layer);
}

/* minute_data_image - fill a buffer with CSV data without line terminator */
/*    format: RFC-3339 time, step count, yaw, pitch, vmc, ambient light */
static uint16_t
minute_data_image(char *buffer, size_t size,
    HealthMinuteData *data, time_t key) {
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

	uint16_t yaw = data->orientation & 0xF;
	uint16_t pitch = data->orientation >> 4;

	int i = snprintf(buffer + ret, size - ret,
	    ",%" PRIu8 ",%" PRIu16 ",%" PRIu16 ",%" PRIu16 ",%d",
	    data->steps,
	    yaw,
	    pitch,
	    data->vmc,
	    (int)data->light);

	if (i <= 0) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "minute_data_image: "
		    "Unexpected return value %d of snprintf", i);
		return 0;
	}

	return ret + i;
}

/* send_minute_data - use AppMessage to send the given minute data to phone */
static void
send_minute_data(HealthMinuteData *data, time_t key) {
	int32_t int_key = key / 60;

	if (sent > 10) return;

	if (key % 60 != 0) {
		APP_LOG(APP_LOG_LEVEL_WARNING,
		    "Discarding %" PRIi32 " second from time key %" PRIi32,
		    key % 60, int_key);
	}

	uint16_t size = minute_data_image(global_buffer, sizeof global_buffer,
	    data, key);
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

	APP_LOG(APP_LOG_LEVEL_INFO, "sent data for key %" PRIi32, int_key);

	sent += 1;
}

static int16_t
get_next_minute_index(time_t previous_t) {
	time_t t = previous_t ? previous_t + 60 - (previous_t % 60) : 0;
	int16_t index = 0;

	if (t < minute_first || t >= minute_last) {
		uint32_t u;
		minute_first = t;
		minute_last = time(0);
		u = health_service_get_minute_history(minute_data,
		    ARRAY_LENGTH(minute_data),
		    &minute_first, &minute_last);

		if (!u) {
			APP_LOG(APP_LOG_LEVEL_ERROR,
			    "health_service_get_minute_history returned 0");
			minute_first = minute_last = 0;
			return -1;
		}

		if (t >= minute_last) {
			APP_LOG(APP_LOG_LEVEL_ERROR,
			    "Unexpected minute_last %" PRIi32
			    " when t is %" PRIi32,
			    minute_last, t);
			return -1;
		}
	} else {
		index = (t - minute_first) / 60;
	}

	while (minute_data[index].is_invalid) {
		index += 1;
		t = minute_first + 60 * index;
		if (t >= minute_last)
			return get_next_minute_index(t);
	}

	return index;
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

	int16_t index = get_next_minute_index(ikey * 60);
	if (index < 0) return;

	APP_LOG(APP_LOG_LEVEL_INFO,
	    "got index %" PRIi16 ", sending time %" PRIi32,
	    index, minute_first + index * 60);

	send_minute_data(minute_data + index, minute_first + index * 60);
}

static void
inbox_received_handler(DictionaryIterator *iterator, void *context) {
	Tuple *tuple;
	(void)context;

	tuple = dict_find(iterator, MSG_KEY_LAST_SENT);
	if (tuple) handle_last_sent (tuple);
}

static time_t
get_first_health_minute(void) {
	time_t start = 0, end = time(0);
	uint32_t ret;

	ret = health_service_get_minute_history(minute_data, 1, &start, &end);
	return ret ? start : 0;
}

static void
init_text(void) {
	time_t t = get_first_health_minute();
	struct tm *tm = localtime(&t);
	strftime(buffer, sizeof buffer, "First health minute: %FT%T%z", tm);
	minute_first = t;
}

static void
init(void) {
	app_message_register_inbox_received(inbox_received_handler);
	app_message_open(256, 2048);

	init_text();
	window = window_create();
	window_set_window_handlers(window, (WindowHandlers) {
	    .load = window_load,
	    .unload = window_unload,
	});
	window_stack_push(window, true);
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
