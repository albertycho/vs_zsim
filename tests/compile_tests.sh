#!/bin/sh

#rm buffer_test
#rm buffer_test_2
#g++ buffer_test.cpp -std=c++11 -o buffer_test
#g++ buffer_test2.cpp -std=c++11 -o buffer_test_2
#rm wqcq_test
#rm wqcq_test2
#g++ wqcq_test.cpp -std=c++11 -o wqcq_test
#g++ wqcq_test2.cpp -std=c++11 -o wqcq_test2
#
#rm buffer_test_2
#rm buffer_test
#g++ buffer_test.cpp -std=c++11 -o buffer_test
#g++ buffer_test2.cpp -std=c++11 -o buffer_test_2

echo "compile_tests.sh: rebuild tests"
rm qp_test
g++ qp_test.cpp zsim_nic_defines.cpp -std=c++11 -o qp_test
