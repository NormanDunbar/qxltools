#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// How to work out the values that _should_ be found in a QXL.WIN (aka QLWA) map.
// Based on information at http://qdosmsq.dunbar-it.co.uk/doku.php?id=qdosmsq:fs:qlwa which
// happens to be my own QDOS/SMSQ repository of useful information.
//
// Norman Dunbar
// 17 February 2019.

int main(int argc, char *argv[]) {

    // I'm expecting MB as the first parameter.
    if (argc != 2) {
        printf("\nERROR: QXLMap expects the first parameter to be the desired hard disc\n");
        printf("size in MB.\n");
        return 1;
    }

    unsigned int megaBytes;
    unsigned int totalSectors;
    unsigned int sectorsPerGroup;
    unsigned int numberOfGroups;
    unsigned int sectorsPerMap;
    unsigned int rootDirectoryId;
    unsigned int firstFreeGroup;
    unsigned int numberOfFreeGroups;

    // Grab desired megaBytes...
    megaBytes = atoi(argv[1]);

    // Error? OR zero are the same. Bale out.
    if (!megaBytes) {
        return 2;
    }

    // Multiply by 1048 to get actual bytes
    // then by a further 2, to get sectors (512 bytes)
    totalSectors = megaBytes << 11;

    // SectorsPerGroup is 4 minimum, or megaBytes/32 rounded up.
    sectorsPerGroup = ceil(megaBytes / 32);
    sectorsPerGroup = sectorsPerGroup < 4 ? 4 : sectorsPerGroup;

    // NumberOfGroups is totalSectors / sectorsPerGroup.
    numberOfGroups = totalSectors / sectorsPerGroup;

    // Sectors in the map is ((numberOfGroups * 2) + 64) / 512 rounded up.
    double temp = (double)((numberOfGroups * 2) + 64) / 512.0;
    sectorsPerMap = ceil(temp);

    // The root  directory id is sectorsPerMap / sectorsPerGroup + 1;
    rootDirectoryId = (sectorsPerMap / sectorsPerGroup) + 1;

    // The first free group is rootDirectoryId + 1;
    firstFreeGroup = rootDirectoryId + 1;

    // The remaining free groups is numberOfGroups - rootDirectoryId.
    numberOfFreeGroups = numberOfGroups - rootDirectoryId;

    // Display the details.
    printf("\nMegabytes: %u\n"
           "Total Sectors: %u ($%x)\n"
           "Sectors per group: %u ($%x)\n"
           "Number of groups: %u ($%x)\n"
           "Sectors per map: %u ($%x)\n"
           "Root directory ID: %u ($%x)\n"
           "First free group: %u ($%x)\n"
           "Number of free groups: %u ($%x)\n\n",
           megaBytes,
           totalSectors, totalSectors,
           sectorsPerGroup, sectorsPerGroup,
           numberOfGroups, numberOfGroups,
           sectorsPerMap, sectorsPerMap,
           rootDirectoryId, rootDirectoryId,
           firstFreeGroup, firstFreeGroup,
           numberOfFreeGroups, numberOfFreeGroups);

    return 0;
}

