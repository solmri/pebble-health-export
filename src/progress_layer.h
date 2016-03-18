/*
 * This is copied dirictly without any modification other than the addition
 * of this header, from "UI Pattern" project in Pebble examples
 * https://github.com/pebble-examples/ui-patterns
 * It is covered by the MIT licence detailed below:
 *
 * Copyright (c) 2015 Pebble Examples
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once

#include <pebble.h>

typedef Layer ProgressLayer;

ProgressLayer* progress_layer_create(GRect frame);
void progress_layer_destroy(ProgressLayer* progress_layer);
void progress_layer_increment_progress(ProgressLayer* progress_layer, int16_t progress);
void progress_layer_set_progress(ProgressLayer* progress_layer, int16_t progress_percent);
void progress_layer_set_corner_radius(ProgressLayer* progress_layer, uint16_t corner_radius);
void progress_layer_set_foreground_color(ProgressLayer* progress_layer, GColor color);
void progress_layer_set_background_color(ProgressLayer* progress_layer, GColor color);
