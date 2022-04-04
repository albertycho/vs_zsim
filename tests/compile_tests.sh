#!/bin/sh



echo "compile_tests.sh: rebuild tests"
rm qp_test
g++ -g qp_test.cpp ../src/libzsim/zsim_nic_defines.cpp -std=c++11 -O3 -o qp_test -pthread

rm nic_proxy_app
g++ -g nic_proxy_app.cpp ../src/libzsim/zsim_nic_defines.cpp -std=c++11 -o nic_proxy_app -pthread

rm nic_egress_proxy_app
g++ -g nic_egress_proxy_app.cpp ../src/libzsim/zsim_nic_defines.cpp -std=c++11 -o nic_egress_proxy_app -pthread

rm memhog
g++ -g memhog.cpp ../src/libzsim/zsim_nic_defines.cpp -std=c++11 -o memhog -pthread 

rm memhog_mt
g++ -g memhog_mt_wrapper.cpp memhog_mt.cpp ../src/libzsim/zsim_nic_defines.cpp -std=c++11 -o memhog_mt -pthread

rm matmul
g++ -g matmul_wrapper.cpp matmul.cpp ../src/libzsim/zsim_nic_defines.cpp -std=c++11 -o matmul -pthread


#g++ -g qp_test_mt_wrapper.cpp qp_test_thread_ver.cpp ../src/libzsim/zsim_nic_defines.cpp -std=c++11 -O3 -Wall -o qp_test_thread_ver -pthread

#rm ma_uarch_check
#g++ memaccess_uarch_check.cpp zsim_nic_defines.cpp -std=c++11 -o ma_uarch_check

#rm loop_test
#g++ loop_test.cpp zsim_nic_defines.cpp -std=c++11 -o loop_test

#rm control_test
#g++ array_access.cpp zsim_nic_defines.cpp -std=c++11 -o control_test

#rm control_test_no_locality
#g++ array_access_large.cpp zsim_nic_defines.cpp -std=c++11 -o control_test_no_locality

#rm control_test_l2_locality
#g++ array_access_l2_locality.cpp zsim_nic_defines.cpp -std=c++11 -o control_test_l2_locality
