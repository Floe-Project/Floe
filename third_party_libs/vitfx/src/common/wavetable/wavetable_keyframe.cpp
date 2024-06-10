// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wavetable_keyframe.h"

#include "utils.h"
#include "wavetable_component.h"

float WavetableKeyframe::linearTween(float point_from, float point_to, float t) {
  return vital::utils::interpolate(point_from, point_to, t);
}

float WavetableKeyframe::cubicTween(float point_prev, float point_from, float point_to, float point_next,
                                    float range_prev, float range, float range_next, float t) {
  float slope_from = 0.0f;
  float slope_to = 0.0f;
  if (range_prev > 0.0f)
    slope_from = (point_to - point_prev) / (1.0f + range_prev / range);
  if (range_next > 0.0f)
    slope_to = (point_next - point_from) / (1.0f + range_next / range);
  float delta = point_to - point_from;

  float movement = linearTween(point_from, point_to, t);
  float smooth = t * (1.0f - t) * ((1.0f - t) * (slope_from - delta) + t * (delta - slope_to));
  return movement + smooth;
}

int WavetableKeyframe::index() {
  return owner()->indexOf(this);
}

json WavetableKeyframe::stateToJson() {
  return { { "position", position_ } };
}

void WavetableKeyframe::jsonToState(json data) {
  position_ = data["position"];
}
