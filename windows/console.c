// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// console.c: windows helper methods for console operations.
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
#include <signal.h>

#define MODES_INTERACTIVE_ROWS          22      // Rows on screen

int cls()
{
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD coord = { 0, 0 };
    DWORD count;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (0 == GetConsoleScreenBufferInfo(hStdOut, &csbi))
    {
        return -1;
    }

    if (0 == FillConsoleOutputCharacter(hStdOut, ' ', csbi.dwSize.X * csbi.dwSize.Y, coord, &count))
    {
        return -1;
    }

    if (0 == SetConsoleCursorPosition(hStdOut, coord))
    {
        return -1;
    }

    return 0;
}

int getTermRows() { return MODES_INTERACTIVE_ROWS; }