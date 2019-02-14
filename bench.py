import os
import sys
import time
import subprocess
from shutil import copy2
import argparse
import statistics

RED='\033[0;31m'
NC='\033[0m'

class Test:
    def __init__(self, folder, name, args):
        self.folder = folder
        self.name = name
        self.args = args
        
    def getNormal(self):
        return self.folder + "/normal_" + self.name
        
    def getModified(self):
        return self.folder + "/" + self.name
        
    def getArgs(self):
        return " " + self.args
    

TESTS = [   Test("/efs/home/vettorel/cilkbench/cilk5", "cholesky", "-n 2000 -z 4000"),
         #   Test("/home/daniele/cilkbench/cilk5", "nqueens", "13")
        ]
        
RESULTS_DIR = "./results/"

def getTime():
    return time.perf_counter()

def writeTimes(times, filename):
    times.sort()
    times = times[1:-1]
        
    file = open(filename,"w") 
    for time in times:
        file.write(str(time))
        file.write("\n")    
    file.close()
    return times

NUM_TESTS =  6  # Actual runs for benchmarking purposes.
NUM_DISCARD = 1 # Warmup runs (discarded). 

parser = argparse.ArgumentParser(description='Benchmark program')
parser.add_argument('-n', dest='numTests', default=NUM_TESTS, type=int)
parser.add_argument('-d', dest="numDiscard",default=NUM_DISCARD, type=int)
args = parser.parse_args()

NUM_TESTS = args.numTests + args.numDiscard
NUM_DISCARD = args.numDiscard

print("Running benchmarks " + str(NUM_TESTS - NUM_DISCARD) + " times after discarding " + str(NUM_DISCARD) + " runs")

doNormal = True
doModified = True
nothing = False

for test in TESTS:
    os.environ["CILK_NWORKERS"] = "1"
    os.environ["MHWM_FullSPDAG"] = "0"
    os.environ["MHWM_Source"] = "0"
    os.environ["MHWM_NumProcessors"] = "32"
    os.environ["MHWM_OutputDAG"] = "0"
    
    NORMAL_PROGRAM = test.getNormal() + test.getArgs() + " > /dev/null 2>&1"
    MODIFIED_PROGRAM = test.getModified() + test.getArgs() + " > /dev/null 2>&1" 

    RESULTS_NAME = "results_" + test.name
    RESULTS_NORMAL = RESULTS_NAME + "_normal"
    RESULTS_MODIFIED = RESULTS_NAME + "_modified"
    if not nothing:
        if doNormal:
            times = []

            for i in range(0, NUM_TESTS):
                start_time = getTime()

            #   subprocess.call([STITCHED_PROGRAM, STITCHED_PROGRAM_ARGS])
                os.system(NORMAL_PROGRAM);

                end_time = getTime()

                timediff = end_time - start_time

                print ("[" + test.name + "] Run (normal) " + str(i) + " completed: " + str(timediff));

                if i >= NUM_DISCARD:
                    times.append(timediff)  

                time.sleep(1.0)

            normalTimes = writeTimes(times, RESULTS_DIR + RESULTS_NORMAL + ".txt")


        if doModified:
            time.sleep(5)
            times = []

            for i in range(0, NUM_TESTS):
                start_time = getTime()

                #subprocess.call([SIMPLE_PROGRAM, SIMPLE_PROGRAM_ARGS])
                #try:
                #print ( subprocess.check_output(SIMPLE_PROGRAM))
                #except subprocess.CalledProcessError as e:
                #   print ("Stdout output:\n", e.cmd)
                os.system(MODIFIED_PROGRAM);

                end_time = getTime()

                timediff = end_time - start_time

                print ("[" + test.name + "] Run (modified) " + str(i) + " completed: " + str(timediff));

                if i >= NUM_DISCARD:
                    times.append(timediff)

                time.sleep(1.0)

            modifiedTimes = writeTimes(times, RESULTS_DIR + RESULTS_MODIFIED + ".txt")

    normalMedian = statistics.median(normalTimes)
    modifiedMedian = statistics.median(modifiedTimes)
    print("Normal median: " + str(normalMedian))
    print("Modified median: " + str(modifiedMedian))
    print(RED + "Ratio modified/normal: " + str(modifiedMedian/normalMedian) + NC)

    currTime = time.strftime("%y_%m_%d_%H_%M_%S")

    copy2(RESULTS_DIR + RESULTS_NORMAL + ".txt", "logs/" + RESULTS_NORMAL + currTime + ".txt")
    copy2(RESULTS_DIR + RESULTS_MODIFIED + ".txt", "logs/" + RESULTS_MODIFIED + currTime + ".txt")

    file = open("logs/results_info_" + test.name + "_"  + currTime + ".txt","w") 
    file.write("NORMAL PROGRAM = " + NORMAL_PROGRAM);
    file.write("\n");       
    file.write("MODIFIED PROGRAM = " + MODIFIED_PROGRAM);
    file.write("\n");
    file.close()

    #os.system("ministat " + (RESULTS_DIR + RESULTS_NORMAL + ".txt") + " " +
    #              (RESULTS_DIR + RESULTS_MODIFIED + ".txt"))