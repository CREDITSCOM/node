# Credits C++ coding style guide

Coding style guide description.

### Base concept

- C++17 standard
- 2 spaces
- camel case naming style

---

### Naming convention

- local variables starts with lower case

>```auto exampleValue = 100;```

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

>```
>struct A {
>   int goodField;
>};

- method/function starts with lower case

>```
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

>```
>constexpr double kTransactionFee = 0.001;
>
>class Transport {
>public:
>   enum Options {
>     Capacity = 100
>   };
>};


- global/static (not local static) variable starts with 'g' symbol

>```static int gPlatformStatus = 0;```

