/*
 * WINED3D draw functions
 *
 * Copyright 2002-2004 Jason Edmeades
 * Copyright 2002-2004 Raphael Junqueira
 * Copyright 2004 Christian Costa
 * Copyright 2005 Oliver Stieber
 * Copyright 2006 Henri Verbeet
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d_draw);
#define GLINFO_LOCATION ((IWineD3DImpl *)(This->wineD3D))->gl_info

#include <stdio.h>

#if 0 /* TODO */
extern IWineD3DVertexShaderImpl*            VertexShaders[64];
extern IWineD3DVertexDeclarationImpl*       VertexShaderDeclarations[64];
extern IWineD3DPixelShaderImpl*             PixelShaders[64];

#undef GL_VERSION_1_4 /* To be fixed, caused by mesa headers */
#endif

/* Issues the glBegin call for gl given the primitive type and count */
static DWORD primitiveToGl(WINED3DPRIMITIVETYPE PrimitiveType,
                    DWORD            NumPrimitives,
                    GLenum          *primType)
{
    DWORD   NumVertexes = NumPrimitives;

    switch (PrimitiveType) {
    case WINED3DPT_POINTLIST:
        TRACE("POINTS\n");
        *primType   = GL_POINTS;
        NumVertexes = NumPrimitives;
        break;

    case WINED3DPT_LINELIST:
        TRACE("LINES\n");
        *primType   = GL_LINES;
        NumVertexes = NumPrimitives * 2;
        break;

    case WINED3DPT_LINESTRIP:
        TRACE("LINE_STRIP\n");
        *primType   = GL_LINE_STRIP;
        NumVertexes = NumPrimitives + 1;
        break;

    case WINED3DPT_TRIANGLELIST:
        TRACE("TRIANGLES\n");
        *primType   = GL_TRIANGLES;
        NumVertexes = NumPrimitives * 3;
        break;

    case WINED3DPT_TRIANGLESTRIP:
        TRACE("TRIANGLE_STRIP\n");
        *primType   = GL_TRIANGLE_STRIP;
        NumVertexes = NumPrimitives + 2;
        break;

    case WINED3DPT_TRIANGLEFAN:
        TRACE("TRIANGLE_FAN\n");
        *primType   = GL_TRIANGLE_FAN;
        NumVertexes = NumPrimitives + 2;
        break;

    default:
        FIXME("Unhandled primitive\n");
        *primType    = GL_POINTS;
        break;
    }
    return NumVertexes;
}

/* Ensure the appropriate material states are set up - only change
   state if really required                                        */
static void init_materials(IWineD3DDevice *iface, BOOL isDiffuseSupplied) {

    BOOL requires_material_reset = FALSE;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    if (This->tracking_color == NEEDS_TRACKING && isDiffuseSupplied) {
        /* If we have not set up the material color tracking, do it now as required */
        glDisable(GL_COLOR_MATERIAL); /* Note: Man pages state must enable AFTER calling glColorMaterial! Required?*/
        checkGLcall("glDisable GL_COLOR_MATERIAL");
        TRACE("glColorMaterial Parm=%x\n", This->tracking_parm);
        glColorMaterial(GL_FRONT_AND_BACK, This->tracking_parm);
        checkGLcall("glColorMaterial(GL_FRONT_AND_BACK, Parm)");
        glEnable(GL_COLOR_MATERIAL);
        checkGLcall("glEnable GL_COLOR_MATERIAL");
        This->tracking_color = IS_TRACKING;
        requires_material_reset = TRUE; /* Restore material settings as will be used */

    } else if ((This->tracking_color == IS_TRACKING && !isDiffuseSupplied) ||
               (This->tracking_color == NEEDS_TRACKING && !isDiffuseSupplied)) {
        /* If we are tracking the current color but one isn't supplied, don't! */
        glDisable(GL_COLOR_MATERIAL);
        checkGLcall("glDisable GL_COLOR_MATERIAL");
        This->tracking_color = NEEDS_TRACKING;
        requires_material_reset = TRUE; /* Restore material settings as will be used */

    } else if (This->tracking_color == IS_TRACKING && isDiffuseSupplied) {
        /* No need to reset material colors since no change to gl_color_material */
        requires_material_reset = FALSE;

    } else if (This->tracking_color == NEEDS_DISABLE) {
        glDisable(GL_COLOR_MATERIAL);
        checkGLcall("glDisable GL_COLOR_MATERIAL");
        This->tracking_color = DISABLED_TRACKING;
        requires_material_reset = TRUE; /* Restore material settings as will be used */
    }

    /* Reset the material colors which may have been tracking the color*/
    if (requires_material_reset) {
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, (float*) &This->stateBlock->material.Ambient);
        checkGLcall("glMaterialfv");
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, (float*) &This->stateBlock->material.Diffuse);
        checkGLcall("glMaterialfv");
        if (This->stateBlock->renderState[WINED3DRS_SPECULARENABLE]) {
           glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, (float*) &This->stateBlock->material.Specular);
           checkGLcall("glMaterialfv");
        } else {
           float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
           glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, &black[0]);
           checkGLcall("glMaterialfv");
        }
        glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, (float*) &This->stateBlock->material.Emissive);
        checkGLcall("glMaterialfv");
    }

}

static const GLfloat invymat[16] = {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, -1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f};

static BOOL fixed_get_input(
    BYTE usage, BYTE usage_idx,
    unsigned int* regnum) {

    *regnum = -1;

    /* Those positions must have the order in the
     * named part of the strided data */

    if ((usage == D3DDECLUSAGE_POSITION || usage == D3DDECLUSAGE_POSITIONT) && usage_idx == 0)
        *regnum = 0;
    else if (usage == D3DDECLUSAGE_BLENDWEIGHT && usage_idx == 0)
        *regnum = 1;
    else if (usage == D3DDECLUSAGE_BLENDINDICES && usage_idx == 0)
        *regnum = 2;
    else if (usage == D3DDECLUSAGE_NORMAL && usage_idx == 0)
        *regnum = 3;
    else if (usage == D3DDECLUSAGE_PSIZE && usage_idx == 0)
        *regnum = 4;
    else if (usage == D3DDECLUSAGE_COLOR && usage_idx == 0)
        *regnum = 5;
    else if (usage == D3DDECLUSAGE_COLOR && usage_idx == 1)
        *regnum = 6;
    else if (usage == D3DDECLUSAGE_TEXCOORD && usage_idx < WINED3DDP_MAXTEXCOORD)
        *regnum = 7 + usage_idx;
    else if ((usage == D3DDECLUSAGE_POSITION || usage == D3DDECLUSAGE_POSITIONT) && usage_idx == 1)
        *regnum = 7 + WINED3DDP_MAXTEXCOORD;
    else if (usage == D3DDECLUSAGE_NORMAL && usage_idx == 1)
        *regnum = 8 + WINED3DDP_MAXTEXCOORD;
    else if (usage == D3DDECLUSAGE_TANGENT && usage_idx == 0)
        *regnum = 9 + WINED3DDP_MAXTEXCOORD;
    else if (usage == D3DDECLUSAGE_BINORMAL && usage_idx == 0)
        *regnum = 10 + WINED3DDP_MAXTEXCOORD;
    else if (usage == D3DDECLUSAGE_TESSFACTOR && usage_idx == 0)
        *regnum = 11 + WINED3DDP_MAXTEXCOORD;
    else if (usage == D3DDECLUSAGE_FOG && usage_idx == 0)
        *regnum = 12 + WINED3DDP_MAXTEXCOORD;
    else if (usage == D3DDECLUSAGE_DEPTH && usage_idx == 0)
        *regnum = 13 + WINED3DDP_MAXTEXCOORD;
    else if (usage == D3DDECLUSAGE_SAMPLE && usage_idx == 0)
        *regnum = 14 + WINED3DDP_MAXTEXCOORD;

    if (*regnum < 0) {
        FIXME("Unsupported input stream [usage=%s, usage_idx=%u]\n",
            debug_d3ddeclusage(usage), usage_idx);
        return FALSE;
    }
    return TRUE;
}

void primitiveDeclarationConvertToStridedData(
     IWineD3DDevice *iface,
     BOOL useVertexShaderFunction,
     WineDirect3DVertexStridedData *strided,
     BOOL *fixup) {

     /* We need to deal with frequency data!*/

    BYTE  *data    = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVertexDeclarationImpl* vertexDeclaration = NULL;
    int i;
    WINED3DVERTEXELEMENT *element;
    DWORD stride;
    int reg;

    /* Locate the vertex declaration */
    if (This->stateBlock->vertexShader && ((IWineD3DVertexShaderImpl *)This->stateBlock->vertexShader)->vertexDeclaration) {
        TRACE("Using vertex declaration from shader\n");
        vertexDeclaration = (IWineD3DVertexDeclarationImpl *)((IWineD3DVertexShaderImpl *)This->stateBlock->vertexShader)->vertexDeclaration;
    } else {
        TRACE("Using vertex declaration\n");
        vertexDeclaration = (IWineD3DVertexDeclarationImpl *)This->stateBlock->vertexDecl;
    }

    /* Translate the declaration into strided data */
    for (i = 0 ; i < vertexDeclaration->declarationWNumElements - 1; ++i) {
        GLint streamVBO = 0;
        BOOL stride_used;
        unsigned int idx;

        element = vertexDeclaration->pDeclarationWine + i;
        TRACE("%p Element %p (%d of %d)\n", vertexDeclaration->pDeclarationWine,
            element,  i + 1, vertexDeclaration->declarationWNumElements - 1);

        if (This->stateBlock->streamSource[element->Stream] == NULL)
            continue;

        if (This->stateBlock->streamIsUP) {
            TRACE("Stream is up %d, %p\n", element->Stream, This->stateBlock->streamSource[element->Stream]);
            streamVBO = 0;
            data    = (BYTE *)This->stateBlock->streamSource[element->Stream];
            if(fixup && *fixup) FIXME("Missing fixed and unfixed vertices, expect graphics glitches\n");
        } else {
            TRACE("Stream isn't up %d, %p\n", element->Stream, This->stateBlock->streamSource[element->Stream]);
            IWineD3DVertexBuffer_PreLoad(This->stateBlock->streamSource[element->Stream]);
            data    = IWineD3DVertexBufferImpl_GetMemory(This->stateBlock->streamSource[element->Stream], 0, &streamVBO);
            if(fixup) {
                if( streamVBO != 0) *fixup = TRUE;
                else if(*fixup) FIXME("Missing fixed and unfixed vertices, expect graphics glitches\n");
            }
        }
        stride  = This->stateBlock->streamStride[element->Stream];
        data += element->Offset;
        reg = element->Reg;

        TRACE("Offset %d Stream %d UsageIndex %d\n", element->Offset, element->Stream, element->UsageIndex);

        if (useVertexShaderFunction)
            stride_used = vshader_get_input(This->stateBlock->vertexShader,
                element->Usage, element->UsageIndex, &idx);
        else
            stride_used = fixed_get_input(element->Usage, element->UsageIndex, &idx);

        if (stride_used) {
           TRACE("Loaded %s array %u [usage=%s, usage_idx=%u, "
                 "stream=%u, offset=%u, stride=%u, VBO=%u]\n",
                 useVertexShaderFunction? "shader": "fixed function", idx,
                 debug_d3ddeclusage(element->Usage), element->UsageIndex,
                 element->Stream, element->Offset, stride, streamVBO);

           strided->u.input[idx].lpData = data;
           strided->u.input[idx].dwType = element->Type;
           strided->u.input[idx].dwStride = stride;
           strided->u.input[idx].VBO = streamVBO;
           if (!useVertexShaderFunction) {
               if (element->Usage == D3DDECLUSAGE_POSITION)
                   strided->u.s.position_transformed = FALSE;
               else if (element->Usage == D3DDECLUSAGE_POSITIONT)
                   strided->u.s.position_transformed = TRUE;
           }
        }
    };
}

void primitiveConvertFVFtoOffset(DWORD thisFVF, DWORD stride, BYTE *data, WineDirect3DVertexStridedData *strided, GLint streamVBO) {
    int           numBlends;
    int           numTextures;
    int           textureNo;
    int           coordIdxInfo = 0x00;    /* Information on number of coords supplied */
    int           numCoords[8];           /* Holding place for WINED3DFVF_TEXTUREFORMATx  */

    /* Either 3 or 4 floats depending on the FVF */
    /* FIXME: Can blending data be in a different stream to the position data?
          and if so using the fixed pipeline how do we handle it               */
    if (thisFVF & WINED3DFVF_POSITION_MASK) {
        strided->u.s.position.lpData    = data;
        strided->u.s.position.dwType    = WINED3DDECLTYPE_FLOAT3;
        strided->u.s.position.dwStride  = stride;
        strided->u.s.position.VBO       = streamVBO;
        data += 3 * sizeof(float);
        if (thisFVF & WINED3DFVF_XYZRHW) {
            strided->u.s.position.dwType = WINED3DDECLTYPE_FLOAT4;
            strided->u.s.position_transformed = TRUE;
            data += sizeof(float);
        } else
            strided->u.s.position_transformed = FALSE;
    }

    /* Blending is numBlends * FLOATs followed by a DWORD for UBYTE4 */
    /** do we have to Check This->stateBlock->renderState[D3DRS_INDEXEDVERTEXBLENDENABLE] ? */
    numBlends = 1 + (((thisFVF & WINED3DFVF_XYZB5) - WINED3DFVF_XYZB1) >> 1);
    if(thisFVF & WINED3DFVF_LASTBETA_UBYTE4) numBlends--;

    if ((thisFVF & WINED3DFVF_XYZB5 ) > WINED3DFVF_XYZRHW) {
        TRACE("Setting blend Weights to %p\n", data);
        strided->u.s.blendWeights.lpData    = data;
        strided->u.s.blendWeights.dwType    = WINED3DDECLTYPE_FLOAT1 + numBlends - 1;
        strided->u.s.blendWeights.dwStride  = stride;
        strided->u.s.blendWeights.VBO       = streamVBO;
        data += numBlends * sizeof(FLOAT);

        if (thisFVF & WINED3DFVF_LASTBETA_UBYTE4) {
            strided->u.s.blendMatrixIndices.lpData = data;
            strided->u.s.blendMatrixIndices.dwType  = WINED3DDECLTYPE_UBYTE4;
            strided->u.s.blendMatrixIndices.dwStride= stride;
            strided->u.s.blendMatrixIndices.VBO     = streamVBO;
            data += sizeof(DWORD);
        }
    }

    /* Normal is always 3 floats */
    if (thisFVF & WINED3DFVF_NORMAL) {
        strided->u.s.normal.lpData    = data;
        strided->u.s.normal.dwType    = WINED3DDECLTYPE_FLOAT3;
        strided->u.s.normal.dwStride  = stride;
        strided->u.s.normal.VBO     = streamVBO;
        data += 3 * sizeof(FLOAT);
    }

    /* Pointsize is a single float */
    if (thisFVF & WINED3DFVF_PSIZE) {
        strided->u.s.pSize.lpData    = data;
        strided->u.s.pSize.dwType    = WINED3DDECLTYPE_FLOAT1;
        strided->u.s.pSize.dwStride  = stride;
        strided->u.s.pSize.VBO       = streamVBO;
        data += sizeof(FLOAT);
    }

    /* Diffuse is 4 unsigned bytes */
    if (thisFVF & WINED3DFVF_DIFFUSE) {
        strided->u.s.diffuse.lpData    = data;
        strided->u.s.diffuse.dwType    = WINED3DDECLTYPE_SHORT4;
        strided->u.s.diffuse.dwStride  = stride;
        strided->u.s.diffuse.VBO       = streamVBO;
        data += sizeof(DWORD);
    }

    /* Specular is 4 unsigned bytes */
    if (thisFVF & WINED3DFVF_SPECULAR) {
        strided->u.s.specular.lpData    = data;
        strided->u.s.specular.dwType    = WINED3DDECLTYPE_SHORT4;
        strided->u.s.specular.dwStride  = stride;
        strided->u.s.specular.VBO       = streamVBO;
        data += sizeof(DWORD);
    }

    /* Texture coords */
    numTextures   = (thisFVF & WINED3DFVF_TEXCOUNT_MASK) >> WINED3DFVF_TEXCOUNT_SHIFT;
    coordIdxInfo  = (thisFVF & 0x00FF0000) >> 16; /* 16 is from definition of WINED3DFVF_TEXCOORDSIZE1, and is 8 (0-7 stages) * 2bits long */

    /* numTextures indicates the number of texture coordinates supplied */
    /* However, the first set may not be for stage 0 texture - it all   */
    /*   depends on WINED3DTSS_TEXCOORDINDEX.                           */
    /* The number of bytes for each coordinate set is based off         */
    /*   WINED3DFVF_TEXCOORDSIZEn, which are the bottom 2 bits              */

    /* So, for each supplied texture extract the coords */
    for (textureNo = 0; textureNo < numTextures; ++textureNo) {

        strided->u.s.texCoords[textureNo].lpData    = data;
        strided->u.s.texCoords[textureNo].dwType    = WINED3DDECLTYPE_FLOAT1;
        strided->u.s.texCoords[textureNo].dwStride  = stride;
        strided->u.s.texCoords[textureNo].VBO       = streamVBO;
        numCoords[textureNo] = coordIdxInfo & 0x03;

        /* Always one set */
        data += sizeof(float);
        if (numCoords[textureNo] != WINED3DFVF_TEXTUREFORMAT1) {
            strided->u.s.texCoords[textureNo].dwType = WINED3DDECLTYPE_FLOAT2;
            data += sizeof(float);
            if (numCoords[textureNo] != WINED3DFVF_TEXTUREFORMAT2) {
                strided->u.s.texCoords[textureNo].dwType = WINED3DDECLTYPE_FLOAT3;
                data += sizeof(float);
                if (numCoords[textureNo] != WINED3DFVF_TEXTUREFORMAT3) {
                    strided->u.s.texCoords[textureNo].dwType = WINED3DDECLTYPE_FLOAT4;
                    data += sizeof(float);
                }
            }
        }
        coordIdxInfo = coordIdxInfo >> 2; /* Drop bottom two bits */
    }
}

void primitiveConvertToStridedData(IWineD3DDevice *iface, WineDirect3DVertexStridedData *strided, BOOL *fixup) {

    short         LoopThroughTo = 0;
    short         nStream;
    GLint         streamVBO = 0;

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* OK, Now to setup the data locations
       For the non-created vertex shaders, the VertexShader var holds the real
          FVF and only stream 0 matters
       For the created vertex shaders, there is an FVF per stream              */
    if (!This->stateBlock->streamIsUP && !(This->stateBlock->vertexShader == NULL)) {
        LoopThroughTo = MAX_STREAMS;
    } else {
        LoopThroughTo = 1;
    }

    /* Work through stream by stream */
    for (nStream=0; nStream<LoopThroughTo; ++nStream) {
        DWORD  stride  = This->stateBlock->streamStride[nStream];
        BYTE  *data    = NULL;
        DWORD  thisFVF = 0;

        /* Skip empty streams */
        if (This->stateBlock->streamSource[nStream] == NULL) continue;

        /* Retrieve appropriate FVF */
        if (LoopThroughTo == 1) { /* Use FVF, not vertex shader */
            thisFVF = This->stateBlock->fvf;
            /* Handle memory passed directly as well as vertex buffers */
            if (This->stateBlock->streamIsUP) {
                streamVBO = 0;
                data    = (BYTE *)This->stateBlock->streamSource[nStream];
            } else {
                IWineD3DVertexBuffer_PreLoad(This->stateBlock->streamSource[nStream]);
                /* GetMemory binds the VBO */
                data = IWineD3DVertexBufferImpl_GetMemory(This->stateBlock->streamSource[nStream], 0, &streamVBO);
                if(fixup) {
                    if(streamVBO != 0 ) *fixup = TRUE;
                }
            }
        } else {
#if 0 /* TODO: Vertex shader support */
            thisFVF = This->stateBlock->vertexShaderDecl->fvf[nStream];
            data    = IWineD3DVertexBufferImpl_GetMemory(This->stateBlock->streamSource[nStream], 0);
#endif
        }
        VTRACE(("FVF for stream %d is %lx\n", nStream, thisFVF));
        if (thisFVF == 0) continue;

        /* Now convert the stream into pointers */
        primitiveConvertFVFtoOffset(thisFVF, stride, data, strided, streamVBO);
    }
}

#if 0 /* TODO: Software Shaders */
/* Draw a single vertex using this information */
static void draw_vertex(IWineD3DDevice *iface,                         /* interface    */
                 BOOL isXYZ,    float x, float y, float z, float rhw,  /* xyzn position*/
                 BOOL isNormal, float nx, float ny, float nz,          /* normal       */
                 BOOL isDiffuse, float *dRGBA,                         /* 1st   colors */
                 BOOL isSpecular, float *sRGB,                         /* 2ndry colors */
                 BOOL isPtSize, float ptSize,                       /* pointSize    */
                 WINED3DVECTOR_4 *texcoords, int *numcoords)        /* texture info */
{
    unsigned int textureNo;
    float s, t, r, q;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Diffuse -------------------------------- */
    if (isDiffuse) {
        glColor4fv(dRGBA);
        VTRACE(("glColor4f: r,g,b,a=%f,%f,%f,%f\n", dRGBA[0], dRGBA[1], dRGBA[2], dRGBA[3]));
    }

    /* Specular Colour ------------------------------------------*/
    if (isSpecular) {
        if (GL_SUPPORT(EXT_SECONDARY_COLOR)) {
          GL_EXTCALL(glSecondaryColor3fvEXT(sRGB));
          VTRACE(("glSecondaryColor4f: r,g,b=%f,%f,%f\n", sRGB[0], sRGB[1], sRGB[2]));
        } else {
	  VTRACE(("Specular color extensions not supplied\n"));
	}
    }

    /* Normal -------------------------------- */
    if (isNormal) {
        VTRACE(("glNormal:nx,ny,nz=%f,%f,%f\n", nx,ny,nz));
        glNormal3f(nx, ny, nz);
    }

    /* Point Size ----------------------------------------------*/
    if (isPtSize) {

        /* no such functionality in the fixed function GL pipeline */
        FIXME("Cannot change ptSize here in openGl\n");
    }

    /* Texture coords --------------------------- */
    for (textureNo = 0; textureNo < GL_LIMITS(textures); ++textureNo) {

        if (!GL_SUPPORT(ARB_MULTITEXTURE) && textureNo > 0) {
            FIXME("Program using multiple concurrent textures which this opengl implementation doesn't support\n");
            continue ;
        }

        /* Query tex coords */
        if (This->stateBlock->textures[textureNo] != NULL) {

            int    coordIdx = This->stateBlock->textureState[textureNo][WINED3DTSS_TEXCOORDINDEX];
            if (coordIdx >= MAX_TEXTURES) {
                VTRACE(("tex: %d - Skip tex coords, as being system generated\n", textureNo));
                continue;
            } else if (numcoords[coordIdx] == 0) {
                TRACE("tex: %d - Skipping tex coords, as no data supplied or no coords supplied\n", textureNo);
                continue;
            } else {

                /* Initialize vars */
                s = 0.0f;
                t = 0.0f;
                r = 0.0f;
                q = 0.0f;

                switch (numcoords[coordIdx]) {
                case 4: q = texcoords[coordIdx].w; /* drop through */
                case 3: r = texcoords[coordIdx].z; /* drop through */
                case 2: t = texcoords[coordIdx].y; /* drop through */
                case 1: s = texcoords[coordIdx].x;
                }

                switch (numcoords[coordIdx]) {   /* Supply the provided texture coords */
                case WINED3DTTFF_COUNT1:
                    VTRACE(("tex:%d, s=%f\n", textureNo, s));
                    if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                        GLMULTITEXCOORD1F(textureNo, s);
                    } else {
                        glTexCoord1f(s);
                    }
                    break;
                case WINED3DTTFF_COUNT2:
                    VTRACE(("tex:%d, s=%f, t=%f\n", textureNo, s, t));
                    if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                        GLMULTITEXCOORD2F(textureNo, s, t);
                    } else {
                        glTexCoord2f(s, t);
                    }
                    break;
                case WINED3DTTFF_COUNT3:
                    VTRACE(("tex:%d, s=%f, t=%f, r=%f\n", textureNo, s, t, r));
                    if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                        GLMULTITEXCOORD3F(textureNo, s, t, r);
                    } else {
                        glTexCoord3f(s, t, r);
                    }
                    break;
                case WINED3DTTFF_COUNT4:
                    VTRACE(("tex:%d, s=%f, t=%f, r=%f, q=%f\n", textureNo, s, t, r, q));
                    if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                        GLMULTITEXCOORD4F(textureNo, s, t, r, q);
                    } else {
                        glTexCoord4f(s, t, r, q);
                    }
                    break;
                default:
                    FIXME("Should not get here as numCoords should be 0->4 (%x)!\n", numcoords[coordIdx]);
                }
            }
        }
    } /* End of textures */

    /* Position -------------------------------- */
    if (isXYZ) {
        if (1.0f == rhw || rhw < 0.00001f) {
            VTRACE(("Vertex: glVertex:x,y,z=%f,%f,%f\n", x,y,z));
            glVertex3f(x, y, z);
        } else {
            /* Cannot optimize by dividing through by rhw as rhw is required
               later for perspective in the GL pipeline for vertex shaders   */
            VTRACE(("Vertex: glVertex:x,y,z=%f,%f,%f / rhw=%f\n", x,y,z,rhw));
            glVertex4f(x,y,z,rhw);
        }
    }
}
#endif /* TODO: Software shaders */

static void drawStridedFast(IWineD3DDevice *iface,UINT numberOfVertices, GLenum glPrimitiveType,
                     const void *idxData, short idxSize, ULONG minIndex, ULONG startIdx, ULONG startVertex) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    if (idxData != NULL /* This crashes sometimes!*/) {
        TRACE("(%p) : glElements(%x, %d, %d, ...)\n", This, glPrimitiveType, numberOfVertices, minIndex);
        idxData = idxData == (void *)-1 ? NULL : idxData;
#if 1
#if 0
        glIndexPointer(idxSize == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idxSize, startIdx);
        glEnableClientState(GL_INDEX_ARRAY);
#endif
        glDrawElements(glPrimitiveType, numberOfVertices, idxSize == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                     (const char *)idxData+(idxSize * startIdx));
#else /* using drawRangeElements may be faster */

        glDrawRangeElements(glPrimitiveType, minIndex, minIndex + numberOfVertices - 1, numberOfVertices,
                      idxSize == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                      (const char *)idxData+(idxSize * startIdx));
#endif
        checkGLcall("glDrawRangeElements");

    } else {

        /* Note first is now zero as we shuffled along earlier */
        TRACE("(%p) : glDrawArrays(%x, 0, %d)\n", This, glPrimitiveType, numberOfVertices);
        glDrawArrays(glPrimitiveType, startVertex, numberOfVertices);
        checkGLcall("glDrawArrays");

    }

    return;
}

/*
 * Actually draw using the supplied information.
 * Slower GL version which extracts info about each vertex in turn
 */

static void drawStridedSlow(IWineD3DDevice *iface, WineDirect3DVertexStridedData *sd,
                     UINT NumVertexes, GLenum glPrimType,
                     const void *idxData, short idxSize, ULONG minIndex, ULONG startIdx, ULONG startVertex) {

    unsigned int               textureNo    = 0;
    unsigned int               texture_idx  = 0;
    const short               *pIdxBufS     = NULL;
    const long                *pIdxBufL     = NULL;
    LONG                       vx_index;
    float x  = 0.0f, y  = 0.0f, z = 0.0f;  /* x,y,z coordinates          */
    float nx = 0.0f, ny = 0.0, nz = 0.0f;  /* normal x,y,z coordinates   */
    float rhw = 0.0f;                      /* rhw                        */
    float ptSize = 0.0f;                   /* Point size                 */
    DWORD diffuseColor = 0xFFFFFFFF;       /* Diffuse Color              */
    DWORD specularColor = 0;               /* Specular Color             */
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    LONG                       SkipnStrides = startVertex + This->stateBlock->baseVertexIndex;

    TRACE("Using slow vertex array code\n");

    /* Variable Initialization */
    if (idxData != NULL) {
        if (idxSize == 2) pIdxBufS = (const short *) idxData;
        else pIdxBufL = (const long *) idxData;
    }

    /* Start drawing in GL */
    VTRACE(("glBegin(%x)\n", glPrimType));
    glBegin(glPrimType);

    /* We shouldn't start this function if any VBO is involved. Should I put a safety check here?
     * Guess it's not necessary(we crash then anyway) and would only eat CPU time
     */

    /* For each primitive */
    for (vx_index = 0; vx_index < NumVertexes; ++vx_index) {

        /* Initialize diffuse color */
        diffuseColor = 0xFFFFFFFF;

        /* For indexed data, we need to go a few more strides in */
        if (idxData != NULL) {

            /* Indexed so work out the number of strides to skip */
            if (idxSize == 2) {
                VTRACE(("Idx for vertex %d = %d\n", vx_index, pIdxBufS[startIdx+vx_index]));
                SkipnStrides = pIdxBufS[startIdx + vx_index] + This->stateBlock->baseVertexIndex;
            } else {
                VTRACE(("Idx for vertex %d = %d\n", vx_index, pIdxBufL[startIdx+vx_index]));
                SkipnStrides = pIdxBufL[startIdx + vx_index] + This->stateBlock->baseVertexIndex;
            }
        }

        /* Position Information ------------------ */
        if (sd->u.s.position.lpData != NULL) {

            float *ptrToCoords = (float *)(sd->u.s.position.lpData + (SkipnStrides * sd->u.s.position.dwStride));
            x = ptrToCoords[0];
            y = ptrToCoords[1];
            z = ptrToCoords[2];
            rhw = 1.0;
            VTRACE(("x,y,z=%f,%f,%f\n", x,y,z));

            /* RHW follows, only if transformed, ie 4 floats were provided */
            if (sd->u.s.position_transformed) {
                rhw = ptrToCoords[3];
                VTRACE(("rhw=%f\n", rhw));
            }
        }

        /* Blending data -------------------------- */
        if (sd->u.s.blendWeights.lpData != NULL) {
            /* float *ptrToCoords = (float *)(sd->u.s.blendWeights.lpData + (SkipnStrides * sd->u.s.blendWeights.dwStride)); */
            FIXME("Blending not supported yet\n");

            if (sd->u.s.blendMatrixIndices.lpData != NULL) {
                /*DWORD *ptrToCoords = (DWORD *)(sd->u.s.blendMatrixIndices.lpData + (SkipnStrides * sd->u.s.blendMatrixIndices.dwStride));*/
            }
        }

        /* Vertex Normal Data (untransformed only)- */
        if (sd->u.s.normal.lpData != NULL) {

            float *ptrToCoords = (float *)(sd->u.s.normal.lpData + (SkipnStrides * sd->u.s.normal.dwStride));
            nx = ptrToCoords[0];
            ny = ptrToCoords[1];
            nz = ptrToCoords[2];
            VTRACE(("nx,ny,nz=%f,%f,%f\n", nx, ny, nz));
        }

        /* Point Size ----------------------------- */
        if (sd->u.s.pSize.lpData != NULL) {

            float *ptrToCoords = (float *)(sd->u.s.pSize.lpData + (SkipnStrides * sd->u.s.pSize.dwStride));
            ptSize = ptrToCoords[0];
            VTRACE(("ptSize=%f\n", ptSize));
            FIXME("No support for ptSize yet\n");
        }

        /* Diffuse -------------------------------- */
        if (sd->u.s.diffuse.lpData != NULL) {

            DWORD *ptrToCoords = (DWORD *)(sd->u.s.diffuse.lpData + (SkipnStrides * sd->u.s.diffuse.dwStride));
            diffuseColor = ptrToCoords[0];
            VTRACE(("diffuseColor=%lx\n", diffuseColor));
        }

        /* Specular  -------------------------------- */
        if (sd->u.s.specular.lpData != NULL) {

            DWORD *ptrToCoords = (DWORD *)(sd->u.s.specular.lpData + (SkipnStrides * sd->u.s.specular.dwStride));
            specularColor = ptrToCoords[0];
            VTRACE(("specularColor=%lx\n", specularColor));
        }

        /* Texture coords --------------------------- */
        for (textureNo = 0, texture_idx = 0; textureNo < GL_LIMITS(texture_stages); ++textureNo) {

            if (!GL_SUPPORT(ARB_MULTITEXTURE) && textureNo > 0) {
                FIXME("Program using multiple concurrent textures which this opengl implementation doesn't support\n");
                continue ;
            }

            /* Query tex coords */
            if (This->stateBlock->textures[textureNo] != NULL) {

                int    coordIdx = This->stateBlock->textureState[textureNo][WINED3DTSS_TEXCOORDINDEX];
                float *ptrToCoords = NULL;
                float  s = 0.0, t = 0.0, r = 0.0, q = 0.0;

                if (coordIdx > 7) {
                    VTRACE(("tex: %d - Skip tex coords, as being system generated\n", textureNo));
                    ++texture_idx;
                    continue;
                } else if (coordIdx < 0) {
                    FIXME("tex: %d - Coord index %d is less than zero, expect a crash.\n", textureNo, coordIdx);
                    ++texture_idx;
                    continue;
                }

                ptrToCoords = (float *)(sd->u.s.texCoords[coordIdx].lpData + (SkipnStrides * sd->u.s.texCoords[coordIdx].dwStride));
                if (sd->u.s.texCoords[coordIdx].lpData == NULL) {
                    TRACE("tex: %d - Skipping tex coords, as no data supplied\n", textureNo);
                    ++texture_idx;
                    continue;
                } else {

                    int coordsToUse = sd->u.s.texCoords[coordIdx].dwType + 1; /* 0 == WINED3DDECLTYPE_FLOAT1 etc */

                    /* The coords to supply depend completely on the fvf / vertex shader */
                    switch (coordsToUse) {
                    case 4: q = ptrToCoords[3]; /* drop through */
                    case 3: r = ptrToCoords[2]; /* drop through */
                    case 2: t = ptrToCoords[1]; /* drop through */
                    case 1: s = ptrToCoords[0];
                    }

                    /* Projected is more 'fun' - Move the last coord to the 'q'
                          parameter (see comments under WINED3DTSS_TEXTURETRANSFORMFLAGS */
                    if ((This->stateBlock->textureState[textureNo][WINED3DTSS_TEXTURETRANSFORMFLAGS] != WINED3DTTFF_DISABLE) &&
                        (This->stateBlock->textureState[textureNo][WINED3DTSS_TEXTURETRANSFORMFLAGS] & WINED3DTTFF_PROJECTED)) {

                        if (This->stateBlock->textureState[textureNo][WINED3DTSS_TEXTURETRANSFORMFLAGS] & WINED3DTTFF_PROJECTED) {
                            switch (coordsToUse) {
                            case 0:  /* Drop Through */
                            case 1:
                                FIXME("WINED3DTTFF_PROJECTED but only zero or one coordinate?\n");
                                break;
                            case 2:
                                q = t;
                                t = 0.0;
                                coordsToUse = 4;
                                break;
                            case 3:
                                q = r;
                                r = 0.0;
                                coordsToUse = 4;
                                break;
                            case 4:  /* Nop here */
                                break;
                            default:
                                FIXME("Unexpected WINED3DTSS_TEXTURETRANSFORMFLAGS value of %d\n",
                                      This->stateBlock->textureState[textureNo][WINED3DTSS_TEXTURETRANSFORMFLAGS] & WINED3DTTFF_PROJECTED);
                            }
                        }
                    }

                    switch (coordsToUse) {   /* Supply the provided texture coords */
                    case WINED3DTTFF_COUNT1:
                        VTRACE(("tex:%d, s=%f\n", textureNo, s));
                        if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                            GL_EXTCALL(glMultiTexCoord1fARB(texture_idx, s));
                        } else {
                            glTexCoord1f(s);
                        }
                        break;
                    case WINED3DTTFF_COUNT2:
                        VTRACE(("tex:%d, s=%f, t=%f\n", textureNo, s, t));
                        if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                            GL_EXTCALL(glMultiTexCoord2fARB(texture_idx, s, t));
                        } else {
                            glTexCoord2f(s, t);
                        }
                        break;
                    case WINED3DTTFF_COUNT3:
                        VTRACE(("tex:%d, s=%f, t=%f, r=%f\n", textureNo, s, t, r));
                        if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                            GL_EXTCALL(glMultiTexCoord3fARB(texture_idx, s, t, r));
                        } else {
                            glTexCoord3f(s, t, r);
                        }
                        break;
                    case WINED3DTTFF_COUNT4:
                        VTRACE(("tex:%d, s=%f, t=%f, r=%f, q=%f\n", textureNo, s, t, r, q));
                        if (GL_SUPPORT(ARB_MULTITEXTURE)) {
                            GL_EXTCALL(glMultiTexCoord4fARB(texture_idx, s, t, r, q));
                        } else {
                            glTexCoord4f(s, t, r, q);
                        }
                        break;
                    default:
                        FIXME("Should not get here as coordsToUse is two bits only (%x)!\n", coordsToUse);
                    }
                }
            }
            if (/*!GL_SUPPORT(NV_REGISTER_COMBINERS) || This->stateBlock->textures[textureNo]*/TRUE) ++texture_idx;
        } /* End of textures */

        /* Diffuse -------------------------------- */
        if (sd->u.s.diffuse.lpData != NULL) {
	  glColor4ub(D3DCOLOR_B_R(diffuseColor),
		     D3DCOLOR_B_G(diffuseColor),
		     D3DCOLOR_B_B(diffuseColor),
		     D3DCOLOR_B_A(diffuseColor));
            VTRACE(("glColor4ub: r,g,b,a=%lu,%lu,%lu,%lu\n", 
                    D3DCOLOR_B_R(diffuseColor),
		    D3DCOLOR_B_G(diffuseColor),
		    D3DCOLOR_B_B(diffuseColor),
		    D3DCOLOR_B_A(diffuseColor)));
        } else {
            if (vx_index == 0) glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        }

        /* Specular ------------------------------- */
        if (sd->u.s.specular.lpData != NULL) {
            /* special case where the fog density is stored in the diffuse alpha channel */
            if(This->stateBlock->renderState[WINED3DRS_FOGENABLE] &&
              (This->stateBlock->renderState[WINED3DRS_FOGVERTEXMODE] == WINED3DFOG_NONE || sd->u.s.position.dwType == WINED3DDECLTYPE_FLOAT4 )&&
              This->stateBlock->renderState[WINED3DRS_FOGTABLEMODE] == WINED3DFOG_NONE) {
                if(GL_SUPPORT(EXT_FOG_COORD)) {
                    GL_EXTCALL(glFogCoordfEXT(specularColor >> 24));
                } else {
                    static BOOL warned = FALSE;
                    if(!warned) {
                        /* TODO: Use the fog table code from old ddraw */
                        FIXME("Implement fog for transformed vertices in software\n");
                        warned = TRUE;
                    }
                }
            }

            VTRACE(("glSecondaryColor4ub: r,g,b=%lu,%lu,%lu\n", 
                    D3DCOLOR_B_R(specularColor), 
                    D3DCOLOR_B_G(specularColor), 
                    D3DCOLOR_B_B(specularColor)));
            if (GL_SUPPORT(EXT_SECONDARY_COLOR)) {
                GL_EXTCALL(glSecondaryColor3ubEXT)(
                           D3DCOLOR_B_R(specularColor),
                           D3DCOLOR_B_G(specularColor),
                           D3DCOLOR_B_B(specularColor));
            } else {
                /* Do not worry if specular colour missing and disable request */
                VTRACE(("Specular color extensions not supplied\n"));
            }
        } else {
            if (vx_index == 0) {
                if (GL_SUPPORT(EXT_SECONDARY_COLOR)) {
                    GL_EXTCALL(glSecondaryColor3fEXT)(0, 0, 0);
                } else {
                    /* Do not worry if specular colour missing and disable request */
                    VTRACE(("Specular color extensions not supplied\n"));
                }
            }
        }

        /* Normal -------------------------------- */
        if (sd->u.s.normal.lpData != NULL) {
            VTRACE(("glNormal:nx,ny,nz=%f,%f,%f\n", nx,ny,nz));
            glNormal3f(nx, ny, nz);
        } else {
            if (vx_index == 0) glNormal3f(0, 0, 1);
        }

        /* Position -------------------------------- */
        if (sd->u.s.position.lpData != NULL) {
            if (1.0f == rhw || ((rhw < eps) && (rhw > -eps))) {
                VTRACE(("Vertex: glVertex:x,y,z=%f,%f,%f\n", x,y,z));
                glVertex3f(x, y, z);
            } else {
                GLfloat w = 1.0 / rhw;
                VTRACE(("Vertex: glVertex:x,y,z=%f,%f,%f / rhw=%f\n", x,y,z,rhw));
                glVertex4f(x*w, y*w, z*w, w);
            }
        }

        /* For non indexed mode, step onto next parts */
        if (idxData == NULL) {
            ++SkipnStrides;
        }
    }

    glEnd();
    checkGLcall("glEnd and previous calls");
}

#if 0 /* TODO: Software/Hardware vertex blending support */
/*
 * Draw with emulated vertex shaders
 * Note: strided data is uninitialized, as we need to pass the vertex
 *     shader directly as ordering irs yet
 */
void drawStridedSoftwareVS(IWineD3DDevice *iface, WineDirect3DVertexStridedData *sd,
                     int PrimitiveType, ULONG NumPrimitives,
                     const void *idxData, short idxSize, ULONG minIndex, ULONG startIdx) {

    unsigned int               textureNo    = 0;
    GLenum                     glPrimType   = GL_POINTS;
    int                        NumVertexes  = NumPrimitives;
    const short               *pIdxBufS     = NULL;
    const long                *pIdxBufL     = NULL;
    LONG                       SkipnStrides = 0;
    LONG                       vx_index;
    float x  = 0.0f, y  = 0.0f, z = 0.0f;  /* x,y,z coordinates          */
    float rhw = 0.0f;                      /* rhw                        */
    float ptSize = 0.0f;                   /* Point size                 */
    D3DVECTOR_4 texcoords[8];              /* Texture Coords             */
    int   numcoords[8];                    /* Number of coords           */
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    IDirect3DVertexShaderImpl* vertexShader = NULL;

    TRACE("Using slow software vertex shader code\n");

    /* Variable Initialization */
    if (idxData != NULL) {
        if (idxSize == 2) pIdxBufS = (const short *) idxData;
        else pIdxBufL = (const long *) idxData;
    }

    /* Ok, Work out which primitive is requested and how many vertexes that will be */
    NumVertexes = primitiveToGl(PrimitiveType, NumPrimitives, &glPrimType);

    /* Retrieve the VS information */
    vertexShader = (IWineD3DVertexShaderImp *)This->stateBlock->VertexShader;

    /* Start drawing in GL */
    VTRACE(("glBegin(%x)\n", glPrimType));
    glBegin(glPrimType);

    /* For each primitive */
    for (vx_index = 0; vx_index < NumVertexes; ++vx_index) {

        /* For indexed data, we need to go a few more strides in */
        if (idxData != NULL) {

            /* Indexed so work out the number of strides to skip */
            if (idxSize == 2) {
                VTRACE(("Idx for vertex %d = %d\n", vx_index, pIdxBufS[startIdx+vx_index]));
                SkipnStrides = pIdxBufS[startIdx+vx_index];
            } else {
                VTRACE(("Idx for vertex %d = %d\n", vx_index, pIdxBufL[startIdx+vx_index]));
                SkipnStrides = pIdxBufL[startIdx+vx_index];
            }
        }

        /* Fill the vertex shader input */
        IDirect3DDeviceImpl_FillVertexShaderInputSW(This, vertexShader, SkipnStrides);

        /* Initialize the output fields to the same defaults as it would normally have */
        memset(&vertexShader->output, 0, sizeof(VSHADEROUTPUTDATA8));
        vertexShader->output.oD[0].x = 1.0;
        vertexShader->output.oD[0].y = 1.0;
        vertexShader->output.oD[0].z = 1.0;
        vertexShader->output.oD[0].w = 1.0;

        /* Now execute the vertex shader */
        IDirect3DVertexShaderImpl_ExecuteSW(vertexShader, &vertexShader->input, &vertexShader->output);

        /*
        TRACE_VECTOR(vertexShader->output.oPos);
        TRACE_VECTOR(vertexShader->output.oD[0]);
        TRACE_VECTOR(vertexShader->output.oD[1]);
        TRACE_VECTOR(vertexShader->output.oT[0]);
        TRACE_VECTOR(vertexShader->output.oT[1]);
        TRACE_VECTOR(vertexShader->input.V[0]);
        TRACE_VECTOR(vertexShader->data->C[0]);
        TRACE_VECTOR(vertexShader->data->C[1]);
        TRACE_VECTOR(vertexShader->data->C[2]);
        TRACE_VECTOR(vertexShader->data->C[3]);
        TRACE_VECTOR(vertexShader->data->C[4]);
        TRACE_VECTOR(vertexShader->data->C[5]);
        TRACE_VECTOR(vertexShader->data->C[6]);
        TRACE_VECTOR(vertexShader->data->C[7]);
        */

        /* Extract out the output */
        /* FIXME: Fog coords? */
        x = vertexShader->output.oPos.x;
        y = vertexShader->output.oPos.y;
        z = vertexShader->output.oPos.z;
        rhw = vertexShader->output.oPos.w;
        ptSize = vertexShader->output.oPts.x; /* Fixme - Is this right? */

        /** Update textures coords using vertexShader->output.oT[0->7] */
        memset(texcoords, 0x00, sizeof(texcoords));
        memset(numcoords, 0x00, sizeof(numcoords));
        for (textureNo = 0; textureNo < GL_LIMITS(textures); ++textureNo) {
            if (This->stateBlock->textures[textureNo] != NULL) {
               texcoords[textureNo].x = vertexShader->output.oT[textureNo].x;
               texcoords[textureNo].y = vertexShader->output.oT[textureNo].y;
               texcoords[textureNo].z = vertexShader->output.oT[textureNo].z;
               texcoords[textureNo].w = vertexShader->output.oT[textureNo].w;
               if (This->stateBlock->texture_state[textureNo][WINED3DTSS_TEXTURETRANSFORMFLAGS] != WINED3DTTFF_DISABLE) {
                   numcoords[textureNo]    = This->stateBlock->texture_state[textureNo][WINED3DTSS_TEXTURETRANSFORMFLAGS] & ~WINED3DTTFF_PROJECTED;
               } else {
                   switch (IDirect3DBaseTexture8Impl_GetType((LPDIRECT3DBASETEXTURE8) This->stateBlock->textures[textureNo])) {
                   case WINED3DRTYPE_TEXTURE:       numcoords[textureNo] = 2; break;
                   case WINED3DRTYPE_VOLUMETEXTURE: numcoords[textureNo] = 3; break;
                   default:                         numcoords[textureNo] = 4;
                   }
               }
            } else {
                numcoords[textureNo] = 0;
            }
        }

        /* Draw using this information */
        draw_vertex(iface,
                    TRUE, x, y, z, rhw,
                    TRUE, 0.0f, 0.0f, 1.0f,
                    TRUE, (float*) &vertexShader->output.oD[0],
                    TRUE, (float*) &vertexShader->output.oD[1],
                    FALSE, ptSize,         /* FIXME: Change back when supported */
                    texcoords, numcoords);

        /* For non indexed mode, step onto next parts */
        if (idxData == NULL) {
           ++SkipnStrides;
        }

    } /* for each vertex */

    glEnd();
    checkGLcall("glEnd and previous calls");
}

#endif

inline static void drawPrimitiveDrawStrided(
    IWineD3DDevice *iface,
    BOOL useVertexShaderFunction,
    BOOL usePixelShaderFunction,
    WineDirect3DVertexStridedData *dataLocations,
    ULONG baseVIndex,
    UINT numberOfvertices,
    UINT numberOfIndicies,
    GLenum glPrimType,
    const void *idxData,
    short idxSize,
    int minIndex,
    long StartIdx) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

/* Generate some fixme's if unsupported functionality is being used */
#define BUFFER_OR_DATA(_attribute) dataLocations->u.s._attribute.lpData
    /* TODO: Either support missing functionality in fixupVertices or by creating a shader to replace the pipeline. */
    if (!useVertexShaderFunction && (BUFFER_OR_DATA(blendMatrixIndices) || BUFFER_OR_DATA(blendWeights))) {
        FIXME("Blending data is only valid with vertex shaders %p %p\n",dataLocations->u.s.blendWeights.lpData,dataLocations->u.s.blendWeights.lpData);
    }
    if (!useVertexShaderFunction && (BUFFER_OR_DATA(position2) || BUFFER_OR_DATA(normal2))) {
        FIXME("Tweening is only valid with vertex shaders\n");
    }
    if (!useVertexShaderFunction && (BUFFER_OR_DATA(tangent) || BUFFER_OR_DATA(binormal))) {
        FIXME("Tangent and binormal bump mapping is only valid with vertex shaders\n");
    }
    if (!useVertexShaderFunction && (BUFFER_OR_DATA(tessFactor) || BUFFER_OR_DATA(fog) || BUFFER_OR_DATA(depth) || BUFFER_OR_DATA(sample))) {
        FIXME("Extended attributes are only valid with vertex shaders\n");
    }
#undef BUFFER_OR_DATA

    /* Make any shaders active */
    This->shader_backend->shader_select(iface, usePixelShaderFunction, useVertexShaderFunction);

    /* Load any global constants/uniforms that may have been set by the application */
    This->shader_backend->shader_load_constants(iface, usePixelShaderFunction, useVertexShaderFunction);

    /* Draw vertex-by-vertex */
    if (This->useDrawStridedSlow)
        drawStridedSlow(iface, dataLocations, numberOfIndicies, glPrimType, idxData, idxSize, minIndex, StartIdx, baseVIndex);
    else
        drawStridedFast(iface, numberOfIndicies, glPrimType, idxData, idxSize, minIndex, StartIdx, baseVIndex);

    /* Cleanup any shaders */
    This->shader_backend->shader_cleanup(usePixelShaderFunction, useVertexShaderFunction);
}

static void check_fbo_status(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    GLenum status = GL_EXTCALL(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT));
    switch(status) {
        case GL_FRAMEBUFFER_COMPLETE_EXT: TRACE("FBO complete.\n"); break;
        default: TRACE("FBO status %#x.\n", status); break;
    }
}

static void depth_blt(IWineD3DDevice *iface, GLuint texture) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    GLint old_binding = 0;

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);

    GL_EXTCALL(glActiveTextureARB(GL_TEXTURE0_ARB));
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_binding);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_TEXTURE_2D);

    This->shader_backend->shader_select_depth_blt(iface);

    glBegin(GL_TRIANGLE_STRIP);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(-1.0f, 1.0f);
    glVertex2f(1.0f, 1.0f);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, old_binding);

    glPopAttrib();
}

static void depth_copy(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSurfaceImpl *depth_stencil = (IWineD3DSurfaceImpl *)This->depthStencilBuffer;

    /* Only copy the depth buffer if there is one. */
    if (!depth_stencil) return;

    /* TODO: Make this work for modes other than FBO */
    if (wined3d_settings.offscreen_rendering_mode != ORM_FBO) return;

    if (This->render_offscreen) {
        static GLuint tmp_texture = 0;
        GLint old_binding = 0;

        TRACE("Copying onscreen depth buffer to offscreen surface\n");

        if (!tmp_texture) {
            glGenTextures(1, &tmp_texture);
        }

        /* Note that we use depth_blt here as well, rather than glCopyTexImage2D
         * directly on the FBO texture. That's because we need to flip. */
        GL_EXTCALL(glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0));
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_binding);
        glBindTexture(GL_TEXTURE_2D, tmp_texture);
        glCopyTexImage2D(depth_stencil->glDescription.target,
                depth_stencil->glDescription.level,
                depth_stencil->glDescription.glFormatInternal,
                0,
                0,
                depth_stencil->currentDesc.Width,
                depth_stencil->currentDesc.Height,
                0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE);
        glBindTexture(GL_TEXTURE_2D, old_binding);

        GL_EXTCALL(glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, This->fbo));
        checkGLcall("glBindFramebuffer()");
        depth_blt(iface, tmp_texture);
        checkGLcall("depth_blt");
    } else {
        TRACE("Copying offscreen surface to onscreen depth buffer\n");

        GL_EXTCALL(glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0));
        checkGLcall("glBindFramebuffer()");
        depth_blt(iface, depth_stencil->glDescription.textureName);
        checkGLcall("depth_blt");
    }
}

/* Routine common to the draw primitive and draw indexed primitive routines */
void drawPrimitive(IWineD3DDevice *iface,
                   int PrimitiveType,
                   long NumPrimitives,
                   /* for Indexed: */
                   long  StartVertexIndex,
                   UINT  numberOfVertices,
                   long  StartIdx,
                   short idxSize,
                   const void *idxData,
                   int   minIndex) {

    IWineD3DDeviceImpl           *This = (IWineD3DDeviceImpl *)iface;
    BOOL                          useVertexShaderFunction = FALSE;
    BOOL                          usePixelShaderFunction = FALSE;
    IWineD3DSwapChainImpl         *swapchain;
    int                           i;
    DWORD                         dirtyState, idx;
    BYTE                          shift;

    /* Shaders can be implemented using ARB_PROGRAM, GLSL, or software -
     * here simply check whether a shader was set, or the user disabled shaders */
    if (This->vs_selected_mode != SHADER_NONE && This->stateBlock->vertexShader &&
        ((IWineD3DVertexShaderImpl *)This->stateBlock->vertexShader)->baseShader.function != NULL) 
        useVertexShaderFunction = TRUE;

    if (This->ps_selected_mode != SHADER_NONE && This->stateBlock->pixelShader &&
        ((IWineD3DPixelShaderImpl *)This->stateBlock->pixelShader)->baseShader.function) 
        usePixelShaderFunction = TRUE;

    /* Invalidate the back buffer memory so LockRect will read it the next time */
    for(i = 0; i < IWineD3DDevice_GetNumberOfSwapChains(iface); i++) {
        IWineD3DDevice_GetSwapChain(iface, i, (IWineD3DSwapChain **) &swapchain);
        if(swapchain) {
            if(swapchain->backBuffer) ((IWineD3DSurfaceImpl *) swapchain->backBuffer[0])->Flags |= SFLAG_GLDIRTY;
            IWineD3DSwapChain_Release( (IWineD3DSwapChain *) swapchain);
        }
    }

    /* Ok, we will be updating the screen from here onwards so grab the lock */
    ENTER_GL();

    /* Apply dirty states */
    for(i=0; i < This->numDirtyEntries; i++) {
        dirtyState = This->dirtyArray[i];
        idx = dirtyState >> 5;
        shift = dirtyState & 0x1f;
        This->isStateDirty[idx] &= ~(1 << shift);
        StateTable[dirtyState].apply(dirtyState, This->stateBlock);
    }
    This->numDirtyEntries = 0; /* This makes the whole list clean */


    if (TRACE_ON(d3d_draw) && wined3d_settings.offscreen_rendering_mode == ORM_FBO) {
        check_fbo_status(iface);
    }

    if (This->depth_copy_state == WINED3D_DCS_COPY) {
        depth_copy(iface);
    }
    This->depth_copy_state = WINED3D_DCS_INITIAL;

    /* Now initialize the materials state */
    init_materials(iface, (This->strided_streams.u.s.diffuse.lpData != NULL || This->strided_streams.u.s.diffuse.VBO != 0));

    {
        GLenum glPrimType;
        /* Ok, Work out which primitive is requested and how many vertexes that
           will be                                                              */
        UINT calculatedNumberOfindices = primitiveToGl(PrimitiveType, NumPrimitives, &glPrimType);
        if (numberOfVertices == 0 )
            numberOfVertices = calculatedNumberOfindices;

        drawPrimitiveDrawStrided(iface, useVertexShaderFunction, usePixelShaderFunction,
            &This->strided_streams, StartVertexIndex, numberOfVertices, calculatedNumberOfindices, glPrimType,
            idxData, idxSize, minIndex, StartIdx);
    }

    /* Finshed updating the screen, restore lock */
    LEAVE_GL();
    TRACE("Done all gl drawing\n");

    /* Diagnostics */
#ifdef SHOW_FRAME_MAKEUP
    {
        static long int primCounter = 0;
        /* NOTE: set primCounter to the value reported by drawprim 
           before you want to to write frame makeup to /tmp */
        if (primCounter >= 0) {
            WINED3DLOCKED_RECT r;
            char buffer[80];
            IWineD3DSurface_LockRect(This->renderTarget, &r, NULL, WINED3DLOCK_READONLY);
            sprintf(buffer, "/tmp/backbuffer_%d.tga", primCounter);
            TRACE("Saving screenshot %s\n", buffer);
            IWineD3DSurface_SaveSnapshot(This->renderTarget, buffer);
            IWineD3DSurface_UnlockRect(This->renderTarget);

#ifdef SHOW_TEXTURE_MAKEUP
           {
            IWineD3DSurface *pSur;
            int textureNo;
            for (textureNo = 0; textureNo < GL_LIMITS(textures); ++textureNo) {
                if (This->stateBlock->textures[textureNo] != NULL) {
                    sprintf(buffer, "/tmp/texture_%p_%d_%d.tga", This->stateBlock->textures[textureNo], primCounter, textureNo);
                    TRACE("Saving texture %s\n", buffer);
                    if (IWineD3DBaseTexture_GetType(This->stateBlock->textures[textureNo]) == WINED3DRTYPE_TEXTURE) {
                            IWineD3DTexture_GetSurfaceLevel((IWineD3DTexture *)This->stateBlock->textures[textureNo], 0, &pSur);
                            IWineD3DSurface_SaveSnapshot(pSur, buffer);
                            IWineD3DSurface_Release(pSur);
                    } else  {
                        FIXME("base Texture isn't of type texture %d\n", IWineD3DBaseTexture_GetType(This->stateBlock->textures[textureNo]));
                    }
                }
            }
           }
#endif
        }
        TRACE("drawprim #%d\n", primCounter);
        ++primCounter;
    }
#endif
}
