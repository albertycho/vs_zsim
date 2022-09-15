#!/usr/bin/env python3

import os
import sys
import csv
import math
#import statistics
#import numpy as np
#import matplotlib

files=os.listdir()
latbin=''
for ff in files:
    if (('lats' in ff) and ('bin' in ff)):
        latbin=ff

print(latbin)


#os.system('../get_mem_lat.py');
#os.system('../get_avg_mbw_trim_roi.py');
os.system('/shared/acho44/get_zsimout_stats_dramsim.py');
os.system('/shared/acho44/get_avg_mbw_dramsim_trim.py');
if(latbin != ''):
    os.system('/shared/acho44/parselats_partial.py  '+latbin+' >> stat_summary.txt');
os.system('cat stat_summary.txt');

