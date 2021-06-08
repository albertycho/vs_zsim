#!/bin/sh



echo "compile_tests.sh: rebuild tests"
rm qp_test
g++ qp_test.cpp zsim_nic_defines.cpp -std=c++11 -o qp_test

#rm loop_test
#g++ loop_test.cpp zsim_nic_defines.cpp -std=c++11 -o loop_test

rm control_test
g++ array_access.cpp zsim_nic_defines.cpp -std=c++11 -o control_test

rm control_test_no_locality
g++ array_access_large.cpp zsim_nic_defines.cpp -std=c++11 -o control_test_no_locality

rm nic_proxy_app
g++ nic_proxy_app.cpp zsim_nic_defines.cpp -std=c++11 -o nic_proxy_app