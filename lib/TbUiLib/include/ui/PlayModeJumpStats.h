/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "ui/PlayModeJumpGuide.h"
#include "ui/PlayModeShowPre.h"

#include <optional>

namespace tb::ui
{
/** Session-local analysis; reads movement and collision without modifying either.
 * Geometry spans and KZ jump distances deliberately remain separate measurements. */
class PlayModeJumpStats
{
public:
  enum class Technique
  {
    LongJump,
    HighJump,
    Unsupported
  };
  struct Result
  {
    Technique technique;
    bool failed;
    float distance;
    float preSpeed;
    float maxSpeed;
    float heightDifference;
    double timeMs;
  };

private:
  struct Flight
  {
    Technique technique;
    vm::vec3f startFeet;
    float preSpeed;
    float maxSpeed;
  };
  PlayModeShowPre m_showPre;
  std::optional<Flight> m_flight;
  std::optional<Result> m_result;
  bool m_initialized = false;
  bool m_assisted = false;
  double m_lastDuckMs = -1.0e9;
  double m_timeMs = 0.0;
  float m_gravity = 800.0f;

public:
  void reset();
  void setGravity(float gravity);
  /** An explicit discontinuity: CP restore, focus loss, or skipped simulation time.
   * The next normal grounded state starts fresh. Hook momentum is handled by tick. */
  void interrupt();
  void tick(
    scs_context* context,
    const std::vector<PlayModeJumpSurface>& surfaces,
    const scs_move_sample& sample,
    bool assisted);
  const std::optional<PlayModeShowPre::Pre>& pre() const;
  const std::optional<Result>& result() const;
  double timeMs() const;
  static bool visible(const Result& result, bool showFailed);

private:
  void finish(const vm::vec3f& feet, bool failed);
};
} // namespace tb::ui
