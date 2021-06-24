/****************************************************************************
 *
 * (C) 2021 Cadence Design Systems, Inc. All rights reserved worldwide.
 *
 * This sample source code is not supported by Cadence Design Systems, Inc.
 * It is provided freely for demonstration purposes only.
 * SEE THE WARRANTY DISCLAIMER AT THE BOTTOM OF THIS FILE.
 *
 ***************************************************************************/

#include "apiCAEP.h"
#include "apiCAEPUtils.h"
#include "apiPWP.h"
#include "runtimeWrite.h"


#define LU(i)   ((unsigned long)(i))


static bool
writeComment(CAEP_RTITEM &rti, const char *txt = 0)
{
    // %[ txt]
    return 0 < fprintf(rti.fp, "%%%s%s\n", (txt ? " " : ""), (txt ? txt : ""));
}


static bool
writeSectionComment(CAEP_RTITEM &rti, const char *txt = 0)
{
    // %
    // % txt
    // %
    return writeComment(rti) && writeComment(rti, txt) && writeComment(rti);
}


template<typename T>
static bool
writeKeyVal(CAEP_RTITEM &rti, const char *key, const T &val)
{
    // key= intVal
    return 0 < fprintf(rti.fp, "%s= %lu\n", key, LU(val));
}


static bool
writeKeyVal(CAEP_RTITEM &rti, const char *key, const char *val)
{
    // key= strVal
    return 0 < fprintf(rti.fp, "%s= %s\n", key, val);
}


enum SU2_ELEMTYPE {
    SU2_ELEMTYPE_INVALID = 0,
    SU2_ELEMTYPE_BAR     = 3,
    SU2_ELEMTYPE_TRI     = 5,
    SU2_ELEMTYPE_QUAD    = 9,
    SU2_ELEMTYPE_TET     = 10,
    SU2_ELEMTYPE_HEX     = 12,
    SU2_ELEMTYPE_WEDGE   = 13,
    SU2_ELEMTYPE_PYRAMID = 14
};

// convert pointwise element type enum to SU2 integer
static SU2_ELEMTYPE
pw2SuType(PWGM_ENUM_ELEMTYPE type)
{
    SU2_ELEMTYPE ret = SU2_ELEMTYPE_INVALID;
    switch (type) {
    case PWGM_ELEMTYPE_BAR:
        ret = SU2_ELEMTYPE_BAR;
        break;
    case PWGM_ELEMTYPE_TRI:
        ret = SU2_ELEMTYPE_TRI;
        break;
    case PWGM_ELEMTYPE_QUAD:
        ret = SU2_ELEMTYPE_QUAD;
        break;
    case PWGM_ELEMTYPE_TET:
        ret = SU2_ELEMTYPE_TET;
        break;
    case PWGM_ELEMTYPE_HEX:
        ret = SU2_ELEMTYPE_HEX;
        break;
    case PWGM_ELEMTYPE_WEDGE:
        ret = SU2_ELEMTYPE_WEDGE;
        break;
    case PWGM_ELEMTYPE_PYRAMID:
        ret = SU2_ELEMTYPE_PYRAMID;
        break;
    case PWGM_ELEMTYPE_POINT:
    default:
        ret = SU2_ELEMTYPE_INVALID;
        break;
    }
    return ret;
}


// Count global total number of elements in grid
static PWP_UINT32
getElemCnt(CAEP_RTITEM &rti)
{
    PWP_UINT32 ret = 0;
    PWP_UINT32 iBlk = 0;
    PWGM_HBLOCK hBlk = PwModEnumBlocks(rti.model, iBlk);
    while (PWGM_HBLOCK_ISVALID(hBlk)) {
        ret += PwBlkElementCount(hBlk, 0);
        hBlk = PwModEnumBlocks(rti.model, ++iBlk);
    }
    return ret;
}


// Write an elements connectivity as "ElemType Ndx1 ... NdxN[ GlobalNdx]\n"
static bool
writeElemData(CAEP_RTITEM &rti, const PWGM_ELEMDATA &ed,
    PWP_UINT32 *globNdx = 0)
{
    bool ret = true;
    const SU2_ELEMTYPE suType = pw2SuType(ed.type);
    // Faster writes using a single call per canonical element.
    switch (suType) {
    case SU2_ELEMTYPE_BAR:
        ret = (0 < fprintf(rti.fp, "%2i  %4lu %4lu", suType, LU(ed.index[0]),
            LU(ed.index[1])));
        break;
    case SU2_ELEMTYPE_TRI:
        ret = (0 < fprintf(rti.fp, "%2i  %4lu %4lu %4lu", suType,
            LU(ed.index[0]), LU(ed.index[1]), LU(ed.index[2])));
        break;
    case SU2_ELEMTYPE_QUAD:
        ret = (0 < fprintf(rti.fp, "%2i  %4lu %4lu %4lu %4lu", suType,
           LU(ed.index[0]), LU(ed.index[1]), LU(ed.index[2]), LU(ed.index[3])));
        break;
    case SU2_ELEMTYPE_TET:
        ret = (0 < fprintf(rti.fp, "%2i  %4lu %4lu %4lu %4lu", suType,
           LU(ed.index[0]), LU(ed.index[1]), LU(ed.index[2]), LU(ed.index[3])));
        break;
    case SU2_ELEMTYPE_PYRAMID:
        ret = (0 < fprintf(rti.fp, "%2i  %4lu %4lu %4lu %4lu %4lu", suType,
            LU(ed.index[0]), LU(ed.index[1]), LU(ed.index[2]), LU(ed.index[3]),
            LU(ed.index[4])));
        break;
    case SU2_ELEMTYPE_WEDGE:
        // PW-22662 Prism node ordering incorrect for SU2
        // SU2 expects the normal of f(3,4,5) to point towards f(0,1,2) which is
        // opposite the PW scheme of f(0,1,2) --> f(3,4,5)
        ret = (0 < fprintf(rti.fp, "%2i  %4lu %4lu %4lu %4lu %4lu %4lu", suType,
            LU(ed.index[3]), LU(ed.index[4]), LU(ed.index[5]),
            LU(ed.index[0]), LU(ed.index[1]), LU(ed.index[2])));
        break;
    case SU2_ELEMTYPE_HEX:
        ret = (0 < fprintf(rti.fp, "%2i  %4lu %4lu %4lu %4lu %4lu %4lu %4lu"
            " %4lu", suType, LU(ed.index[0]), LU(ed.index[1]), LU(ed.index[2]),
            LU(ed.index[3]), LU(ed.index[4]), LU(ed.index[5]), LU(ed.index[6]),
            LU(ed.index[7])));
        break;
    default: {
        // not canonical - use slower multi-write
        ret = (0 < fprintf(rti.fp, "%2i ", suType));
        // Write vertex indices
        for (PWP_UINT32 ii = 0; ret && (ii < ed.vertCnt); ++ii) {
            ret = (0 < fprintf(rti.fp, " %4lu", LU(ed.index[ii])));
        }
        break; }
    }
    if (ret) {
        if (0 != globNdx) {
            // write and incr the global element count, and EOL
            ret = (0 < fprintf(rti.fp, " %4lu\n", LU(*globNdx)));
            ++(*globNdx);
        }
        else {
            // write EOL
            ret = (EOF != fputs("\n", rti.fp));
        }
    }
    return ret;
}


static bool
writeBlockElements(CAEP_RTITEM &rti, const PWGM_HBLOCK &hBlk,
    PWP_UINT32 &globElemNdx)
{
    bool ret = false;
    if (caeuProgressBeginStep(&rti, PwBlkElementCount(hBlk, 0))) {
        ret = true;
        PWP_UINT32 ndx = 0;
        PWGM_HELEMENT hE = PwBlkEnumElements(hBlk, ndx);
        PWGM_ELEMDATA eData;
        while (PwElemDataMod(hE, &eData)) {
            writeElemData(rti, eData, &globElemNdx);
            if (!caeuProgressIncr(&rti)) {
                ret = false;
                break;
            }
            hE = PwBlkEnumElements(hBlk, ++ndx);
        }
    }
    return caeuProgressEndStep(&rti) && ret;
}


static bool
writeElementsSection(CAEP_RTITEM &rti)
{
    bool ret = writeSectionComment(rti, "Inner element connectivity") &&
        writeKeyVal(rti, "NELEM", getElemCnt(rti));
    if (ret) {
        // Tracks the serialized index (0..N-1) for all elements in all blocks
        PWP_UINT32 globElemNdx = 0;
        // Write blocks, treat all blocks as one set of elements
        PWP_UINT32 blkNdx = 0;
        PWGM_HBLOCK hBlk = PwModEnumBlocks(rti.model, blkNdx);
        while (PWGM_HBLOCK_ISVALID(hBlk)) {
            if (!writeBlockElements(rti, hBlk, globElemNdx)) {
                ret = false;
                break;
            }
            hBlk = PwModEnumBlocks(rti.model, ++blkNdx);
        }
    }
    return ret;
}


static bool
writeNode(CAEP_RTITEM &rti, const PWGM_HVERTEX &vertex)
{
    bool ret = false;
    PWGM_VERTDATA vd;
    if (PwVertDataMod(vertex, &vd)) {
        const int prec = (CAEPU_RT_PREC_SINGLE(&rti) ? 8 : 16);
        const int wd = prec + 8;
        if (CAEPU_RT_DIM_3D(&rti)) {
            // print 3D vertex data
            ret = (0 < fprintf(rti.fp, "%#*.*g %#*.*g %#*.*g %4lu\n",
                wd, prec, vd.x, wd, prec, vd.y, wd, prec, vd.z, LU(vd.i)));
        }
        else {
            // print 2D vertex data
            ret = (0 < fprintf(rti.fp, "%#*.*g %#*.*g %4lu\n", wd, prec, vd.x,
                wd, prec, vd.y, LU(vd.i)));
        }
    }
    return ret;
}


static bool
writeNodesSection(CAEP_RTITEM &rti)
{
    const PWP_UINT32 vertCnt = PwModVertexCount(rti.model);
    bool ret = caeuProgressBeginStep(&rti, vertCnt) &&
        writeSectionComment(rti, "Node coordinates") &&
        writeKeyVal(rti, "NPOIN", vertCnt);
    if (ret) {
        PWP_UINT32 ndx = 0;
        PWGM_HVERTEX hV = PwModEnumVertices(rti.model, ndx);
        while (PWGM_HVERTEX_ISVALID(hV)) {
            if (!writeNode(rti, hV)) {
                ret = false;
                break;
            }
            hV = PwModEnumVertices(rti.model, ++ndx);
        }
    }
    return caeuProgressEndStep(&rti) && ret;
}


static bool
writeDomainElements(CAEP_RTITEM &rti, const PWGM_HDOMAIN &hDom)
{
    PWGM_ELEMCOUNTS elemCnts;
    PWGM_CONDDATA condData;
    const PWP_UINT32 eCnt = PwDomElementCount(hDom, &elemCnts);
    bool ret = caeuProgressBeginStep(&rti, eCnt) &&
        PwDomCondition(hDom, &condData) &&
        writeKeyVal(rti, "MARKER_TAG", condData.name) &&
        writeKeyVal(rti, "MARKER_ELEMS", eCnt);
    if (ret) {
        PWP_UINT32 ndx = 0;
        PWGM_HELEMENT hE = PwDomEnumElements(hDom, ndx);
        PWGM_ELEMDATA eData;
        while (PwElemDataMod(hE, &eData)) {
            if (!writeElemData(rti, eData)) {
                ret = false;
                break;
            }
            if (!caeuProgressIncr(&rti)) {
                ret = false;
                break;
            }
            hE = PwDomEnumElements(hDom, ++ndx);
        }
    }
    return caeuProgressEndStep(&rti) && ret;
}


static bool
writeBoundariesSection(CAEP_RTITEM &rti)
{
    const PWP_UINT32 domCnt = PwModDomainCount(rti.model);
    bool ret = writeSectionComment(rti, "Boundary elements") &&
        writeKeyVal(rti, "NMARK", domCnt);
    if (ret) {
        PWP_UINT32 ndx = 0;
        PWGM_HDOMAIN hDom = PwModEnumDomains(rti.model, ndx);
        while (PWGM_HDOMAIN_ISVALID(hDom)) {
            if (!writeDomainElements(rti, hDom)) {
                ret = false;
                break;
            }
            hDom = PwModEnumDomains(rti.model, ++ndx);
        }
    }
    return ret;
}


static bool
writeDimensionSection(CAEP_RTITEM &rti)
{
    const PWP_UINT32 su2Dim =
        ((PWP_DIMENSION_2D == rti.pWriteInfo->dimension) ? 2 :
        ((PWP_DIMENSION_3D == rti.pWriteInfo->dimension) ? 3 : 0));
    return writeSectionComment(rti, "Problem dimension") &&
        writeKeyVal(rti, "NDIME", su2Dim);
}


// Plugin entry point
PWP_BOOL
runtimeWrite(CAEP_RTITEM *pRti, PWGM_HGRIDMODEL model, const CAEP_WRITEINFO *)
{
    const PWP_UINT32 numMajorSteps = PwModBlockCount(model) +
        PwModDomainCount(model) + 1;
    const bool ret = caeuProgressInit(pRti, numMajorSteps) &&
        writeDimensionSection(*pRti) && writeElementsSection(*pRti) &&
        writeNodesSection(*pRti) && writeBoundariesSection(*pRti);
    caeuProgressEnd(pRti, PWP_CAST_BOOL(ret));
    return PWP_CAST_BOOL(ret);
}


PWP_BOOL
runtimeCreate(CAEP_RTITEM * /*pRti*/)
{
    return  PWP_TRUE;
}


PWP_VOID
runtimeDestroy(CAEP_RTITEM * /*pRti*/)
{
}

/****************************************************************************
 *
 * This file is licensed under the Cadence Public License Version 1.0 (the
 * "License"), a copy of which is found in the included file named "LICENSE",
 * and is distributed "AS IS." TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE
 * LAW, CADENCE DISCLAIMS ALL WARRANTIES AND IN NO EVENT SHALL BE LIABLE TO
 * ANY PARTY FOR ANY DAMAGES ARISING OUT OF OR RELATING TO USE OF THIS FILE.
 * Please see the License for the full text of applicable terms.
 *
 ****************************************************************************/
