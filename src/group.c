#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <assert.h>
#include <pthread.h>
#include <fuzzy.h>
#include "ds.h"
#include "spew.h"
#include "group.h"


/*======================================================================*
 *                Declaration for PE Related Information                *
 *======================================================================*/
/* DOS(MZ) header related information. */
#define MZ_HEADER_SIZE                      (0x40) /* The size of DOS(MZ) header. */
#define MZ_HEADER_OFF_PE_HEADER_OFFSET      (0x3c) /* The starting offset of PE header. */

/* PE header related information. */
#define PE_HEADER_SIZE                      (0x18) /* The size of PE header. */
#define PE_HEADER_OFF_NUMBER_OF_SECTION     (0x6)  /* The number of sections. */
#define PE_HEADER_OFF_SIZE_OF_OPT_HEADER    (0x14) /* The size of PE optional header.*/

/* Section header related information. */
#define SECTION_HEADER_PER_ENTRY_SIZE       (0x28) /* The size of each section entry. */
#define SECTION_HEADER_OFF_RAW_SIZE         (0x10) /* The section raw size. */
#define SECTION_HEADER_OFF_RAW_OFFSET       (0x14) /* The section raw offset. */
#define SECTION_HEADER_OFF_CHARS            (0x24) /* The sectoin characteristics. */
#define SECTION_HEADER_NAME_SIZE            (0x8)  /* The maximum length of section name. */

/* The size definition of some data type. */
#define DATATYPE_SIZE_DWORD                 (4)
#define DATATYPE_SIZE_WORD                  (2)
#define SHIFT_RANGE_8BIT                    (8)


/*======================================================================*
 *                   Declaration for Private Datatype                   *
 *======================================================================*/
/* The ds to pass information for thread processing. */
typedef struct THREAD_PARAM {
    uint32_t uiBinCount;
    uint8_t ucThreadId;
    uint8_t ucThreadCount;
    uint8_t ucSimThrld;
    RELATION *pRelHead;
} THREAD_PARAM;


/*======================================================================*
 *                    Declaration for Private Object                    *
 *======================================================================*/
static UT_array *_aBin;
static FAMILY *_pMapFam;


/*======================================================================*
 *                  Declaration for Internal Functions                  *
 *======================================================================*/
/**
 * This function extracts the physical offset and size of each PE section.
 *
 * @param fpSample      The file pointer to the analyzed sample.
 * @param szNameSample  The name of the given PE.
 * @param pUiIdBin      The pointer to the binary id.
 *
 * @return              0: Task is finished successfully.
 *                     <0: Possible causes:
 *                         1. Insufficient memory.
 *                         2. Invalid binary format of certain samples.
 */
static int _GrpExtractSectionInfo(FILE *fpSample, char *szNameSample, uint32_t *pUiIdBin);

/**
 * This function generate the szHash of each PE section.
 *
 * @param fpSample      The file pointer to the analyzed sample.
 * @param uiIdBinBgn    The beginning binary id of the given PE.
 * @param uiIdBinEnd    The ending binary id of the given PE.
 *
 * @return              0: Task is finished successfully.
 *                     <0: Possible causes:
 *                         1. Insufficient memory.
 *                         2. Invalid binary format of certain samples.
 */
static int _GrpGenerateSectionHash(FILE *fpSample, uint32_t uiIdBinBgn, uint32_t uiIdBinEnd);

/**
 * This function correlates the similar hashes into groups.
 *
 * @param aThrdParam  The array of thread parameters which stores the
 *                      szHash relation pairs.
 * @param ucLenArray    The length of parameter array.
 *
 * @return              0: Task is finished successfully.
 *                     <0: Possible causes:
 *                         1. Insufficient memory.
 */
static int _GrpCorrelateSimilarHash(THREAD_PARAM *aThrdParam, uint8_t ucLenArray);

/**
 * This function calcuates the pairwise similarity scores for the specified
 * pairs of hashes.
 * 
 * @param vpThreadParam     The pointer to the thread parameter which should
 *                          be stored with szHash relation pairs.
 * 
 * @return                  Should be the thread status but currently ignored.
 */
static void* _GrpComputeHashPairSimilarity(void *vpThreadParam);
  

/*======================================================================*
 *                Implementation for External Functions                 *
 *======================================================================*/
 /**
  * !EXTERNAL
  * GrpInitTask(): The constructor of GROUP structure.
  */
int GrpInitTask(GROUP *self, CONFIG *pCfg) {
    int iRtnCode;
    
    iRtnCode = 0;
    self->pCfg = pCfg;
    self->generateHash = GrpGenerateHash;
    self->groupHash = GrpCorrelateHash;
    self->pGrpRes = (GROUP_RESULT*)malloc(sizeof(GROUP_RESULT));
    if (self->pGrpRes == NULL) {
        Spew0("Error: Cannot allocate GROUP_RESULT structure for result reference.");
        iRtnCode = -1;
        goto EXIT;
    }
    _aBin = NULL;
    _pMapFam = NULL;
    self->pGrpRes->pABin = NULL;
    self->pGrpRes->pMapFam = NULL;
    
EXIT:
    return iRtnCode;
}

/**
 * !EXTERNAL
 * GrpDeinitTask(): The destructor of GROUP structure.
 */
int GrpDeinitTask(GROUP *self) {
    FAMILY *pFamCurr, *pFamHelp;

    /* Free the array of SAMPLE structure. */
    if (_aBin != NULL) {
        ARRAY_FREE(_aBin);
    }
    
    /* Free the FAMILY hash map. */
    if (_pMapFam != NULL) {
        HASH_ITER(hh, _pMapFam, pFamCurr, pFamHelp) {
			ARRAY_FREE(pFamCurr->aFamMbr);
            HASH_FREE(hh, _pMapFam, pFamCurr);
        }    
    }
    
    /* Free the GROUP_RESULT structure. */
    if (self->pGrpRes != NULL) {
        free(self->pGrpRes);
    }
    
    return 0;
}

/**
 * !EXTERNAL
 * GrpGenerateHash(): Generate section hashes for all the given samples.
 */
int GrpGenerateHash(GROUP *self) {
    int iRtnCode;
    uint32_t uiIdBinBgn, uiIdBinEnd;
    char *szPathInput;
    DIR *dirRoot;
    FILE *fpSample;
    struct dirent *entFile;
    char szPathSample[BUF_SIZE_MEDIUM];

    iRtnCode = 0;
    /* Open the root path of the designated sample set. */
    szPathInput = self->pCfg->szPathInput;
    dirRoot = opendir(szPathInput);
    if (dirRoot == NULL) {
        Spew1("Error: %s", strerror(errno));
        iRtnCode = -1;
        goto EXIT;
    }

    /* Initialize the array to record per sample information. */
    UT_icd icdBin = {sizeof(BINARY), NULL, UTArrayBinaryCopy, UTArrayBinaryDeinit};
    ARRAY_NEW(_aBin, &icdBin);

    /* Traverse each sample for section szHash generation. */
    uiIdBinBgn = uiIdBinEnd = 0;
    while ((entFile = readdir(dirRoot)) != NULL) {
        if ((strcmp(entFile->d_name, ".") == 0) || 
            (strcmp(entFile->d_name, "..") == 0)) {
            continue;
        }
        memset(szPathSample, 0, sizeof(char) * BUF_SIZE_MEDIUM);
        snprintf(szPathSample, BUF_SIZE_MEDIUM, "%s\%s", szPathInput, entFile->d_name);
        fpSample = fopen(szPathSample, "rb");
        if (fpSample == NULL) {
            Spew1("Error: %s", strerror(errno));
            iRtnCode = -1;
            goto CLOSE_DIR;
        }
        
        /* Extract the section information from file header.*/
        iRtnCode = _GrpExtractSectionInfo(fpSample, entFile->d_name, &uiIdBinEnd);
        if (iRtnCode != 0) {
            goto CLOSE_FILE;
        }

        /* Generate the szHash of each section. */
        iRtnCode = _GrpGenerateSectionHash(fpSample, uiIdBinBgn, uiIdBinEnd);
        if (iRtnCode != 0) {
            goto CLOSE_FILE;
        }
        uiIdBinBgn = uiIdBinEnd;

    CLOSE_FILE:
        fclose(fpSample);
    }

CLOSE_DIR:
    closedir(dirRoot);
EXIT:
    return iRtnCode;
}

/**
 * !EXTERNAL
 * GrpCorrelateHash(): Group the hashes using the given similarity threshold.
 */
int GrpCorrelateHash(GROUP *self) {
    int iRtnCode;
    uint32_t uiBinCount;
    uint8_t ucIter, ucThreadCount, ucSimThrld;
    pthread_t *aThrd;
    THREAD_PARAM *aThrdParam;
    RELATION *pRelCurr, *pRelPrev, *pRelHelp;
   
    iRtnCode = 0;
    /* Prepare the thread parameters. */
    uiBinCount = ARRAY_LEN(_aBin);
    ucThreadCount = self->pCfg->ucParallelity;
    ucSimThrld = self->pCfg->ucSimilarity;
    aThrd = (pthread_t*)malloc(sizeof(pthread_t) * ucThreadCount);    
    if (aThrd == NULL) {
        Spew0("Error: Cannot allocate the array for thread id.");
        iRtnCode = -1;
        goto EXIT;
    }    
    aThrdParam = (THREAD_PARAM*)malloc(sizeof(THREAD_PARAM) * ucThreadCount);
    if (aThrdParam == NULL) {
        Spew0("Error: Cannot allocate the array for thread parameters.");
        iRtnCode = -1;
        goto FREE_THREAD;
    }

    /* Fork the specified number of threads for parallel similarity computation. */        
    for (ucIter = 0 ; ucIter < ucThreadCount ; ucIter++) {
        aThrdParam[ucIter].uiBinCount = uiBinCount;
        aThrdParam[ucIter].ucThreadCount = ucThreadCount;
        aThrdParam[ucIter].ucThreadId = ucIter + 1;
        aThrdParam[ucIter].ucSimThrld = ucSimThrld;
        aThrdParam[ucIter].pRelHead = NULL;
        pthread_create(&aThrd[ucIter], NULL, _GrpComputeHashPairSimilarity, 
                       (void*)&(aThrdParam[ucIter]));
    }
    
    /* Wait for the thread termination. */
    for (ucIter = 0 ; ucIter < ucThreadCount ; ucIter++) {
        pthread_join(aThrd[ucIter], NULL);
    }
    
    /* Construct the szHash groups with similarity correlation. */
    iRtnCode = _GrpCorrelateSimilarHash(aThrdParam, ucThreadCount);
    if (iRtnCode == 0) {
        self->pGrpRes->pABin = _aBin;
        self->pGrpRes->pMapFam = _pMapFam;
    }

FREE_PARAM:
    for (ucIter = 0 ; ucIter < ucThreadCount ; ucIter++) {
        DL_FOREACH_SAFE(aThrdParam[ucIter].pRelHead, pRelCurr, pRelHelp) {
            DL_FREE(aThrdParam[ucIter].pRelHead, pRelCurr);
        }
    }
    free(aThrdParam);
FREE_THREAD:    
    free(aThrd);            
EXIT:        
    return iRtnCode;
}


/*======================================================================*
 *                Implementation for Internal Functions                 *
 *======================================================================*/
/**
 * !INTERNAL
 * _GrpExtractSectionInfo(): Extract the physical offset and size of each PE section.
 */
static int _GrpExtractSectionInfo(FILE *fpSample, char *szNameSample, uint32_t *pUiIdBin) {
    int iRtnCode, status;
    size_t nExptRead, nRealRead;
    uint32_t uiReg, uiOfstPEHeader;
    uint16_t usIterFst, usIterSnd, usReg, usSectCount;
    BINARY binInst;
    char bufRead[BUF_SIZE_LARGE];

    iRtnCode = 0;
    /* Check the MZ header. */
    nExptRead = MZ_HEADER_SIZE;
    nRealRead = fread(bufRead, sizeof(char), nExptRead, fpSample);
    if ((nExptRead != nRealRead) || (bufRead[0] != 'M') || (bufRead[1] != 'Z')) {
        Spew0("Error: Invalid PE file (Invalid MZ header).");
        iRtnCode = -1;
        goto EXIT;
    }

    /* Resolve the starting offset of PE header and move to it. */
    uiReg = 0;
    for (usIterFst = 1 ; usIterFst <= DATATYPE_SIZE_DWORD ; usIterFst++) {
        uiReg <<= SHIFT_RANGE_8BIT;
        uiReg += bufRead[MZ_HEADER_OFF_PE_HEADER_OFFSET + DATATYPE_SIZE_DWORD - usIterFst] & 0xff;
    }
    uiOfstPEHeader = uiReg;
    status = fseek(fpSample, uiOfstPEHeader, SEEK_SET);
    if (status != 0) {
        Spew0("Error: Invalid PE file (Unreachable PE header).");
        iRtnCode = -1;
        goto EXIT;
    }
      
    /* Check the PE header. */
    nExptRead = PE_HEADER_SIZE;
    nRealRead = fread(bufRead, sizeof(char), nExptRead, fpSample);
    if ((nExptRead != nRealRead) || (bufRead[0] != 'P' || bufRead[1] != 'E')) {
        Spew0("Invalid PE file (Invalid PE header).");
        iRtnCode = -1;
        goto EXIT;
    }
    
    /* Resolve the number of sections. */
    usReg = 0;
    for (usIterFst = 1 ; usIterFst <= DATATYPE_SIZE_WORD ; usIterFst++) {
        usReg <<= SHIFT_RANGE_8BIT;
        usReg += bufRead[PE_HEADER_OFF_NUMBER_OF_SECTION + DATATYPE_SIZE_WORD - usIterFst] & 0xff;
    }
    usSectCount = usReg;

    /* Resolve the size of optional header. */
    usReg = 0;
    for (usIterFst = 1 ; usIterFst <= DATATYPE_SIZE_WORD ; usIterFst++) {
        usReg <<= SHIFT_RANGE_8BIT;
        usReg += bufRead[PE_HEADER_OFF_SIZE_OF_OPT_HEADER + DATATYPE_SIZE_WORD - usIterFst] & 0xff;
    }
        
    /* Move to the starting offset of section header. */
    status = fseek(fpSample, (uiOfstPEHeader + PE_HEADER_SIZE + usReg), SEEK_SET);
    if (status != 0) {
        Spew0("Error: Invalid PE file (Unreachable section header).");
        iRtnCode = -1;
        goto EXIT;
    }
    
    /* Traverse each section header to retrieve the raw section offset and size. */
    for (usIterFst = 0 ; usIterFst < usSectCount ; usIterFst++) {
        nExptRead = SECTION_HEADER_PER_ENTRY_SIZE;
        nRealRead = fread(bufRead, sizeof(char), nExptRead, fpSample);
        if (nExptRead != nRealRead) {
            Spew0("Error: Invalid PE file (Invalid section header).");
            iRtnCode = -1;
            goto EXIT;
        }

        /* Record the raw section size. */
        uiReg = 0;
        for (usIterSnd = 1 ; usIterSnd <= DATATYPE_SIZE_DWORD ; usIterSnd++) {
            uiReg <<= SHIFT_RANGE_8BIT;
            uiReg += bufRead[SECTION_HEADER_OFF_RAW_SIZE + DATATYPE_SIZE_DWORD - usIterSnd] & 0xff;
        }
        if (uiReg == 0) {
            continue;
        }
        binInst.uiSectSize = uiReg;

        /* Record the raw section offset. */
        uiReg = 0;
        for (usIterSnd = 1 ; usIterSnd <= DATATYPE_SIZE_DWORD ; usIterSnd++) {
            uiReg <<= SHIFT_RANGE_8BIT;
            uiReg += bufRead[SECTION_HEADER_OFF_RAW_OFFSET + DATATYPE_SIZE_DWORD - usIterSnd] & 0xff;
        }
        binInst.uiSectOfst = uiReg;
    
        binInst.uiIdBin = *pUiIdBin;
        binInst.usSectIdx = usIterFst;
        binInst.szHash = NULL;
        binInst.szNameSample = strdup(szNameSample);
        if (binInst.szNameSample == NULL) {
            Spew0("Error: Cannot duplicate the sample name.");
            iRtnCode = -1;
            goto EXIT;        
        }

        /* Insert the BINARY structure into array. */
        ARRAY_PUSH_BACK(_aBin, &binInst);
        *pUiIdBin = *pUiIdBin + 1;        
    }
    
EXIT:
    return iRtnCode;
}

/**
 * !INTERNAL
 * _GrpGenerateSectionHash(): Generate the szHash of each PE section.
 */
static int _GrpGenerateSectionHash(FILE *fpSample, uint32_t uiIdBinBgn, uint32_t uiIdBinEnd) {
    int iRtnCode, status;
    uint32_t uiIter, rawOffset, rawSize;
    size_t nExptRead, nRealRead;
    char *content;
    BINARY *pBin;    

    iRtnCode = 0;
    /* Traverse all the sections with non-zero size. */
    for (uiIter = uiIdBinBgn ; uiIter < uiIdBinEnd ; uiIter++) {
        pBin = (BINARY*)ARRAY_ELTPTR(_aBin, uiIter);
        rawOffset = pBin->uiSectOfst;
        rawSize = pBin->uiSectSize;
        
        status = fseek(fpSample, rawOffset, SEEK_SET);
        if (status != 0) {
            Spew0("Error: Invalid PE file (Unreachable section).");
            iRtnCode = -1;
            goto EXIT;
        }
        content = (char*)malloc(sizeof(char) * rawSize);
        if (content == NULL) {
            Spew0("Error: Cannot allocate buffer for section content.");
            iRtnCode = -1;
            goto EXIT;
        }
        nExptRead = rawSize;
        nRealRead = fread(content, sizeof(char), nExptRead, fpSample);
        if (nExptRead != nRealRead) {
            Spew0("Error: Cannot read the full section content.");
            iRtnCode = -1;
            goto FREE;
        }
    
        pBin->szHash = (char*)malloc(sizeof(char) * FUZZY_MAX_RESULT);
        if (pBin->szHash == NULL) {
            Spew0("Error: Cannot allocate buffer for section szHash.");
            iRtnCode = -1;
            goto FREE;
        }
        
        /* Apply the ssdeep library to generate section szHash. */
        status = fuzzy_hash_buf(content, rawSize, pBin->szHash);
        if (status != 0) {
            Spew0("Error: Fail to generate section szHash.");
            iRtnCode = -1;
        }
    FREE:
        free(content);    
    }

EXIT:
    return iRtnCode;
}

/**
 * !INTERNAL
 * _GrpCorrelateSimilarHash(): Correlate the similar hashes into groups.
 */
static int _GrpCorrelateSimilarHash(THREAD_PARAM *aThrdParam, uint8_t ucLenArray) {
    int iRtnCode;
    uint32_t uiIter, uiIdBinSrc, uiIdBinTge, uiIdGrpSrc, uiIdGrpTge, uiIdGrpMge;
    THREAD_PARAM pThrdParam;
    RELATION *pRelCurr, *pRelHelp;
    BINARY *pBinSrc, *pBinTge;
    FAMILY *pFamNew;

    iRtnCode = 0;
    /* Link the hashes with the recorded relation pairs. */
    for (uiIter = 0 ; uiIter < ucLenArray ; uiIter++) {
        pThrdParam = aThrdParam[uiIter];
        DL_FOREACH_SAFE(pThrdParam.pRelHead, pRelCurr, pRelHelp) {
            uiIdBinSrc = pRelCurr->uiIdBinSrc;
            uiIdBinTge = pRelCurr->uiIdBinTge;
            pBinSrc = (BINARY*)ARRAY_ELTPTR(_aBin, uiIdBinSrc);
            pBinTge = (BINARY*)ARRAY_ELTPTR(_aBin, uiIdBinTge);
            uiIdGrpSrc = pBinSrc->uiIdGrp;
            uiIdGrpTge = pBinTge->uiIdGrp;
            uiIdGrpMge = MIN(uiIdGrpSrc, uiIdGrpTge);
            pBinSrc->uiIdGrp = uiIdGrpMge;
            pBinTge->uiIdGrp = uiIdGrpMge;
            pRelCurr = pRelCurr->next;
        }
    }

    /* Rearrange the group id of each binary. */
    pBinSrc = NULL;
    while ((pBinSrc = (BINARY*)ARRAY_NEXT(_aBin, pBinSrc)) != NULL) {
        uiIdGrpTge = pBinSrc->uiIdGrp;
        do {
            uiIdGrpSrc = uiIdGrpTge;
            pBinTge = (BINARY*)ARRAY_ELTPTR(_aBin, uiIdGrpSrc);
            uiIdGrpTge = pBinTge->uiIdGrp;
        } while (uiIdGrpTge < uiIdGrpSrc);
        pBinSrc->uiIdGrp = uiIdGrpTge;
    }
    
    pBinSrc = NULL;
    uiIdBinSrc = 0;
    UT_icd icdUi = {sizeof(uint32_t), NULL, NULL, NULL};
    while ((pBinSrc = (BINARY*)ARRAY_NEXT(_aBin, pBinSrc)) != NULL) {
        uiIdGrpSrc = pBinSrc->uiIdGrp;
        /* Check if the binary group already exists. */
        HASH_FIND(hh, _pMapFam, &uiIdGrpSrc, sizeof(uint32_t), pFamNew);
        if (pFamNew == NULL) {
            pFamNew = (FAMILY*)malloc(sizeof(FAMILY));
            if (pFamNew == NULL) {
                Spew0("Error: Cannot allocate FAMILY for grouping result.");
                iRtnCode = -1;
                break;
            }
            pFamNew->uiIdRep = uiIdGrpSrc;
            ARRAY_NEW(pFamNew->aFamMbr, &icdUi);
            HASH_ADD(hh, _pMapFam, uiIdRep, sizeof(uint32_t), pFamNew);
        }
        ARRAY_PUSH_BACK(pFamNew->aFamMbr, &uiIdBinSrc);
        uiIdBinSrc++;
    }

    return iRtnCode;
}

/**
 * !INTERNAL
 * _GrpComputeHashPairSimilarity(): Calcuate the pairwise similarity scores 
 * for the specified pairs of hashes.
 */
static void* _GrpComputeHashPairSimilarity(void *vpThreadParam) {
    uint32_t uiBinCount, uiIdBinSrc, uiIdBinTge;
    uint8_t ucThreadCount, ucThreadId, ucSimThrld;
    int8_t cSimScore;
    THREAD_PARAM *pThrdParam;
    BINARY *pBinSrc, *pBinTge;
    RELATION *pRelNew;

    pThrdParam = (THREAD_PARAM*)vpThreadParam;
    uiBinCount = pThrdParam->uiBinCount;
    ucThreadCount = pThrdParam->ucThreadCount;
    ucThreadId = pThrdParam->ucThreadId;
    ucSimThrld = pThrdParam->ucSimThrld;
    
    uiIdBinSrc = 0;
    uiIdBinTge = uiIdBinSrc + ucThreadId - ucThreadCount;
    while (true) {
        uiIdBinTge += ucThreadCount;
        while (uiIdBinTge >= uiBinCount) {
            uiIdBinSrc++;
            if (uiIdBinSrc == uiBinCount) {
                break;
            }
            uiIdBinTge -= uiBinCount;
            uiIdBinTge += (uiIdBinSrc + 1);
        }
        if ((uiIdBinSrc >= uiBinCount) || (uiIdBinTge >= uiBinCount)) {
            break;
        }
        pBinSrc = (BINARY*)ARRAY_ELTPTR(_aBin, uiIdBinSrc);
        pBinTge = (BINARY*)ARRAY_ELTPTR(_aBin, uiIdBinTge);
        
        /* Apply the ssdeep library to compute similarity score and record the 
           szHash pairs with scores fitting the threshold. */
        cSimScore = fuzzy_compare(pBinSrc->szHash, pBinTge->szHash);
        if (cSimScore >= ucSimThrld) {
            pRelNew = (RELATION*)malloc(sizeof(RELATION));
            assert(pRelNew != NULL);
            pRelNew->uiIdBinSrc = uiIdBinSrc;
            pRelNew->uiIdBinTge = uiIdBinTge;
            DL_APPEND(pThrdParam->pRelHead, pRelNew);
        }
    }

    return;
}
