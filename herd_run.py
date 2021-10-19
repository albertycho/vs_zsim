#!/usr/bin/env python

import os
import sys

#args: 1 outdir 2 zsim_exec 3 base cfg file 4 num_server 5 phaseLength 
#      6 packet_injection_rate 7 packet_count 8 update fraction
#this script assumes process 2 will have herd and its params in the base cfg file

# this is your base zsim directory
home = '/nethome/acho44/zsim/zSim'

# 1st input: the output directory you want the run to take place + store results
outdir = sys.argv[1] 
os.system('rm -rf '+outdir)
os.system('mkdir '+outdir)
os.chdir(outdir)


# 2nd input: the zsim executable to use
zsim_exec = sys.argv[2]
os.system ('cp '+home+'/'+zsim_exec+' .')

# 3rd input: the base config file (assuming it's in the tests folder)
confile = sys.argv[3]
os.system('cp '+home+'/tests/'+confile+' .')

#### copy executables known to be needed
os.system('cp '+home+'/tests/nic_proxy_app .')
os.system('cp '+home+'/tests/nic_egress_proxy_app .')

# configurable param input:
num_server = sys.argv[4]
phaseLength = sys.argv[5]
packet_injection_rate = sys.argv[6]
packet_count = sys.argv[7]
update_fraction = sys.argv[8]


# create params.txt in outdir to record input params to this run/dir
fpar = open('params.txt', 'w')
fpar.write('num_server ='+num_server+'\n')
fpar.write('phaseLength ='+phaseLength+'\n')
fpar.write('packet_injection_rate ='+packet_injection_rate+'\n')
fpar.write('packet_count ='+packet_count+'\n')


# this script assumes process 2 will have herd and its params in the base cfg file,
#  so comment out this part
# 5th input: the process command to run and all its inputs
#cmd = sys.argv[5]
#cmd_args = len(sys.argv) - 6
#tmp = 0
#while cmd_args>0:
#	cmd = cmd+" "+sys.argv[6+tmp]
#	tmp+=1
#	cmd_args-=1


# create the new config file
f1 = open(confile)
f2 = open('conf.cfg','w')
line = f1.readline()
while line:
	if 'num_server' in line:
		tmp = line.split('=')[0]
		f2.write(tmp+'= '+num_server+';\n')
	elif 'phaseLength' in line:
		tmp = line.split('=')[0]
		f2.write(tmp+'= '+phaseLength+';\n')
        elif 'packet_injection_rate' in line:
		tmp = line.split('=')[0]
		f2.write(tmp+'= '+packet_injection_rate+';\n')
	elif 'packet_count' in line:
		tmp = line.split('=')[0]
		f2.write(tmp+'= '+packet_count+';\n')
	elif 'packet_count' in line:
		tmp = line.split('=')[0]
		f2.write(tmp+'= '+packet_count+';\n')
        elif '--qps-to-create' in line:
		tmp = line.split(' ')[0]
		f2.write(tmp+' '+num_server+' \\\n')
        elif '--num-threads' in line:
		tmp = line.split(' ')[0]
		f2.write(tmp+' '+num_server+' \\\n')


	else:
		f2.write(line)
	line = f1.readline()
f1.close()
f2.close()

# launch the run, redirectung stdin and stderr to log files 
#os.system('./'+zsim_exec+' conf.cfg 1> res.txt 2>app.txt')
#os.system('./'+zsim_exec+' conf.cfg 2> err.txt')

os.system('cd ..')
