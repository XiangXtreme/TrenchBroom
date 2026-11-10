# 游戏配置与表达式语言规范 {#game_config_and_expressions}

## 表达式语言 {#expression_language}

TrenchBroom 包含一种简单的表达式语言，可用于轻松地将变量和更复杂的表达式嵌入字符串中。目前，该语言主要用于“编译”对话框和“启动引擎”对话框。下面我们将介绍表达式语言的语法和语义。

### 求值 {#evaluation}

每个表达式都可以求值为一个值。例如，字符串 `"This is a string."` 是一个有效表达式，它将被求值为一个包含字符串 `This is a string.` 的 `String` 类型值。表达式语言定义了以下类型：

类型       说明
----       -----------
Boolean    该类型的值可以是 true 或 false。
String     字符串。
Number     浮点数。
Array      数组是值的列表。
Map        映射是键值对列表。同义词：dictionary、table。
Range      范围类型仅供内部使用。
Null       `null` 值的类型。
Undefined  未定义值的类型。

#### 类型转换 {#el_type_conversion}

下表描述这些类型之间可能的类型转换。第一列为源类型，后续列说明如何进行转换，或转换结果是否为错误。`Range`、`Null` 和 `Undefined` 类型的列被省略，因为除平凡转换外，没有类型可以转换为这些类型。将某类型 `X` 的值转换为相同类型称为_平凡转换_。

-----------------------------------------------------------------------------------------------------------------------------
            `Boolean`                     `String`               `Number`                      `Array`     `Map`
----        ----------------------------- ---------------------- ----------------------------- ----------- ---------
`Boolean`   _trivial_                     `"true"` or `"false"`  `1.0` or `0.0`                错误        错误

`String`    `false` if value is `"false"` _trivial_              `0.0` if blank, number        错误        错误
            or `""`, `true` otherwise                            representation if possible,
                                                                 error otherwise

`Number`    `false` if value is `0.0`,    string representation, _trivial_                     错误        错误
            `true` otherwise              e.g. "1.0"

`Array`     错误                          错误                   错误                          _trivial_   错误

`Map`       错误                          错误                   错误                          错误        _trivial_

`Range`     错误                          错误                   错误                          错误        错误

`Null`      `false`                       `""` (empty string)    `0.0`                         空数组      空映射

`Undefined` 错误                          错误                   错误                          错误        错误
-----------------------------------------------------------------------------------------------------------------------------

字符串值当且仅当自身是数字字面量时才能转换为数字值（见下文）。反过来，任意数字都可以转换为字符串值，格式如下：如果数字是整数，字符串只包含整数部分，不添加小数部分；如果不是整数，小数部分会以 17 位精度格式化。

### 表达式与项 {#expressions-and-terms}

每个表达式都由一个项组成。项是可以求值的内容，例如加法 (`7.0 + 3.0`) 或变量（变量随后会被求值为其值）。

    Expression     = GroupedTerm | Term
    GroupedTerm    = "(" Term ")"
    Term           = SimpleTerm | Switch | CompoundTerm

    SimpleTerm     = Name | Literal | Subscript | UnaryTerm | GroupedTerm
    CompoundTerm   = AlgebraicTerm | LogicalTerm | ComparisonTerm | Case

    UnaryTerm      = Plus | Minus | LogicalNegation | BinaryNegation
    AlgebraicTerm  = Addition | Subtraction | Multiplication | Division | Modulus
    LogicalTerm    = LogicalAnd | LogicalOr
    BinaryTerm     = BinaryAnd | BinaryXor | BinaryOr | BinaryLeftShift | BinaryRightShift
    ComparisonTerm = Less | LessOrEqual | Equal | Inequal | GreaterOrEqual | Greater

### 名称与字面量 {#names-and-literals}

名称是以字母字符或下划线开头的字符串，后面可以跟随更多字母数字字符和下划线。

    Name = ( "_" | Alpha ) { "_" | Alpha | Numeric }

`MODS`、`_var1` 和 `_123` 都是有效名称，而 `1_MODS`、`$MODS` 和 `_$MODS` 无效。表达式求值时，所有变量名都会被替换为其引用变量的值。如果值不是 `String` 类型，会转换为该类型；如果无法转换为 `String`，则会抛出错误。

字面量可以是字符串、数字、布尔值、数组或映射字面量。

    Literal = String | Number | Boolean | Array | Map

    Boolean = "true" | "false"
    String  = """ { Char } """ | "'" { Char } "'"
    Number  = Numeric { Numeric } [ "." Numeric { Numeric } ]

请注意，字符串可以使用双引号或单引号包围，但不能混用这两种方式。如果使用双引号包围字符串，必须用反斜杠转义其中的所有字面双引号，例如："this is a \"fox\""；使用单引号包围时则不需要转义：'this is a "fox"' 同样是有效的字符串字面量。

另外，数字字面量不必包含小数部分，可以像整数一样书写，例如使用 `1` 而不是 `1.0`。

数组字面量可以写成方括号包围的、由逗号分隔的表达式或范围列表。

    Array      = "[" [ ExpOrRange { "," ExpOrRange } ] "]"
    ExpOrRange = Expression | Range
    Range      = Expression ".." Expression

数组字面量是一个可以为空的表达式或范围列表，元素之间以逗号分隔。范围是一种表示整数值区间的特殊类型，由两个以两个点分隔的表达式构成。范围表示数字值列表，因此两个表达式都必须求值为可转换为 `Number` 的值。第一个表达式是范围起点，第二个是终点，两端都包含在内。因此，范围 `1.0..3.0` 表示列表 `1.0`、`2.0`、`3.0`。起点也可以大于终点，例如 `3.0..1.0` 表示与 `1.0..3.0` 相同的列表，但顺序相反。

下表给出一些有效数组字面量表达式的示例。

表达式       值
----------   -----
`[]`         空数组。
`[1,2,3]`    包含 `1.0`、`2.0` 和 `3.0` 的数组。
`[1..3]`     包含 `1.0`、`2.0` 和 `3.0` 的数组。
`[1,2,4..6]` 包含 `1.0`、`2.0`、`4.0`、`5.0` 和 `6.0` 的数组。
`[1+1,3.0]`  包含 `2.0` 和 `3.0` 的数组。
`[-5,-1]`    包含 `-5.0`、`-4.0`、……、`-1.0` 的数组。

映射是用大括号包围的、以逗号分隔的键值对列表。键可以是字符串或名称。如果键中包含特殊字符或空白，必须将其写成字符串。键和值之间使用冒号分隔。

    Map          = "{" [ KeyValuePair { "," KeyValuePair } ] "}"
    KeyValuePair = StringOrName ":" Expression
    StringOrName = String | Name

有效映射表达式示例如下：

    {
      "some key":  "a string",
      other_key:   1+2,
      another_key: [1..3]
    }

该表达式求值为一个映射：键 `some key` 的值为 `"a string"`，键 `other_key` 的值为 `3.0`，键 `another_key` 的值为包含 `1.0`、`2.0` 和 `3.0` 的数组。

### 下标 {#subscript}

某些特定值（例如字符串、数组或映射）可以通过下标访问其中的某些元素。

    Subscript     = SimpleTerm "[" ExpOrAnyRange { "," ExpOrAnyRange } "]"
    ExpOrAnyRange = ExpOrRange | AutoRange
    AutoRange     = ".." Expression | Expression ".."

下标表达式由两部分组成：被索引的表达式和索引表达式。前者可以是求值为 `String`、`Array` 或 `Map` 类型值的任意表达式，而后者是表达式或范围的列表。根据被加下标的表达式类型，仅允许某些特定值作为索引。以下各小节说明三种支持下标的类型所允许的索引值类型。

#### 字符串下标 {#subscripting-strings}

下表说明了允许的索引类型及其效果。

索引     效果
-----    ------
`Number` 返回包含指定索引处字符的字符串；如果索引越界，则返回空字符串。允许使用负索引。
`Array`  返回包含指定索引处字符的字符串。假定数组的所有元素均可转换为 `Number`。越界索引将被忽略，但允许使用负索引。

如果索引值为 `Number` 类型，它将向最接近 `0` 的整数舍入，即值 `1.7` 向下舍入为 `1`，而值 `-2.3` 向上舍入为 `-2`。字符串下标非常强大，因为它支持多个下标索引值甚至负索引。以下是一些使用字符串下标的示例。

    "This is a test."[0]  // "T"
    "This is a test."[1]  // "h"

多个索引或数组索引可用于提取子字符串。范围表达式是提取子字符串的一种更简短的方式。

    "This is a test."[0, 1, 2, 3] // "This"
    "This is a test."[0..3]       // "This"
    "This is a test."[5..6]       // "is"

你甚至可以在下标中使用多个范围表达式，并且还可以组合使用范围表达式与单个索引。

    "This is a test."[0..3, 5..6]    // "Thisis"
    "This is a test."[0..3, 5..6, 8] // "Thisisa"

负索引可用于提取字符串后缀。请注意，索引值 `-1` 访问数组的最后一个字符，值 `-2` 访问倒数第二个字符，依此类推。假定被加下标的字符串长度为 `7`，则值 `-7` 访问该字符串的第一个字符。

    "This is a test."[-1]     // "."
    "This is a test."[-5..-2] // "test"

你甚至可以使用下标和范围反转字符串。

    "This is a test."[14..0] // .tset a si sihT

自动范围（auto range）是仅在下标表达式中允许的特殊构造。自动范围是指未指定起始或结束的范围。自动范围中未指定的一端会自动替换为字符串长度减一。

    "This is a test."[..0] // .tset a si sihT
    "This is a test."[5..] // "is a test."

#### 数组下标 {#subscripting-arrays}

下表说明了允许的索引类型及其效果。

索引     效果
-----    ------
`Number` 返回指定索引处的值。如果索引越界则抛出错误。允许使用负索引。应用与字符串下标相同的舍入规则。
`Array`  返回包含指定索引处值的数组。假定索引数组的所有元素均可转换为 `Number`。如果索引数组包含越界的索引，则抛出错误。允许使用负索引。

就像字符串下标一样，数组下标非常强大，因为它支持多个下标索引值甚至负索引。对于以下示例，假定变量 `arr` 为以下数组：

    [ 7, 8, 9, "test", [ 10, 11, 12 ] ]

使用整数索引的简单下标行为符合预期：

    arr[0] // 7
    arr[3] // "test"
    arr[4] // [10, 11, 12]

多个索引或数组索引可用于提取子数组。范围表达式是提取子数组的一种更简短的方式。

    arr[0, 1, 2, 3] // [ 7, 9, 9, "test" ]
    arr[0..3]       // [ 7, 9, 9, "test" ]
    arr[3..4]       // [ "test", [ 10, 11, 12 ] ]

你甚至可以在下标中使用多个范围表达式，并且还可以组合使用范围表达式与单个索引。

    arr[0..1, 3..4] // [ 7, 8, "test", [ 10, 11, 12 ] ]
    arr[0..3, 4]    // [ 7, 8, 9, "test", [ 10, 11, 12 ] ]

负索引可用于提取数组后缀。请注意，索引值 `-1` 访问数组的最后一个元素，值 `-2` 访问倒数第二个元素，依此类推。假定被加下标的数组长度为 `7`，则值 `-7` 访问该数组的第一个元素。

    arr[-2]     // "test"
    arr[-2..-1] // [ "test", [ 10, 11, 12 ] ]

你甚至可以使用下标和范围反转数组。

    arr[4..0] // [ [ 10, 11, 12 ], "test", 9, 8, 7 ]

自动范围是仅在下标表达式中允许的特殊构造。自动范围是指未指定起始或结束的范围。自动范围中未指定的一端会自动替换为数组长度减一。

    arr[..0] // [ [ 10, 11, 12 ], "test", 9, 8, 7 ]
    arr[3..] // [ "test", [ 10, 11, 12 ] ]

由于数组可以包含其他可加下标的值（如字符串、数组和映射），你可以使用多个下标表达式来访问嵌套元素。

    arr[3][2..3] // "st"
    arr[4][1]    // 11

#### 映射下标 {#subscripting-maps}

下表说明了允许的索引类型及其效果。

索引     效果
-----    ------
`String` 返回给定键对应的值；如果被索引的映射不包含给定键，则返回特殊值 `undefined`。
`Array`  返回包含具有给定键的键值对的映射。假定索引数组的所有元素均为 `String` 类型。被索引映射中不存在的键将被忽略。

对于以下示例，假定变量 `map` 的值为以下映射：

    {
      "some number": 1.0,
      "some string": "test",
      "some array" : [ 1, 2, 3, 4 ],
      "some map"   : { "key1": 5, "key2": "asdf" }
    }

我们从使用字符串的简单索引开始：

    map["some number"] // 1.0
    map["some array"]  // [ 1, 2, 3, 4 ]
    map["missing key"] // undefined

多个索引或数组索引可用于提取子映射。范围表达式不适用于映射下标，因为范围生成数字列表，而映射要求索引值为 `String` 类型。映射中不存在的索引值将被忽略。

    map["some number", "some string"] // { "some number": 1.0, "some string": "test" }
    map["some number", "missing"]     // { "some number": 1.0 }

与数组类似，映射也可以包含其他可加下标的值（如字符串、数组和映射）。你可以使用多个下标表达式访问嵌套元素。

    map["some array"][1]          // 2
    map["some map"]["key2"]       // "asdf"
    map["some map"]["key2"][1..3] // "ey2"

### 一元运算符项 {#unary-operator-terms}

一元运算符是作用于单个操作数的运算符。在 TrenchBroom 的表达式语言中，有四个一元运算符：一元加、一元减、逻辑非和按位取反。

    Plus            = "+" SimpleTerm
    Minus           = "-" SimpleTerm
    LogicalNegation = "!" SimpleTerm
    BinaryNegation  = "~" SimpleTerm

下表说明了根据值的类型对值应用一元运算符的效果。

-------------------------------------------------------------------------------------------------------
运算符           `Boolean`         `String`     `Number`     `Array` `Map`   `Range` `Null`  `Undefined`
--------          ----              ----         ----         ----    ----    ----    ----    ----
`Plus`            转换为数字        见下文       无效果       错误    错误    错误    错误    错误

`Minus`           转换为数字        见下文       值取反       错误    错误    错误    错误    错误
                  and negate value

`LogicalNegation` 值取反            错误         错误         错误    错误    错误    错误    错误

`BinaryNegation`  错误              见下文       按位取反     错误    错误    错误    错误    错误
-------------------------------------------------------------------------------------------------------

关于对 `String` 类型的值应用一元运算符的说明：除 `LogicalNegation` 外的每个运算符都会在可能的情况下尝试将 `String` 类型的值转换为数字。

以下是一些使用一元运算符的示例。

    +1.0   //  1.0
    -1.0   // -1.0
    -'1'   // -1.0
    +true  //  1.0
    -true  // -1.0
    !true  // false
    !false // true
    ~1     // -2
    ~-2    // 1
    ~'-2'  // 1

### 二元运算符项 {#binary-operator-terms}

二元运算符是接受两个操作数的运算符。二元运算符以中缀记法指定，即先指定第一个操作数，然后指定运算符符号，最后指定第二个操作数。请注意，在以下二元运算符的 EBNF 记法中，第二个操作数始终是表达式。

#### 代数项 {#algebraic-terms}

代数项是使用二元运算符 `+`、`-`、`*`、`/` 或 `%` 的项。

    Addition       = SimpleTerm "+" Expression
    Subtraction    = SimpleTerm "-" Expression
    Multiplication = SimpleTerm "*" Expression
    Division       = SimpleTerm "/" Expression
    Modulus        = SimpleTerm "%" Expression

所有这些运算符都可以应用于 `Boolean` 或 `Number` 类型的操作数。如果操作数是 `Boolean` 类型，则在应用运算前会将其转换为 `Number` 类型。

如果其中一个操作数是 `Boolean` 或 `Number` 类型，而另一个操作数是 `String` 类型且其值可以转换为数字，则可以应用该运算符，并且 `String` 类型的操作数也会转换为 `Number` 类型。

    "1.23" + 1 // 2.23
    1.23 + "1" // 2.23
    "1" + "2"  // 12, see below

此外，如果两个操作数都是 `String` 类型、都是 `Array` 类型，或者都是 `Map` 类型，也可以应用 `+` 运算符。

    "This is" + " " + "test." // "This is a test."
    [ 1, 2, 3 ] + [ 3, 4, 5 ] // [ 1, 2, 3, 3, 4, 5 ]

在前面的两个示例中，操作数只是被简单地拼接在一起。然而，如果两个操作数都是 `Map` 类型，这两个映射将被合并，即重复的键将被第二个操作数中的值覆盖：

    { 'k1': 1, 'k2': 2, 'k3': 3 } + { 'k3': 4, 'k4': 5 } // { 'k1': 1, 'k2': 2, 'k3': 4, 'k4': 5 }

请注意，键 `'k3'` 下的值是 `4` 而不是 `3`！

#### 逻辑项 {#logical-terms}

当两个操作数都是 `Boolean` 类型时，可以应用逻辑项。如果其中一个操作数不是 `Boolean` 类型，则会抛出错误。

    LogicalAnd = SimpleTerm "&&" Expression
    LogicalOr  = SimpleTerm "||" Expression

下表展示了应用逻辑运算符的效果。

左侧     右侧    &&      ||
-------- ------- ----    ----
`true`   `true`  `true`  `true`
`true`   `false` `false` `true`
`false`  `true`  `false` `true`
`false`  `false` `false` `false`

#### 位运算项 {#binary-terms}

位运算项操作 `Number` 类型操作数的位表示。请注意，由于操作浮点数的位表示没有太大意义，操作数会先通过忽略其小数部分转换为整数表示。如果任一操作数不是 `Number` 类型，则根据[类型转换规则](#el_type_conversion)将该操作数转换为 `Number` 类型。

    BinaryAnd        = SimpleTerm "&" SimpleTerm
    BinaryXor        = SimpleTerm "|" SimpleTerm
    BinaryOr         = SimpleTerm "^" SimpleTerm
    BinaryShiftLeft  = SimpleTerm "<<" SimpleTerm
    BinaryShiftRight = SimpleTerm ">>" SimpleTerm

以下是一些使用运算符的示例：

    1 & 0  // 0
    1 | 0  // 1
    3 & 1  // 1
    2 | 1  // 3
    1 ^ 1  // 0
    1 ^ 0  // 1
    3 ^ 1  // 2
    1 << 1 // 2
    2 >> 1 // 1

#### 比较项 {#comparison-terms}

比较运算符始终根据比较结果返回布尔值。

    Less           = SimpleTerm "<"  Expression
    LessOrEqual    = SimpleTerm "<=" Expression
    Equal          = SimpleTerm "==" Expression
    InEqual        = SimpleTerm "!=" Expression
    GreaterOrEqual = SimpleTerm ">=" Expression
    Greater        = SimpleTerm ">"  Expression

 左侧        右侧        效果
------      -------     ------
`Boolean`   `Boolean`   `true` 大于 `false`
`Boolean`   `Number`    将右侧转换为 `Boolean` 并进行比较。
`Boolean`   `String`    将右侧转换为 `Boolean` 并进行比较。
`Boolean`   `Array`     错误
`Boolean`   `Map`       错误
`Boolean`   `Range`     错误
`Boolean`   `Null`      左侧大于右侧。
`Boolean`   `Undefined` 左侧大于右侧。
`Number`    `Boolean`   将左侧转换为 `Boolean` 并进行比较。
`Number`    `Number`    作为数字进行比较。
`Number`    `String`    将右侧转换为 `Number` 并进行比较。
`Number`    `Array`     错误
`Number`    `Map`       错误
`Number`    `Range`     错误
`Number`    `Null`      左侧大于右侧。
`Number`    `Undefined` 左侧大于右侧。
`String`    `Boolean`   将左侧转换为 `Boolean` 并进行比较。
`String`    `Number`    将左侧转换为 `Number` 并进行比较。
`String`    `String`    按字典序比较（区分大小写）。
`String`    `Array`     错误
`String`    `Map`       错误
`String`    `Range`     错误
`String`    `Null`      左侧大于右侧。
`String`    `Undefined` 左侧大于右侧。
`Array`     `Boolean`   错误
`Array`     `Number`    错误
`Array`     `String`    错误
`Array`     `Array`     按字典序比较。
`Array`     `Map`       错误
`Array`     `Range`     错误
`Array`     `Null`      左侧大于右侧。
`Array`     `Undefined` 左侧大于右侧。
`Map`       `Boolean`   错误
`Map`       `Number`    错误
`Map`       `String`    错误
`Map`       `Array`     错误
`Map`       `Map`       按字典序比较键值对（先比较键，再比较值）。
`Map`       `Range`     错误
`Map`       `Null`      左侧大于右侧。
`Map`       `Undefined` 左侧大于右侧。
`Range`     任意类型    错误
`Null`      `Null`      两者相等。
`Null`      `Undefined` 两者相等。
`Null`      任意类型    右侧大于左侧。
`Undefined` `Null`      两者相等。
`Undefined` `Undefined` 两者相等。
`Undefined` 任意类型    右侧大于左侧。

以下示例展示了比较运算符在不同操作数类型下的运行情况。除非注释中另有说明，假定所有表达式的求值结果均为 `true`。

    true > false
    true == true
    false == false

    true == "true"
    true == "True"
    true == "asdf"
    true != ""
    true != "false"
    true == "False"
    true == 1
    true == 2
    true != 0

    1 == "1"
    1 == "1.0"
    1 < "2"
    1 == "asdf" // throws an error because "asdf" cannot be converted to Number

    "asdf" == "asdf"
    "asdf" < "bsdf"

    null == null
    null == undefined
    null < -1
    null < "asdf"

    [ 1, 2, 3 ] == [ 1, 2, 3 ]
    [ 1, 2, 3 ] <  [ 2, 2, 3 ]
    [ 1, 2 ]    <  [ 1, 2, 3 ]

#### 条件项 {#case-term}

条件运算符允许对表达式进行条件求值。这通常与下一小节中介绍的分支运算符结合使用时最为有用。

     Case = SimpleTerm "->" Expression

在条件表达式中，`->` 运算符前面的部分称为*前提*（premise），后面的部分称为*结论*（conclusion）。条件运算符的求值方式如下：

- 如果前提求值为可转换为 `boolean` 的值 `r`：
    - If `r` converts to `true`:
        - The result of the case expression is the result of evaluating the conclusion.
    - Otherwise, the result of the case expression is `undefined`.
- 否则，抛出错误。

以下示例演示了条件运算符的语义：

    true   -> false  // false
    false  -> true   // undefined
    1      -> "test" // "test", because 1 converts to true
    0      -> "test" // undefined, because 0 converts to false
    "true" -> ""     // "", because "true" converts to true
    ""     -> ""     // undefined, because "" converts to false

#### 分支项 {#switch-term}

分支运算符由零个或多个子表达式组成，对其求值将返回第一个求值结果不为 `undefined` 的表达式的结果。与条件运算符结合使用时，它实现了一个分段定义的函数。

    Switch = "{{" [ Expression { "," Expression } ] "}}"

以下示例演示了分支项非常简单的 `if / then / else` 用法。

    {{
      x == 0 -> 'x equals 0',
      x == 1 -> 'x equals 1'
    }}

如果变量 `x` 的值等于 `0`，则该表达式求值为字符串 `'x equals 0'`；如果变量 `x` 的值等于 `1`，则求值为字符串 `'x equals 1'`。在所有其他情况下，分支表达式求值为 `undefined`。

但是如果我们想为所有其他情况提供一个默认结果呢？使用分支表达式很容易实现：

    {{
      x == 0 -> 'x equals 0',
      x == 1 -> 'x equals 1',
      true   -> 'otherwise'   // the default case
    }}

不过，由于分支表达式的子表达式求值方式，我们可以简写默认情况：

    {{
      x == 0 -> 'x equals 0',
      x == 1 -> 'x equals 1',
                'otherwise'   // the default case
    }}

请记住，分支表达式将返回第一个求值结果不为 `undefined` 的表达式的值。由于前两个子表达式的求值结果为 `undefined`，而字符串 `'otherwise'` 不是 `undefined`，因此分支表达式将返回 `'otherwise'` 作为其结果。

#### 二元运算符优先级 {#binary-operator-precedence}

由于表达式可以是二元运算符的另一个实例，你可以简单地链接二元运算符并写成 `1 + 2 + 3`。在这种情况下，相同优先级的运算符将从左到右求值。下表说明了可用二元运算符的优先级。表中数字越大表示优先级越高。

运算符名称                优先级
----     ----                ----
`*`      乘法                12
`/`      除法                12
`%`      取模                12
`+`      加法                11
`-`      减法                11
`<<`     按位左移            10
`>>`     按位右移            10
`<`      小于                9
`<=`     小于或等于          9
`>`      大于                9
`>=`     大于或等于          9
`==`     等于                8
`!=`     不等于              8
`&`      按位与              7
`^`      按位异或            6
`|`      按位或              5
`&&`     逻辑与              4
`||`     逻辑或              3
`..`     范围                2
`->`     条件                1
` `      其他运算符          13

一些示例：

    2 * 3 + 4       // 10 because * has a higher precedence than +
    7 < 10 && 8 < 3 // comparisons are evaluated before the logical and operator

如果内置优先级不符合你的意图，你可以使用圆括号强制先对某个运算符求值。

    2 * (3 + 4) // 14

### 终结符 {#terminals}

在 EBNF 中，终结符规则是指右侧仅包含终结符符号的规则。如果符号用双引号括起来，则该符号是终结符。请注意，对于 `Char` 规则，我们选择不枚举所有实际的 ASCII 字符，而是使用了一个占位符字符串。

    Alpha   = "a" | "b" | ... "z" | "A" | "B" | ... "Z"
    Numeric = "0" | "1" | ... "9"
    Char    = Any ASCII character

TrenchBroom 表达式语言手册到此结束。

## 游戏配置文件 {#game_configuration_files}

TrenchBroom 使用游戏配置文件来提供对不同游戏的支持。部分游戏配置文件随编辑器附带提供。它们安装在 `<ResourcePath>/games`，其中 `<ResourcePath>` 的值根据下表取决于平台。

平台      位置
--------  --------
Windows   TrenchBroom 可执行文件所在的目录。
macOS     `TrenchBroom.app/Contents/Resources`
Linux     `<prefix>/share/trenchbroom`，其中 `<prefix>` 是安装前缀。

文件夹 `<ResourcePath>/games` 包含每个受支持游戏的 `.cfg` 文件，以及可包含与游戏相关的其他资源（如图标、调色板或实体定义文件）的附加文件夹。

不建议修改这些内置的游戏配置，因为在安装更新时它们将被覆盖。要修改现有游戏配置或添加新配置，可以将它们放置在文件夹 `<UserDataPath>/games` 中，其中 `<UserDataPath>` 的值同样取决于平台。

平台      位置
--------  --------
Windows   `C:\Users\<username>\AppData\Roaming\TrenchBroom`
macOS     `~/Library/Application Support/TrenchBroom`
Linux     `~/.TrenchBroom`

使用 `--portable` 参数运行 TrenchBroom 则会将 `<UserDataPath>` 放置在当前目录中。这适用于在 `<ResourcePath>` 目录内运行，以提供完全自包含的应用程序实例。

要向 TrenchBroom 添加新的游戏配置，请将其放入 `<UserDataPath>/games` 下的文件夹中——请注意，如果该文件夹不存在，你可能需要创建它。你需要编写自己的 `GameConfig.cfg` 文件，或者可以复制某个内置文件并以此为基础构建你的游戏配置。此外，你可以在创建的文件夹中放置其他资源。例如，假设你想为名为“Example”的游戏添加游戏配置。为此，你需要创建一个新文件夹 `<UserDataPath>/games/Example`，并在该文件夹内创建一个名为 `GameConfig.cfg` 的游戏配置文件。如果需要其他资源（如图标或实体定义文件），你也可以将这些文件放入这个新创建的文件夹中。

你还可以使用[游戏配置对话框](#game_configuration)中游戏列表下方的文件夹图标按钮访问此目录。

要覆盖内置游戏配置文件，请复制包含内置文件的文件夹并将其放入 `<UserDataPath>/games` 中。TrenchBroom 会优先使用你的自定义游戏配置而不是内置文件，但你仍然可以毫无问题地访问游戏资源子文件夹中的资源。如果需要，你还可以通过在游戏资源子目录中放置同名文件来覆盖其中某些资源。

例如，假设你想覆盖内置的 Quake 游戏配置和 Quake 的内置实体定义文件。将文件 `<ResourcePath>/games/Quake/GameConfig.cfg` 复制到 `<UserDataPath>/games/Quake` 并根据需要进行修改。然后将文件 `<ResourcePath>/games/Quake/Quake.fgd` 复制到 `<UserDataPath>/games/Quake` 并同样进行修改。当你在 TrenchBroom 中加载该游戏配置时，编辑器将采用修改后的文件而不是内置文件。

### 游戏配置文件语法 {#game-configuration-file-syntax}

游戏配置文件需要指定以下信息。

* **名称**（name）：用于在 UI 中显示，并用于在游戏配置文件夹的子文件夹中查找资源
* **图标**（icon）：用于在 UI 中显示（可选）
* **文件格式**（file formats）：用于标识该游戏支持的地图文件格式
* **文件系统**（Filesystem）：用于指定游戏资产搜索路径和包文件格式（例如 pak 文件）
* **材质**（Textures）
  * **文件扩展名**列表（如 `.jpg`）
  * **调色板文件**（可选）
  * 用于在地图文件中存储材质包的 **worldspawn 属性**
  * 用于隐藏匹配任一模式的材质的**排除模式**列表
* **实体**（Entities）
  * 内置**实体定义文件**
  * UI 中使用的**默认颜色**
  * 默认**模型缩放表达式**
  * 是否自动设置默认实体属性
* **标签**（Tags）：用于在编辑器中向面或 Brush 附加附加信息，例如面是 detail 还是 hint（可选）
* **面属性**（Face attributes）：用于指定 Brush 面上允许的附加属性（可选）
* **地图边界**（Map bounds）：在 2D 视口中显示（可选）
* **编译工具**（Compilation tools）：可由用户配置其路径（可选）

游戏配置是一个具有特定结构的[表达式语言](#expression_language)映射，下面通过一个示例进行说明。

    {
      "version": 4, // mandatory, indicates the version of the file's syntax
      "name": "Example Resembling Quake 2", // mandatory, the name to use in the UI
      "icon": "Icon.png", // optional, the icon to show in the UI
      "fileformats": [ // supported file formats, each with optional initial map to use as "new map"
        { "format": "Quake2" },
        { "format": "Quake2 (Valve)", "initialmap": "initial_valve.map" }
      ],
      "filesystem": { // defines the file system used to search for game assets
        "searchpath": "baseq2", // the path in the game folder at which to search for assets
        "packageformat": { "extension": ".pak", "format": "idpak" } // the package file format
      },
      "textures": { // where to search for textures and how to read them, see below
        "root": "textures",
        "extensions": [ ".wal" ],
        "palette": "pics/colormap.pcx",
      },
      "entities": { // the builtin entity definition files for this game
        "definitions": [ "Quake2.fgd" ],
        "defaultcolor": "0.6 0.6 0.6 1.0",
        "scale": [ modelscale, modelscale_vec ]
      },
      "tags": { // "smart tags" select or modify a brush/face based on its characteristics
        "brush": [
          {
            "name": "Trigger",
            "attribs": [ "transparent" ],
            "match": "classname",
            "pattern": "trigger*",
            "texture": "trigger"
          }
        ],
        "brushface": [
          {
            "name": "Clip",
            "attribs": [ "transparent" ],
            "match": "texture",
            "pattern": "clip"
          },
          {
            "name": "Liquid",
            "match": "contentflag",
            "flags": [ "lava", "water" ]
          },
          {
            "name": "Transparent",
            "attribs": [ "transparent" ],
            "match": "surfaceflag",
            "flags": [ "trans33" ]
          }
        ]
      },
      "faceattribs": { // bitflags assigned to a face to affect its behavior
        "surfaceflags": [
          {
            "name": "light",
            "description": "Emit light from the surface, brightness is specified in the 'value' field"
          },
          {
            "name": "trans33",
            "description": "The surface is 33% transparent"
          },
          {
            "name": "hint",
            "description": "Make a primary bsp splitter"
          }
        ],
        "contentflags": [
          {
            "name": "solid",
            "description": "Default for all brushes"
          },
          {
            "name": "lava",
            "description": "The brush is lava"
          },
          {
            "unused": true
          },
          {
            "name": "water",
            "description": "The brush is water"
          }
        ]
      },
      "softMapBounds":"-4096 -4096 -4096 4096 4096 4096",
      "compilationTools": [
        { "name": "bsp" },
        { "name": "vis" },
        { "name": "rad" }
      ]
    }

#### 版本 {#versions}

游戏配置文件带有版本控制。每当引入对游戏配置格式的不兼容修改时，版本号就会增加，并且 TrenchBroom 会拒绝旧格式并显示错误消息。

**当前版本**

TrenchBroom 目前支持游戏配置版本 9。

**版本历史**

* 版本 9
  - 适应术语变更：`texture` 重命名为 `material`
* 版本 8
  - 移除材质格式配置，仅保留要搜索的扩展名列表。
* 版本 7
  - 将材质包配置替换为根路径。移除了基于文件和基于目录的材质配置之间的区别。
* 版本 6
  - 向实体配置中添加了可选的 `setDefaultProperties` 键。
* 版本 5
  - 使模型格式白名单变为可选。如果配置文件中仍然存在白名单，它将被忽略。
* 版本 4
  - 添加了对表面标志和内容标志中 `unused` 键的支持；版本 3 中不存在此键。
  - 添加了对在 surfaceparm 类型的智能标签中为 `pattern` 键指定值列表的支持；版本 3 中仅允许单个值。
  - 添加了可选的 `softMapBounds` 键。
  - 添加了可选的 `compilationTools` 键。

**从版本 2 迁移**

版本 3 废弃了 `brushtypes` 键以支持 `tags` 键，但内容非常相似。`brushtypes` 键的值是类型匹配器数组。版本 2 中支持以下 Brush 类型匹配器：

匹配项       说明
-----        -----------
material     匹配材质名称，必须匹配所有 Brush 面
contentflag  匹配面内容标志（用于 Quake 2、Quake 3）
surfaceflag  匹配面表面标志（用于 Quake 2）
surfaceparm  匹配着色器表面参数（用于 Quake 3）
classname    匹配 Brush 实体类名

#### 文件格式 {#file-formats}

文件格式由 `fileformats` 键下的映射数组指定。支持以下格式。

格式             说明
------           -----------
Standard         标准 Quake 地图文件
Valve            Valve 地图文件（类似 Standard，但对 UV 贴图有更多控制）
Quake2           具有 Standard 样式材质信息的 Quake 2 地图文件
Quake2 (Valve)   具有 Valve 样式材质信息的 Quake 2 地图文件
Quake3 (legacy)  具有 Standard 样式材质信息的 Quake 3 地图文件
Quake3 (Valve)   具有 Valve 样式材质信息的 Quake 3 地图文件
Hexen2           Hexen 2 地图文件（类似 Quake，但每个面带有一个额外但未使用的值）

请注意，“Quake3”格式（将包含 Quake 3 Brush 图元支持）尚未完全实现，因此从上表中省略。“Quake3 (Valve)”格式在材质放置方面的表达能力与 Brush 图元格式相同，但“Quake3 (Valve)”不能用于读取包含 Brush 图元的现有地图文件。

数组的每个条目必须具有以下结构：

    {
      "format": "Standard",
      "initialmap: "initial_standard.map"
    }

其中，`format` 键是必填的，但 `initialmap` 键是可选的。`initialmap` 键引用游戏配置子文件夹中的地图文件，如果创建新文档，应加载该文件。如果未指定初始地图，或者找不到该文件，TrenchBroom 将创建一个在原点包含单个 Brush 的地图。

#### 文件系统 {#file-system}

文件系统在编辑器中用于加载游戏资产，由 `filesystem` 键下的映射指定。该映射包含两个键：`searchpath` 和 `packageformat`。

* `searchpath` 是游戏目录（在游戏偏好设置中设置）下的子目录，编辑器将在此处搜索游戏资产。编辑器将在此路径中搜索松散文件，但也会挂载在此处找到的资源包。
* `packageformat` 指定要挂载的资源包的格式。它是一个包含两个键（`extension` 和 `format`）的映射。
  * `extensions` 指定要挂载的包文件的文件扩展名（或者允许使用 `extension` 仅指定一个扩展名）
  * `format` 指定包文件的格式

支持以下包格式。

格式         说明
------       -----------
idpak        Id pak 文件
dkpak        Daikatana pak 文件
zip          Zip 文件，通常使用其他扩展名（如 pk3）

#### 材质配置 {#material-configurations}

每个材质配置包含一个根搜索目录，以及可选的包含文件扩展名列表、调色板路径、wad 文件列表属性和排除模式列表。

    "materials": {
      "root": "textures",
      "extensions": [ ".D" ],
      "palette": "pics/colormap.pcx",
      "attribute": "wad",
      "excludes": [ "*_norm", "*_gloss" ],
    },

`root` 键指定搜索材质包的文件夹。该文件夹相对于文件前面根据 `filesystem` 配置设置的游戏文件系统。TrenchBroom 将为此处指定的根文件夹中包含的每个文件夹创建一个材质集合。

对于 Quake 2，内置游戏配置将文件系统的搜索路径指定为 `"baseq2"`，材质包根目录指定为 `"textures"`，因此 TrenchBroom 将为 `<Game Path>/baseq2/textures` 中找到的每个文件夹创建一个材质集合。

TrenchBroom 支持多种图像格式，如 tga、pcx、jpeg 等。TrenchBroom 使用 [FreeImage Library] 加载这些图像，并支持该库支持的任何文件类型。

你还可以选择指定调色板。`palette` 键的值指定相对于文件系统的路径，TrenchBroom 将在该路径查找游戏资产随附的调色板文件。

`attribute` 键指定 worldspawn 属性的名称，TrenchBroom 将在地图文件中将 wad 文件存储在该属性中。

可选的 `excludes` 键指定与材质名称匹配的模式列表，这些模式将被忽略且不会显示在[材质浏览器](#material_browser)中。允许使用通配符 `*` 和 `?`。使用反斜杠转义字面量 `*` 和 `?` 字符。

    "materials": {
      "root": "textures",
      "extensions": [ "" ],
      "excludes": [ "*_norm", "*_gloss" ]
    },

#### 实体配置 {#game_configuration_files_entities}

在实体配置部分中，你可以指定游戏配置随附的实体定义文件、实体的默认颜色以及根据实体属性求值时产生默认缩放比例的表达式。

    "entities": { // the builtin entity definition files for this game
    "definitions": [ "Quake2/Quake2.fgd" ],
      "defaultcolor": "0.6 0.6 0.6 1.0",
      "scale": [ modelscale, modelscale_vec ],
      "setDefaultProperties": true
    },

`definitions` 键提供实体定义文件列表。这些文件由相对于 TrenchBroom 搜索游戏配置的 `games` 目录的路径指定。

`scale` 键包含一个根据实体属性求值以确定模型缩放比例的表达式。该表达式可以引用实体的任何属性，也可以提供固定值。

示例                                      说明
-------                                   -----------
`"scale": 2`                              固定的统一缩放系数 `2`。
`"scale": "1 2 3"`                        固定的非统一缩放系数，X 缩放 1，Y 缩放 2，Z 缩放 3。
`"scale": modelscale`                     使用实体的 `modelscale` 属性值。
`"scale": [ modelscale, modelscale_vec ]` 依次尝试数组中的各个值，直到找到一个求值结果不为 `Undefined` 或 `Null` 的值。

当然，对于更复杂的情况，你可以使用 switch 和 case 运算符。

可选的 `setDefaultProperties` 键控制在 TrenchBroom 创建新实体时是否自动实例化[默认实体属性](#entity_properties_defaults)。如果未设置，默认为 `false`。

#### 标签 {#game_configuration_files_tags}

TrenchBroom 可以识别某些特殊的 Brush 或面类型。例如 clip 面或 trigger Brush。但由于具体细节可能因游戏而异，这些特殊类型在游戏配置中定义。为了获得更大的灵活性和未来的增强功能，使用通用的“智能标签”（smart tags）系统来实现此功能。

TrenchBroom 使用这些标签定义自动将属性应用于匹配的 Brush/面&mdash;&mdash;例如将 trigger Brush 渲染为半透明&mdash;&mdash;并填充[视图菜单](#filtering_rendering_options)中提供的过滤选项。

每个智能标签定义还会提供一个或多个相关的[键盘快捷键](#keyboard_shortcuts)（通过“Tags”搜索快捷键将显示所有这些快捷键）。每个标签始终具有一个相关快捷键，可用于切换面与该标签匹配的 Brush 的可见性。还可以使用其他快捷键将标签的特征应用于当前选择，或移除这些特征。这些快捷键取决于标签的 `match` 条件，如下文所述。

标签在相应键下分别针对 Brush 和面进行指定：

    "tags": {
      "brush": [ ... ],
      "brushface": [ ... ]
    }

每个键都包含一个标签列表。每个标签如下所示。

    {
      "name": "Clip",
      "attribs": [ "transparent" ],
      "match": "material",
      "pattern": "clip"
    },

`attribs` 列表中当前唯一支持的属性类型是 "transparent"，如上所述，它将使匹配该标签的面在 3D 视口中以部分透明的方式渲染。

`match` 键指定 TrenchBroom 如何确定该标签是否适用于 Brush 或面。

对于 `brush` 智能标签，`match` 键只能具有 "classname" 值。除了用于视图过滤的常用键盘快捷键之外，此类智能标签还将生成键盘快捷键以应用标签（从选中的 Brush 创建 Brush 实体）或移除标签（将选中的 Brush 返回到 worldspawn）。可总结如下：

匹配项       说明                                   应用快捷键         移除快捷键
-----        -----------                            -----------------  ------------------
classname    匹配 Brush 实体类名                    有                 有

对于 `brushface` 智能标签，`match` 键可以具有以下值，并将相应地生成键盘快捷键以在所选面上应用或移除匹配条件：

匹配项       说明                                   应用快捷键         移除快捷键
-----        -----------                            -----------------  ------------------
material     匹配材质名称                           有                 无
contentflag  匹配面内容标志                         有                 有
surfaceflag  匹配面表面标志                         有                 有
surfaceparm  匹配着色器表面参数                     有                 无

根据 `match` 键的值，需要额外的键来配置匹配器。

* 对于 `classname` 匹配器，键 `pattern` 包含与包含该 Brush 的 Brush 实体的 classname 匹配的模式。允许使用通配符 `*` 和 `?`。使用反斜杠转义字面量 `*` 和 `?` 字符。
  - 此外，`classname` 匹配器还可以包含可选的 `material` 键。通过键盘快捷键应用该标签时，选中的 Brush 会获得以该键值命名的材质（例如，`"material": "trigger"` 会分配 `trigger` 材质）。
* 对于 `material` 匹配器，键 `pattern` 包含与面的材质名称匹配的模式。如果模式*不*包含斜杠，则它仅与材质名称中最后一个斜杠（如果有）之后的段进行匹配。允许使用通配符 `*` 和 `?`。使用反斜杠转义字面量 `*` 和 `?` 字符。
* 对于 `contentflag` 和 `surfaceflag` 匹配器，键 `flags` 包含要匹配的内容标志或表面标志名称列表（有关内容标志和表面标志的更多信息，请参见下文）。
* 对于 `surfaceparm` 匹配器，键 `pattern` 包含与面的着色器表面参数匹配的名称。不允许使用通配符；参数名称必须完全匹配。在游戏配置格式的版本 4 中，你可以为此值指定一个 surfaceparm 名称*列表*，如果着色器具有这些 surfaceparm 中的任何一个，则将与着色器匹配。

#### 面属性 {#face-attributes}

`faceattribs` 对象的主要部分是一组可用表面标志（通常影响单个面的外观/行为）和内容标志（通常影响包含该面的 Brush 的行为）的定义。这些定义通过 `surfaceflags` 和 `contentflags` 键指定。

这些键中每一个的值都是标志定义列表。请注意，每个标志定义在其列表中的位置决定了该标志的值。假定 `i` 是标志从 0 开始的列表索引；该标志的值对应于 2 的 `i` 次方。列表中的第一个标志的值为 2^0 = 1，第二个为 2^1 = 2，第三个为 2^2 = 4，依此类推。

请看以下示例：

    "faceattribs": {
      "surfaceflags": [
        {
          "name": "light",
          "description": "Emit light from the surface, brightness is specified in the 'value' field"
        }, // value 1
        {
          "name": "slick",
          "description": "The surface is slippery"
        } // value 2
      ],
      "contentflags": [
        {
          "name": "solid",
          "description": "Default for all brushes"
        }, // value 1
        {
          "name": "window",
          "description": "Brush is a window (not really used)"
        }, // value 2
        {
          "unused": true
        }, // value 4
        {
          "name": "playerclip",
          "description": "Player cannot pass through the brush (other things can)"
        }, // value 8
      ]
    }

有两个值为 1 和 2 的表面标志，以及三个有效的内容标志（值为 1、2 和 8）。请注意，contentflags 列表中的第三个元素标记为 unused，它只是一个占位符。这是必需的，以便下一个标志“playerclip”接收正确的值 8。

对于*未*标记为 unused 的任何标志定义，`name` 键是必填的，`description` 键是可选的。名称值将出现在标志编辑器 UI 中，描述（如果提供）将在将鼠标悬停在标志复选框上时在工具提示中可见。

请注意，在游戏配置格式的版本 4 之前，`unused` 键不适用于标志定义。在版本 3 中，你仍然可以使用按惯例仅命名为“unused”的占位符标志定义有效跳过未使用的标志值，例如上面 contentflags 列表中的第三个元素可以改为

    {
      "name": "unused"
    }, // value 4

但是，此方法会导致名为“unused”的标志出现在标志编辑器 UI 中。

`faceattribs` 对象可以包含另一个键 `defaults`。此键的值是一个对象，它定义了新创建 Brush 的面在[面属性编辑器](#face_attribute_editor)中看到的初始值。支持以下 `defaults` 键：

键               说明
------           -----------
offset           包含两个数字的列表，指定默认 X 和 Y 偏移
scale            包含两个数字的列表，指定默认 X 和 Y 缩放
rotation         指定默认旋转角度的数字
surfaceValue     指定默认表面值的数字（仅在存在 surfaceflags 时适用）
surfaceFlags     命名默认表面标志的字符串列表
surfaceContents  命名默认内容标志的字符串列表
color            指定默认表面颜色的字符串（仅适用于 Daikatana）

列表值必须以数组格式给出，例如沿每个轴的默认缩放 0.5 将在 `defaults` 对象中指定为：`"scale": [0.5, 0.5]`

为 `surfaceFlags` 或 `surfaceContents` 指定的标志名称必须分别对应于在 `faceattribs` 对象的 `surfaceflags` 或 `contentflags` 中定义的现有标志的 `name` 值。

`color` 值必须是形式为 "R G B" 或 "R G B A" 的字符串。R、G、B 和 A 分别是 0.0 到 1.0 的浮点数。如果省略 A，则假定为 1.0。

#### 地图边界 {#map-bounds}

可选的 `softMapBounds` 键定义了在 2D 视口中绘制的默认[地图边界](#map_bounds)。其值是一个包含两个对角点坐标的字符串，用于定义边界所包围的体积。此处的示例定义了从点 (-4096, -4096, -4096) 到点 (4096, 4096, 4096) 的立方体：

    "softMapBounds":"-4096 -4096 -4096 4096 4096 4096",

#### 编译工具 {#compilation-tools}

可选的 `compilationTools` 列表标识将出现在[游戏配置对话框](#game_configuration)中的工具名称，允许用户将这些名称与工具可执行文件的路径相关联。该名称可以用作该游戏[编译配置](#compiling_maps)中的变量来表示关联的路径。

列表中的每个元素都是一个对象，必须包含 `name` 键，并且可以包含可选的 `description` 键（用于工具提示）。定义了两个工具的 Quake 3 游戏配置示例如下：

    "compilationTools": [
      { "name": "q3map2", "description": "Path to your q3map2 executable, which performs the main bsp/vis/light compilation phases" },
      { "name": "bspc", "description": "Path to your bspc or mbspc executable, which creates .aas files for bot support" }
    ]

## 查询语言 {#query_language}

[搜索框](#searching)支持普通文本或[表达式](#expression_language)。文本搜索忽略大小写，匹配名称、实体类名、属性键和值、材质、标签及所属图层或组名称。通配符 `*`、`?` 和 `%` 匹配整个值。文本搜索选择对象，不选择单独的面。

单独的名称或字面量视为文本。`visible` 和 `locked` 字段是例外，可独立作为查询；若要搜索这两个单词本身，请加引号。无效表达式或未知字段、函数名视为文本。查询仅选择求值结果为 `true` 的项，求值错误不会匹配。

### 字段 {#query_fields}

字段 | 含义
--- | ---
`type` | 对象类型：世界、图层、组、实体、刷子、曲面、面。
`name` | 图层或组名称。
`classname`, `properties` | 实体类名及属性映射。
`entity` | 刷子、曲面或面所属的实体。
`materials`, `material` | 刷子、曲面的材质，或单个面的材质。
`normal` | 面法线。
`tags` | 匹配的智能标签名称。
`bounds`, `center` | 包围盒及其中心。
`layerName`, `groupName` | 所属图层及最内层组。
`visible`, `locked` | 可见性及锁定状态。

对象不具备的字段求值为 `undefined`。属性值是字符串，但与数字比较时会转为数字。相等比较区分大小写，`like` 忽略大小写。函数形式运算符的优先级高于普通运算符，对比较取反时请使用括号。

### 搜索范围 {#query_domain}

字段将搜索限制在具备该字段的对象类型中，类型条件会进一步缩小范围。逻辑与采用范围的交集，逻辑或采用并集，并且每个分支只对适用类型求值。仅当范围只含面时，查询才选择面；图层也要求范围只含图层。其他查询会排除面和图层，避免仅因图层包围盒与查询区域重叠就选中整个图层。

### 示例 {#query_examples}

查询 | 选择结果
--- | ---
`classname is "light"` | 灯光实体。
`classname like "monster_*"` | 怪物实体。
`classname is "light" && properties.light > 300` | 亮度大于 300 的灯光。
`properties contains "target"` | 具有目标属性的实体。
`type in [brush, patch]` | 刷子和曲面。
`materials like "*trigger*"` | 使用触发器材质的刷子或曲面。
`material like "sky*"` | 单独的天空面。
`type is layer && name like "Combat*"` | 名称匹配的图层内容。
`visible && !locked` | 可见且未锁定的对象。
`bounds intersects bbox(vec(-512, -512, 0), vec(512, 512, 256))` | 与指定包围盒相交的对象。
