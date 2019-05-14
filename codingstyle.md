# Credits C++ coding style guide

Glance coding style guide description.

### Base concept

- C++17 standard
- 4 spaces
- only camelCase naming style
- no exceptions

---

### Brackets and spaces

- 1 space after if/else switch instructions
- else operator use on the next line after closing bracket
- if/else operator with one code line must be used with brackets

>```cpp
>
>void bracketsExample() {
>     foo();
>}
>
>if (transactions.empty()) {
>     foo();
>}
>else {
>     bar();
>}

### Types

- reference and pointer symbols add to right part of type
>```cpp
>int i = 0;
>int& ref = i;
>int* pointer = &ref;

- variable instantiation must starts on new line
>```cpp
>int first = 0;
>int second = 0;
>int* third = &second;

### Naming convention

- local variables starts with lower case

>```cpp
>auto exampleValue = 100;

- class/struct names starts with upper case

>```cpp
>class ExampleClass {
>};

- private fields starts with lower case and ends with symbol _

>```cpp
>class A {
>private:
>     int exampleField_;
>};

- public fields starts with lower case

>```cpp
>struct A {
>     int demoField;
>};

- method and function starts with lower case

>```cpp
>void runTransport() {
>     object->call();
>}

- namespace name starts with lower case

>```cpp
>namespace cs {
>class A {
>};
>}

- constant starts with 'k' symbol, or goes to enum

>```cpp
>constexpr double kTransactionFee = 0.001;
>
>class Transport {
>public:
>     enum Options {
>       Capacity = 100
>     };
>};

- global variable starts with 'g' symbol

>```cpp
>static int gPlatformStatus = 0;

### Clang format settings

>```
>BasedOnStyle: Google
>IndentWidth: 4
>ColumnLimit: 180
>AccessModifierOffset: -4
>AllowShortFunctionsOnASingleLine: 'false'
>AllowShortIfStatementsOnASingleLine: 'false'
>AllowShortLoopsOnASingleLine: 'false'
>AlignTrailingComments: 'true'
>ConstructorInitializerAllOnOneLineOrOnePerLine: 'false'
>ConstructorInitializerIndentWidth: 0
>BreakConstructorInitializersBeforeComma: 'true'
>BreakBeforeBraces: 'Custom'
>BraceWrapping: { AfterEnum: true, BeforeElse: true, BeforeCatch: true }
>MaxEmptyLinesToKeep: 1
