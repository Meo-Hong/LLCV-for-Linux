///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2018, Intel Corporation
//
// Licensed under the Apache License, Version 2.0 ( the "License" );
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Conservative Morphological Anti-Aliasing, version: 2.3
//
// Author(s):       Filip Strugar (filip.strugar@intel.com)
//
// More info:       https://github.com/GameTechDev/CMAA2
//
// Modified for LLCV: OpenGL 4.3 GLSL port of CMAA2.hlsl (commit 071c6b0857559f4e36f614362e6d2aab1b61938a) reduced to
// the configuration LLCV uses: High preset, color edge detection, single sample, direct candidate processing, FP32,
// typed float output without sRGB conversion, clamped source loads and bounded list writes. See NOTICE.txt.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define CMAA2_CS_INPUT_KERNEL_SIZE_X 16
#define CMAA2_CS_INPUT_KERNEL_SIZE_Y 16
#define CMAA2_CS_OUTPUT_KERNEL_SIZE_X (CMAA2_CS_INPUT_KERNEL_SIZE_X - 2)
#define CMAA2_CS_OUTPUT_KERNEL_SIZE_Y (CMAA2_CS_INPUT_KERNEL_SIZE_Y - 2)
#define CMAA2_PROCESS_CANDIDATES_NUM_THREADS 128
#define CMAA2_DEFERRED_APPLY_NUM_THREADS 32

const uint c_maxLineLength = 86u;
const float g_CMAA2_EdgeThreshold = 0.07;
#ifndef CMAA2_EXTRA_SHARPNESS
#define CMAA2_EXTRA_SHARPNESS 0
#endif
#if CMAA2_EXTRA_SHARPNESS
const float g_CMAA2_LocalContrastAdaptationAmount = 0.15;
const float g_CMAA2_SimpleShapeBlurinessAmount = 0.07;
const float c_dampeningEffect = 0.11;
#else
const float g_CMAA2_LocalContrastAdaptationAmount = 0.10;
const float g_CMAA2_SimpleShapeBlurinessAmount = 0.10;
const float c_dampeningEffect = 0.15;
#endif
const float c_symmetryCorrectionOffset = 0.22;

layout(binding = 0) uniform sampler2D g_inoutColorReadonly;
layout(binding = 0, rgba16f) uniform writeonly image2D g_inoutColorWriteonly;
layout(binding = 1, r8ui) uniform uimage2D g_workingEdges;
layout(binding = 5, r32ui) uniform uimage2D g_workingDeferredBlendItemListHeads;

layout(std430, binding = 2) buffer ShapeCandidates { uint g_workingShapeCandidates[]; };
layout(std430, binding = 3) buffer DeferredBlendLocationList { uint g_workingDeferredBlendLocationList[]; };
layout(std430, binding = 4) buffer DeferredBlendItemList { uvec2 g_workingDeferredBlendItemList[]; };
layout(std430, binding = 6) buffer ControlBuffer { uint g_workingControlBuffer[]; };
layout(std430, binding = 7) buffer ExecuteIndirectBuffer { uint g_workingExecuteIndirectBuffer[]; };

float Saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

float HlslFmod(float x, float y) {
    return x - y * trunc(x / y);
}

uint PackEdges(vec4 edges) {
    return uint(dot(edges, vec4(1.0, 2.0, 4.0, 8.0)));
}

vec4 UnpackEdgesFlt(uint value) {
    return vec4((value & 0x01u) != 0u, (value & 0x02u) != 0u, (value & 0x04u) != 0u, (value & 0x08u) != 0u);
}

vec3 LoadSourceColor(ivec2 pixelPos, ivec2 offset) {
    ivec2 size = textureSize(g_inoutColorReadonly, 0);
    ivec2 sourcePos = clamp(pixelPos + offset, ivec2(0, 0), size - ivec2(1, 1));
    return texelFetch(g_inoutColorReadonly, sourcePos, 0).rgb;
}

uint F32ToF16(float value) {
    return packHalf2x16(vec2(value, 0.0)) & 0xFFFFu;
}

float F16ToF32(uint value) {
    return unpackHalf2x16(value & 0xFFFFu).x;
}

uint Pack_R11G11B10_E4_FLOAT(vec3 rgb) {
    rgb = clamp(rgb, vec3(0.0), vec3(uintBitsToFloat(0x3FFFFFFFu)));
    uint r = ((F32ToF16(rgb.r) + 4u) >> 3) & 0x000007FFu;
    uint g = ((F32ToF16(rgb.g) + 4u) << 8) & 0x003FF800u;
    uint b = ((F32ToF16(rgb.b) + 8u) << 18) & 0xFFC00000u;
    return r | g | b;
}

vec3 Unpack_R11G11B10_E4_FLOAT(uint rgb) {
    float r = F16ToF32((rgb << 3) & 0x3FF8u);
    float g = F16ToF32((rgb >> 8) & 0x3FF8u);
    float b = F16ToF32((rgb >> 18) & 0x3FF0u);
    return vec3(r, g, b);
}

vec3 InternalUnpackColor(uint packedColor) {
    return Unpack_R11G11B10_E4_FLOAT(packedColor);
}

uint InternalPackColor(vec3 color) {
    return Pack_R11G11B10_E4_FLOAT(color);
}

void StoreColorSample(uvec2 pixelPos, vec3 color, bool isComplexShape) {
    uint counterIndex = atomicAdd(g_workingControlBuffer[12], 1u);
    if (counterIndex >= min(uint(g_workingDeferredBlendItemList.length()), 1u << 26)) return;

    uvec2 quadPos = pixelPos / uvec2(2u, 2u);
    uint offsetXY = (pixelPos.y % 2u) * 2u + (pixelPos.x % 2u);
    uint header = (offsetXY << 30) | (uint(isComplexShape) << 26);

    uint counterIndexWithHeader = counterIndex | header;

    uint originalIndex = imageAtomicExchange(g_workingDeferredBlendItemListHeads, ivec2(quadPos), counterIndexWithHeader);
    g_workingDeferredBlendItemList[counterIndex] = uvec2(originalIndex, InternalPackColor(color));

    if (originalIndex == 0xFFFFFFFFu) {
        uint edgeListCounter = atomicAdd(g_workingControlBuffer[8], 1u);
        if (edgeListCounter < uint(g_workingDeferredBlendLocationList.length())) {
            g_workingDeferredBlendLocationList[edgeListCounter] = (quadPos.x << 16) | quadPos.y;
        }
    }
}

void FinalUAVStore(uvec2 pixelPos, vec3 color) {
    imageStore(g_inoutColorWriteonly, ivec2(pixelPos), vec4(color, 0.0));
}

float EdgeDetectColorCalcDiff(vec3 colorA, vec3 colorB) {
    const vec3 LumWeights = vec3(0.299, 0.587, 0.114);
    vec3 diff = abs(colorA - colorB);
    return dot(diff, LumWeights);
}

vec3 ProcessColorForEdgeDetect(vec3 color) {
    return sqrt(color);
}

vec2 ComputeEdge(int x, int y, vec3 pixelColors[8]) {
    vec2 temp;
    temp.x = EdgeDetectColorCalcDiff(pixelColors[x + y * 3], pixelColors[x + 1 + y * 3]);
    temp.y = EdgeDetectColorCalcDiff(pixelColors[x + y * 3], pixelColors[x + (y + 1) * 3]);
    return temp;
}

float ComputeLocalContrastV(int x, int y, vec2 neighbourhood[4][4]) {
    return max(max(neighbourhood[x + 1][y + 0].y, neighbourhood[x + 1][y + 1].y),
               max(neighbourhood[x + 2][y + 0].y, neighbourhood[x + 2][y + 1].y)) *
           g_CMAA2_LocalContrastAdaptationAmount;
}

float ComputeLocalContrastH(int x, int y, vec2 neighbourhood[4][4]) {
    return max(max(neighbourhood[x + 0][y + 1].x, neighbourhood[x + 1][y + 1].x),
               max(neighbourhood[x + 0][y + 2].x, neighbourhood[x + 1][y + 2].x)) *
           g_CMAA2_LocalContrastAdaptationAmount;
}

vec4 ComputeSimpleShapeBlendValues(vec4 edges, vec4 edgesLeft, vec4 edgesRight, vec4 edgesTop, vec4 edgesBottom,
                                   bool dontTestShapeValidity) {
    float fromRight = edges.r;
    float fromBelow = edges.g;
    float fromLeft = edges.b;
    float fromAbove = edges.a;

    float blurCoeff = g_CMAA2_SimpleShapeBlurinessAmount;

    float numberOfEdges = dot(edges, vec4(1.0, 1.0, 1.0, 1.0));

    float numberOfEdgesAllAround =
        dot(edgesLeft.bga + edgesRight.rga + edgesTop.rba + edgesBottom.rgb, vec3(1.0, 1.0, 1.0));

    if (!dontTestShapeValidity) {
        if (numberOfEdges == 1.0) blurCoeff = 0.0;
        if (numberOfEdges == 2.0) blurCoeff *= ((1.0 - fromBelow * fromAbove) * (1.0 - fromRight * fromLeft));
    }

    if (numberOfEdges == 2.0) {
        blurCoeff *= 0.75;

        float k = 0.9;
        fromRight += k * (edges.g * edgesTop.r * (1.0 - edgesLeft.g) + edges.a * edgesBottom.r * (1.0 - edgesLeft.a));
        fromBelow += k * (edges.b * edgesRight.g * (1.0 - edgesTop.b) + edges.r * edgesLeft.g * (1.0 - edgesTop.r));
        fromLeft += k * (edges.a * edgesBottom.b * (1.0 - edgesRight.a) + edges.g * edgesTop.b * (1.0 - edgesRight.g));
        fromAbove += k * (edges.r * edgesLeft.a * (1.0 - edgesBottom.r) + edges.b * edgesRight.a * (1.0 - edgesBottom.b));
    }

#if CMAA2_EXTRA_SHARPNESS
    blurCoeff *= Saturate(1.15 - numberOfEdgesAllAround / 8.0);
#else
    blurCoeff *= Saturate(1.30 - numberOfEdgesAllAround / 10.0);
#endif

    return vec4(fromLeft, fromAbove, fromRight, fromBelow) * blurCoeff;
}

uint LoadEdge(ivec2 pixelPos, ivec2 offset) {
    ivec2 position = pixelPos + offset;
    if (position.x < 0 || position.y < 0) return 0u;
    uint a = uint(position.x) % 2u;
    uint edge = imageLoad(g_workingEdges, ivec2(position.x / 2, position.y)).x;
    return (edge >> (a * 4u)) & 0xFu;
}

#ifdef CMAA2_PASS_EDGES

shared vec4 g_groupShared2x2FracEdgesH[CMAA2_CS_INPUT_KERNEL_SIZE_X * CMAA2_CS_INPUT_KERNEL_SIZE_Y];
shared vec4 g_groupShared2x2FracEdgesV[CMAA2_CS_INPUT_KERNEL_SIZE_X * CMAA2_CS_INPUT_KERNEL_SIZE_Y];

void GroupsharedLoadQuadHV(uint addr, out vec2 e00, out vec2 e10, out vec2 e01, out vec2 e11) {
    vec4 valH = g_groupShared2x2FracEdgesH[addr];
    vec4 valV = g_groupShared2x2FracEdgesV[addr];
    e00 = vec2(valV.x, valH.x);
    e10 = vec2(valV.y, valH.y);
    e01 = vec2(valV.z, valH.z);
    e11 = vec2(valV.w, valH.w);
}

layout(local_size_x = CMAA2_CS_INPUT_KERNEL_SIZE_X, local_size_y = CMAA2_CS_INPUT_KERNEL_SIZE_Y, local_size_z = 1) in;
void main() {
    uvec2 groupID = gl_WorkGroupID.xy;
    uvec2 groupThreadID = gl_LocalInvocationID.xy;

    ivec2 pixelPos = ivec2(groupID) * ivec2(CMAA2_CS_OUTPUT_KERNEL_SIZE_X, CMAA2_CS_OUTPUT_KERNEL_SIZE_Y) +
                     ivec2(groupThreadID) - ivec2(1, 1);
    pixelPos *= ivec2(2, 2);

    const ivec2 qeOffsets[4] = ivec2[4](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
    const uint rowStride2x2 = uint(CMAA2_CS_INPUT_KERNEL_SIZE_X);
    uint centerAddr2x2 = groupThreadID.x + groupThreadID.y * rowStride2x2;
    bool inOutputKernel = !(groupThreadID.x == uint(CMAA2_CS_INPUT_KERNEL_SIZE_X - 1) || groupThreadID.x == 0u ||
                            groupThreadID.y == uint(CMAA2_CS_INPUT_KERNEL_SIZE_Y - 1) || groupThreadID.y == 0u);

    vec2 qe0, qe1, qe2, qe3;
    uvec4 outEdges = uvec4(0u, 0u, 0u, 0u);

    vec3 pixelColors[8];
    for (int i = 0; i < 8; i++) pixelColors[i] = LoadSourceColor(pixelPos, ivec2(i % 3, i / 3));
    for (int i = 0; i < 8; i++) pixelColors[i] = ProcessColorForEdgeDetect(pixelColors[i]);

    qe0 = ComputeEdge(0, 0, pixelColors);
    qe1 = ComputeEdge(1, 0, pixelColors);
    qe2 = ComputeEdge(0, 1, pixelColors);
    qe3 = ComputeEdge(1, 1, pixelColors);

    g_groupShared2x2FracEdgesV[centerAddr2x2] = vec4(qe0.x, qe1.x, qe2.x, qe3.x);
    g_groupShared2x2FracEdgesH[centerAddr2x2] = vec4(qe0.y, qe1.y, qe2.y, qe3.y);

    memoryBarrierShared();
    barrier();

    if (inOutputKernel) {
        vec2 topRow = g_groupShared2x2FracEdgesH[centerAddr2x2 - rowStride2x2].zw;
        vec2 leftColumn = g_groupShared2x2FracEdgesV[centerAddr2x2 - 1u].yw;

        bool someNonZeroEdges =
            any(notEqual(vec4(qe0, qe1) + vec4(qe2, qe3) + vec4(topRow[0], topRow[1], leftColumn[0], leftColumn[1]),
                         vec4(0.0)));

        if (someNonZeroEdges) {
            imageStore(g_workingDeferredBlendItemListHeads, pixelPos / 2, uvec4(0xFFFFFFFFu));

            vec4 ce[4];
            vec2 dummyd0, dummyd1, dummyd2;
            vec2 neighbourhood[4][4];

            GroupsharedLoadQuadHV(centerAddr2x2 - rowStride2x2 - 1u, dummyd0, dummyd1, dummyd2, neighbourhood[0][0]);
            GroupsharedLoadQuadHV(centerAddr2x2 - rowStride2x2, dummyd0, dummyd1, neighbourhood[1][0], neighbourhood[2][0]);
            GroupsharedLoadQuadHV(centerAddr2x2 - rowStride2x2 + 1u, dummyd0, dummyd1, neighbourhood[3][0], dummyd2);
            GroupsharedLoadQuadHV(centerAddr2x2 - 1u, dummyd0, neighbourhood[0][1], dummyd1, neighbourhood[0][2]);
            GroupsharedLoadQuadHV(centerAddr2x2 + 1u, neighbourhood[3][1], dummyd0, neighbourhood[3][2], dummyd1);
            GroupsharedLoadQuadHV(centerAddr2x2 - 1u + rowStride2x2, dummyd0, neighbourhood[0][3], dummyd1, dummyd2);
            GroupsharedLoadQuadHV(centerAddr2x2 + rowStride2x2, neighbourhood[1][3], neighbourhood[2][3], dummyd0, dummyd1);
            neighbourhood[1][0].y = topRow[0];
            neighbourhood[2][0].y = topRow[1];
            neighbourhood[0][1].x = leftColumn[0];
            neighbourhood[0][2].x = leftColumn[1];
            neighbourhood[1][1] = qe0;
            neighbourhood[2][1] = qe1;
            neighbourhood[1][2] = qe2;
            neighbourhood[2][2] = qe3;

            topRow[0] = float((topRow[0] - ComputeLocalContrastH(0, -1, neighbourhood)) > g_CMAA2_EdgeThreshold);
            topRow[1] = float((topRow[1] - ComputeLocalContrastH(1, -1, neighbourhood)) > g_CMAA2_EdgeThreshold);
            leftColumn[0] = float((leftColumn[0] - ComputeLocalContrastV(-1, 0, neighbourhood)) > g_CMAA2_EdgeThreshold);
            leftColumn[1] = float((leftColumn[1] - ComputeLocalContrastV(-1, 1, neighbourhood)) > g_CMAA2_EdgeThreshold);

            ce[0].x = float((qe0.x - ComputeLocalContrastV(0, 0, neighbourhood)) > g_CMAA2_EdgeThreshold);
            ce[0].y = float((qe0.y - ComputeLocalContrastH(0, 0, neighbourhood)) > g_CMAA2_EdgeThreshold);
            ce[1].x = float((qe1.x - ComputeLocalContrastV(1, 0, neighbourhood)) > g_CMAA2_EdgeThreshold);
            ce[1].y = float((qe1.y - ComputeLocalContrastH(1, 0, neighbourhood)) > g_CMAA2_EdgeThreshold);
            ce[2].x = float((qe2.x - ComputeLocalContrastV(0, 1, neighbourhood)) > g_CMAA2_EdgeThreshold);
            ce[2].y = float((qe2.y - ComputeLocalContrastH(0, 1, neighbourhood)) > g_CMAA2_EdgeThreshold);
            ce[3].x = float((qe3.x - ComputeLocalContrastV(1, 1, neighbourhood)) > g_CMAA2_EdgeThreshold);
            ce[3].y = float((qe3.y - ComputeLocalContrastH(1, 1, neighbourhood)) > g_CMAA2_EdgeThreshold);

            ce[0].z = leftColumn[0];
            ce[1].z = ce[0].x;
            ce[2].z = leftColumn[1];
            ce[3].z = ce[2].x;

            ce[0].w = topRow[0];
            ce[1].w = topRow[1];
            ce[2].w = ce[0].y;
            ce[3].w = ce[1].y;

            for (int i = 0; i < 4; i++) {
                ivec2 localPixelPos = pixelPos + qeOffsets[i];
                vec4 edges = ce[i];

                bool isCandidate = (edges.x * edges.y + edges.y * edges.z + edges.z * edges.w + edges.w * edges.x) != 0.0;
                if (isCandidate) {
                    uint counterIndex = atomicAdd(g_workingControlBuffer[4], 1u);
                    if (counterIndex < uint(g_workingShapeCandidates.length())) {
                        g_workingShapeCandidates[counterIndex] = (uint(localPixelPos.x) << 18) | uint(localPixelPos.y);
                    }
                }

                outEdges[i] = PackEdges(edges);
            }
        }
    }

    if (inOutputKernel) {
        imageStore(g_workingEdges, ivec2(pixelPos.x / 2, pixelPos.y + 0), uvec4((outEdges[1] << 4) | outEdges[0]));
        imageStore(g_workingEdges, ivec2(pixelPos.x / 2, pixelPos.y + 1), uvec4((outEdges[3] << 4) | outEdges[2]));
    }
}

#endif

#ifdef CMAA2_PASS_DISPATCH_ARGS

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main() {
    if (gl_WorkGroupID.x == 1u) {
        uint shapeCandidateCount = g_workingControlBuffer[4];
        uint overflow = shapeCandidateCount > uint(g_workingShapeCandidates.length()) ? 1u : 0u;
        g_workingControlBuffer[0] = overflow;
        if (overflow != 0u) shapeCandidateCount = 0u;

        g_workingExecuteIndirectBuffer[0] =
            (shapeCandidateCount + uint(CMAA2_PROCESS_CANDIDATES_NUM_THREADS) - 1u) / uint(CMAA2_PROCESS_CANDIDATES_NUM_THREADS);
        g_workingExecuteIndirectBuffer[1] = 1u;
        g_workingExecuteIndirectBuffer[2] = 1u;

        g_workingControlBuffer[3] = shapeCandidateCount;
    } else if (gl_WorkGroupID.y == 1u) {
        uint blendLocationCount = g_workingControlBuffer[8];
        uint overflow = g_workingControlBuffer[0];
        if (g_workingControlBuffer[12] > min(uint(g_workingDeferredBlendItemList.length()), 1u << 26)) overflow |= 2u;
        if (blendLocationCount > uint(g_workingDeferredBlendLocationList.length())) overflow |= 4u;
        g_workingControlBuffer[0] = overflow;
        if (overflow != 0u) blendLocationCount = 0u;

        g_workingExecuteIndirectBuffer[0] = 1u;
        g_workingExecuteIndirectBuffer[1] =
            (blendLocationCount + uint(CMAA2_DEFERRED_APPLY_NUM_THREADS) - 1u) / uint(CMAA2_DEFERRED_APPLY_NUM_THREADS);
        g_workingExecuteIndirectBuffer[2] = 1u;

        g_workingControlBuffer[3] = blendLocationCount;

        g_workingControlBuffer[4] = 0u;
        g_workingControlBuffer[8] = 0u;
        g_workingControlBuffer[12] = 0u;
    }
}

#endif

#ifdef CMAA2_PASS_PROCESS_CANDIDATES

void FindZLineLengths(out float lineLengthLeft, out float lineLengthRight, ivec2 screenPos, bool horizontal,
                      bool invertedZShape, vec2 stepRight) {
    uint maskTraceLeft, maskTraceRight;
    if (horizontal) {
        maskTraceLeft = 0x08u;
        maskTraceRight = 0x02u;
    } else {
        maskTraceLeft = 0x04u;
        maskTraceRight = 0x01u;
    }
    if (invertedZShape) {
        uint temp = maskTraceLeft;
        maskTraceLeft = maskTraceRight;
        maskTraceRight = temp;
    }
    uint maskLeft = maskTraceLeft;
    uint bitsContinueLeft = maskTraceLeft;
    uint maskRight = maskTraceRight;
    uint bitsContinueRight = maskTraceRight;

    bool continueLeft = true;
    bool continueRight = true;
    lineLengthLeft = 1.0;
    lineLengthRight = 1.0;
    for (;;) {
        uint edgeLeft = LoadEdge(ivec2(vec2(screenPos) - stepRight * lineLengthLeft), ivec2(0, 0));
        uint edgeRight = LoadEdge(ivec2(vec2(screenPos) + stepRight * (lineLengthRight + 1.0)), ivec2(0, 0));

        continueLeft = continueLeft && ((edgeLeft & maskLeft) == bitsContinueLeft);
        continueRight = continueRight && ((edgeRight & maskRight) == bitsContinueRight);

        lineLengthLeft += float(continueLeft);
        lineLengthRight += float(continueRight);

        float maxLR = max(lineLengthRight, lineLengthLeft);

        if (!continueLeft && !continueRight) maxLR = float(c_maxLineLength);

#if CMAA2_EXTRA_SHARPNESS
        if (maxLR >= min(float(c_maxLineLength), (1.20 * min(lineLengthRight, lineLengthLeft) - 0.20))) break;
#else
        if (maxLR >= min(float(c_maxLineLength), (1.25 * min(lineLengthRight, lineLengthLeft) - 0.25))) break;
#endif
    }
}

void BlendZs(ivec2 screenPos, bool horizontal, bool invertedZShape, float shapeQualityScore, float lineLengthLeft,
             float lineLengthRight, vec2 stepRight) {
    vec2 blendDir = horizontal ? vec2(0.0, -1.0) : vec2(-1.0, 0.0);

    if (invertedZShape) blendDir = -blendDir;

    float leftOdd = c_symmetryCorrectionOffset * HlslFmod(lineLengthLeft, 2.0);
    float rightOdd = c_symmetryCorrectionOffset * HlslFmod(lineLengthRight, 2.0);

    float dampenEffect = Saturate((lineLengthLeft + lineLengthRight - shapeQualityScore) * c_dampeningEffect);

    float loopFrom = -floor((lineLengthLeft + 1.0) / 2.0) + 1.0;
    float loopTo = floor((lineLengthRight + 1.0) / 2.0);

    float totalLength = (loopTo - loopFrom) + 1.0 - leftOdd - rightOdd;
    float lerpStep = 1.0 / totalLength;

    float lerpFromK = (0.5 - leftOdd - loopFrom) * lerpStep;

    for (float i = loopFrom; i <= loopTo; i++) {
        float secondPart = float(i > 0.0);
        float srcOffset = 1.0 - secondPart * 2.0;

        float lerpK = (lerpStep * i + lerpFromK) * srcOffset + secondPart;
        lerpK *= dampenEffect;

        vec2 pixelPos = vec2(screenPos) + stepRight * i;

        vec3 colorCenter = LoadSourceColor(ivec2(pixelPos), ivec2(0, 0));
        vec3 colorFrom = LoadSourceColor(ivec2(pixelPos + blendDir * srcOffset), ivec2(0, 0));

        vec3 outputColor = mix(colorCenter, colorFrom, lerpK);

        StoreColorSample(uvec2(max(pixelPos, vec2(0.0))), outputColor, true);
    }
}

void DetectZsHorizontal(vec4 edges, vec4 edgesM1P0, vec4 edgesP1P0, vec4 edgesP2P0, out float invertedZScore,
                        out float normalZScore) {
    invertedZScore = edges.r * edges.g * edgesP1P0.a;
    invertedZScore *= 2.0 + ((edgesM1P0.g + edgesP2P0.a)) - (edges.a + edgesP1P0.g) -
                      0.7 * (edgesP2P0.g + edgesM1P0.a + edges.b + edgesP1P0.r);

    normalZScore = edges.r * edges.a * edgesP1P0.g;
    normalZScore *= 2.0 + ((edgesM1P0.a + edgesP2P0.g)) - (edges.g + edgesP1P0.a) -
                    0.7 * (edgesP2P0.a + edgesM1P0.g + edges.b + edgesP1P0.r);
}

layout(local_size_x = CMAA2_PROCESS_CANDIDATES_NUM_THREADS, local_size_y = 1, local_size_z = 1) in;
void main() {
    uint numCandidates = g_workingControlBuffer[3];
    if (gl_GlobalInvocationID.x >= numCandidates) return;

    uint pixelID = g_workingShapeCandidates[gl_GlobalInvocationID.x];
    ivec2 pixelPos = ivec2(int(pixelID >> 18), int(pixelID & 0x3FFFu));

    uint edgesCenterPacked = LoadEdge(pixelPos, ivec2(0, 0));
    vec4 edges = UnpackEdgesFlt(edgesCenterPacked);
    vec4 edgesLeft = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(-1, 0)));
    vec4 edgesRight = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(1, 0)));
    vec4 edgesBottom = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(0, 1)));
    vec4 edgesTop = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(0, -1)));

    {
        vec4 blendVal = ComputeSimpleShapeBlendValues(edges, edgesLeft, edgesRight, edgesTop, edgesBottom, true);

        float fourWeightSum = dot(blendVal, vec4(1.0, 1.0, 1.0, 1.0));
        float centerWeight = 1.0 - fourWeightSum;

        vec3 outColor = LoadSourceColor(pixelPos, ivec2(0, 0)) * centerWeight;
        if (blendVal.x > 0.0) outColor += blendVal.x * LoadSourceColor(pixelPos, ivec2(-1, 0));
        if (blendVal.y > 0.0) outColor += blendVal.y * LoadSourceColor(pixelPos, ivec2(0, -1));
        if (blendVal.z > 0.0) outColor += blendVal.z * LoadSourceColor(pixelPos, ivec2(1, 0));
        if (blendVal.w > 0.0) outColor += blendVal.w * LoadSourceColor(pixelPos, ivec2(0, 1));

        StoreColorSample(uvec2(pixelPos), outColor, false);
    }

    {
        float invertedZScore;
        float normalZScore;
        float maxScore;
        bool horizontal = true;
        bool invertedZ = false;

        {
            vec4 edgesM1P0 = edgesLeft;
            vec4 edgesP1P0 = edgesRight;
            vec4 edgesP2P0 = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(2, 0)));

            DetectZsHorizontal(edges, edgesM1P0, edgesP1P0, edgesP2P0, invertedZScore, normalZScore);
            maxScore = max(invertedZScore, normalZScore);

            if (maxScore > 0.0) invertedZ = invertedZScore > normalZScore;
        }

        {
            vec4 edgesM1P0 = edgesBottom;
            vec4 edgesP1P0 = edgesTop;
            vec4 edgesP2P0 = UnpackEdgesFlt(LoadEdge(pixelPos, ivec2(0, -2)));

            DetectZsHorizontal(edges.argb, edgesM1P0.argb, edgesP1P0.argb, edgesP2P0.argb, invertedZScore, normalZScore);
            float vertScore = max(invertedZScore, normalZScore);

            if (vertScore > maxScore) {
                maxScore = vertScore;
                horizontal = false;
                invertedZ = invertedZScore > normalZScore;
            }
        }

        if (maxScore > 0.0) {
#if CMAA2_EXTRA_SHARPNESS
            float shapeQualityScore = roundEven(clamp(4.0 - maxScore, 0.0, 3.0));
#else
            float shapeQualityScore = floor(clamp(4.0 - maxScore, 0.0, 3.0));
#endif

            vec2 stepRight = horizontal ? vec2(1.0, 0.0) : vec2(0.0, -1.0);
            float lineLengthLeft, lineLengthRight;
            FindZLineLengths(lineLengthLeft, lineLengthRight, pixelPos, horizontal, invertedZ, stepRight);

            lineLengthLeft -= shapeQualityScore;
            lineLengthRight -= shapeQualityScore;

            if ((lineLengthLeft + lineLengthRight) >= 5.0) {
                BlendZs(pixelPos, horizontal, invertedZ, shapeQualityScore, lineLengthLeft, lineLengthRight, stepRight);
            }
        }
    }
}

#endif

#ifdef CMAA2_PASS_DEFERRED_APPLY

layout(local_size_x = 4, local_size_y = CMAA2_DEFERRED_APPLY_NUM_THREADS, local_size_z = 1) in;
void main() {
    uint numCandidates = g_workingControlBuffer[3];
    uint currentCandidate = gl_GlobalInvocationID.y;
    uint currentQuadOffsetXY = gl_LocalInvocationID.x;

    if (currentCandidate >= numCandidates) return;

    uint pixelID = g_workingDeferredBlendLocationList[currentCandidate];
    uvec2 quadPos = uvec2(pixelID >> 16, pixelID & 0xFFFFu);
    const uvec2 qeOffsets[4] = uvec2[4](uvec2(0u, 0u), uvec2(1u, 0u), uvec2(0u, 1u), uvec2(1u, 1u));
    uvec2 pixelPos = quadPos * 2u + qeOffsets[currentQuadOffsetXY];

    uint counterIndexWithHeader = imageLoad(g_workingDeferredBlendItemListHeads, ivec2(quadPos)).x;

    vec4 outColors = vec4(0.0, 0.0, 0.0, 0.0);

    const uint maxLoops = 32u;
    uint itemCount = uint(g_workingDeferredBlendItemList.length());
    for (uint i = 0u; (counterIndexWithHeader != 0xFFFFFFFFu) && (i < maxLoops); i++) {
        uint offsetXY = (counterIndexWithHeader >> 30) & 0x03u;
        bool isComplexShape = ((counterIndexWithHeader >> 26) & 0x01u) != 0u;
        uint itemIndex = counterIndexWithHeader & ((1u << 26) - 1u);
        if (itemIndex >= itemCount) return;

        uvec2 val = g_workingDeferredBlendItemList[itemIndex];

        counterIndexWithHeader = val.x;

        if (offsetXY == currentQuadOffsetXY) {
            vec3 color = InternalUnpackColor(val.y);
            float weight = 0.8 + 1.0 * float(isComplexShape);
            outColors += vec4(color * weight, weight);
        }
    }

    if (outColors.a == 0.0) return;
    if (counterIndexWithHeader != 0xFFFFFFFFu) return;

    vec4 outColor = outColors;
    outColor.rgb /= outColor.a;
    FinalUAVStore(pixelPos, outColor.rgb);
}

#endif
