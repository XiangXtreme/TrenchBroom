/*
 Copyright (C) 2026 KreedzBroom contributors

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/CatchConfig.h"
#include "ui/PlayModeRawMouse.h"

#ifdef TB_PLAY_MODE_XINPUT2
#include <xcb/xinput.h>
#endif

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

#ifdef TB_PLAY_MODE_XINPUT2

namespace
{

constexpr uint8_t XInputOpcode = 131;

// The wire layout of a raw motion event with one valuator mask word, as xcb delivers it:
// header, mask, accelerated values for each set bit, then raw values for each set bit.
struct RawMotionEvent
{
  xcb_input_raw_button_press_event_t header;
  uint32_t mask;
  xcb_input_fp3232_t values[2];
  xcb_input_fp3232_t rawValues[2];
};

RawMotionEvent rawMotionEvent(const uint32_t mask)
{
  auto event = RawMotionEvent{};
  event.header.response_type = XCB_GE_GENERIC;
  event.header.extension = XInputOpcode;
  event.header.event_type = XCB_INPUT_RAW_MOTION;
  event.header.time = 12345;
  event.header.valuators_len = 1;
  event.mask = mask;
  return event;
}

} // namespace

TEST_CASE("parseXInputRawMotion")
{
  SECTION("returns the unaccelerated counts, not the accelerated values")
  {
    auto event = rawMotionEvent(0b11);
    event.values[0] = {30, 0};              // accelerated x
    event.values[1] = {-12, 0};             // accelerated y
    event.rawValues[0] = {10, 0x80000000u}; // raw x = 10.5
    event.rawValues[1] = {-4, 0};           // raw y = -4

    const auto motion = parseXInputRawMotion(&event, XInputOpcode);
    REQUIRE(motion);
    CHECK(motion->dx == 10.5);
    CHECK(motion->dy == -4.0);
    CHECK(motion->timestamp == 12345u);
  }

  SECTION("returns the accelerated values when asked to")
  {
    auto event = rawMotionEvent(0b11);
    event.values[0] = {30, 0};
    event.values[1] = {-12, 0};
    event.rawValues[0] = {10, 0};
    event.rawValues[1] = {-4, 0};

    const auto motion = parseXInputRawMotion(&event, XInputOpcode, true);
    REQUIRE(motion);
    CHECK(motion->dx == 30.0);
    CHECK(motion->dy == -12.0);
  }

  SECTION("handles events that only move one axis")
  {
    auto event = rawMotionEvent(0b10); // only y: one value of each kind
    event.values[0] = {-9, 0};
    event.values[1] = {-3, 0}; // with a single valuator, this slot holds the raw y value

    const auto motion = parseXInputRawMotion(&event, XInputOpcode);
    REQUIRE(motion);
    CHECK(motion->dx == 0.0);
    CHECK(motion->dy == -3.0);
  }

  SECTION("ignores other events")
  {
    auto event = rawMotionEvent(0b11);
    event.rawValues[0] = {1, 0};

    CHECK_FALSE(parseXInputRawMotion(&event, XInputOpcode + 1));

    event.header.event_type = XCB_INPUT_RAW_BUTTON_PRESS;
    CHECK_FALSE(parseXInputRawMotion(&event, XInputOpcode));
  }
}

#endif

} // namespace tb::ui
