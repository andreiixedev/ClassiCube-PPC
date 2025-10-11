#include <math.h>
#include <kos.h>
#include <dc/pvr.h>
#include "gldc.h"

#define PREFETCH(addr) __builtin_prefetch((addr))
static volatile uint32_t* sq;
#define GL_FORCE_INLINE __attribute__((always_inline)) inline

// calculates 1/sqrt(x)
static GL_FORCE_INLINE float sh4_fsrra(float x) {
  asm volatile ("fsrra %[value]\n"
  : [value] "+f" (x) // outputs (r/w to FPU register)
  : // no inputs
  : // no clobbers
  );
  return x;
}

static GL_FORCE_INLINE float _glFastInvert(float x) {
    return sh4_fsrra(x * x);
}

static GL_FORCE_INLINE void PushVertex(Vertex* v) {
    volatile Vertex* dst = (Vertex*)(sq);
    float f = _glFastInvert(v->w);
    // Convert to NDC (viewport already applied)
    float x = v->x * f;
    float y = v->y * f;

    dst->flags = v->flags;
    dst->x = x;
    dst->y = y;
    dst->z = f;
    dst->u = v->u;
    dst->v = v->v;
    dst->bgra = v->bgra;
    __asm__("pref @%0" : : "r"(dst));
    dst++;
}

static inline void PushCommand(Vertex* v)  {
    uint32_t* s = (uint32_t*)v;
    sq[0] = *(s++);
    sq[1] = *(s++);
    sq[2] = *(s++);
    sq[3] = *(s++);
    sq[4] = *(s++);
    sq[5] = *(s++);
    sq[6] = *(s++);
    sq[7] = *(s++);
    __asm__("pref @%0" : : "r"(sq));
    sq += 8;
}

extern void ClipEdge(const Vertex* const v1, const Vertex* const v2, Vertex* vout);

#define V0_VIS (1 << 0)
#define V1_VIS (1 << 1)
#define V2_VIS (1 << 2)
#define V3_VIS (1 << 3)


// https://casual-effects.com/research/McGuire2011Clipping/clip.glsl
static void SubmitClipped(Vertex* v0, Vertex* v1, Vertex* v2, Vertex* v3, uint8_t visible_mask) {
    Vertex __attribute__((aligned(32))) scratch[4];
    Vertex* a = &scratch[0];
    Vertex* b = &scratch[1];

    switch(visible_mask) {
    case V0_VIS:
    {
        //          v0
        //         / |
        //       /   |
        // .....A....B...
        //    /      |
        //  v3--v2---v1
        ClipEdge(v3, v0, a);
        a->flags = PVR_CMD_VERTEX_EOL;
        ClipEdge(v0, v1, b);
        b->flags = PVR_CMD_VERTEX;

        PushVertex(v0);
        PushVertex(b);
        PushVertex(a);
    }
    break;
    case V1_VIS:
    {
        //          v1
        //         / |
        //       /   |
        // ....A.....B...
        //    /      |
        //  v0--v3---v2
        ClipEdge(v0, v1, a);
        a->flags = PVR_CMD_VERTEX;
        ClipEdge(v1, v2, b);
        b->flags = PVR_CMD_VERTEX_EOL;

        PushVertex(a);
        PushVertex(v1);
        PushVertex(b);
    } break;
    case V2_VIS:
    {
        //          v2
        //         / |
        //       /   |
        // ....A.....B...
        //    /      |
        //  v1--v0---v3

        ClipEdge(v1, v2, a);
        a->flags = PVR_CMD_VERTEX;
        ClipEdge(v2, v3, b);
        b->flags = PVR_CMD_VERTEX_EOL;

        PushVertex(a);
        PushVertex(v2);
        PushVertex(b);
    } break;
    case V3_VIS:
    {
        //          v3
        //         / |
        //       /   |
        // ....A.....B...
        //    /      |
        //  v2--v1---v0
        ClipEdge(v2, v3, a);
        a->flags = PVR_CMD_VERTEX;
        ClipEdge(v3, v0, b);
        b->flags = PVR_CMD_VERTEX;

        PushVertex(b);
        PushVertex(a);
        PushVertex(v3);
    }
    break;
    case V0_VIS | V1_VIS:
    {
        //    v0-----------v1
        //      \           |
        //   ....B..........A...
        //         \        |
        //          v3-----v2
        ClipEdge(v1, v2, a);
        a->flags = PVR_CMD_VERTEX;
        ClipEdge(v3, v0, b);
        b->flags = PVR_CMD_VERTEX_EOL;

        PushVertex(v1);
        PushVertex(a);
        PushVertex(v0);
        PushVertex(b);
    } break;
    // case V0_VIS | V2_VIS: degenerate case that should never happen
    case V0_VIS | V3_VIS:
    {
        //    v3-----------v0
        //      \           |
        //   ....B..........A...
        //         \        |
        //          v2-----v1
        ClipEdge(v0, v1, a);
        a->flags = PVR_CMD_VERTEX;
        ClipEdge(v2, v3, b);
        b->flags = PVR_CMD_VERTEX;

        PushVertex(a);
        PushVertex(b);
        PushVertex(v0);
        PushVertex(v3);
    } break;
    case V1_VIS | V2_VIS:
    {
        //    v1-----------v2
        //      \           |
        //   ....B..........A...
        //         \        |
        //          v0-----v3
        ClipEdge(v2, v3, a);
        a->flags = PVR_CMD_VERTEX_EOL;
        ClipEdge(v0, v1, b);
        b->flags = PVR_CMD_VERTEX;

        PushVertex(v1);
        PushVertex(v2);
        PushVertex(b);
        PushVertex(a);
    } break;
    // case V1_VIS | V3_VIS: degenerate case that should never happen
    case V2_VIS | V3_VIS:
    {
        //    v2-----------v3
        //      \           |
        //   ....B..........A...
        //         \        |
        //          v1-----v0
        ClipEdge(v3, v0, a);
        a->flags = PVR_CMD_VERTEX;
        ClipEdge(v1, v2, b);
        b->flags = PVR_CMD_VERTEX;

        PushVertex(b);
        PushVertex(v2);
        PushVertex(a);
        PushVertex(v3);
    } break;
    case V0_VIS | V1_VIS | V2_VIS:
    {
        //        --v1--
        //    v0--      --v2
        //      \        |
        //   .....B.....A...
        //          \   |
        //            v3
        // v1,v2,v0  v2,v0,A  v0,A,B
        ClipEdge(v2, v3, a);
        a->flags = PVR_CMD_VERTEX;
        ClipEdge(v3, v0, b);
        b->flags = PVR_CMD_VERTEX_EOL;

        PushVertex(v1);
        PushVertex(v2);
        PushVertex(v0);
        PushVertex(a);
        PushVertex(b);
    } break;
    case V0_VIS | V1_VIS | V3_VIS:
    {
        //        --v0--
        //    v3--      --v1
        //      \        |
        //   .....B.....A...
        //          \   |
        //            v2
        // v0,v1,v3  v1,v3,A  v3,A,B
        ClipEdge(v1, v2, a);
        a->flags  = PVR_CMD_VERTEX;
        ClipEdge(v2, v3, b);
        b->flags  = PVR_CMD_VERTEX_EOL;
        v3->flags = PVR_CMD_VERTEX;

        PushVertex(v0);
        PushVertex(v1);
        PushVertex(v3);
        PushVertex(a);
        PushVertex(b);
    } break;
    case V0_VIS | V2_VIS | V3_VIS:
    {
        //        --v3--
        //    v2--      --v0
        //      \        |
        //   .....B.....A...
        //          \   |
        //            v1
        // v3,v0,v2  v0,v2,A  v2,A,B
        ClipEdge(v0, v1, a);
        a->flags  = PVR_CMD_VERTEX;
        ClipEdge(v1, v2, b);
        b->flags  = PVR_CMD_VERTEX_EOL;
        v3->flags = PVR_CMD_VERTEX;

        PushVertex(v3);
        PushVertex(v0);
        PushVertex(v2);
        PushVertex(a);
        PushVertex(b);
    } break;
    case V1_VIS | V2_VIS | V3_VIS:
    {
        //        --v2--
        //    v1--      --v3
        //      \        |
        //   .....B.....A...
        //          \   |
        //            v0
        // v2,v3,v1  v3,v1,A  v1,A,B
        ClipEdge(v3, v0, a);
        a->flags  = PVR_CMD_VERTEX;
        ClipEdge(v0, v1, b);
        b->flags  = PVR_CMD_VERTEX_EOL;
        v3->flags = PVR_CMD_VERTEX;

        PushVertex(v2);
        PushVertex(v3);
        PushVertex(v1);
        PushVertex(a);
        PushVertex(b);
    } break;
    }
}

extern void ProcessVertexList(Vertex* v3, int n, void* sq_addr);

void SceneListSubmit(Vertex* v3, int n) {
	sq = (uint32_t*)MEM_AREA_SQ_BASE;

    for (int i = 0; i < n; i++, v3++) 
	{
        PREFETCH(v3 + 1);
        switch(v3->flags & 0xFF000000) {
        case PVR_CMD_VERTEX_EOL:
            break;
        case PVR_CMD_VERTEX:
            continue;
        default:
            PushCommand(v3);
            continue;
        };

		// Quads [0, 1, 2, 3] -> Triangles [{0, 1, 2}  {2, 3, 0}]
        Vertex* const v0 = v3 - 3;
        Vertex* const v1 = v3 - 2;
        Vertex* const v2 = v3 - 1;
        uint8_t visible_mask = v3->flags & 0xFF;

        switch(visible_mask) {
        case V0_VIS | V1_VIS | V2_VIS | V3_VIS: // All vertices visible
        {
            // Triangle strip: {1,2,0} {2,0,3}
            PushVertex(v1);
            PushVertex(v2);
            PushVertex(v0);
            PushVertex(v3);
        }
        break;
        
        default: // Some vertices visible
            SubmitClipped(v0, v1, v2, v3, visible_mask);
            break;
        }
    }
}
