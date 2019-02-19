# Tools - Readme

The files in this folder are those used by me, Norman Dunbar, when attempting to calculate something, or check the results of something. I got fed up using pen and paper, so decided to get a computer to do it for me!

Some of these might be useful. Maybe.


## The Tools

### QXLMap

When called with a single parameter, the desired size of a hard disc file, in MB, this utility will print out the various header fields that are not zero. It will also print out all the block details for the map and root directory, as well as the first and last 5 blocks in the map itself.

The output will resemble the following for an 8 MB QXL file:

````
$ ./QXLMap 8


Summary of Map Header:
Megabytes:                  8
Total Sectors:          16384 ($004000)
Sectors per group:          4 ($0004)
Number of groups:        4096 ($1000)
Sectors per map:           17 ($0011)
Groups per map:             5 ($0005)
Root directory ID:          5 ($0005)
First free group:           6 ($0006)
Number of free groups:   4090 ($0ffa)

Summary of Map Blocks:
Block No      | Map Address      | Next Block No | Block Usage
=================================================================
    0 ($0000) |     64 ($000040) |     1 ($0001) | Map
    1 ($0001) |     66 ($000042) |     2 ($0002) | Map
    2 ($0002) |     68 ($000044) |     3 ($0003) | Map
    3 ($0003) |     70 ($000046) |     4 ($0004) | Map
    4 ($0004) |     72 ($000048) |     0 ($0000) | Map
    5 ($0005) |     74 ($00004a) |     0 ($0000) | Root directory
    6 ($0006) |     76 ($00004c) |     7 ($0007) | Free block
    7 ($0007) |     78 ($00004e) |     8 ($0008) | Free block
    8 ($0008) |     80 ($000050) |     9 ($0009) | Free block
    9 ($0009) |     82 ($000052) |    10 ($000a) | Free block
...
 4092 ($0ffc) |   8248 ($002038) |  4093 ($0ffd) | Free block
 4093 ($0ffd) |   8250 ($00203a) |  4094 ($0ffe) | Free block
 4094 ($0ffe) |   8252 ($00203c) |  4095 ($0fff) | Free block
 4095 ($0fff) |   8254 ($00203e) |     0 ($0000) | Free block
````

It allows me to check that `qxltool` is actually doing the right thing. At least, when I fix it, because right now, it's way way off!
