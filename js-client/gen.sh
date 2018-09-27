#!/bin/bash
PATH=$PATH:third-party/thrift/compiler/msvc/
thrift --gen js -out js-client/js third-party/thrift-interface-definitions/api.thrift
thrift --gen js -out js-client/js third-party/thrift-interface-definitions/variant.thrift
