// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// posix_strings.c: windows stubs for POSIX string functions.
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

#include <Windows.h>

int strcasecmp(char const *s1, char const *s2)
{
    // Better safe than sorry...
    if (s1 == NULL || s2 == NULL)
        return -1;

    for ( ; ; s1++, s2++) {
        int result = tolower(*s1) - tolower(*s1);
        if (result != 0 || !*s1)
            return result;
    }
}