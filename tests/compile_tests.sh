#!/bin/sh



echo "compile_tests.sh: rebuild tests"
rm qp_test
g++ -g qp_test.cpp zsim_nic_defines.cpp -std=c++11 -o qp_test -pthread

#rm loop_test
#g++ loop_test.cpp zsim_nic_defines.cpp -std=c++11 -o loop_test

rm control_test
g++ array_access.cpp zsim_nic_defines.cpp -std=c++11 -o control_test

rm control_test_no_locality
g++ array_access_large.cpp zsim_nic_defines.cpp -std=c++11 -o control_test_no_locality

rm control_test_l2_locality
g++ array_access_l2_locality.cpp zsim_nic_defines.cpp -std=c++11 -o control_test_l2_locality

rm nic_proxy_app
g++ -g nic_proxy_app.cpp zsim_nic_defines.cpp -std=c++11 -o nic_proxy_app -pthread

rm ma_uarch_check
g++ memaccess_uarch_check.cpp zsim_nic_defines.cpp -std=c++11 -o ma_uarch_check
