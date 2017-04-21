// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_GEOMETRY_SCALING2D_H_
#define UI_GFX_GEOMETRY_SCALING2D_H_

#include <iosfwd>
#include <string>

#include "ui/gfx/gfx_export.h"

namespace gfx {

class GFX_EXPORT Scaling2d {
 public:
  Scaling2d() : x_(1), y_(1) {}
  Scaling2d(float s) : x_(s), y_(s) {}
  Scaling2d(float x, float y) : x_(x), y_(y) {}

  float x() const { return x_; }
  void set_x(float x) { x_ = x; }

  float y() const { return y_; }
  void set_y(float y) { y_ = y; }

  // True if both components of the scaling are 0.
  bool IsZero() const;
  bool IsOne() const;
  bool IsPositive() const { return x_ > 0.f && y_ > 0.f; }

  // Multiply the components of the |other| scaling to the current scaling.
  void Multiply(const Scaling2d& other);
  // Divide the components of the |other| scaling by the current scaling.
  void Divide(const Scaling2d& other);  

  void operator*=(const Scaling2d& other) { Multiply(other); }
  void operator/=(const Scaling2d& other) { Divide(other); }

  void SetToMin(const Scaling2d& other) {
    x_ = x_ <= other.x_ ? x_ : other.x_;
    y_ = y_ <= other.y_ ? y_ : other.y_;
  }

  void SetToMax(const Scaling2d& other) {
    x_ = x_ >= other.x_ ? x_ : other.x_;
    y_ = y_ >= other.y_ ? y_ : other.y_;
  }

  // Scale the x and y components of the scaling by |scale|.
  void Scale(float scale) { Scale(scale, scale); }
  // Scale the x and y components of the scaling by |x_scale| and |y_scale|
  // respectively.
  void Scale(float x_scale, float y_scale);

  std::string ToString() const;

 private:
  float x_;
  float y_;
};

inline bool operator==(const Scaling2d& lhs, const Scaling2d& rhs) {
  return lhs.x() == rhs.x() && lhs.y() == rhs.y();
}

inline bool operator!=(const Scaling2d& lhs, const Scaling2d& rhs) {
  return !(lhs == rhs);
}

inline bool operator<(const Scaling2d& lhs, const Scaling2d& rhs) {
  return lhs.x() < rhs.x() || lhs.y() < rhs.y();
}

inline bool operator<=(const Scaling2d& lhs, const Scaling2d& rhs) {
  return lhs.x() <= rhs.x() || lhs.y() <= rhs.y();
}

inline bool operator>(const Scaling2d& lhs, const Scaling2d& rhs) {
  return lhs.x() > rhs.x() && lhs.y() > rhs.y();
}

inline bool operator>=(const Scaling2d& lhs, const Scaling2d& rhs) {
  return lhs.x() >= rhs.x() && lhs.y() >= rhs.y();
}

inline Scaling2d GetMin(const Scaling2d& lhs, const Scaling2d& rhs) {
  return Scaling2d(
    lhs.x() <= rhs.x() ? lhs.x() : rhs.x(),
    lhs.y() <= rhs.y() ? lhs.y() : rhs.y());
}

inline Scaling2d GetMax(const Scaling2d& lhs, const Scaling2d& rhs) {
  return Scaling2d(
    lhs.x() >= rhs.x() ? lhs.x() : rhs.x(),
    lhs.y() >= rhs.y() ? lhs.y() : rhs.y());
}

inline Scaling2d operator-(const Scaling2d& s) {
  return Scaling2d(-s.x(), -s.y());
}

inline Scaling2d operator*(const Scaling2d& lhs, const Scaling2d& rhs) {
  Scaling2d result = lhs;
  result.Multiply(rhs);
  return result;
}

inline Scaling2d operator/(const Scaling2d& lhs, const Scaling2d& rhs) {
  Scaling2d result = lhs;
  result.Divide(rhs);
  return result;
}

// Return a scaling that is |v| scaled by the given scale factors along each
// axis.
GFX_EXPORT Scaling2d ScaleVector2d(const Scaling2d& v,
                                   float x_scale,
                                   float y_scale);

// Return a scaling that is |v| scaled by the given scale factor.
inline Scaling2d ScaleVector2d(const Scaling2d& v, float scale) {
  return ScaleVector2d(v, scale, scale);
}

// This is declared here for use in gtest-based unit tests but is defined in
// the gfx_test_support target. Depend on that to use this in your unit test.
// This should not be used in production code - call ToString() instead.
void PrintTo(const Scaling2d& scaling, ::std::ostream* os);

}  // namespace gfx

#endif // UI_GFX_GEOMETRY_SCALING2D_F_H_
