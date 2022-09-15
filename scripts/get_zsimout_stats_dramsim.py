#!/usr/bin/env python3

import os
import sys 
import csv 
import math
import statistics
import numpy as np
from os.path import exists

f_mem_lat = open('mem_lats.csv','w')

#rd=0;
#wr=0;
#rdlat=0;
#wrlat=0;


wr_lat_total=0
rd_lat_total=0
rd_count=0
wr_count=0

accesses=0

l3misses=0;
l3hits=0;





total_insts=0
total_cycles=0

#sancheck counter
num_mem_con=0
num_llc_banks=0

#if exists('zsim.out'):
#    zsimout = open('zsim.out','r')
if exists('zsim_final.out'):
    zsimout = open('zsim_final.out','r')
elif exists('zsim.out'):
    zsimout = open('zsim.out','r')
else:
    print('zsim_final.out or zsim.out not found\n')
    exit(0);

line = zsimout.readline()
while line:
    if 'OCore1-' in line:
        line=zsimout.readline()
        if not ' cycles:' in line:
            print('expected cycles for core but didnt find\n')
        tmp1=line.split(': ')[1]
        tmp2=int(tmp1.split(' #')[0])
        total_cycles +=tmp2
        
        line=zsimout.readline()
        line=zsimout.readline()
        
        if not 'instrs:' in line:
            print('expected instrs for core but didnt find\n')
        tmp1=line.split(': ')[1]
        tmp2=int(tmp1.split(' #')[0])
        total_insts +=tmp2

    if 'mem-' in line:
        #rd=0;
        #wr=0;
        #rdlat=0;
        #wrlat=0;

        wr_lat_total=0
        rd_lat_total=0
        rd_count=0
        wr_count=0


        rdl=zsimout.readline()
        wrl=zsimout.readline()
        rdlatl=zsimout.readline()
        wrlatl=zsimout.readline()
        if not 'rd:' in rdl:
            print('expected rd: in mem but not found\n')
        if not 'wr:' in wrl:
            print('expected wr: in mem but not found\n')
        if not 'rdlat:' in rdlatl:
            print('expected rdlat: in mem but not found\n')
        if not 'wrlat:' in wrlatl:
            print('expected wrlat: in mem but not found\n')
        tmp=rdl.split('rd:')[1]
        tmp=tmp.split('#')[0]
        rd_count=int(tmp)
        tmp=wrl.split('wr:')[1]
        tmp=tmp.split('#')[0]
        wr_count=int(tmp)
        tmp=rdlatl.split('rdlat:')[1]
        tmp=tmp.split('#')[0]
        rd_lat_total=int(tmp)
        tmp=wrlatl.split('wrlat:')[1]
        tmp=tmp.split('#')[0]
        wr_lat_total=int(tmp)



    if 'l3-' in line:
        num_llc_banks+=1
        while not 'hGETS' in line:
            line=zsimout.readline()
        tmp1=line.split(': ')[1]
        tmp2=int(tmp1.split(' #')[0])
        l3hits += tmp2
        line=zsimout.readline()
        if not 'hGETX' in line:
            print('expected hGETX but didnt find\n')
        tmp1=line.split(': ')[1]
        tmp2=int(tmp1.split(' #')[0])
        l3hits += tmp2
        line=zsimout.readline()

        while not 'mGETS' in line:
            line=zsimout.readline()
        if not 'mGETS' in line:
            print('expected mGETS but didnt find\n')
        tmp1=line.split(': ')[1]
        tmp2=int(tmp1.split(' #')[0])
        l3misses += tmp2
        line=zsimout.readline()
        if not 'mGETXIM' in line:
            print('expected mGEXIM but didnt find\n')
        tmp1=line.split(': ')[1]
        tmp2=int(tmp1.split(' #')[0])
        l3misses += tmp2

            


    line=zsimout.readline()

zsimout.close()
if(wr_count!=0):
    wr_lat_avg = wr_lat_total/wr_count
else:
    print('no wr count logged')
    wr_lat_avg = 0
    
if(rd_count!=0):
    rd_lat_avg = rd_lat_total/rd_count
else:
    print('no rd count logged')
    rd_lat_avg = 0

all_lat_avg=0
if(rd_count+wr_count!=0):
    all_lat_avg = (rd_lat_total+wr_lat_total) / (rd_count+wr_count)



mpki=0
if(total_insts!=0):
    mpki= (l3misses*1000) / total_insts;

ipc_all = 0
if(total_cycles!=0):
    ipc_all=total_insts / total_cycles

l3miss_rate = 0
if((l3misses+l3hits)!=0):
    l3miss_rate=l3misses/ (l3misses+l3hits)

#rdlat_avg=0
#wrlat_avg=0
#if(rd!=0):
#    rdlat_avg = rdlat/rd;

f_mem_lat.write('\nMEM:\n')

f_mem_lat.write('\nwr_lat_avg, '+str(wr_lat_avg)+',\n')
f_mem_lat.write('rd_lat_avg, '+str(rd_lat_avg)+',\n')
f_mem_lat.write('all_lat_avg, '+str(all_lat_avg)+',\n')
#f_mem_lat.write('net_l3_miss_ratio, '+str(net_l3_miss_ratio)+',\n')
#f_mem_lat.write('net_l3_miss_count, '+str(total_net_miss)+',\n')
#f_mem_lat.write('net_l3_hit_count, '+str(total_net_hit)+',\n')



f_mem_lat.write('\nL3:\n')
  

f_mem_lat.write('l3_miss_rate , '+str(l3miss_rate)+',\n')
f_mem_lat.write('l3misses ,    '+str(l3misses)+',\n')

f_mem_lat.write('\nmem accesses        ,'+str(accesses)+',\n')

f_mem_lat.write('\nMPKI: '+str(mpki)+',\n')
f_mem_lat.write('IPC_ALL: '+str(ipc_all)+',\n')


f_mem_lat.close()




#f_stat_summary.close()

os.system('cat mem_lats.csv >> stat_summary.txt')

#print('num llc banks: '+(str(num_llc_banks)))
#print('num mem con: '+(str(num_mem_con)))





