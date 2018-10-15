Prerequisites
-------------

1. SWIG should be present on your system
2. SWIG root directory should be added to environment variable PATH
	(Execute 'swig -version' from CLI to check if you can run SWIG from any directory)
3. JDK should be present on your system
4. JAVA_HOME environment variable should point to JDK root

(Optional) 
Apache Ant needed to run Java example

5. Apache Ant should present on your system
6. Ant root should be added to environment variable PATH
	(Execute 'ant' from CLI to check if you can run Ant from any directory)

How to run
----------

* During build process SWIG will be called to create C-to-Java wrapper classes

* SWIG will take cscrypto_api.i as input and produce .c file nearby and .java files inside java/java-gen directory

* cscrypto_jni.dll will be created during build process

* Find cscrypto_jni.dll and place it in cscrypto/swig/java directory

* Run 'ant' from cscrypto/swig/java directory to compile and run Java example