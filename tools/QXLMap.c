#include <stdio.h>
#include <stdlib.h>
#include <math.h>

//=========================================================================
// How to work out the values that _should_ be found in a QXL.WIN (aka 
// QLWA) map. Based on information at:
//
//      http://qdosmsq.dunbar-it.co.uk/doku.php?id=qdosmsq:fs:qlwa 
//
// which happens to be my own QDOS/SMSQ repository of useful information.
//
// Norman Dunbar
// 17 February 2019.
//=========================================================================

//=========================================================================
// COMPILING:
//=========================================================================
//
// YOu must have the link to the math library AFTER everything on
// the command line. AT least for gcc anyway - I have yet to try this
// with other compilers.
//
// gcc -o QXLMap[.exe] QXLMap.c -lm
//
// If you don't, you'll get a linker error about not finding 'ceil'. (Ask 
// me how I know!)
//=========================================================================

// Prints a map group's block details.
void printMapGroup(unsigned block, char *usage, unsigned lastBlock) {

    // Assume the block is not the final one.
    unsigned nextBlock = block + 1;
    
    // But check anyway!
    nextBlock = (block == lastBlock - 1) ? 0 : nextBlock;
    
    // Print the details. If it is the last block, it's data is zero.
    // Blocks are always word sized, so 65535 is the biggest possible value.
    printf("%5u ($%.4x) | %6u ($%.6x) | %5u ($%.4x) | %s\n",
           block, block, 64 + (block * 2), 64 + (block * 2), nextBlock, nextBlock, usage);
}

    
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
    unsigned int groupsPerMap;
    unsigned int rootDirectoryId;
    unsigned int firstFreeGroup;
    unsigned int numberOfFreeGroups;

    // Grab desired megaBytes...
    megaBytes = atoi(argv[1]);

    // Error? or zero are the same. Bale out.
    if (!megaBytes) {
        printf("\nCannot convert megabytes ('%s') to a non-zero number.\n", argv[1]);
        return 2;
    }

    // Multiply by 1048 to get actual kilobytes
    // then by a further 2, to get sectors (512 bytes)
    totalSectors = megaBytes << 11;

    // SectorsPerGroup is 4 minimum, or megaBytes/32 rounded up.
    sectorsPerGroup = ceil((double)megaBytes / 32.0);
    sectorsPerGroup = sectorsPerGroup < 4 ? 4 : sectorsPerGroup;

    // NumberOfGroups is totalSectors / sectorsPerGroup.
    numberOfGroups = totalSectors / sectorsPerGroup;
    
    // Sectors in the map is ((numberOfGroups * 2) + 64) / 512 rounded up.
    // double temp = (double)((numberOfGroups * 2) + 64) / 512.0;
    sectorsPerMap = ceil(((numberOfGroups * 2) + 64) / 512.0);

    // So how many groups does the map take?
    groupsPerMap = ceil((double)sectorsPerMap / (double)sectorsPerGroup);

    // The root  directory id is just the next value up from groupsPerMap
    // which as we start counting at zero, is just groupsPerMap!
    rootDirectoryId = groupsPerMap;

    // The first free group is the number of groups - map groups - 1 (root dir groups);
    firstFreeGroup = rootDirectoryId + 1;

    // The remaining free groups is numberOfGroups - rootDirectoryId.
    // Which is also numberOfGroups - firstFreeGroup.
    // Take your pick!
    numberOfFreeGroups = numberOfGroups - groupsPerMap - 1;

    // Display the details.
    printf("Summary of Map Header:\n");
    printf("Megabytes:             %6u\n"
           "Total Sectors:        %7u ($%.6x)\n"
           "Sectors per group:     %6u ($%.4x)\n"
           "Number of groups:      %6u ($%.4x)\n"
           "Sectors per map:       %6u ($%.4x)\n"
           "Groups per map:        %6u ($%.4x)\n"
           "Root directory ID:     %6u ($%.4x)\n"
           "First free group:      %6u ($%.4x)\n"
           "Number of free groups: %6u ($%.4x)\n\n",
           megaBytes,
           totalSectors, totalSectors,
           sectorsPerGroup, sectorsPerGroup,
           numberOfGroups, numberOfGroups,
           sectorsPerMap, sectorsPerMap,
           groupsPerMap, groupsPerMap,
           rootDirectoryId, rootDirectoryId,
           firstFreeGroup, firstFreeGroup,
           numberOfFreeGroups, numberOfFreeGroups);
           
    // Display the map, in summary.

    // The map blocks themselves, all of them.
    printf("Summary of Map Blocks:\n"
           "Block No      | Map Address      | Next Block No | Block Usage\n"
           "=================================================================\n");
           
    for (unsigned x = 0; x < groupsPerMap; x++) {
        printMapGroup(x, "Map", groupsPerMap);
    }
    
    // There can be only one root directory block.
    printMapGroup(rootDirectoryId, "Root directory", firstFreeGroup);

    // The first 5 free blocks.
    for (unsigned x = 0; x < 4; x++) {
        printMapGroup(firstFreeGroup + x, "Free block", numberOfGroups);
    }
    
    printf("...\n");    

    // The last 5 free blocks.
    for (unsigned x = numberOfGroups - 4; x < numberOfGroups - 1; x++) {
        printMapGroup(x, "Free block", numberOfGroups);
    }
    
    printMapGroup(numberOfGroups -1, "Free block", numberOfGroups);

    return 0;
}

