%module cscrypto_api

/* Force the generated Java code to use the C enum values rather than making a JNI call */
%javaconst(1);

%include <carrays.i>
%array_class(unsigned char, Bytes);

%include "../include/cscrypto/cscrypto_c_api_types.h" 
%include "../include/cscrypto/cscrypto_c_api_functions.h" 