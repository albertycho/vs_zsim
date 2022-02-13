/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* ZSim master process. Handles global heap creation, configuration, launching
 * slave pin processes, coordinating and terminating runs, and stats printing.
 */

#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <sys/personality.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "config.h"
#include "constants.h"
#include "debug_harness.h"
#include "galloc.h"
#include "log.h"
#include "pin_cmd.h"
#include "version.h" //autogenerated, in build dir, see SConstruct
#include "ooo_core.h"
#include "zsim.h"


/* Globals */

typedef enum {
    OK,
    GRACEFUL_TERMINATION,
    KILL_EM_ALL,
} TerminationStatus;

TerminationStatus termStatus = OK;

typedef enum {
    PS_INVALID,
    PS_RUNNING,
    PS_DONE,
} ProcStatus;

struct ProcInfo {
    int pid;
    volatile ProcStatus status;
};

//At most as many processes as threads, plus one extra process per child if we launch a debugger
#define MAX_CHILDREN (2*MAX_THREADS)
ProcInfo childInfo[MAX_CHILDREN];


volatile uint32_t debuggerChildIdx = MAX_THREADS;

GlobSimInfo* globzinfo = nullptr; //used very sparingly, only in sig handlers. Should probably promote to a global like in zsim processes.

bool perProcessDir, aslr;

PinCmd* pinCmd;

/* Defs & helper functions */

void LaunchProcess(uint32_t procIdx);

int getNumChildren() {
    int num = 0;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (childInfo[i].status == PS_RUNNING) num++;
    }
    return num;
}

int eraseChild(int pid) {
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (childInfo[i].pid == pid) {
            assert_msg(childInfo[i].status == PS_RUNNING, "i=%d pid=%d status=%d", i, pid, childInfo[i].status);
            childInfo[i].status = PS_DONE;
            return i;
        }
    }

    panic("Could not erase child!!");
}

/* Signal handlers */

void chldSigHandler(int sig) {
    assert(sig == SIGCHLD);
    int status;
    int cpid;
    while ((cpid = waitpid(-1, &status, WNOHANG)) > 0) {
        int idx = eraseChild(cpid);
        if (idx < MAX_THREADS) {
            info("Child %d done", cpid);
            int exitCode = WIFEXITED(status)? WEXITSTATUS(status) : 0;
            if (exitCode == PANIC_EXIT_CODE) {
                panic("Child issued a panic, killing simulation");
            }
            //Stricter check: See if notifyEnd was called (i.e. zsim caught this termination)
            //Only works for direct children though
            if (globzinfo && !globzinfo->procExited[idx]) {
                //panic("Child %d (idx %d) exit was anomalous, killing simulation", cpid, idx);
            }

            if (globzinfo && globzinfo->procExited[idx] == PROC_RESTARTME) {
                info("Restarting procIdx %d", idx);
                globzinfo->procExited[idx] = PROC_RUNNING;
                LaunchProcess(idx);
            }
        } else {
            info("Child %d done (debugger)", cpid);
        }
    }
}

void sigHandler(int sig) {
    if (termStatus == KILL_EM_ALL) return; //a kill was already issued, avoid infinite recursion

    switch (sig) {
        case SIGSEGV:
            warn("Segmentation fault");
            termStatus = KILL_EM_ALL;
            break;
        case SIGINT:
            info("Received interrupt");
            termStatus = (termStatus == OK)? GRACEFUL_TERMINATION : KILL_EM_ALL;
            break;
        case SIGTERM:
            info("Received SIGTERM");
            termStatus = KILL_EM_ALL;
            break;
        default:
            warn("Received signal %d", sig);
            termStatus = KILL_EM_ALL;
    }

    if (termStatus == KILL_EM_ALL) {
        warn("Hard death, killing the whole process tree");
        kill(-getpid(), SIGKILL);
        //Exit, we have already killed everything, there should be no strays
        panic("SIGKILLs sent -- exiting");
    } else {
        info("Attempting graceful termination");
        for (int i = 0; i < MAX_CHILDREN; i++) {
            int cpid = childInfo[i].pid;
            if (childInfo[i].status == PS_RUNNING) {
                info("Killing process %d", cpid);
                kill(-cpid, SIGKILL);
                usleep(100000);
                kill(cpid, SIGKILL);
            }
        }

        info("Done sending kill signals");
    }
}

void exitHandler() {
    // If for some reason we still have children, kill everything
    uint32_t children = getNumChildren();
    if (children) {
        warn("Hard death at exit (%d children running), killing the whole process tree", children);
        kill(-getpid(), SIGKILL);
    }
}

void debugSigHandler(int signum, siginfo_t* siginfo, void* dummy) {
    assert(signum == SIGUSR1);
    uint32_t callerPid = siginfo->si_pid;
    // Child better have this initialized...
    struct LibInfo* zsimAddrs = (struct LibInfo*) gm_get_secondary_ptr();
    uint32_t debuggerPid = launchXtermDebugger(callerPid, zsimAddrs);
    childInfo[debuggerChildIdx].pid = debuggerPid;
    childInfo[debuggerChildIdx++].status = PS_RUNNING;
}

/* Heartbeats */

static time_t startTime;
static time_t lastHeartbeatTime;
static uint64_t lastCycles = 0;

static void printHeartbeat(GlobSimInfo* zinfo) {
    uint64_t cycles = zinfo->numPhases*zinfo->phaseLength;
    time_t curTime = time(nullptr);
    time_t elapsedSecs = curTime - startTime;
    time_t heartbeatSecs = curTime - lastHeartbeatTime;

    if (elapsedSecs == 0) return;
    if (heartbeatSecs == 0) return;

    char time[128];
    char hostname[256];
    gethostname(hostname, 256);

    std::ofstream hb("heartbeat");
    hb << "Running on: " << hostname << std::endl;
    hb << "Start time: " << ctime_r(&startTime, time);
    hb << "Heartbeat time: " << ctime_r(&curTime, time);
    hb << "Stats since start:" << std:: endl;
    hb << " " << zinfo->numPhases << " phases" << std::endl;
    hb << " " << cycles << " cycles" << std::endl;
    hb << " " << (cycles)/elapsedSecs << " cycles/s" << std::endl;
    hb << "Stats since last heartbeat (" << heartbeatSecs << "s):" << std:: endl;
    hb << " " << (cycles-lastCycles)/heartbeatSecs << " cycles/s" << std::endl;

    lastHeartbeatTime = curTime;
    lastCycles = cycles;
}



void LaunchProcess(uint32_t procIdx) {
    int cpid = fork();
    if (cpid) { //parent
        assert(cpid > 0);
        childInfo[procIdx].pid = cpid;
        childInfo[procIdx].status = PS_RUNNING;
    } else { //child
        // Set the child's vars and get the command
        // NOTE: We set the vars first so that, when parsing the command, wordexp takes those vars into account
        pinCmd->setEnvVars(procIdx);
        const char* inputFile;
        g_vector<g_string> args = pinCmd->getFullCmdArgs(procIdx, &inputFile);

        //Copy args to a const char* [] for exec
        int nargs = args.size()+1;
        const char* aptrs[nargs];

        trace(Harness, "Calling arguments:");
        for (unsigned int i = 0; i < args.size(); i++) {
            trace(Harness, " arg%d = %s", i, args[i].c_str());
            aptrs[i] = args[i].c_str();
        }
        aptrs[nargs-1] = nullptr;

        //Chdir to process dir if needed
        if (perProcessDir) {
            std::stringstream dir_ss;
            dir_ss << "p" << procIdx << "/";
            int res = chdir(dir_ss.str().c_str());
            if (res == -1) {
                perror("Coud not chdir");
                panic("chdir to %s failed", dir_ss.str().c_str());
            }
        }

        //Input redirection if needed
        if (inputFile) {
            int fd = open(inputFile, O_RDONLY);
            if (fd == -1) {
                perror("open() failed");
                panic("Could not open input redirection file %s", inputFile);
            }
            dup2(fd, 0);
        }

        /* In a modern kernel, we must disable address space randomization. Otherwise,
         * different zsim processes will load zsim.so on different addresses,
         * which would be fine except that the vtable pointers will be different
         * per process, and virtual functions will not work.
         *
         * WARNING: The harness itself is run with randomization on, which should
         * be fine because it doesn't load zsim.so anyway. If this changes at some
         * point, we'll need to have the harness be executed via a wrapper that just
         * changes the personalily and forks, or run the harness with setarch -R
         */
        if (!aslr) {
            //Get old personality flags & update
            int pers = personality(((unsigned int)-1) /*returns current pers flags; arg is a long, hence the cast, see man*/);
            if (pers == -1 || personality(pers | ADDR_NO_RANDOMIZE) == -1) {
                perror("personality() call failed");
                panic("Could not change personality to disable address space randomization!");
            }
            int newPers = personality(((unsigned int)-1));
            if ((newPers & ADDR_NO_RANDOMIZE) == 0) panic("personality() call was not honored! old 0x%x new 0x%x", pers, newPers);
        }

        if (execvp(aptrs[0], (char* const*)aptrs) == -1) {
            perror("Could not exec, killing child");
            panic("Could not exec %s", aptrs[0]);
        } else {
            panic("Something is SERIOUSLY wrong. This should never execute!");
        }
    }
}


void dump_IR_SR_stat(){
    load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
    if (lg_p->num_loadgen==0) {
        return;
    }
    glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();
    std::ofstream f("IRSR_dump.csv");
    std::ofstream f2("lg_clk_slack.txt");
    info("sampling phase count: %d",nicInfo->sampling_phase_index);
    f<<"IR,SR,cq_size,ceq_size,\n";
    for(uint32_t ii=0; ii<nicInfo->sampling_phase_index; ii++){
        f << nicInfo->IR_per_phase[ii] <<","<<nicInfo->SR_per_phase[ii]<<","<<nicInfo->cq_size_per_phase[ii]<<","
        <<nicInfo->ceq_size_per_phase[ii]<<",\n";

        f2 << nicInfo->lg_clk_slack[ii] << std::endl;
    }
    f.close();
    f2.close();

}

void generate_raw_timestamp_files(bool run_success){


	if(run_success){
		//put a dummy file for processing script to know run finished
		std::ofstream ff("run_success");
		ff<<"run_success"<<std::endl;
		ff.close();
	}

    GlobSimInfo* zinfo  = static_cast<GlobSimInfo*>(gm_get_glob_ptr());
	glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();

	if(nicInfo->memtype!=0){ // this hangs when using simple mem
    	for(int i=0; i<(nicInfo->num_controllers); i++) {
    	    std::ofstream f("memory_controller_"+std::to_string(i)+"_bandwidth.txt");
    	    int j=0;
    	    while (zinfo->mem_bwdth[i][j]!=100){
    	        f << zinfo->mem_bwdth[i][j] << std::endl;
    	        j++;
    	        
    	    }
    	    info("%d",j);
    	    f.close();
    	}
	}

    
    for(int i=0; i<MAX_NUM_CORES; i++) {
        if(nicInfo->nic_elem[i].ts_nic_idx) {
			if(run_success){
            	assert(nicInfo->nic_elem[i].ts_idx*2 == nicInfo->nic_elem[i].ts_nic_idx*5);
            	assert(nicInfo->nic_elem[i].phase_nic_idx == nicInfo->nic_elem[i].ts_nic_idx);
            	assert(nicInfo->nic_elem[i].ts_idx/5 == nicInfo->nic_elem[i].phase_idx);
			}
			else{
				//run failed. sync counts for ts and ts_nic
				while(nicInfo->nic_elem[i].ts_idx*2 > nicInfo->nic_elem[i].ts_nic_idx*5){
					nicInfo->nic_elem[i].ts_idx--;
				}
				while(nicInfo->nic_elem[i].ts_idx*2 < nicInfo->nic_elem[i].ts_nic_idx*5){
					nicInfo->nic_elem[i].ts_nic_idx--;
				}

			}
            std::ofstream f("timestamps_core_"+std::to_string(i)+".txt");
            int temp=0;

            int jstart = (nicInfo->warmup_packets)*5;
            for (int j=jstart; j<nicInfo->nic_elem[i].ts_idx; j++) {
                if(j%5==0){
                    f << "\nrequest " << temp << ": ";
                    temp++;
                }
                f << nicInfo->nic_elem[i].ts_queue[j] << " ";
            }
            f.close();



            f.open("timestamps_nic_core_"+std::to_string(i)+".txt");
            int temp1=0;
            jstart = (nicInfo->warmup_packets)*2;
            for (int j=jstart; j<nicInfo->nic_elem[i].ts_nic_idx; j++) {
                if(j%2==0){
                    /*
                    if (j >= 2) {//add phases
                        f<< nicInfo->nic_elem[i].phase_queue[(j - 2) / 2] << " ";
                    }
                    */
                    f << "\nrequest " << temp1 << ": ";
                    temp1++;
                }
                f << nicInfo->nic_elem[i].ts_nic_queue[j] << " ";
                if (j % 2 == 1) {
                    int jtmp = (j - 1);
                    //uint32_t start_phase = (nicInfo->nic_elem[i].phase_nic_queue[jtmp]));
                    //uint32_t done_phase = (nicInfo->nic_elem[i].phase_nic_queue[jtmp+1]));
                    uint32_t phases = ((nicInfo->nic_elem[i].phase_nic_queue[jtmp + 1]) - (nicInfo->nic_elem[i].phase_nic_queue[jtmp])) + 1;
                    f << phases << " ";
                }
            }
            //f << nicInfo->nic_elem[i].phase_queue[((nicInfo->nic_elem[i].ts_nic_idx) - 2) / 2] << " ";
            f.close();

            f.open("bound_phase_core_"+std::to_string(i)+".txt");
            temp=0;
            for (int j=0; j<nicInfo->nic_elem[i].phase_nic_idx; j+=2) {
                f << "\nrequest " << temp << ": ";
                f << nicInfo->nic_elem[i].phase_nic_queue[j] << " ";
                f << nicInfo->nic_elem[i].phase_queue[temp] << " ";
                f << nicInfo->nic_elem[i].phase_nic_queue[j+1] << " ";
                temp++;
            }
            f.close();
			if(run_success){
           		assert(nicInfo->nic_elem[i].phase_nic_idx==nicInfo->nic_elem[i].phase_idx*2);
			}

        }
    }

	
    std::ofstream latency_hist_file("latency_hist.txt");
    for(int i=0; i<zinfo->numCores; i++) {
        std::ofstream f;
        f.open("bound-weave_skew_core_"+std::to_string(i)+".txt");
        int temp=0;
        auto ooocore = (OOOCore*)(zinfo->cores[i]);
        f << "Started counting from phase " << ooocore->start_cnt_phases;
        for (int j=0; j<ooocore->cycle_adj_idx; j+=2) {
            f << "\nbound phase: " << temp << ": ";
            f << ooocore->cycle_adj_queue[j] << " ";
            f << ooocore->cycle_adj_queue[j+1] << " ";
            temp++;
        }
        f << "\n num phases from zinfo: " << zinfo->numPhases;
        f.close();
    }

	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
	uint64_t average_interval = (lg_p->sum_interval) / (nicInfo->latencies_size);
	std::cout<<"average interval: "<<average_interval<<std::endl;

	latency_hist_file <<"average interval: "<<average_interval<<std::endl;
	latency_hist_file.close();
}


int main(int argc, char *argv[]) {
    if (argc == 2 && std::string(argv[1]) == "-v") {
        printf("%s\n", ZSIM_BUILDVERSION);
        exit(0);
    }

    InitLog("[H] ", nullptr /*log to stdout/err*/);
    info("Starting zsim, built %s (rev %s)", ZSIM_BUILDDATE, ZSIM_BUILDVERSION);
    startTime = time(nullptr);

    if (argc != 2) {
        info("Usage: %s config_file", argv[0]);
        exit(1);
    }

    //Canonicalize paths --- because we change dirs, we deal in absolute paths
    const char* configFile = realpath(argv[1], nullptr);
    const char* outputDir = getcwd(nullptr, 0); //already absolute

    Config conf(configFile);

    if (atexit(exitHandler)) panic("Could not register exit handler");

    signal(SIGSEGV, sigHandler);
    signal(SIGINT,  sigHandler);
    signal(SIGABRT, sigHandler);
    signal(SIGTERM, sigHandler);

    signal(SIGCHLD, chldSigHandler);

    //SIGUSR1 is used by children processes when they want to get a debugger session started;
    struct sigaction debugSa;
    debugSa.sa_flags = SA_SIGINFO;
    sigemptyset(&debugSa.sa_mask); //NOTE: We might want to start using sigfullsets in other signal handlers to avoid races...
    debugSa.sa_sigaction = debugSigHandler;
    if (sigaction(SIGUSR1, &debugSa, nullptr) != 0)
        panic("sigaction() failed");

    waitid(P_ALL, 0, nullptr, WEXITED);

    //Remove all zsim.log.* files (we append to them, and want to avoid outputs from multiple simulations)
    uint32_t removedLogfiles = 0;
    while (true) {
        std::stringstream ss;
        ss << "zsim.log." << removedLogfiles;
        if (remove(ss.str().c_str()) != 0) break;
        removedLogfiles++;
    }
    if (removedLogfiles) info("Removed %d old logfiles", removedLogfiles);

    uint32_t gmSize = conf.get<uint32_t>("sim.gmMBytes", (1<<14) /*default 1024MB*/);
    info("Creating global segment, %d MBs", gmSize);
    int shmid = gm_init(((size_t)gmSize) << 20 /*MB to Bytes*/);
    info("Global segment shmid = %d", shmid);
    //fprintf(stderr, "%sGlobal segment shmid = %d\n", logHeader, shmid); //hack to print shmid on both streams
    //fflush(stderr);

    trace(Harness, "Created global segment, starting pin processes, shmid = %d", shmid);

    //Do we need per-process direcories?
    perProcessDir = conf.get<bool>("sim.perProcessDir", false);

    if (perProcessDir) {
        info("Running each process in a different subdirectory"); //p0, p1, ...
    }

    bool deadlockDetection;
    bool attachDebugger = conf.get<bool>("sim.attachDebugger", false);

    if (attachDebugger) {
        info("Pausing PIN to attach debugger, and not running deadlock detection");
        deadlockDetection = false;
    } else {
        deadlockDetection = conf.get<bool>("sim.deadlockDetection", true);
    }

    info("Deadlock detection %s", deadlockDetection? "ON" : "OFF");

    aslr = conf.get<bool>("sim.aslr", false);
    if (aslr) info("Not disabling ASLR, multiprocess runs will fail");


    //Create children processes
    pinCmd = new PinCmd(&conf, configFile, outputDir, shmid);
    uint32_t numProcs = pinCmd->getNumCmdProcs();

    for (uint32_t procIdx = 0; procIdx < numProcs; procIdx++) {
        LaunchProcess(procIdx);
    }

    if (numProcs == 0) panic("No process config found. Config file needs at least a process0 entry");

    //Wait for all processes to finish
    int sleepLength = 10;
    GlobSimInfo* zinfo = nullptr;
    int32_t secsStalled = 0;

    int64_t lastNumPhases = 0;

    glob_nic_elements* nicInfo;

    while (getNumChildren() > 0) {
        if (!gm_isready()) {
            usleep(1000);  // wait till proc idx 0 initializes everyhting
            continue;
        }

        if (zinfo == nullptr) {
            zinfo = static_cast<GlobSimInfo*>(gm_get_glob_ptr());
            globzinfo = zinfo;
            info("Attached to global heap");
        }

        printHeartbeat(zinfo);  // ensure we dump hostname etc on early crashes

        int left = sleep(sleepLength);
        int secsSlept = sleepLength - left;
        //info("Waking up, secs elapsed %d", secsSlept);

        __sync_synchronize();

        uint32_t activeProcs = zinfo->globalActiveProcs;
        uint32_t ffProcs = zinfo->globalFFProcs;
        uint32_t sffProcs = zinfo->globalSyncedFFProcs;
        bool simShouldAdvance = (ffProcs < activeProcs) && (sffProcs == 0);

        int64_t numPhases = zinfo->numPhases;

        if (deadlockDetection) {
            if (simShouldAdvance) {
                //info("In deadlock check zone");
                if (numPhases <= lastNumPhases) {
                    secsStalled += secsSlept;
                    if (secsStalled > 10) warn("Stalled for %d secs so far", secsStalled);
                } else {
                    //info("Not stalled, did %ld phases since last check", numPhases-lastNumPhases);
                    lastNumPhases = numPhases;
                    secsStalled = 0;
                }
            } else if (activeProcs) {
                if (numPhases == lastNumPhases) info("Some fast-forwarding is going on, not doing deadlock detection (a: %d, ff: %d, sff: %d)", activeProcs, ffProcs, sffProcs);
                lastNumPhases = numPhases;
            } //otherwise, activeProcs == 0; we're done
        }

        printHeartbeat(zinfo);

        //This solves a weird race in multiprocess where SIGCHLD does not always fire...
        int cpid = -1;
        while ((cpid = waitpid(-1, nullptr, WNOHANG)) > 0) {
            eraseChild(cpid);
            info("Child %d done (in-loop catch)", cpid);
        }

        if (secsStalled > 120) {
            warn("Deadlock detected, killing children");
            sigHandler(SIGINT);
            exit(42);
        }

        nicInfo = (glob_nic_elements*)gm_get_nic_ptr();

        if (!nicInfo->nic_egress_proc_on) {
            int temp=0;
            for (int i = 0; i < MAX_CHILDREN; i++) {
                if (childInfo[i].status == PS_RUNNING)
                    temp++;
            }
            if (temp == nicInfo->expected_non_net_core_count + 1) {
                info("Attempting graceful termination");
                for (int i = 0; i < MAX_CHILDREN; i++) {
                    int cpid = childInfo[i].pid;
                    if (childInfo[i].status == PS_RUNNING) {
                        info("Killing process %d", cpid);
                        kill(-cpid, SIGKILL);
                        usleep(100000);
                        kill(cpid, SIGKILL);
                    }
                }
            }
        }
    }


    nicInfo->sim_end_time = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = (nicInfo->sim_end_time) - (nicInfo->sim_start_time);
    std::cout << "sim elapsed time: " << elapsed_seconds.count() << "s" << std::endl;

    if(nicInfo->out_of_rbuf){
        std::cout<<"\
         :::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n\
         :::::SIM TERMINATED WITH OUT OF RECV BUFFER sim terminated with out of recv_buffer:::::\n\
         :::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::"<<std::endl;
		load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
        if (lg_p->num_loadgen > 0) {
            generate_raw_timestamp_files(false);
        }

    }
    else{
        load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
        if (lg_p->num_loadgen > 0) {
            generate_raw_timestamp_files(true);
        }
    }
    dump_IR_SR_stat();


    uint32_t exitCode = 0;
    if (termStatus == OK) {
        info("All children done, exiting");
    } else {
        info("Graceful termination finished, exiting");
        exitCode = 1;
    }



    if (zinfo && zinfo->globalActiveProcs) warn("Unclean exit of %d children, termination stats were most likely not dumped", zinfo->globalActiveProcs);
    exit(exitCode);
}

