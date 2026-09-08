#pragma once

#include <optional>

namespace tb::ui
{

struct PythonPendingPreferenceChanges
{
  std::optional<bool> textureLock;
  std::optional<bool> uvLock;
};

class ScopedPythonPendingPreferenceChanges
{
private:
  PythonPendingPreferenceChanges* m_previous;

public:
  explicit ScopedPythonPendingPreferenceChanges(PythonPendingPreferenceChanges& changes);
  ~ScopedPythonPendingPreferenceChanges();

  ScopedPythonPendingPreferenceChanges(const ScopedPythonPendingPreferenceChanges&) =
    delete;
  ScopedPythonPendingPreferenceChanges& operator=(
    const ScopedPythonPendingPreferenceChanges&) = delete;
};

PythonPendingPreferenceChanges* currentPythonPendingPreferenceChanges();

} // namespace tb::ui
