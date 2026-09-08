# Python API {#python_api}

使用 `import trenchbroom as tb` 导入公共 API。Python 控制台、清单插件和 MCP Python
执行共用这套接口。`tb_api` 会返回当前实际绑定的参数、默认值、返回类型、属性可写性和效果；
随应用分发的 `trenchbroom.pyi` 用于编辑器补全。

## 文档与选区 {#python_api_documents}

```python
doc = tb.documents.current()
selection = doc.selection
```

`documents` 负责地图生命周期和保存。`documents.open(path)` 会验证已打开的文档，
`documents.save(path=None)` 保存当前文档，`documents.snapshot()` 返回紧凑的地图摘要。

当前选区通过 `Selection` 访问。`entities`、`all_entities`、`brushes` 和 `brush_faces`
返回实时句柄。`set`、`add` 和 `clear` 修改选区。`translate(offset)`、
`rotate(axis, angle_degrees, center=None)` 与 `scale(factors, center=None)` 使用地图单位、
轴角度数和选区包围盒中心默认值。`duplicate()` 保留原生的复制后选中副本语义。

## 显式对象编辑 {#python_api_objects}

已知目标时使用 `objects.translate(targets, offset)`、`objects.rotate(targets, axis, angle_degrees, center=None)`、
`objects.scale(targets, factors, center=None)`、
`objects.duplicate(targets, select=False)` 和 `objects.delete(targets)`。它们不会改动无关
选区。目标可以是一个 `Entity`、`Brush` 或它们的集合；重复值和另一个目标的子节点只处理一次。

坐标、向量、轴和中心接受 `tb.Vec3` 或有限的 `(x, y, z)` 值。缩放还接受有限标量。
输入结构错误抛出 `TypeError`，数值或句柄错误抛出 `ValueError`，原生编辑失败抛出
`RuntimeError`，不会提交部分修改。

## 创建与领域模块 {#python_api_domains}

`brushes.create`、`create_box`、`create_boxes`、`create_prism` 和 `create_prisms` 创建凸几何。
`entities.create` 创建普通实体；`entities.create_from_schema` 与
`create_from_schema_batch` 还会验证游戏实体定义。创建默认保持当前选区，传入 `select=True`
才选中新对象。

`entities.update_many(entities, properties, remove_keys=...)` 批量更新属性，
`entities.definitions()` 列出游戏定义。`faces` 和 `materials` 负责表面与 UV；UV 循环坐标是
纹理像素，不是归一化坐标。`actions.list()` 与 `actions.execute()` 调用原生动作，`history`
负责撤销重做，`viewport` 负责 action 模式的相机控制。

## 句柄与事务 {#python_api_handles}

句柄是实时编辑器引用。重新加载、关闭文档或删除节点后，相关句柄会失效，应从当前文档重新查询。
使用 `with doc.transaction("Description"):` 将普通脚本编辑合并为一个撤销步骤。

此前的顶层编辑名称和 camelCase 别名均已移除。请参阅项目中的 Python API 迁移说明获取替换方式。
