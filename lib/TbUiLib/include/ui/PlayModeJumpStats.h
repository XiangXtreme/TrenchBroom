/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "ui/PlayModeShowPre.h"

#include "vm/vec.h"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

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
    CountJump,
    StandupCountJump,
    DoubleCountJump,
    MultiCountJump,
    Bhop,
    StandupBhop,
    DuckBhop,
    Unsupported
  };
  struct Strafe
  {
    char key = 'A';
    float gain = 0.0f;
    float loss = 0.0f;
    unsigned frames = 0;
    unsigned goodFrames = 0;
    float efficiency() const;
  };
  struct Takeoff
  {
    Technique technique = Technique::LongJump;
    unsigned fog = 0;
    unsigned duckTaps = 0;
    float landingSpeed = 0.0f;
    float preSpeed = 0.0f;
    float postSpeed = 0.0f;
    double timeMs = 0.0;
  };
  struct Result
  {
    Technique technique = Technique::Unsupported;
    bool failed = false;
    float distance = 0.0f;
    float preSpeed = 0.0f;
    float maxSpeed = 0.0f;
    float heightDifference = 0.0f;
    double timeMs = 0.0;
    Takeoff takeoff{};
    std::vector<Strafe> strafes;
    unsigned airFrames = 0;
    unsigned duckFrames = 0;
    bool inDuck = false;
    bool touched = false;
    float weaponSpeed = 250.0f;
    bool truncated = false;
  };

  static std::string_view name(Technique technique);

private:
  struct Flight
  {
    vm::vec3f startFeet;
    Result analysis{};
    struct ActiveStrafe
    {
      Strafe strafe;
      unsigned start = 0;
      unsigned overlaps = 0;
    };
    std::array<std::optional<ActiveStrafe>, 4> activeKeys{};
  };
  PlayModeShowPre m_showPre;
  std::optional<Flight> m_flight;
  std::optional<Result> m_result;
  std::optional<Takeoff> m_takeoff;
  bool m_initialized = false;
  bool m_assisted = false;
  enum class AirAction
  {
    None,
    Jump,
    DuckTap
  };
  AirAction m_airAction = AirAction::None;
  bool m_landed = false;
  bool m_landedDucked = false;
  unsigned m_groundFrames = 0;
  unsigned m_duckTaps = 0;
  float m_landingSpeed = 0.0f;
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
    const scs_move_sample& sample,
    bool assisted);
  const std::optional<PlayModeShowPre::Pre>& pre() const;
  const std::optional<Result>& result() const;
  const Result* current() const;
  const std::optional<Takeoff>& takeoff() const;
  double timeMs() const;
  static bool visible(const Result& result, bool showFailed);

private:
  void finish(const vm::vec3f& feet, bool failed);
  void recordStrafes(const scs_move_sample& sample, bool badLastFrame);
  void closeStrafe(size_t index);
  Takeoff classifyTakeoff(const scs_move_sample& sample);
};
} // namespace tb::ui
