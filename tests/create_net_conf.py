#!/usr/bin/env python3

#from asyncio import write

#l1is = []
#l1ds = []
#l2s = []
#l3s = []
#mem = []

nmcs=["1","splitter"]
mem_delays=["0","20","60","100","200"]

for nmc in nmcs:
    for mem_delay in mem_delays:

        delay_value = str(int(int(mem_delay) / 2))

        #nmc="4"
        #mem_delay="0"
        l1is = []
        l1ds = []
        l2s = []
        l3s = []
        mem = []



        for i in range(128):
            l1is.append("cl1i-"+str(i))
            l1ds.append("cl1d-"+str(i))
            l2s.append("l2-"+str(i))
            #l3s.append("l3-0b"+str(i))
        
        for i in range(128):
            l3s.append("l3-0b"+str(i))
        
        #for i in range(int(nmc)):
        #    mem.append("mem-"+str(i))
        mem.append("mem-"+str(nmc))
        nmc_title=str(nmc)
        if 'splitter' in nmc:
            nmc_title = "mem_splitter"
        #fname = "network_"+nmc
        fout = open("network_"+nmc_title+"_"+mem_delay+".conf",'w')
        
        for i in range(len(l1is)):
            fout.write(l1is[i]+" "+l2s[i]+" 0\n")
        for i in range(len(l1ds)):
            fout.write(l1ds[i]+" "+l2s[i]+" 0\n")
        
        for l in l2s:
            for m in l3s:
                fout.write(l+" "+m+" 4\n")
        
        #for l in l3s:
        #    fout.write("ncinl1d-0 "+l+" 4\n")
        #    fout.write("ncinl1i-0 "+l+" 4\n")
        #    fout.write("ncoutl1d-0 "+l+" 4\n")
        #    fout.write("ncoutl1i-0 "+l+" 4\n")
        #    fout.write("hscl1d-0 "+l+" 4\n")
        #    fout.write("hscl1i-0 "+l+" 4\n")
        
        for l in l3s:
            for m in mem:
                fout.write(l+" "+m+" "+delay_value+"\n")
        
        fout.close()
