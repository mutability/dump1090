// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// compiler.h: windows stubs for compile POSIX functions
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

#include <signal.h>

#define __attribute__(x) 
#define inline __inline
#define signal(sigid, hdlr) posix_signal(sigid, hdlr) // workaround for using custom implementation of signal()
                                                      // (by default body of the function is being provided
                                                      // in Visual C++)
#define SIGWINCH 0
#define SIGPIPE 0