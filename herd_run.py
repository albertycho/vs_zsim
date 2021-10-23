#!/usr/bin/env python

import os
import sys
import argparse

#args: 1 outdir 2 zsim_exec 3 base cfg file 4 num_server 5 phaseLength 
#      6 packet_injection_rate 7 packet_count 8 update fraction
#this script assumes process 2 will have herd and its params in the base cfg file

# this is your base zsim directory
home = '/nethome/acho44/zsim/zSim'
herd_path = '/nethome/acho44/zsim/herd-sonuma-zsim/build/herd'

# parse cmd args
parser = argparse.ArgumentParser()
parser.add_argument('--out_dir', type=str, default='out_dir')
parser.add_argument('--exec_file', type=str, default='build/opt/zsim')
parser.add_argument('--base_cfg', type=str, default='herd_base.cfg')

parser.add_argument('--num_server', type=str, default='16')
parser.add_argument('--phaseLength', type=str, default='10000')
parser.add_argument('--packet_injection_rate', type=str, default='40')
parser.add_argument('--packet_count', type=str, default='20000')
parser.add_argument('--update_fraction', type=str, default='100')
parser.add_argument('--pp_policy', type=str, default='2')
args = parser.parse_args()



# 1st input: the output directory you want the run to take place + store results
#outdir = sys.argv[1] 
outdir = args.out_dir
os.system('rm -rf '+outdir)
os.system('mkdir '+outdir)
os.chdir(outdir)


# 2nd input: the zsim executable to use
#zsim_exec = sys.argv[2]
zsim_exec = args.exec_file
os.system ('cp '+home+'/'+zsim_exec+' .')

# 3rd input: the base config file (assuming it's in the tests folder)
#confile = sys.argv[3]
confile = args.base_cfg
os.system('cp '+home+'/tests/'+confile+' .')

#### copy executables known to be needed
os.system('cp '+home+'/tests/nic_proxy_app .')
os.system('cp '+home+'/tests/nic_egress_proxy_app .')
os.system('cp '+herd_path+' .')

# configurable param input:
num_server = args.num_server
phaseLength = args.phaseLength
packet_injection_rate = args.packet_injection_rate
packet_count = args.packet_count
update_fraction = args.update_fraction
pp_policy = args.pp_policy

# configurable param input:
##num_server = sys.argv[4]
##phaseLength = sys.argv[5]
##packet_injection_rate = sys.argv[6]
##packet_count = sys.argv[7]
##update_fraction = sys.argv[8]


# create params.txt in outdir to record input params to this run/dir
fpar = open('params.txt', 'w')
fpar.write('num_server ='+num_server+'\n')
fpar.write('phaseLength ='+phaseLength+'\n')
fpar.write('packet_injection_rate ='+packet_injection_rate+'\n')
fpar.write('packet_count ='+packet_count+'\n')
fpar.close()

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
	elif 'update_fraction' in line:
		tmp = line.split('=')[0]
		f2.write(tmp+'= '+update_fraction+';\n')
	elif 'pp_policy' in line:
		tmp = line.split('=')[0]
		f2.write(tmp+'= '+pp_policy+';\n')

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
os.system('./zsim conf.cfg 2> err.txt')
#os.system('./zsim conf.cfg 1> res.txt 2>app.txt')

os.system('cd ..')
