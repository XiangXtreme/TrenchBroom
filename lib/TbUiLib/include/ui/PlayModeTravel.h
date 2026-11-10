/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "cs/scs.h"

#include "vm/vec.h"

#include <optional>

namespace tb::ui
{
/** KZ travel aids, applied on simulation ticks without changing normal movement. */
class PlayModeTravel
{
private:
  std::optional<vm::vec3f> m_hookAnchor;
  bool m_hookHeld = false;
  bool m_noclipExitBlocked = false;
  float m_hookSpeed = 600.0f;

public:
  void setHookSpeed(float speed);
  void hook(scs_context* context, const vm::vec3f& viewAngles, bool down);
  void cancelHook();
  void clearInput();
  void toggleNoclip(scs_context* context);
  void beforeStep(scs_context* context, uint8_t msec);

  const std::optional<vm::vec3f>& hookAnchor() const;
  bool noclipExitBlocked() const;
};
} // namespace tb::ui
