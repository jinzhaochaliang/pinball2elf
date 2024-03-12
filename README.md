# pinball2elf 
*pinball2elf* is a tool to create stand-alone Linux binaries from checkpoints for arbitrary portions of execution of other Linux programs . The checkpoints it converts are called ‘pinballs’ and they are generated by “Program Record Replay Toolkit” (aka PinPlay kit) that is available at [Intel PinPlay](http://www.pinplay.org). *pinball2elf* was developed by Alexander Isaev between 2015-2017 while he was at Intel. Some enhancements and use-case development was done by Harish Patil (Intel) with input from Trevor Carlson(NUS), Karthik Sankaranarayanan(Intel), and Wim Heirman(Intel).

[8-threaded SPEC2017(OpenMP) ELFies] (TBD)

[ELFie CGO2021 paper](https://heirman.net/papers/patil2021elfies.pdf)

[ELFie CGO2021 presentation recording](https://www.youtube.com/watch?v=MYxhvRmVoSw&list=PLadGdFFn83gXCQAj8D8LuabxOu3XMbgPJ&index=11)

## Pre-requisites
  Make sure:'perf' is installed and /usr/include/linux/hw\_breakpoint.h exists and is not empty.

pinball2elf has been tested with the following OS/tool/hardware combinations:

 1. OS: Ubuntu 18.04, gcc: 7.5.0, g++: 7.5.0, 'perf' 4.15.18, GNU ld 2.30 : Skylake 

 2. OS: Ubuntu 16.04, gcc: 5.4.0, g++: 5.4.0, 'perf' 4.4.228, GNU ld 2.26.1 : Broadwell 

### Known issues 
   
 1. OS: Ubuntu 16.04, gcc: 8.4.0, g++: 8.4.0, GNU ld 2.26.1

      Loader errors: *relocation truncated to fit: R_X86_64_32S against `data`*

## Quick Start Example
1. Build pinball2elf

    `cd src`
    
    `make all`

    `cd ..`


2. Test with a single-threaded pinball

    `cd examples/ST`

    `./testST.sh`

This shows:
### Creation and running of *basic* elfie
------------------------------------------

```
  Running ../../scripts/pinball2elf.basic.sh pinball.st/log_0
  Running ./pinball.st/log_0.elfie
ELFIE_COREBASE=0
core_base: 0
 pid: 4203
process_callback() [ inside ELFie] called. Num_threads: 1
 tid: 4203
thread_callback() [ inside ELFie] called for thread 0
Hello world 1
---------------------
```
### Creation and running of *perf* elfie
-----------------------------------------
```
  Running ../../scripts/pinball2elf.perf.sh pinball.st/log_0 st
  export ELFIE_PERFLIST=0:0,0:1,1:1
   [ based on /usr/include/linux/perf_event.h
     Comma separated pairs 'perftype:counter'
     perftype: 0 --> HW 1 --> SW
     HW counter: 0 --> PERF_COUNT_HW_CPU_CYCLES
     HW counter: 1 --> PERF_COUNT_HW_CPU_INSTRUCTIONS
     SW counter: 0 --> PERF_COUNT_SW_CPU_CLOCK
      ... <see perf_event.h:'enum perf_hw_ids' and 'enum perf_sw_ids']
  Running ./pinball.st/log_0.perf.elfie
ELFIE_COREBASE=0
ELFIE_PERFLIST=0:0,0:1,1:1
Will set affinity using core_base: 0
  elfie tid: 0  core id: 0
------------------------------------------------
------------------------------------------------
Hello world 1
    graceful exit SUCCESS
```
### Show performance counters reported in *st.0.perf.txt*
----------------------------------------------------------
```
  Performance counters reported in st.0.perf.txt
ROI start: TSC 74744470196596112
Thread start: TSC 74744470231500862
------------------------------------------------
Simulation end: TSC 74744470232488632
        Sim-end-icount 3922
hw_cpu_cycles:45584 hw_instructions:3964 sw_task_clock:134934 
------------------------------------------------
Thread end: TSC 74744470232782076
ROI end: TSC 74744470232944672
hw_cpu_cycles:49566 hw_instructions:4647 sw_task_clock:184142 
```
--------------------------------------------------------
## ELFie creation basics
The tool *pinball2elf*, supports three types of callbacks:
```
  -p FUNC, --process-cbk FUNC      name of process start callback
  -e FUNC, --process-exit-cbk FUNC name of process exit callback
  -t FUNC, --thread-cbk FUNC       name of thread start callback
```
### Scripts provided
```
scripts/
├── pinball2elf.basic.sh
├── pinball2elf.perf.sh
└── pinball2elf.sim.sh
```
 These scripts use the environment variable **P2E\_TEMP** as the location (default '/tmp') of the directory to use to store intermediate temporary files.
### Instrumentation code
```
instrumentation/
├── basic_callbacks.c
├── environ.c
├── perf_callbacks.c
├── set_heap.c
└── sim_callbacks.c

environ.c : supports the processing of ELFIE_* environment variables.
set_heap.c : supports the handling of brk() system call behavior based on SYSSTATE
```
 A custom callback file is created for each ELFie using the instrumentation files above. That file is copied to the current working directory for debugging/viewing.

### ELFie types supported
1. **basic ELFie**
```
script: pinball2elf.basic.sh
  Usage: scripts/pinball2elf.basic.sh pinball
instrumentation: basic_callbacks.c
callbacks used:
-p elfie_on_start: outputs informational messages; processes SYSSTATE
-t elfie_on_thread_start: outputs informational messages
Graceful exit : NO

 Customized basic_callbacks.c file is copied to the working directory for debugging/viewing.
```

2. **Simulator(sim) ELFie**
```
script: pinball2elf.sim.sh
  Usage: scripts/pinball2elf.sim.sh pinball <message>
instrumentation: sim_callbacks.c
callbacks used:
-p elfie_on_start: processes SYSSTATE; checks if ELFIE_COREBASE is a positive number
-t elfie_on_thread_start:  sets affinity for thread etid to 'COREBASE+apptid'
Graceful exit : NO ( the simulator is supposed to end ELFie execution)

 Customized sim_callbacks.c  file is copied to the working directory for debugging/viewing.
```

3. **Performance monitoring(perf) ELFie**
```
script: pinball2elf.perf.sh
  Usage :  scripts/pinball2elf.perf.sh  pinball filename-prefix <use_warmup>
instrumentation: perf_callbacks.c
callbacks used:
-p elfie_on_start: processes SYSSTATE; checks if ELFIE_COREBASE is a positive number
                     Opens per-thread perf stats files
-t elfie_on_thread_start:  sets affinity for thread etid to 'COREBASE+apptid'
                 * sets up warmup/simulation end handlers
                 * Uses "ELFIE_WARMUP" to decide whether to use warmup.
                 * Uses "ELFIE_PCCONT" to decide how to end warmup/simulation regions
                 * sets up performance counters to end warmup/simulation regions
                      either using instruction counts or PC + count
                 * Based on ELFIE_PERFLIST, enables performance counting
                   (  based on /usr/include/linux/perf_event.h
                      perftype: 0 --> HW 1 --> SW
                      HW counter: 0 --> PERF_COUNT_HW_CPU_CYCLES
                      HW counter: 1 --> PERF_COUNT_HW_CPU_INSTRUCTIONS
                      SW counter: 0 --> PERF_COUNT_SW_CPU_CLOCK
                       ... <see perf_event.h:'enum perf_hw_ids' and 'enum perf_sw_ids')          
-e elfie_on_exit: runs in the monitor thread, outputs final performance counter 
                  values for each application thread. 

A monitor thread is first created, it creates the main application thread and waits
 for it to exit. The main application thread creates other application threads if
  needed

Graceful exit : YES: either icount of pc+count used for exiting each thread.

 Customized perf_callbacks.c  file is copied to the working directory for debugging/viewing.
```

--------------------------------------------------------
## Input to *pinball2elf*

User-level checkpoints known as *pinballs* are input to the *pinbll2elf* tool. These are created using the [PinPlay](http://wwww.pinplay.org) tool kit. A *pinball* is a collection of files capturing the execution state of a "Region of Interest" (ROI) from an application execution. A typical sequence of commands to generate and test a *pinball* suitable for *pinball2elf* is shown below:
### Creating a *fat* pinball
 (Assuming 'bash' shell)
```
  export SDE_BUILD_KIT=<absolute path to the SDE kit (version 9.0 or higher>

  export LD_BIND_NOW=1 #to load any application dynamic libraries early

  $SDE_BUILD_KIT/sde -log -log:fat -log:mt -log:compressed bzip2 -log:basename pinball/foo <ROI specification> -- <application arguments>
```
### Test the *fat* pinball with *injection-less replay*
This step makes sure the pinball can be replayed *without injections* of any  system call side-effects.

```
$SDE_BUILD_KIT/sde -replay -replay:addr_trans -replay:injection 0  -replay:basename pinball/foo -- $SDE_BUILD_KIT/intel64/nullapp
```
If the ROI does any file inpout, this replay step may fail. You will need to extract from the pinball the necessary OS state that the ROI uses.

### (Optional) Create SYSSTATE directory for the pinball
* Build the pintool pinball-sysstate.so
```
    setenv SDE_BUILD_KIT to point to SDE version 9.0 or higher
    cd *pinball2elf-directory*

    cd pintool/SYSSTATE

    make
```

 This will create *$SDE_BUILD_KIT/intel64/sde-pinball-sysstate.so*.

#### Create SYSSTATE for the pinball
```
 $SDE_BUILD_KIT/sde64 -t64 $SDE_BUILD_KIT/intel64/sde-pinball-sysstate.so -replay -replay:basename pinball.st/log_0 -sysstate:out pinball.st/log_0 -- $SDE_BUILD_KIT/intel64/nullapp 
Will produce sysstate in pinball.st/log_0.sysstate
Producer pinball.st/log_0
Hello world 1
 ROI used pre-existing FD : 1
Creates:

pinball.st/log_0.sysstate/
└── workdir
    └── FD_1
```
#### Known issue
  *  During multi-threaded pinball replay, an extra monitoring thread is created to detect 'deadlocks' hence tools relying on thread count may get confused.
 Adding "-replay:deadlock_timeout 0" during replay prevents the creation of the extra monitoring thread.

#### Micro-architecture dependance of pinballs and ELFies

 A 'pinball' has the register state captured at recording time. This is either for the native x86 micro-architecture (if generated with [Pin](www.pinplay.org))
 or the emulated micro-architecture  (if generated with [SDE](http://www.intel.com/software/sde)). The pinball2elf tool transfers the register state in a pinball to the ELFie it creates. This register state gets restored before each ELFie-created thread jumps to its application code embedded inside the ELFie. 

* An ELFie will only run correctly on machines where the embedded register state  is valid.
* The binary used to generate a pinball is compiled for a specific micro-architecture. When creating a pinball using SDE, make sure to use the right  micro-architecture flag (e.g. *sde -skx* for a binary compiled for Skylake server) to embedd the right register state in the pinball and hence in the corresponding ELFie. This is important to preserve the performance characteristics of the original binary.



### (Optional) warmup specification: Create  *event_icoun
t.tid.txt* files for pinball
* Build the sde/pintool event_counter.so
```
    setenv SDE_BUILD_KIT to point to SDE version 9.0 or higher
    cd *pinball2elf-directory*

    cd pintool/EventCounter

    make
```

 This will create *$SDE_BUILD_KIT/intel64/sde-event-counter.so*
#### Create warmup/simulation specification for the pinball
* warmup ends/simulation starts at icount 3000 : -control start:icount:3000:global
* simulation ends at icount 3500 : -control stop:icount:3500:global
```
$SDE_BUILD_KIT/sde64 -t64 $SDE_BUILD_KIT/intel64/sde-event-counter.so -thread_count 1 -prefix pinball.st/log_0 -control start:icount:3000:global -control stop:icount:3500:global -replay -replay:addr_trans -replay:basename pinball.st/log_0 -- $SDE_BUILD_KIT/intel64/nullapp
Hello world 1
 global icount 2998 Sim-Start
 global icount 3496 Sim-End
```
**Creates event_icount file**
```
 cat pinball.st/log_0.event_icount.0.txt 
Sim-Start global_icount: 2998
Sim-Start tid: 0 icount 2998
Sim-End global_icount: 3496
Sim-End tid: 0 icount 3496
Fini  global_icount: 3922
Fini  tid: 0 icount 3922
```
#### Example of PC+count in warmup/simulation specification
* Specify warmup/simulation end using PC+count

 -control start:address:0x000814187:count84738:global -control stop:address:0x0009decb1:count34069:global

* Also monitor warmup-end and simulation-end PCs

-watch\_addr 0x000814187 -watch\_addr 0x0009decb1

```
RPB=<an 8-threaded pinball with known warmup-end/simulation end PC+count values>
$SDE_BUILD_KIT/sde64 -t $SDE_BUILD_KIT/sde-event-counter.so -thread_count 8  -prefix $RPB  -watch_addr 0x000814187 -watch_addr 0x0009decb1  -control start:address:0x000814187:count84738:global -control stop:address:0x0009decb1:count34069:global -replay -replay:addr_trans -xyzzy  -replay:deadlock_timeout 0 -replay:basename $RPB -- $SDE_BUILD_KIT/intel64/nullapp

Creates 8 $RPB.event_icount.tid.txt files: one for each thread
Example event_icount file:

cat $RPB.event_icount.3.txt 
Sim-Start global_icount: 1597134619
Sim-Start tid: 3 icount 217651514
        Marker  814187  global_addrcount: 84737
        Marker  814187 tid: 3 addrcount 12816
        Marker  9decb1  global_addrcount: 0
        Marker  9decb1 tid: 3 addrcount 0
Sim-End global_icount: 2400630567
Sim-End tid: 3 icount 278389598
        Marker  814187  global_addrcount: 116928
        Marker  814187 tid: 3 addrcount 12960
        Marker  9decb1  global_addrcount: 34067
        Marker  9decb1 tid: 3 addrcount 4368
Fini  global_icount: 2400722002
Fini  tid: 3 icount 278420906
Addr  814187  global_addrcount: 116928
Addr  814187 tid: 3 addrcount 12960
Addr  9decb1  global_addrcount: 34070
Addr  9decb1 tid: 3 addrcount 4368

```
## Debugging ELFies

The  pages  inside an ELFie containing  application code  are  marked  as  not loadable hence  tools,  such  as  the Gnu  debugger  (gdb),  can  not  *see*  application  pages  inside an  ELFie  right  away  after  the  initial  loading  of  an  ELFie. For  setting  a breakpoint  at  an  application  instruction,  the suggested  way  is  to  first  break  at *elfie\_on\_start()* where all application pages are guaranteed to be in memory and then  set  a  breakpoint  at  the  desired  application  address(hex). Symbolic  debugging of application code is  currently  not  supported  with  ELFies although pinball2elf can be extended to add application debug information  for  symbolic  debugging. ELFie  generation  scripts  make  sure  debug  information  does exist for ELFie callback routines hence they can be debugged symbolically (use the customized callbacks.c file copied to the working directory).  For  debugging  multi-threaded  ELFies  with  gdb,  first  doing  a  *set  detach-on-fork off* followed by *break elfie\_on\_thread\_start’ and using *info inferior* and *inferior N* commands works well.

A perf ELFie, creates a monitor thread which spawns application main thread and waits for it. For debugging a perf help, use 'set follow-fork-mode' child to break on 'elfie_thread_on_start' routine which is executed in the application master thread.

## Open issues
 ELFie execution sometimes ends pre-maturely (before reaching the expected instruction
 count). This could be because of many reasons:
 1. The execution diverges and tries to access code/data pages that were not captured.
 2. Operating system state is not fully captured for the system calls in the region to work correctly.

A common workaround is to find an *alternate* region which will hopefully avoid these issues. 
Contributions/suggestions to solve these open issues are most welcome!

## Project ideas
 1. Help solve 'open issues' listed above.
    - A more robust SYSSTATE capture: extend pintools/SYSSTATE/pinball-sysstate.cpp
    -  Extend pinball with OS state using ideas from [CRIU](http://www.criu.org)
 2. Symbolic debugging of application code inside an ELFie 
     Generate debug information for the application code inside the ELFie file.
 3. Similar tools for Windows and MacOS.
    Pinball generation is supported for Windows and MacOS as well. Consider writing a converter from pinball to *Portable Executable (PE)* format on Windows and *Mach-O* format on MacOS.

## Useful tips
- Since the ELFie uses lots of mmap calls to allocate each 4KiB page, it's possible to overrun the max number of vm maps. You can "fix" this by adding vm.max_map_count = 2097152 to /etc/sysctl.conf (from Jason Lowe-Power). See the [link](https://stackoverflow.com/questions/42889241/how-to-increase-vm-max-map-count) to reload the configuration after setting the new value.

#### Tips for creating a portable ELFie (that works on older processors, native or simulated)
1. Build your binary with for a generic x86 processor architecture: (gcc/g++) -march=x86_64 or -march=core2.
    - Adding '-static' will make sure all libraries are linked in statically so no surprises on moving to a different machine.
2. Generate pinball using "sde -nhm" so no new registers are saved in the initial pinball state.

## External Uses of ELFie : References
- [Public Release and Validation of SPEC CPU2017 PinPoints](https://arxiv.org/pdf/2112.06981.pdf)
- We have released the representative ELFies of a subset of the multi-threaded SPEC CPU2017 benchmarks [here](https://looppoint.github.io/hpca2023/)
