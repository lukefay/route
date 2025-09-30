# route
ROUTE protocol as per ATSC A/331.  
This is updated code that originally came from Thomas Stockhammer, available at https://github.com/haudiobe/ATSC_ROUTE
Only the receiver code from that URL is updated to comply with ATSC A/331 version of ROUTE.  (This is also available as RFC 9223 at https://www.rfc-editor.org/info/rfc9223).  Sender was not updated as live over the air captures provide transmitter streams.  This code binds to a socket, so playback of pcaps or live data (with Sony USB dongle) provide input IP stream data.  Both Source Flow and Repair Flow are supported.  Repair Flow support comes from nanorq library available at https://github.com/sleepybishop/nanorq.  A few edits were needed to allow for binary files and other errata, so that nanorq-stable library is included here. 

To compile on Windows OS
1. Install Project Studio (tested with version 2017 and 2022)
2. Load mad_fcl.sln from route directory
3. Install below library packages with NuGet tool in Visual Studio
   - pthreads version 2.9.1.4
   - expat version 2.1.0.11
   - rmt_curl version 7.45.0.2
   - zlib_native version 1.2.11
4.  Please pay attention to above versions of libraries
5.  Build solution

To compile on Linux OS
1. Install code in desired directory
2. Run command: sudo apt-get update
3. Install below libraries
   - sudo apt-get install libpthread-stubs0-dev
   - sudo apt-get install libexpat-dev
   - sudo apt-get install libcurl4-openssl-dev
   - sudo apt-get install libwebsockets-dev
4. Run Makefile in each sub-directory or from the top route directory

EXAMPLE Command line runs in Windows command line are:
>> a3route.exe -A -B:../DASH_Content50 -m:239.255.50.1 -p:50001 -t:0 -E -b:1 -Y:1 -v:4 -J:"Rcv_Log_MPD.txt" > logout1.txt

OR without verbose logging:

>> a3route.exe -A -B:../DASH_Content50 -m:239.255.50.1 -p:50001 -t:0 -E -b:1 -Y:1 -v:0 -J:"Rcv_Log_MPD.txt"

EXAMPLE Command line runs in Linux:
>> ./bin/flute.exe -A -B:../DASH_Content50 -m:239.255.50.1 -p:50001 -t:0 -E -b:1 -Y:1 -v:4 -J:"Rcv_Log_MPD.txt" > logout1.txt

OR without verbose logging:

>> ./bin/flute.exe -A -B:../DASH_Content50 -m:239.255.50.1 -p:50001 -t:0 -E -b:1 -Y:1 -v:0 -J:"Rcv_Log_MPD.txt"


To render media, Apache2.4 server needs to be installed and use a Chrome Browser. Entry URL pages are located in the /Receiver directory and can be symbolic linked from the Apache24/htdocs directory to /Receiver/index.php page.  Apache24 extensions for the latest php should be added.  Python3.x or newer should also be installed. 
Absolute paths to executables need to be checked or corrected in the following files within the Receiver directory.
1. ProcessROUTE.php
2. index.php
3. PlayFFMPEG.php
4. updateTime.php
5. /ReceiverConfig/onloadfunc.php
6. /RecevierConfig/updateTime.php


