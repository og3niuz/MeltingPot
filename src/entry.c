#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include "cluster.h"
#include "entry.h"


/* Print the program usage message. */
void print_usage();


int main(int argc, char **argv, char *envp) {
    int     iRtnCode, opt, idxOpt;
    uint8_t nParallelity, nSimilarity, nBlkCount, nBlkSize;
    char    *szPathRoot, *pathPattern;
    CONFIG  *hConfig;
    CLUSTER *hCluster;
    char    bufOrder[BUF_SIZE_SMALL];
    
    /* Craft the structure to store command line options. */    
    static struct option options[] = {
        {OPT_LONG_HELP           , no_argument      , 0, OPT_HELP           },
        {OPT_LONG_PATH_SAMPLE_SET, required_argument, 0, OPT_PATH_SAMPLE_SET},
        {OPT_LONG_PATH_PATTERN   , required_argument, 0, OPT_PATH_PATTERN   },
        {OPT_LONG_PARALLELITY    , required_argument, 0, OPT_PARALLELITY    },
        {OPT_LONG_BLK_COUNT      , required_argument, 0, OPT_BLK_COUNT      },
        {OPT_LONG_BLK_SIZE       , required_argument, 0, OPT_BLK_SIZE       },
        {OPT_LONG_SIMILARITY     , required_argument, 0, OPT_SIMILARITY     },
    };

    memset(bufOrder, 0, sizeof(char) * BUF_SIZE_SMALL);
    sprintf(bufOrder, "%c%c:%c:%c:%c:%c:%c:", OPT_HELP, OPT_PATH_SAMPLE_SET, OPT_PATH_PATTERN,
                                              OPT_PARALLELITY, OPT_BLK_COUNT, OPT_BLK_SIZE, 
                                              OPT_SIMILARITY);    
    iRtnCode = 0;
    szPathRoot = NULL;
    nParallelity = nSimilarity = nBlkCount = nBlkSize = -1;

    /* Get the command line options. */
    idxOpt = 0;
    while ((opt = getopt_long(argc, argv, bufOrder, options, &idxOpt)) != -1) {
        switch (opt) {
            case OPT_PATH_SAMPLE_SET: {
                szPathRoot = optarg;
                break;
            }
            case OPT_PATH_PATTERN: {
                pathPattern = optarg;
                break;
            }
            case OPT_PARALLELITY: {
                nParallelity = atoi(optarg);
                break;
            }
            case OPT_SIMILARITY: {
                nSimilarity = atoi(optarg);
                break;
            }
            case OPT_BLK_COUNT: {
                nBlkCount = atoi(optarg);
                break;
            }
            case OPT_BLK_SIZE: {
                nBlkSize = atoi(optarg);
                break;
            }
            default: {
                EARLY_RETURN(iRtnCode);
            }
        }
    }

    /* Check the configuration parameters. */
    if ((szPathRoot == NULL) || (pathPattern == NULL)) {
        EARLY_RETURN(iRtnCode);
    }
    if ((nParallelity < 1) || (nSimilarity < 0) ||
        (nBlkCount < 1) || (nBlkSize <= 1)) {
        EARLY_RETURN(iRtnCode);
    }

    /* Initialize the clustering library. */
    INIT_CLUSTER(hCluster);
    if (hCluster == NULL) {
        EARLY_RETURN(iRtnCode);
    }

    /* Set up the context. */
    hConfig = (CONFIG*)malloc(sizeof(CONFIG));
    if (hConfig == NULL) {
        iRtnCode = -1;
        goto DEINIT;
    }
    hConfig->nParallelity = nParallelity;
    hConfig->nSimilarity = nSimilarity;
    hConfig->nBlkCount = nBlkCount;
    hConfig->nBlkSize = nBlkSize;
    hConfig->szPathRoot = szPathRoot;
    hConfig->pathPattern = pathPattern;
    iRtnCode = hCluster->init_ctx(hCluster, hConfig);
    if (iRtnCode != 0) {
        iRtnCode = -1;
        goto DEINIT;   
    }

    /* Group the binaries of the given sample set. */
    iRtnCode = hCluster->generate_group(hCluster);
    if (iRtnCode != 0) {
        iRtnCode = -1;
        goto FREECTX;
    }

FREECTX:
    /* Free the context. */
    hCluster->deinit_ctx(hCluster);

DEINIT:
    if (hConfig != NULL) {
        free(hConfig);
    }
    /* Deinitialize the clustering library. */
    DEINIT_CLUSTER(hCluster);

EXIT:
    return iRtnCode;
}


void print_usage() {

    const char *strMsg = "Usage: entry --input path_in --output path_out --parallelity num_p --similarity num_t"
                         " --blkcount num_c --blksize num_s\n"
                         "       entry -i      path_in -o       path_out -p            num_p -t           num_t"
                         " -c         num_c -s        num_s\n\n"
                         "Example: entry --input /path/set --output /path/pattern --parallelity 2 --similarity 80"
                         " --blkcount 4 --blksize 16\n"
                         "         entry -i      /path/set -o       /path/pattern -p            2 -t           80"
                         " -c         4 -s        16\n\n";
    printf("%s", strMsg);
    return;
}
