#!/usr/bin/env python

import os
import sys
import argparse

#args: 1 outdir 2 zsim_exec 3 base cfg file 4 num_server 5 phaseLength 
#      6 packet_injection_rate 7 packet_count 8 update fraction
#this script assumes process 2 will have herd and its params in the base cfg file

# this is your base zsim directory
home = '/nethome/acho44/zsim/zSim'

parser = argparse.ArgumentParser()
parser.add_argument('--batch_dir', type=str, default='batch_dir')
#parser.add_argument('--batch_dir', type=str, required=True)
parser.add_argument('--out_dir_prefix', type=str, default='out_dir_')

parser.add_argument('--exec_file', type=str, default='build/opt/zsim')
parser.add_argument('--base_cfg', type=str, default='herd_base.cfg')

parser.add_argument('--num_server', type=str, default='16')
parser.add_argument('--phaseLength', type=str, default='10000')
#parser.add_argument('--packet_injection_rate', type=str, default='100')
parser.add_argument('--packet_count', type=str, default='200000')
parser.add_argument('--update_fraction', type=str, default='100')
parser.add_argument('--pp_policy', type=str, default='0')
args = parser.parse_args()

batch_dir = args.batch_dir
out_dir_prefix = args.out_dir_prefix
zsim_exec = args.exec_file
base_cfg = args.base_cfg

# configurable param input:
num_server = args.num_server
phaseLength = args.phaseLength
#packet_injection_rate = args.packet_injection_rate
packet_count = args.packet_count
update_fraction = args.update_fraction
pp_policy = args.pp_policy

if os.path.isdir(batch_dir):
    print "batch_dir "+batch_dir+" already exists!\n"
    quit()

os.system('rm -rf '+batch_dir)
os.system('mkdir '+batch_dir)
os.chdir(batch_dir)

#injection_rates = ["10","15","20","25","30","35","40","45","50"]
#injection_rates = ["2","4","5","6","8","10","15","20","25","30","35","40","45","50"]
#injection_rates = ["40"]
#injection_rates = ["25","50","75","100","125","150"]
#injection_rates = ["25","30","35","40","45","50"]
#injection_rates = ["1","2","3","4","5","6","7","8","9","10"]
#injection_rates = ["20","30","40","50","60"]
#injection_rates = ["60","70","80","90","100","110","120"]
#injection_rates = ["80","82","84","86","88","90","92","94","96","98","100"]
#injection_rates = ["160","170","180","190","200","210","220","230"]
#injection_rates = ["190","192","194","196","198","200","202","204","206","208","210","212","214"]
injection_rates = ["80","90","100","110","120","130","140","150","160","170","180","190","200","210","220","230"]


for ir in injection_rates:
    out_dir_name = out_dir_prefix+ir
    os.system('../herd_run.py --out_dir '+out_dir_name+' --exec_file '+zsim_exec+' --base_cfg '+base_cfg+' --num_server '+num_server+' --phaseLength '+phaseLength+' --packet_injection_rate '+ir+' --packet_count '+packet_count+' --update_fraction '+update_fraction+' --pp_policy '+pp_policy)



