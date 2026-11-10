# Game Configuration and Expressions {#game_config_and_expressions}

## Expression Language {#expression_language}

TrenchBroom contains a simple expression language that can be used to easily embed variables and more complex expressions into strings. Currently, the language is mainly used in the Compilation dialog and the Launch Engine dialog. In the following, we will introduce the syntax and the semantics of the expression language.

### Evaluation {#evaluation}

Every expression can be evaluated to a value. For example, the string `"This is a string."` is a valid expression that will be evaluated to a value of type `String` containing the string `This is a string.`. The expression language defines the following types.

Type       Description
----       -----------
Boolean    A value of this type can either be true or false.
String     A string of characters.
Number     A floating point number.
Array      An array is a list of values.
Map        A map is a list of key-value pairs. Synonyms: dictionary, table.
Range      The range type is only used internally.
Null       The type of `null` values.
Undefined  The type of undefined values.

#### Type Conversion {#el_type_conversion}

The following matrix describes the possible type conversions between these types. The first column contains the source type, while the following columns describe how a type conversion takes place, or if the result is an error. Note that the columns for types `Range`, `Null`, and `Undefined` are omitted because no type can be converted to these types (except for the trivial conversions). Converting a value of some type `X` to the same type is called _trivial_.

-----------------------------------------------------------------------------------------------------------------------------
            `Boolean`                     `String`               `Number`                      `Array`     `Map`
----        ----------------------------- ---------------------- ----------------------------- ----------- ---------
`Boolean`   _trivial_                     `"true"` or `"false"`  `1.0` or `0.0`                error       error

`String`    `false` if value is `"false"` _trivial_              `0.0` if blank, number        error       error
            or `""`, `true` otherwise                            representation if possible,
                                                                 error otherwise

`Number`    `false` if value is `0.0`,    string representation, _trivial_                     error       error
            `true` otherwise              e.g. "1.0"

`Array`     error                         error                  error                         _trivial_   error

`Map`       error                         error                  error                         error       _trivial_

`Range`     error                         error                  error                         error       error

`Null`      `false`                       `""` (empty string)    `0.0`                         empty array empty map

`Undefined` error                         error                  error                         error       error
-----------------------------------------------------------------------------------------------------------------------------

A string value can be converted to a number value if and only if the string is a number literal (see below). Conversely, any number can always be converted to a string value, and the number is formatted as follows. If the number is integer, then only the decimal part and no fractional part will be added to the string. If the number is not integer, the fractional part will be formatted with a precision of 17 places.

### Expressions and Terms {#expressions-and-terms}

Every expression is made of one single term. A term is something that can be evaluated, such as an addition (`7.0 + 3.0`) or a variable (which is then evaluated to its value).

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

### Names and Literals {#names-and-literals}

A name is a string that begins with an alphabetic character or an underscore, possibly followed by more alphanumeric characters and underscores.

    Name = ( "_" | Alpha ) { "_" | Alpha | Numeric }

`MODS`, `_var1`, `_123` are all valid names while `1_MODS`, `$MODS`, `_$MODS` are not. When an expression is evaluated, all variable names are simply replaced by the values of the variables they reference. If a value is not of type `String`, it will be converted to that type. If the value is not convertible to type `String`, then an error will be thrown.

A literal is either a string, a number, a boolean, an array, or a map literal.

    Literal = String | Number | Boolean | Array | Map

    Boolean = "true" | "false"
    String  = """ { Char } """ | "'" { Char } "'"
    Number  = Numeric { Numeric } [ "." Numeric { Numeric } ]

Note that strings can either be enclosed by double or single quotes, but you cannot mix these two styles. If you enclose a string by double quotes, you need to escape all literal double quotes within the string with backslashes like so: "this is a \\"fox\\"", but this is not necessary when using single quotes to enclose that string: 'this is a "fox"' is also a valid string literal.

Further note that number literals need not contain a fractional part and can be written like integers, i.e. `1` instead of `1.0`.

Array literals can be specified by giving a comma-separated list of expressions or ranges enclosed in brackets.

    Array      = "[" [ ExpOrRange { "," ExpOrRange } ] "]"
    ExpOrRange = Expression | Range
    Range      = Expression ".." Expression

An array literal is a possibly empty comma-separated list of expressions or ranges. A range is a special type that represents a range of integer values. Ranges are specified by two expressions separated by two dots. A range denotes a list of number values, so both expressions must evaluate to a value that is convertible to type `Number`. The first expression denotes the starting value of the range, and the second expression denotes the ending value of the range, both of which are inclusive. The range `1.0..3.0` therefore denotes the list `1.0`, `2.0`, `3.0`. Note that the starting value may also be greater than the ending value, e.g. `3.0..1.0`, which denotes the same list as `1.0..3.0`, but in the opposite order.

The following table gives some examples of valid array literal expressions.

Expression   Value
----------   -----
`[]`         An empty array.
`[1,2,3]`    An array containing the values `1.0`, `2.0`, and `3.0`.
`[1..3]`     An array containing the values `1.0`, `2.0`, and `3.0`.
`[1,2,4..6]` An array containing the values `1.0`, `2.0`, `4.0`, `5.0`, and `6.0`.
`[1+1,3.0]`  An array containing the values `2.0` and `3.0`.
`[-5,-1]`    An array containing the values `-5.0`, `-4.0`, ..., `-1.0`.

A map is a comma-separated list of of key-value pairs, enclosed in braces. Note that keys are strings or names. To use certain special characters or whitespace in the key, it must be given as a string. The value is separated from the key by a colon character.

    Map          = "{" [ KeyValuePair { "," KeyValuePair } ] "}"
    KeyValuePair = StringOrName ":" Expression
    StringOrName = String | Name

An example of a valid map expression looks as follows:

    {
      "some key":  "a string",
      other_key:   1+2,
      another_key: [1..3]
    }

This expression evaluates to a map containing the value `"a string"` under the key `some key`, the value `3.0` under the key `other_key`, and an array containing the values `1.0`, `2.0`, and `3.0` under the key `another_key`.

### Subscript {#subscript}

Certain values such as strings, arrays, or maps can be subscripted to access some of their elements.

    Subscript     = SimpleTerm "[" ExpOrAnyRange { "," ExpOrAnyRange } "]"
    ExpOrAnyRange = ExpOrRange | AutoRange
    AutoRange     = ".." Expression | Expression ".."

A subscript expression comprises of two parts: The expression that is being indexed and the indexing expression. The former can be any expression that evaluates to a value of type `String`, `Array` or `Map`, while the latter is a list of expressions or ranges. Depending of the type of the expression being subscripted, only certain values are allows as indices. The following sections explain which types of indexing values are permissible for the three subscriptable types.

#### Subscripting Strings {#subscripting-strings}

The following table explains the permissible indexing types and their effects.

Index    Effect
-----    ------
`Number` Returns a string containing the character at the specified index or the empty string if the index is out of bounds. Negative indices are allowed.
`Array`  Returns a string containing the characters at the specified indices. Assumes that all elements of the array are convertible to `Number`. Indices that are out of bounds are ignored, but negative indices are allowed.

If an index value is of type `Number`, it is rounded towards the closest integer towards `0`, that is, the value `1.7` is rounded down to `1`, while the value `-2.3` is rounded up to `-2`. String subscripts are very powerful because they allow multiple subscript index values and even negative indices. Here are some examples for using string subscripts.

    "This is a test."[0]  // "T"
    "This is a test."[1]  // "h"

Multiple indices, or array indices, can be used to extract substrings. Range expressions are a shorter way of extracting substrings.

    "This is a test."[0, 1, 2, 3] // "This"
    "This is a test."[0..3]       // "This"
    "This is a test."[5..6]       // "is"

You can even use multiple range expressions in a subscript, and you can combine range expressions and single indices, too.

    "This is a test."[0..3, 5..6]    // "Thisis"
    "This is a test."[0..3, 5..6, 8] // "Thisisa"

Negative indices can be used to extract a string suffix. Note that the index value `-1` accesses the last character of the array, the value `-2` accesses the last but one character, and so on. Assuming that the string that is being subscripted has a length of `7`, then the value `-7` accesses the string's first character.

    "This is a test."[-1]     // "."
    "This is a test."[-5..-2] // "test"

You can even reverse strings using subscripts and ranges.

    "This is a test."[14..0] // .tset a si sihT

Auto ranges are special constructs that are only permissible in subscript expressions. An auto range is a range where the start or end is unspecified. The unspecified side of an auto range is automatically replaced by the length of the string minus one.

    "This is a test."[..0] // .tset a si sihT
    "This is a test."[5..] // "is a test."

#### Subscripting Arrays {#subscripting-arrays}

The following table explains the permissible indexing types and their effects.

Index    Effect
-----    ------
`Number` Returns the value at the specified index. An error is thrown if the index is out of bounds. Negative indices are allowed. The same rounding rules as for string subscripts are applied.
`Array`  Returns an array containing the values at the specified indices. Assumes that all elements of the indexing array are convertible to `Number`. If the indexing array contains an index that is out of bounds, an error is thrown. Negative indices are allowed.

Just like string subscripts, array subscripts are very powerful because they allow multiple subscript index values and even negative indices. For the following examples, assume that the variable `arr` is the following array:

    [ 7, 8, 9, "test", [ 10, 11, 12 ] ]

Simple subscripting with integer indices behaves as expected:

    arr[0] // 7
    arr[3] // "test"
    arr[4] // [10, 11, 12]

Multiple indices, or array indices, can be used to extract sub arrays. Range expressions are a shorter way of extracting sub arrays.

    arr[0, 1, 2, 3] // [ 7, 9, 9, "test" ]
    arr[0..3]       // [ 7, 9, 9, "test" ]
    arr[3..4]       // [ "test", [ 10, 11, 12 ] ]

You can even use multiple range expressions in a subscript, and you can combine range expressions and single indices, too.

    arr[0..1, 3..4] // [ 7, 8, "test", [ 10, 11, 12 ] ]
    arr[0..3, 4]    // [ 7, 8, 9, "test", [ 10, 11, 12 ] ]

Negative indices can be used to extract an array suffix. Note that the index value `-1` accesses the last element of the array, the value `-2` accesses the last but one element, and so on. Assuming that the array that is being subscripted has a length of `7`, then the value `-7` accesses the array's first element.

    arr[-2]     // "test"
    arr[-2..-1] // [ "test", [ 10, 11, 12 ] ]

You can even reverse arrays using subscripts and ranges.

    arr[4..0] // [ [ 10, 11, 12 ], "test", 9, 8, 7 ]

Auto ranges are special constructs that are only permissible in subscript expressions. An auto range is a range where the start or end is unspecified. The unspecified side of an auto range is automatically replaced by the length of the array minus one.

    arr[..0] // [ [ 10, 11, 12 ], "test", 9, 8, 7 ]
    arr[3..] // [ "test", [ 10, 11, 12 ] ]

Since arrays can contain other subscriptable values such as strings, arrays, and maps, you can use multiple subscript expressions to access nested elements.

    arr[3][2..3] // "st"
    arr[4][1]    // 11

#### Subscripting Maps {#subscripting-maps}

The following table explains the permissible indexing types and their effects.

Index    Effect
-----    ------
`String` Returns the value under the given key or the special value `undefined` if the map being indexed does not contain the given key.
`Array`  Returns a map containing the key-value pairs with the given keys. Assumes that all elements of the indexing array are of type `String`. Keys that are not contained in the map being indexed are ignored.

For the following example, assume that the value of variable `map` is the following map:

    {
      "some number": 1.0,
      "some string": "test",
      "some array" : [ 1, 2, 3, 4 ],
      "some map"   : { "key1": 5, "key2": "asdf" }
    }

We begin with simple indexing using strings:

    map["some number"] // 1.0
    map["some array"]  // [ 1, 2, 3, 4 ]
    map["missing key"] // undefined

Multiple indices, or array indices, can be used to extract sub maps. Range expressions are not available for map subscripts because the generate lists of numbers and maps require the indexing values to be of type `String`. Indexing values that are not present in the map are ignored.

    map["some number", "some string"] // { "some number": 1.0, "some string": "test" }
    map["some number", "missing"]     // { "some number": 1.0 }

Like arrays, maps can contain other subscriptable values such as strings, arrays, and maps. You can use multiple subscript expressions to access nested elements.

    map["some array"][1]          // 2
    map["some map"]["key2"]       // "asdf"
    map["some map"]["key2"][1..3] // "ey2"

### Unary Operator Terms {#unary-operator-terms}

A unary operator is an operator that applies to a single operand. In TrenchBroom's expression language, there are four unary operators: unary plus, unary minus, logical negation, and binary negation.

    Plus            = "+" SimpleTerm
    Minus           = "-" SimpleTerm
    LogicalNegation = "!" SimpleTerm
    BinaryNegation  = "~" SimpleTerm

The following table explains the effects of applying the unary operators to values depending on the type of the values.

-------------------------------------------------------------------------------------------------------
Operator         `Boolean`         `String`     `Number`     `Array` `Map`   `Range` `Null`  `Undefined`
--------          ----              ----         ----         ----    ----    ----    ----    ----
`Plus`            convert to number see below    no effect    error   error   error   error   error

`Minus`           convert to number see below    negate value error   error   error   error   error
                  and negate value

`LogicalNegation` invert value      error        error        error   error   error   error   error

`BinaryNegation`  error             see below    invert bits  error   error   error   error   error
-------------------------------------------------------------------------------------------------------

Note on using applying a unary operator to a value of type `String`: Every operator except `LogicalNegation` will try to convert a value of type `String` to a number if possible.

Some examples of using unary operators follow.

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

### Binary Operator Terms {#binary-operator-terms}

A binary operator is an operator that takes two operands. Binary operators are specified in infix notation, that is, the first operator is specified first, then the operator symbol, and finally the second operator. Note that in the following EBNF notation for binary operators, the second operator is always an expression.

#### Algebraic Terms {#algebraic-terms}

Algebraic terms are terms that use the binary operators `+`, `-`, `*`, `/`, or `%`.

    Addition       = SimpleTerm "+" Expression
    Subtraction    = SimpleTerm "-" Expression
    Multiplication = SimpleTerm "*" Expression
    Division       = SimpleTerm "/" Expression
    Modulus        = SimpleTerm "%" Expression

All of these operators can be applied to operands of type `Boolean` or `Number`. If an operand is of type `Boolean`, it is converted to type `Number` before the operation is applied.

If one of the operators is of type `Boolean` or `Number` and the other operand is of type `String`, and its value can be converted to a number, then the operand can be applied, and the operand of type `String` is also converted to type `Number`.

    "1.23" + 1 // 2.23
    1.23 + "1" // 2.23
    "1" + "2"  // 12, see below

In addition, the `+` operator can be applied if both operands are of type `String`, if both are of type `Array`, or if both are of type `Map`.

    "This is" + " " + "test." // "This is a test."
    [ 1, 2, 3 ] + [ 3, 4, 5 ] // [ 1, 2, 3, 3, 4, 5 ]

In the previous two examples, the operands are simply concatenated. If both operands are of type `Map` however, the two maps are merged, that is, duplicate keys are overwritten by the values in the second operand:

    { 'k1': 1, 'k2': 2, 'k3': 3 } + { 'k3': 4, 'k4': 5 } // { 'k1': 1, 'k2': 2, 'k3': 4, 'k4': 5 }

Note that the value under key `'k3'` is `4` and not `3`!

#### Logical Terms {#logical-terms}

Logical terms can be applied to if both operands are of type `Boolean`. If one of the operands is not of type `Boolean`, an error is thrown.

    LogicalAnd = SimpleTerm "&&" Expression
    LogicalOr  = SimpleTerm "||" Expression

The following table shows the effects of applying the logical operators.

Left     Right   &&      ||
-------- ------- ----    ----
`true`   `true`  `true`  `true`
`true`   `false` `false` `true`
`false`  `true`  `false` `true`
`false`  `false` `false` `false`

#### Binary Terms {#binary-terms}

Binary terms manipulate the bit representation of operands of type `Number`. Note that, since manipulating the bit representation of a floating point number does not make much sense, the operands are converted to an integer representation first by omitting their fractional portion. If either of the operands is not of type `Number`, the operand is converted to type `Number` according to the [type conversion rules](#el_type_conversion).

    BinaryAnd        = SimpleTerm "&" SimpleTerm
    BinaryXor        = SimpleTerm "|" SimpleTerm
    BinaryOr         = SimpleTerm "^" SimpleTerm
    BinaryShiftLeft  = SimpleTerm "<<" SimpleTerm
    BinaryShiftRight = SimpleTerm ">>" SimpleTerm

Here are some examples of the operators in use:

    1 & 0  // 0
    1 | 0  // 1
    3 & 1  // 1
    2 | 1  // 3
    1 ^ 1  // 0
    1 ^ 0  // 1
    3 ^ 1  // 2
    1 << 1 // 2
    2 >> 1 // 1

#### Comparison Terms {#comparison-terms}

Comparison operators always return a boolean value depending on the result of the comparison.

    Less           = SimpleTerm "<"  Expression
    LessOrEqual    = SimpleTerm "<=" Expression
    Equal          = SimpleTerm "==" Expression
    InEqual        = SimpleTerm "!=" Expression
    GreaterOrEqual = SimpleTerm ">=" Expression
    Greater        = SimpleTerm ">"  Expression

 Left        Right      Effect
------      -------     ------
`Boolean`   `Boolean`   `true` is greater than `false`
`Boolean`   `Number`    Convert right to `Boolean` and compare.
`Boolean`   `String`    Convert right to `Boolean` and compare.
`Boolean`   `Array`     error
`Boolean`   `Map`       error
`Boolean`   `Range`     error
`Boolean`   `Null`      Left is greater than right.
`Boolean`   `Undefined` Left is greater than right.
`Number`    `Boolean`   Convert left to `Boolean` and compare.
`Number`    `Number`    Compare as numbers.
`Number`    `String`    Convert right to `Number` and compare.
`Number`    `Array`     error
`Number`    `Map`       error
`Number`    `Range`     error
`Number`    `Null`      Left is greater than right.
`Number`    `Undefined` Left is greater than right.
`String`    `Boolean`   Convert left to `Boolean` and compare.
`String`    `Number`    Convert left to `Number` and compare.
`String`    `String`    Compare lexicographically (case sensitive).
`String`    `Array`     error
`String`    `Map`       error
`String`    `Range`     error
`String`    `Null`      Left is greater than right.
`String`    `Undefined` Left is greater than right.
`Array`     `Boolean`   error
`Array`     `Number`    error
`Array`     `String`    error
`Array`     `Array`     Compare lexicographically.
`Array`     `Map`       error
`Array`     `Range`     error
`Array`     `Null`      Left is greater than right.
`Array`     `Undefined` Left is greater than right.
`Map`       `Boolean`   error
`Map`       `Number`    error
`Map`       `String`    error
`Map`       `Array`     error
`Map`       `Map`       Compare key-value pairs lexicographically (key first, then value).
`Map`       `Range`     error
`Map`       `Null`      Left is greater than right.
`Map`       `Undefined` Left is greater than right.
`Range`     Any type    error
`Null`      `Null`      Both are equal.
`Null`      `Undefined` Both are equal
`Null`      Any type    Right is greater than left.
`Undefined` `Null`      Both are equal.
`Undefined` `Undefined` Both are equal
`Undefined` Any type    Right is greater than left.

The following examples show the comparison operators in action with different operand types. Assume that all expressions evaluate to `true` unless otherwise stated in comments.

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

#### Case Term {#case-term}

The case operator allows for conditional evaluation of expressions. This is usually most useful in combination with the switch operator, which is explained in the next subsection.

     Case = SimpleTerm "->" Expression

In a case expression, the part before the `->` operator is called the _premise_ and the part after it is called the _conclusion_. The case operator is evaluated as follows:

- If the premise evaluates to a value `r` that is convertible to `boolean`:
    - If `r` converts to `true`:
        - The result of the case expression is the result of evaluating the conclusion.
    - Otherwise, the result of the case expression is `undefined`.
- Otherwise, an error is thrown.

The following examples demonstrate the semantics of the case operator:

    true   -> false  // false
    false  -> true   // undefined
    1      -> "test" // "test", because 1 converts to true
    0      -> "test" // undefined, because 0 converts to false
    "true" -> ""     // "", because "true" converts to true
    ""     -> ""     // undefined, because "" converts to false

#### Switch Term {#switch-term}

The switch operator comprises of zero or more sub expressions and its evaluation returns the result of the first expression that does not evaluate to `undefined`. In combination with the case operator, it implements a piecewise defined function.

    Switch = "{{" [ Expression { "," Expression } ] "}}"

The following example demonstrates a very simple `if / then / else` use of the switch term.

    {{
      x == 0 -> 'x equals 0',
      x == 1 -> 'x equals 1'
    }}

This expression evaluates to the string `'x equals 0'` if the value of the variable `x` equals `0` and it evaluates to the string `'x equals 1'` if the value of the variable `x` equals `1`. In all other cases, the switch expression evaluates to `undefined`.

But what if we wanted to have a default result for all those other cases? That's easy with the switch expression.

    {{
      x == 0 -> 'x equals 0',
      x == 1 -> 'x equals 1',
      true   -> 'otherwise'   // the default case
    }}

However, due to how the sub expressions of the switch expression are evaluated, we can abbreviate the default case:

    {{
      x == 0 -> 'x equals 0',
      x == 1 -> 'x equals 1',
                'otherwise'   // the default case
    }}

Remember that the switch expression will return the value of the first expression that does not evaluate to `undefined`. Since the first two sub expressions do evaluate to `undefined`, and the string `'otherwise'` is not `undefined`, the switch expression will return `'otherwise'` as its result.

#### Binary Operator Precedence {#binary-operator-precedence}

Since an expression can be another instance of a binary operator, you can simply chain binary operators and write `1 + 2 + 3`. In that case, operators of the same precedence are evaluated from left to right. The following table explains the precedence of the available binary operators. In the table, higher numbers indicate higher precedence.

Operator Name                Precedence
----     ----                ----
`*`      Multiplication      12
`/`      Division            12
`%`      Modulus             12
`+`      Addition            11
`-`      Subtraction         11
`<<`     Bitwise shift left  10
`>>`     Bitwise shift right 10
`<`      Less                9
`<=`     Less or equal       9
`>`      Greater             9
`>=`     Greater or equal    9
`==`     Equal               8
`!=`     Inequal             8
`&`      Bitwise and         7
`^`      Bitwise xor         6
`|`      Bitwise or          5
`&&`     Logical and         4
`||`     Logical or          3
`..`     Range               2
`->`     Case                1
` `      Other operators     13

Some examples:

    2 * 3 + 4       // 10 because * has a higher precedence than +
    7 < 10 && 8 < 3 // comparisons are evaluated before the logical and operator

If the builtin precedence does not reflect your intention, you can use parentheses to force an operator to be evaluated first.

    2 * (3 + 4) // 14

### Terminals {#terminals}

In EBNF, terminal rules are those which only contain terminal symbols on the right hand side. A symbol is terminal if it is enclosed in double quotes. Note that for the `Char` rule, we have chosen to not enumerate all actual ASCII characters and have used a placeholder string instead.

    Alpha   = "a" | "b" | ... "z" | "A" | "B" | ... "Z"
    Numeric = "0" | "1" | ... "9"
    Char    = Any ASCII character

This concludes the manual for TrenchBroom's expression language.

## Game Configuration Files {#game_configuration_files}

TrenchBroom uses game configuration files to provide support for different games. Some game configuration files come with the editor. They are installed at `<ResourcePath>/games`, where the value of `<ResourcePath>` depends on the platform according to the following table.

Platform  Location
--------  --------
Windows   The directory where the TrenchBroom executable is located.
macOS     `TrenchBroom.app/Contents/Resources`
Linux     `<prefix>/share/trenchbroom`, where `<prefix>` is the installation prefix.

The folder `<ResourcePath>/games` contains a `.cfg` file for each supported game, and additional folders which can contain additional resources related to the game such as icons, palettes or entity definition files.

It is not recommended to change these builtin game configurations, as they will be overwritten when an update is installed. To modify the existing game configurations or to add new configurations, you can place them in the folder `<UserDataPath>/games`, where the value of `<UserDataPath>` is again platform dependent.

Platform  Location
--------  --------
Windows   `C:\Users\<username>\AppData\Roaming\TrenchBroom`
macOS     `~/Library/Application Support/TrenchBroom`
Linux     `~/.TrenchBroom`

Running TrenchBroom with the `--portable` argument will instead put the `<UserDataPath>` in the current directory. This is intended to be run from within the `<ResourcePath>` directory to provide a fully self-contained instance of the application.

To add a new game configuration to TrenchBroom, place it into a folder under `<UserDataPath>/games` -- note that you might need to create that folder if it does not exist. You will need to write your own `GameConfig.cfg` file, or you can copy one of the builtin files and base your game configuration on that. Additionally, you can place additional resources in the folder you created. As an example, suppose you want to add a game configuration for a game called "Example". For this, you would create a new folder `<UserDataPath>/games/Example`, and within that folder, you would create a game configuration file called `GameConfig.cfg`. If you need additional resource such as an icon or entity definition files, you would place those files into this newly created folder as well.

You can also access this directory using the folder icon button below the game list in the [game configuration dialog](#game_configuration).

To override a builtin game configuration file, copy the folder containing the builtin file and place it in `<UserDataPath>/games`. TrenchBroom will prioritize your custom game configurations over the builtin files, but you can still access the resources in the game's resource sub folder without problems. If you wish, you can also override some of these resources by placing a file of the same name in your game resource sub directory.

As an example, consider the case where you want to override the builtin Quake game configuration and the builtin entity definition file for Quake. Copy the file `<ResourcePath>/games/Quake/GameConfig.cfg` to `<UserDataPath>/games/Quake` and modify it as needed. Then copy the file `<ResourcePath>/games/Quake/Quake.fgd` to `<UserDataPath>/games/Quake` and modify it, too. When you load the game configuration in TrenchBroom, the editor will pick up the modified files instead of the builtin ones.

### Game Configuration File Syntax {#game-configuration-file-syntax}

Game configuration files need to specify the following information.

* A **name** used show in the UI and used to find resources in a sub folder of the game configuration folders
* An **icon** to show in the UI (optional)
* **File formats** to identify which map file formats to support for this game
* A **Filesystem** to specify the game asset search paths and package file format (e.g. pak files)
* **Textures**
  * A list of **file extensions** such as `.jpg`
  * A **palette file** (optional)
  * The **worldspawn property** to store the texture packages in the map file
  * A list of **exclusion patterns** to hide textures matching any of these patterns.
* **Entities**
  * The builtin **entity definition files**
  * The **default color** to use in the UI
  * A default **model scale expression**
  * Whether to automatically set default entity properties
* **Tags** to attach additional information to faces or brushes in the editor, e.g. whether a face is detail or hint. (optional)
* **Face attributes** to specify which additional attributes to allow on brush faces (optional)
* **Map bounds** to be displayed in the 2D viewports (optional)
* **Compilation tools** that can have their paths configured by the user (optional)

The game configuration is an [expression language](#expression_language) map with a specific structure, which is explained using an example.

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

#### Versions {#versions}

The game configuration files are versioned. Whenever a breaking change to the game configuration format is introduced, the version number will increase and TrenchBroom will reject the old format with an error message.

**Current Versions**

TrenchBroom currently supports game config version 9.

**Version History**

* Version 9
  - Adapt to terminology change: `texture` renamed to `material`
* Version 8
  - Remove texture format configuration and just keep a list of extensions to search for.
* Version 7
  - Replace texture package configuration with a root path. Removes the distinction between file and directory based texture configurations.
* Version 6
  - Adds the optional `setDefaultProperties` key to the entity configuration.
* Version 5
  - Makes the model format whitelist optional. If a whitelist is still present in a config file, it is ignored.
* Version 4
  - Adds support for the `unused` key in surface flags and content flags; this key does not exist in version 3.
  - Adds support for specifying a list of values for the `pattern` key in surfaceparm-type smart tags; in version 3 only a single value is allowed.
  - Adds the optional `softMapBounds` key.
  - Adds the optional `compilationTools` key.

**Migrating from Version 2**

Version 3 deprecated the `brushtypes` key in favor of the `tags` key, but the contents are very similar. The value of the `brushtypes` key is an array of type matchers. The following brush type matchers are supported in version 2:

Match        Description
-----        -----------
material     Match against a material name, must match all brush faces
contentflag  Match against face content flags (used by Quake 2, Quake 3)
surfaceflag  Match against face surface flags (used by Quake 2)
surfaceparm  Match against shader surface parameters (used by Quake 3)
classname    Match against a brush entity class name

#### File Formats {#file-formats}

The file format is specified by an array of maps under the key `fileformats`. The following formats are supported.

Format           Description
------           -----------
Standard         Standard Quake map file
Valve            Valve map file (like Standard, but with more control over UV mapping)
Quake2           Quake 2 map file with Standard style texture info
Quake2 (Valve)   Quake 2 map file with Valve style texture info
Quake3 (legacy)  Quake 3 map file with Standard style texture info
Quake3 (Valve)   Quake 3 map file with Valve style texture info
Hexen2           Hexen 2 map file (like Quake, but with an additional, but unused value per face)

Note that the "Quake3" format, which will include Quake 3 brush primitives support, is not yet fully implemented and so is omitted from the list above. The "Quake3 (Valve)" format is as expressive for texture placement as the brush primitives format, but "Quake3 (Valve)" cannot be used to read existing map files that contain brush primitives.

Each entry of the array must have the following structure:

    {
      "format": "Standard",
      "initialmap: "initial_standard.map"
    }

Thereby, the `format` key is mandatory but the `initialmap` key is optional. The `initialmap` key refers to a map file in the game's configuration sub folder which should be loaded if a new document is created. If no initial map is specified, or if the file cannot be found, TrenchBroom will create a map containing a single brush at the origin.

#### File System {#file-system}

The file system is used in the editor to load game assets, and it is specified by a map under the key `filesystem`. The map contains two keys, `searchpath` and `packageformat`.

* `searchpath` is the subdirectory under the game directory (set in the game preferences) at which the editor will search for game assets. The editor will search loose files in this path, but it will also mount packages found here.
* `packageformat` specifies the format of the packages to mount. It is a map with two keys, `extension` and `format`.
  * `extensions` specifies the file extensions of the package files to mount (alternatively allows `extension` to specify only one extension)
  * `format` specifies the format of the package files

The following package formats are supported.

Format       Description
------       -----------
idpak        Id pak file
dkpak        Daikatana pak file
zip          Zip file, often uses other extensions such as pk3

#### Material Configurations {#material-configurations}

Every material configuration consists of a root search directory, and optionally a list of included file extensions, a palette path, an attribute for wad file lists and a list of exclusion patterns.

    "materials": {
      "root": "textures",
      "extensions": [ ".D" ],
      "palette": "pics/colormap.pcx",
      "attribute": "wad",
      "excludes": [ "*_norm", "*_gloss" ],
    },

The `root` key specifies the folder at which to search for the material packages. This folder is relative to the game file system set up according to the `filesystem` configuration earlier in the file. TrenchBroom will create a material collection for each folder contained in the root folder specified here.

In the case of Quake 2, the builtin game configuration specifies the search path of the file system as `"baseq2"` and the material package root as `"textures"`, so TrenchBroom will create a material collection for each folder found in `<Game Path>/baseq2/textures`.

TrenchBroom supports a wide array of image formats such as tga, pcx, jpeg, and so on. TrenchBroom uses the [FreeImage Library] to load these images and supports any file type supported by this library.

Optionally, you can specify a palette. The value of the `palette` key specifies a path, relative to the file system, where TrenchBroom will look for a palette file that comes with the game's assets.

The `attribute` key specifies the name of a worldspawn property where TrenchBroom will store the wad files in the map file.

The optional `excludes` key specifies a list of patterns matched against material names which will be ignored and not displayed in the [material browser](#material_browser). Wildcards `*` and `?` are allowed. Use backslashes to escape literal `*` and `?` chars.

    "materials": {
      "root": "textures",
      "extensions": [ "" ],
      "excludes": [ "*_norm", "*_gloss" ]
    },

#### Entity Configuration {#game_configuration_files_entities}

In the entity configuration section, you can specify which entity definition files come with your game configuration, a default color for entities and an expression that yields a default scale when evaluated against an entities' properties.

    "entities": { // the builtin entity definition files for this game
    "definitions": [ "Quake2/Quake2.fgd" ],
      "defaultcolor": "0.6 0.6 0.6 1.0",
      "scale": [ modelscale, modelscale_vec ],
      "setDefaultProperties": true
    },

The `definitions` key provides a list of entity definition files. These files are specified by a path that is relative to the `games` directory where TrenchBroom searches for the game configurations.

The `scale` key has an expression that is evaluated against an entities' properties to determine the model scale. This expression can refer to any of the entities' properties, or it can provide fixed values.

Example                                   Description
-------                                   -----------
`"scale": 2`                              A fixed uniform scale factor of `2`.
`"scale": "1 2 3"`                        A fixed non-uniform scale factor scaling X by 1, Y by 2 and Z by 3.
`"scale": modelscale`                     Use the value of the entities' `modelscale` property.
`"scale": [ modelscale, modelscale_vec ]` Try the individual values in the array until we find one that doesn't evaluate to `Undefined` or `Null`.

Of course, you could use the switch and case operators for more complicated cases.

The optional `setDefaultProperties` key controls whether [default entity properties](#entity_properties_defaults) are instantiated automatically when TrenchBroom creates a new entity. Defaults to `false` if not set.

#### Tags {#game_configuration_files_tags}

TrenchBroom can recognize certain special brush or face types. An example would be clip faces or trigger brushes. But since the details can be game dependent, these special types are defined in the game configuration. For greater flexibility and future enhancements, a general "smart tags" system is used to realize this functionality.

TrenchBroom uses these tag definitions to automatically apply attributes to matching brushes/faces &mdash; for example to render trigger brushes partially transparent &mdash; and to populate the filtering options available in the [View menu](#filtering_rendering_options).

Each smart tag definition also makes one or more related [keyboard shortcuts](#keyboard_shortcuts) available (searching the shortcuts by "Tags" will show all of these). Each tag will always have a related shortcut that can be used to toggle the visibility of brushes whose faces match the tag. Additional shortcuts may also be available to apply the characteristics of the tag to the current selection, or remove those characteristics. These shortcuts depend on the tag's `match` criteria as described below.

The tags are specified separately for brushes and faces under the corresponding keys:

    "tags": {
      "brush": [ ... ],
      "brushface": [ ... ]
    }

Each of these keys has a list of tags. Each tag looks as follows.

    {
      "name": "Clip",
      "attribs": [ "transparent" ],
      "match": "material",
      "pattern": "clip"
    },

The only attribute type currently supported in the `attribs` list is "transparent", which as mentioned above will cause faces matching this tag to be rendered with partial transparency in the 3D viewport.

The `match` key specifies how TrenchBroom will determine whether or not this tag applies to a brush or face.

For a `brush` smart tag, the `match` key can only have the "classname" value. In addition to the usual keyboard shortcut for view filtering, this kind of smart tag will also generate keyboard shortcuts to either apply the tag (create a brush entity from selected brushes) or remove it (return selected brushes to worldspawn). This can be summarized as follows:

Match        Description                            Shortcut to apply  Shortcut to remove
-----        -----------                            -----------------  ------------------
classname    Match against brush entity class name  Yes                Yes

For a `brushface` smart tag, the `match` key can have the following values and will generate keyboard shortcuts to apply or remove the match criteria on selected faces accordingly:

Match        Description                            Shortcut to apply  Shortcut to remove
-----        -----------                            -----------------  ------------------
material     Match against a material name          Yes                No
contentflag  Match against face content flags       Yes                Yes
surfaceflag  Match against face surface flags       Yes                Yes
surfaceparm  Match against shader surface parameter Yes                No

Additional keys will be required to configure the matcher, depending on the value of the `match` key.

* For the `classname` matcher, the key `pattern` contains a pattern that is matched against the classname of the brush entity that contains the brush. Wildcards `*` and `?` are allowed. Use backslashes to escape literal `*` and `?` chars.
  - Additionally, the `classname` matcher can contain an optional `material` key. When this tag is applied by the use of its keyboard shortcut, then the selected brushes will receive the material with the name given as the value of this key (e.g. `"material": "trigger"` will assign the `trigger` material).
* For the `material` matcher, the key `pattern` contains a pattern that is matched against a face's material name. If the pattern does *not* contain a slash, it will only be matched against the segment after the final slash (if any) in the material name. Wildcards `*` and `?` are allowed. Use backslashes to escape literal `*` and `?` chars.
* For the `contentflag` and `surfaceflag` matchers, the key `flags` contains a list of content or surface flag names to match against (see below for more info on content and surface flags).
* For the `surfaceparm` matcher, the key `pattern` contains a name that is matched against the surface parameters of a face's shader. No wildcards allowed; the parameter name must match exactly. In version 4 of the game config format, you may alternately specify a *list* of surfaceparm names for this value, which will match against a shader if it has any of those surfaceparms.

#### Face Attributes {#face-attributes}

The main part of the `faceattribs` object is the set of definitions of available surface flags (generally affecting the appearance/behavior of an individual face) and content flags (generally affecting the behavior of the brush containing the face). These definitions are specified through the `surfaceflags` and `contentflags` keys.

The value for each of those keys is a list of flag definitions. Note that the position of each flag definition within its list determines the value of that flag. Suppose that `i` is the 0-based list index of a flag; that flag's value then corresponds to 2 to the power of `i`. The first flag in the list has value 2^0 = 1, the second has value 2^1 = 2, the third has value 2^2 = 4, and so on.

Consider the following example:

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

There are two surface flags with values 1 and 2, and three valid content flags with values 1, 2, and 8. Note that the third element in the contentflags list, marked as unused, is just a placeholder. This is necessary so that the next flag, "playerclip", receives the correct value of 8.

For any flag definition *not* marked as unused, the `name` key is mandatory and the `description` key is optional. The name value will appear in the flag editor UI, and the description (if provided) will be visible in a tooltip when hovering over a flag checkbox.

Note that before version 4 of the game config format, the `unused` key is not available for flag definitions. You can still effectively skip unused flag values in version 3 with placeholder flag definitions that by convention simply have the name "unused", for example the third element in the contentflags list above could instead be

    {
      "name": "unused"
    }, // value 4

This approach will however cause flags named "unused" to appear in the flag editor UI.

The `faceattribs` object may contain one other key, `defaults`. The value of this key is an object that defines the initial values seen in the [face attribute editor](#face_attribute_editor) for the face of a newly created brush. The following `defaults` keys are supported:

Key              Description
------           -----------
offset           List of two numbers specifying the default X and Y offset
scale            List of two numbers specifying the default X and Y scale
rotation         Number specifying the default rotation angle
surfaceValue     Number specifying the default surface value (only applicable if surfaceflags exist)
surfaceFlags     List of strings naming the default surface flags
surfaceContents  List of strings naming the default content flags
color            String specifying the default surface color (only applicable for Daikatana)

List values must be given in array format, e.g. a default scale of 0.5 along each axis would be specified within the `defaults` object as: `"scale": [0.5, 0.5]`

The flag names specified for `surfaceFlags` or `surfaceContents` must correspond to the `name` value for existing flags defined in the `surfaceflags` or `contentflags` (respectively) of the `faceattribs` object.

The `color` value must be a string of the form "R G B" or "R G B A". R G B and A are each a floating-point number from 0.0 to 1.0. If A is omitted it is assumed to be 1.0.

#### Map Bounds {#map-bounds}

The optional `softMapBounds` key defines the default [map bounds](#map_bounds) to draw in the 2D viewports. Its value is a string that contains the coordinates of two opposite points that define the volume enclosed by the bounds. The example here defines a cube from the point (-4096, -4096, -4096) to the point (4096, 4096, 4096):

    "softMapBounds":"-4096 -4096 -4096 4096 4096 4096",

#### Compilation Tools {#compilation-tools}

The optional `compilationTools` list identifies tool names that will appear in the [game configuration dialog](#game_configuration), allowing the user to associate these names with paths to tool executables. Such a name can be used as a variable in this game's [compilation profiles](#compiling_maps) to represent the associated path.

Each element in the list is an object that must have a `name` key and may optionally have a `description` key (used for tooltips). An example from the Quake 3 game configuration that defines two tools:

    "compilationTools": [
      { "name": "q3map2", "description": "Path to your q3map2 executable, which performs the main bsp/vis/light compilation phases" },
      { "name": "bspc", "description": "Path to your bspc or mbspc executable, which creates .aas files for bot support" }
    ]

## Query Language {#query_language}

The [search box](#searching) accepts plain text or an [expression](#expression_language). Text searches ignore case and match names, classnames, property keys and values, materials, tags, and containing layer or group names. Wildcards `*`, `?` and `%` match whole values. Text searches select objects, not individual faces.

A single name or literal is treated as text. The fields `visible` and `locked` are exceptions and work as queries on their own; quote them to search for those words. Invalid expressions or unknown field/function names are treated as text. Queries select only results that evaluate to `true`; evaluation errors do not match.

### Fields {#query_fields}

Field | Meaning
--- | ---
`type` | Object type: world, layer, group, entity, brush, patch, face.
`name` | Layer or group name.
`classname`, `properties` | Entity class and property map.
`entity` | Owning entity of a brush, patch or face.
`materials`, `material` | Brush/patch materials, or an individual face material.
`normal` | Face normal.
`tags` | Matching smart tag names.
`bounds`, `center` | Bounding box and its center.
`layerName`, `groupName` | Containing layer and innermost group.
`visible`, `locked` | Visibility and lock state.

Unavailable fields evaluate to `undefined`. Property values are strings, but numeric comparisons convert them to numbers. Equality is case sensitive; `like` ignores case. Function-style operators bind more tightly than ordinary operators; use parentheses when negating a comparison.

### Search Domain {#query_domain}

Fields restrict searches to object types that provide them. Type conditions further restrict the search. Logical AND uses the intersection of domains; OR uses their union and evaluates each branch only for its applicable types. A query selects faces only when its domain contains faces alone. Layers likewise require a layer-only domain. Otherwise searches omit faces and layers, avoiding selection of an entire layer just because its bounds overlap a query box.

### Examples {#query_examples}

Query | Selects
--- | ---
`classname is "light"` | Light entities.
`classname like "monster_*"` | Monster entities.
`classname is "light" && properties.light > 300` | Lights brighter than 300.
`properties contains "target"` | Entities with a target property.
`type in [brush, patch]` | Brushes and patches.
`materials like "*trigger*"` | Brushes or patches using trigger materials.
`material like "sky*"` | Individual sky faces.
`type is layer && name like "Combat*"` | Contents of matching layers.
`visible && !locked` | Visible, unlocked objects.
`bounds intersects bbox(vec(-512, -512, 0), vec(512, 512, 256))` | Objects intersecting a box.
