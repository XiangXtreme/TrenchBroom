/*
 Copyright (C) 2026 KreedzBroom contributors
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "vm/mat.h"
#include "vm/vec.h"

#include <optional>

namespace tb::mdl
{
class Entity;
struct GameConfig;

bool usesGoldSrcLightDirections(const GameConfig& config);
bool isGoldSrcLight(const Entity& entity);
bool isDirectionalGoldSrcLight(const Entity& entity);
// Angular fallback; named targets are resolved separately by the scene owner.
std::optional<vm::vec3d> goldSrcLightDirection(const Entity& entity);
void applyGoldSrcLightRotation(Entity& entity, const vm::mat4x4d& transformation);
} // namespace tb::mdl
