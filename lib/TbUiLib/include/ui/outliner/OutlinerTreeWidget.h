#pragma once

#include <QTreeWidget>

#include "base/NotifierConnection.h"

#include <unordered_map>
#include <vector>

class QMenu;

namespace tb::mdl
{
class Node;
class LayerNode;
class GroupNode;
struct SelectionChange;
} // namespace tb::mdl

namespace tb::ui
{
class MapDocument;

class OutlinerTreeWidget : public QTreeWidget
{
  Q_OBJECT
public:
  enum class SortMode
  {
    Default = 0,
    NameAsc = 1, // Retained for stored setting compatibility.
    Type = 2,
    FileOrder = 3,
  };

private:
  MapDocument& m_document;

  QIcon m_groupIcon;
  QIcon m_entityIcon;
  QIcon m_brushEntityIcon;
  QIcon m_brushIcon;
  QIcon m_visibleIcon;
  QIcon m_hiddenIcon;
  QIcon m_lockedIcon;
  QIcon m_unlockedIcon;

  bool m_syncingSelection = false;
  int m_suppressScrollToSelectionCount = 0;
  bool m_updateTreeQueued = false;
  mdl::Node* m_revealAfterUpdate = nullptr;
  std::unordered_map<const mdl::Node*, QTreeWidgetItem*> m_itemForNode;
  NotifierConnection m_notifierConnection;
  QPoint m_dragStartPosition;
  bool m_rightMousePressedInside = false;

public:
  explicit OutlinerTreeWidget(MapDocument& document, QWidget* parent = nullptr);
  ~OutlinerTreeWidget() override;

  void updateTree();
  void revealNode(mdl::Node* node);
  void setFilterText(const QString& text);
  void setSortMode(SortMode mode);

signals:
  void contextMenuActionsRequested(QMenu* menu, bool includeRename);
  void pieMenuRequested();

protected:
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  bool dropSelectedItemsOnItem(QTreeWidgetItem* targetItem);

private:
  void scheduleUpdateTree(mdl::Node* revealNode = nullptr);
  void loadIcons();
  void setupTreeItem(QTreeWidgetItem* item, mdl::Node* node);
  void addLayerContents(QTreeWidgetItem* layerItem, mdl::LayerNode* layer);
  void refreshTreeParents(std::vector<mdl::Node*> parents);
  void removeItemMappings(QTreeWidgetItem* item);
  void applyFilter();
  void updateCurrentGroupHighlight();

  void addNodeToTree(QTreeWidgetItem* parentItem, mdl::Node* node);

  void onDocumentSelectionChanged(const mdl::SelectionChange& change);
  void onItemSelectionChanged();
  void syncSelectionFromDocument();
  void findAndSelectNode(const mdl::Node* targetNode);
  QTreeWidgetItem* findItemForNode(const mdl::Node* targetNode);
  void refreshTreeItemRecursively(QTreeWidgetItem* item);
  void updateVisibilityIconRecursively(QTreeWidgetItem* item, bool isVisible);
  void captureExpandedState(
    std::unordered_map<const mdl::Node*, bool>& expandedNodes,
    std::unordered_map<const mdl::LayerNode*, bool>& expandedWorldspawn) const;
  void restoreExpandedState(
    const std::unordered_map<const mdl::Node*, bool>& expandedNodes,
    const std::unordered_map<const mdl::LayerNode*, bool>& expandedWorldspawn);

  mdl::Node* nodeFromItem(QTreeWidgetItem* item) const;

private:
  QString m_filterText;
  bool m_filterActive = false;
  std::unordered_map<const mdl::Node*, bool> m_expandedBeforeFilter;
  std::unordered_map<const mdl::LayerNode*, bool> m_worldspawnExpandedBeforeFilter;
  std::unordered_map<std::string, size_t> m_linkedGroupSetSizes;
  std::unordered_map<std::string, int> m_linkedGroupSetIndices;
  SortMode m_sortMode = SortMode::Default;
  const mdl::GroupNode* m_highlightedCurrentGroup = nullptr;
  const mdl::LayerNode* m_highlightedCurrentLayer = nullptr;
};

} // namespace tb::ui
