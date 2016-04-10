// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// signals.c: windows stubs for POSIX signals
//
// Copyright (c) 2016 Grzegorz Suder <suder.grzegorz@gmail.com>
//
// This file is free software: you may copy, redistribute and/or modify it  
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your  
// option) any later version.  
//
// This file is distributed in the hope that it will be useful, but  
// WITHOUT ANY WARRANTY; without even the implied warranty of  
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License  
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "dump1090.h"
#include "logger.h"
#include <signal.h>

static BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
    Modes.exit = 1;
    log_with_timestamp("Caught CTRL-C, shutting down..\n");

    return TRUE;
}

void posix_signal(int sigid, void* hdlr)
{
    if (SIGINT == sigid)
    {
        SetConsoleCtrlHandler(HandlerRoutine, TRUE);
    }
}

void sigintHandler(int dummy) {

}

void sigtermHandler(int dummy) {

}

void sigWinchCallback() {

}