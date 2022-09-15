#!/usr/bin/env python

import os
import sys

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

# 4rd input: some variable in the config file you want to use with multiple values, eg different L3 latencies
packet_injection_rate = sys.argv[4]

# 5th input: the process command to run and all its inputs
cmd = sys.argv[5]
cmd_args = len(sys.argv) - 6
tmp = 0
while cmd_args>0:
	cmd = cmd+" "+sys.argv[6+tmp]
	tmp+=1
	cmd_args-=1



# create the new config file
f1 = open(confile)
f2 = open('conf.cfg','w')
line = f1.readline()
while line:
	if 'command' in line:
		tmp = line.split('=')[0]
		f2.write(tmp+'= "'+cmd+'";\n')
#	elif 'startFastForwarded' in line:
#		tmp = line.split('=')[0]
#		f2.write(tmp+'= True;\n')
	elif 'packet_injection_rate' in line:
		tmp = line.split('=')[0]
		f2.write(tmp+'= '+packet_injection_rate+';\n')
	else:
		f2.write(line)
	line = f1.readline()
f1.close()
f2.close()

# launch the run, redirectung stdin and stderr to log files 
os.system('./'+zsim_exec+' conf.cfg 1> res.txt 2>app.txt')

