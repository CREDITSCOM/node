# Credits C++ coding style guide

Glance coding style guide description.

### Base concept

- C++17 standard
- 2 spaces
- camelCase naming style

---

### Brackets

>```cpp
>
>void bracketsExample() {
>}
>
>if (transactions.empty()) {
>}
>else {
>}

### Types

- reference and pointer symbols add to right part of type
>```cpp
>int& ref = i;
>int* pointer = &ref;

### Naming convention

- local variables starts with lower case

>```cpp
>auto exampleValue = 100;

- class/struct names starts with upper case

>```cpp
>class ExampleClass {
>};

- class fields starts with lowe case and ends with symbol _

>```cpp
>class A {
>public:
>   int exampleField_;
>};

- struct fields starts with lowe case

>```cpp
>struct A {
>   int demoField;
>};

- method/function starts with lower case

>```cpp
>void runTransport() {
>   object->call();
>}

- namespace name starts with lower case

>```
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
>   enum Options {
>     Capacity = 100
>   };
>};

- global/static (not local static) variable starts with 'g' symbol

>```cpp
>static int gPlatformStatus = 0;