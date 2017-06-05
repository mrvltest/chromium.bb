// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/geometry/scaling2d.h"

#include <cmath>

#include "base/strings/stringprintf.h"

namespace gfx {

std::string Scaling2d::ToString() const {
  return base::StringPrintf("[%f %f]", x_, y_);
}

bool Scaling2d::IsZero() const {
  return x_ == 0 && y_ == 0;
}

bool Scaling2d::IsOne() const {
  return x_ == 1 && y_ == 1;
}

void Scaling2d::Multiply(const Scaling2d& other) {
  x_ *= other.x_;
  y_ *= other.y_;
}

void Scaling2d::Divide(const Scaling2d& other) {
  x_ /= other.x_;
  y_ /= other.y_;
}

void Scaling2d::Scale(float x_scale, float y_scale) {
  x_ *= x_scale;
  y_ *= y_scale;
}

Scaling2d ScaleVector2d(const Scaling2d& v, float x_scale, float y_scale) {
  Scaling2d scaled_s(v);
  scaled_s.Scale(x_scale, y_scale);
  return scaled_s;
}

}  // namespace gfx
