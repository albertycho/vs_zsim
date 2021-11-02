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
                panic("Child %d (idx %d) exit was anomalous, killing simulation", cpid, idx);
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

    uint32_t gmSize = conf.get<uint32_t>("sim.gmMBytes", (1<<10) /*default 1024MB*/);
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
    }

    glob_nic_elements* nicInfo = (glob_nic_elements*)gm_get_nic_ptr();

    nicInfo->sim_end_time = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = (nicInfo->sim_end_time) - (nicInfo->sim_start_time);
    std::cout << "sim elapsed time: " << elapsed_seconds.count() << "s" << std::endl;

    /// latency stat output
    info("writing to map_latency_file");
    std::ofstream map_latency_file("map_latency.txt");


    assert(nicInfo->hist_interval != 0);
    uint64_t hist_width = ((nicInfo->max_latency) / (nicInfo->hist_interval)) + 1;
    //uint64_t* hist_counters = new uint64_t[hist_width];
    //uint64_t* sorted_latencies = new uint64_t[nicInfo->latencies_size];
    //using new or normal calloc causes segfault when going over 10000 packets with herd. using gm_calloc again..
    uint64_t* hist_counters = gm_calloc<uint64_t>(hist_width);
    uint64_t * sorted_latencies = gm_calloc<uint64_t>(nicInfo->latencies_size);

	uint64_t latency_sum=0;
	uint64_t service_time_sum=0;

    uint64_t * sorted_service_time_all = gm_calloc<uint64_t>(nicInfo->latencies_size);

    //gives me garbage if I don't clear
    for (uint64_t iii = 0; iii < hist_width; iii++) {
        hist_counters[iii] = 0;
    }

    for (uint64_t iii = 0; iii < nicInfo->latencies_size; iii++) {
        map_latency_file << nicInfo->latencies[iii] << std::endl;
        uint64_t tmp_index = (nicInfo->latencies[iii] / nicInfo->hist_interval);
        hist_counters[tmp_index]++;

        bool insert_at_begin = true;
        for (uint64_t jjj = iii; jjj > 0; jjj--) {
            if (jjj == 0) {
                info("jjj not supposed to be 0");
            }
            if (sorted_latencies[jjj - 1] > nicInfo->latencies[iii]) {
                sorted_latencies[jjj] = sorted_latencies[jjj - 1];
            }
            else {
                sorted_latencies[jjj] = nicInfo->latencies[iii];
                insert_at_begin = false;
                break;
            }
        }
        if (insert_at_begin) {
            sorted_latencies[0] = nicInfo->latencies[iii];
        }
		latency_sum+=nicInfo->latencies[iii];
    }

    map_latency_file.close();

	uint64_t latency_mean = latency_sum / (nicInfo->latencies_size);

    info("writing to latency_hist file");
    std::ofstream latency_hist_file("latency_hist.txt");

    uint64_t median_index = (nicInfo->latencies_size) / 2;
    uint64_t percentile_80_index = ((nicInfo->latencies_size) * 80) / 100;
    uint64_t percentile_90_index = ((nicInfo->latencies_size) * 90) / 100;
    uint64_t percentile_95_index = ((nicInfo->latencies_size) * 95) / 100;
    uint64_t percentile_99_index = ((nicInfo->latencies_size) * 99) / 100;

    latency_hist_file << "mean        : " << latency_mean << std::endl;
    latency_hist_file << "median        : " << sorted_latencies[median_index] << std::endl;
    latency_hist_file << "80-percentile : " << sorted_latencies[percentile_80_index] << std::endl;
    latency_hist_file << "90-percentile : " << sorted_latencies[percentile_90_index] << std::endl;
    latency_hist_file << "95-percentile : " << sorted_latencies[percentile_95_index] << std::endl;
    latency_hist_file << "99-percentile : " << sorted_latencies[percentile_99_index] << std::endl;

    latency_hist_file << std::endl;

    for (uint64_t iii = 0; iii < hist_width; iii++) {
        latency_hist_file << iii * nicInfo->hist_interval << "," << hist_counters[iii] << "," << std::endl;
    }
    latency_hist_file.close();


/*
	info("start writing to service time file");
    std::ofstream service_time_file("service_times.txt");
    std::ofstream st_stat_file("service_times_stats.txt");
    std::ofstream cq_spin_stat_file("cq_check_spin_count.txt");

	uint64_t total_iter_count=0;
	for(uint64_t kkk = 3; kkk<nicInfo->expected_core_count+3;kkk++){
		service_time_file << "CORE " << kkk-1 << std::endl;
		st_stat_file << "CORE " << kkk-1 << std::endl;
		cq_spin_stat_file << "CORE " << kkk-1 << std::endl;
		cq_spin_stat_file << "rmc_checkcq_spin: "<<nicInfo->nic_elem[kkk].cq_check_spin_count << std::endl;
		cq_spin_stat_file << "inner_loop_spin : "<< nicInfo->nic_elem[kkk].cq_check_inner_loop_count<<std::endl;
        cq_spin_stat_file << "outer_loop_spin : " << nicInfo->nic_elem[kkk].cq_check_outer_loop_count << std::endl;
		
		uint32_t sorted_service_time[65000]; //fixed number for now
		for (uint64_t lll = 0; lll<nicInfo->nic_elem[kkk].st_size+10;lll++){
			sorted_service_time[lll] = 0;
		}

	    for (uint64_t iii = 0; iii < nicInfo->nic_elem[kkk].st_size; iii++){
			service_time_file << nicInfo->nic_elem[kkk].service_times[iii]<<std::endl;

			bool insert_at_begin = true;
        	for (uint64_t jjj = iii; jjj > 0; jjj--) {
        	    if (jjj == 0) {
        	        info("jjj not supposed to be 0");
        	    }
        	    if (sorted_service_time[jjj - 1] > nicInfo->nic_elem[kkk].service_times[iii]) {
        	        sorted_service_time[jjj] = sorted_service_time[jjj - 1];
        	    }
        	    else {
        	        sorted_service_time[jjj] = nicInfo->nic_elem[kkk].service_times[iii];
        	        insert_at_begin = false;
        	        break;
        	    }
        	}
        	if (insert_at_begin) {
        	    sorted_service_time[0] = nicInfo->nic_elem[kkk].service_times[iii];
        	}

        	bool insert_at_begin_for_all = true;
        	for (uint64_t jjj = total_iter_count; jjj > 0; jjj--) {
        	    if (jjj == 0) {
        	        info("jjj not supposed to be 0");
        	    }
        	    if (sorted_service_time_all[jjj - 1] > nicInfo->nic_elem[kkk].service_times[iii]) {
        	        sorted_service_time_all[jjj] = sorted_service_time_all[jjj - 1];
        	    }
        	    else {
        	        sorted_service_time_all[jjj] = nicInfo->nic_elem[kkk].service_times[iii];
        	        insert_at_begin_for_all = false;
        	        break;
        	    }
        	}
        	if (insert_at_begin_for_all) {
        	    sorted_service_time_all[0] = nicInfo->nic_elem[kkk].service_times[iii];
        	}


			total_iter_count++;
			service_time_sum+=nicInfo->nic_elem[kkk].service_times[iii];
		}
		int med_index = nicInfo->nic_elem[kkk].st_size / 2;
		int index_80 = nicInfo->nic_elem[kkk].st_size * 80 / 100;
		int index_90 = nicInfo->nic_elem[kkk].st_size * 90 / 100;
		st_stat_file << "median: " << sorted_service_time[med_index] << std::endl;
		st_stat_file << "80perc: " << sorted_service_time[index_80] << std::endl;
		st_stat_file << "90perc: " << sorted_service_time[index_90] << std::endl;

	}

	int med_index = total_iter_count / 2 ;
	int index_80 = total_iter_count * 80 / 100 ;
	int index_90 = total_iter_count * 90 / 100 ;
	int index_95 = total_iter_count * 95 / 100 ;
	int index_99 = total_iter_count * 99 / 100 ;

	uint64_t service_time_mean = service_time_sum / total_iter_count;

	st_stat_file << "ALL CORE STAT" << std::endl;
	st_stat_file << "mean: " << service_time_mean << std::endl;
	st_stat_file << "median: " << sorted_service_time_all[med_index] << std::endl;
	st_stat_file << "80perc: " << sorted_service_time_all[index_80] << std::endl;
	st_stat_file << "90perc: " << sorted_service_time_all[index_90] << std::endl;
	st_stat_file << "95perc: " << sorted_service_time_all[index_95] << std::endl;
	st_stat_file << "99perc: " << sorted_service_time_all[index_99] << std::endl;


	service_time_file.close();
	st_stat_file.close();
	cq_spin_stat_file.close();
*/
int aggr=0;
    for(int i=0; i<MAX_NUM_CORES; i++) {
        if(nicInfo->nic_elem[i].ts_idx) {
            //assert(nicInfo->nic_elem[i].ts_idx/4 == nicInfo->nic_elem[i].ts_nic_idx/2);
            std::ofstream f("timestamps_core_"+std::to_string(i)+".txt");
            int temp=0;
            for (int j=0; j<nicInfo->nic_elem[i].ts_idx; j++) {
                if(j%4==0){
                    f << "\nrequest " << temp << ": ";
                    temp++;
                }
                f << nicInfo->nic_elem[i].ts_queue[j] << " ";
            }
            f.close();

            f.open("timestamps_nic_core_"+std::to_string(i)+".txt");
            int temp1=0;
            for (int j=0; j<nicInfo->nic_elem[i].ts_nic_idx; j++) {
                if(j%2==0){
                    if (j > 2) {//add phases
                        f<< nicInfo->nic_elem[i].phase_queue[(j - 2) / 2] << " ";
                    }
                    f << "\nrequest " << temp1 << ": ";
                    temp1++;
                }
                f << nicInfo->nic_elem[i].ts_nic_queue[j] << " ";
            }
            f << nicInfo->nic_elem[i].phase_queue[((nicInfo->nic_elem[i].ts_nic_idx) - 2) / 2] << " ";
            f.close();

            //assert(temp == temp1);
        }
        info("%d",nicInfo->nic_elem[i].ts_nic_idx);
        aggr += nicInfo->nic_elem[i].ts_nic_idx;
    }

    info("%d",aggr);

	
	load_generator* lg_p = (load_generator*)gm_get_lg_ptr();
	uint64_t average_interval = (lg_p->sum_interval) / (nicInfo->latencies_size);
	std::cout<<"average interval: "<<average_interval<<std::endl;

//	for (uint64_t iii = 0; iii < nicInfo->latencies_size; iii++) {
//        map_latency_file << nicInfo->latencies[iii] << std::endl;


    gm_free(hist_counters);
    gm_free(sorted_latencies);
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

