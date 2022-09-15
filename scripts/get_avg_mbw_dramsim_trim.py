#!/usr/bin/env python3

import os
import sys
import csv
import math
import statistics
import numpy as np
import matplotlib
import matplotlib.pyplot as plt 





cumulative_bw = []

mbw_stats_file = open("mbw_stats.txt", 'w')

for d in os.listdir('.'):
    if 'dramsim.log' in d:
        mbwraw=open(d,'r')
        line = mbwraw.readline()
        tmpmbw = []
        while line:
            if('aggregate average bandwidth' in line):
                tmp=line.split('aggregate average bandwidth ')[1];
                tmp=tmp.split('GB')[0]
                bw = float(tmp)
                tmpmbw.append(bw)
                cumulative_bw.append(bw)
            line=mbwraw.readline()
        if (len(tmpmbw)>100):
            ## cut off first 1/3 and last 1/3
            full_len = len(tmpmbw)
            roi_start = int(full_len/3)
            roi_start = roi_start*2 #start at 2/3, loading takes really long
            roi_end = roi_start+int((roi_start/2))
            new_bw_arr = tmpmbw[roi_start:roi_end]
            #avgbw = str(sum(tmpmbw)/len(tmpmbw))
            avgbw = str(sum(new_bw_arr)/len(new_bw_arr))
            mbw_stdev = str(statistics.pstdev(new_bw_arr))
            mbw_variance = str(statistics.variance(new_bw_arr))
            mbw_stats_file.write(d+' avgbw: '+avgbw+'\n')
            mbw_stats_file.write('MBW std_dev : '+mbw_stdev+'\n')
            mbw_stats_file.write('MBW variance: '+mbw_variance+'\n')
        mbwraw.close()
##if (len(cumulative_bw) >0):
##    mbw_stats_file.write(d+' avgbw: '+avgbw)

mbw_stats_file.close()

os.system('cat mbw_stats.txt >> stat_summary.txt')

