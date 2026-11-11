/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QPlainTextEdit;

namespace tb::ui
{
// A view of the task's parameter expression, never a second persisted option set.
class HltoolsOptionsEditor : public QWidget
{
  Q_OBJECT
private:
  struct Entry
  {
    QString flag;
    QString stage;
    QWidget* row;
    QCheckBox* checkbox;
    QComboBox* value;
    int presetCount;
    int arity;
    bool repeatable;
    QLineEdit* path = nullptr;
    QPlainTextEdit* list = nullptr;
    std::vector<QLineEdit*> components = {};
  };
  QString m_parameters;
  QLineEdit* m_filter = nullptr;
  QComboBox* m_stage = nullptr;
  QCheckBox* m_modifiedOnly = nullptr;
  QLabel* m_empty = nullptr;
  std::vector<Entry> m_entries;

public:
  explicit HltoolsOptionsEditor(QWidget* parent = nullptr);
  void setParameters(const QString& parameters);

signals:
  void parametersChanged(const QString& parameters);

private:
  void filterEntries();
  void changeOption(const Entry& entry, const QStringList& values);
};
} // namespace tb::ui
