// ─────────────────────────────────────────────────────────────────────────────
// Renderer.cpp  –  OpenGL 3.3 batch renderer implementation.
// ─────────────────────────────────────────────────────────────────────────────

#include "Renderer.h"

#include <juce_opengl/juce_opengl.h>
using namespace juce::gl;

#include <vector>
#include <cstring>
#include <stdexcept>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// Embedded GLSL shaders  (GLSL 330 core, compatible with GL 3.3+)
// ─────────────────────────────────────────────────────────────────────────────

static const char* kVoxelVert = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uVP;
uniform vec3 uModelOffset;

out vec3 vNormal;
out vec3 vWorldPos;

void main()
{
    vec3 worldPos = aPosition + uModelOffset;
    vNormal   = aNormal;
    vWorldPos = worldPos;
    gl_Position = uVP * vec4(worldPos, 1.0);
}
)glsl";

static const char* kVoxelFrag = R"glsl(
#version 330 core

in  vec3 vNormal;
in  vec3 vWorldPos;
out vec4 fragColor;

uniform vec3 uLightDir;  // normalised, toward the light
uniform vec3 uBaseColor;

void main()
{
    vec3 n = normalize(vNormal);

    // Simple hemisphere lighting: ambient + diffuse
    float diffuse = max(dot(n, normalize(uLightDir)), 0.0);
    float ambient = 0.30;
    float light   = ambient + (1.0 - ambient) * diffuse;

    // Subtle face-tinting to reinforce 3-D shape even without shadows
    float tint = 0.0;
    if (n.y >  0.5) tint =  0.12;   // top  face  – slightly brighter
    if (n.y < -0.5) tint = -0.08;   // bottom face – slightly darker

    vec3 color = clamp(uBaseColor + tint, 0.0, 1.0) * light;
    fragColor = vec4(color, 1.0);
}
)glsl";

// ── Unlit shader (grid lines and highlight wireframe) ─────────────────────────
static const char* kUnlitVert = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uVP;
uniform vec3 uOffset;   // world-space translation per draw call

void main()
{
    gl_Position = uVP * vec4(aPosition + uOffset, 1.0);
}
)glsl";

static const char* kUnlitFrag = R"glsl(
#version 330 core

out vec4 fragColor;
uniform vec3 uColor;

void main()
{
    fragColor = vec4(uColor, 1.0);
}
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
// Cube face geometry (for batch voxel mesh)
// Each face defined by 4 vertices in CCW order (viewed from outside).
// Vertex layout: px py pz  nx ny nz
// ─────────────────────────────────────────────────────────────────────────────

struct FaceVert { float px,py,pz, nx,ny,nz; };

static const FaceVert kFaces[6][4] =
{
    // FRONT  (z = 1)  normal  (0, 0, +1)
    { {0,0,1, 0,0,1}, {1,0,1, 0,0,1}, {1,1,1, 0,0,1}, {0,1,1, 0,0,1} },
    // BACK   (z = 0)  normal  (0, 0, -1)
    { {1,0,0, 0,0,-1}, {0,0,0, 0,0,-1}, {0,1,0, 0,0,-1}, {1,1,0, 0,0,-1} },
    // LEFT   (x = 0)  normal  (-1, 0, 0)
    { {0,0,0, -1,0,0}, {0,0,1, -1,0,0}, {0,1,1, -1,0,0}, {0,1,0, -1,0,0} },
    // RIGHT  (x = 1)  normal  (+1, 0, 0)
    { {1,0,1, 1,0,0}, {1,0,0, 1,0,0}, {1,1,0, 1,0,0}, {1,1,1, 1,0,0} },
    // BOTTOM (y = 0)  normal  (0, -1, 0)
    { {0,0,0, 0,-1,0}, {1,0,0, 0,-1,0}, {1,0,1, 0,-1,0}, {0,0,1, 0,-1,0} },
    // TOP    (y = 1)  normal  (0, +1, 0)
    { {0,1,1, 0,1,0}, {1,1,1, 0,1,0}, {1,1,0, 0,1,0}, {0,1,0, 0,1,0} },
};

// Quad → two triangles (CCW): indices 0,1,2 and 0,2,3
static const int kQuadIdx[6] = { 0, 1, 2,  0, 2, 3 };

// ─────────────────────────────────────────────────────────────────────────────
// 12 edges of a unit wireframe cube  (24 endpoint positions, 3 floats each)
// ─────────────────────────────────────────────────────────────────────────────

static const float kWireVerts[24 * 3] =
{
    // Bottom face
    0,0,0, 1,0,0,   1,0,0, 1,0,1,   1,0,1, 0,0,1,   0,0,1, 0,0,0,
    // Top face
    0,1,0, 1,1,0,   1,1,0, 1,1,1,   1,1,1, 0,1,1,   0,1,1, 0,1,0,
    // Verticals
    0,0,0, 0,1,0,   1,0,0, 1,1,0,   1,0,1, 1,1,1,   0,0,1, 0,1,1,
};

// ─────────────────────────────────────────────────────────────────────────────
// Shader helpers
// ─────────────────────────────────────────────────────────────────────────────

GLuint Renderer::compileShader(unsigned int type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        // Silently delete; caller gets 0 from linkProgram
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint Renderer::linkProgram(GLuint vert, GLuint frag)
{
    if (!vert || !frag) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ─────────────────────────────────────────────────────────────────────────────
// init / shutdown
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::init()
{
    // ── Compile shaders ───────────────────────────────────────────────────────

    progVoxels = linkProgram(
        compileShader(GL_VERTEX_SHADER,   kVoxelVert),
        compileShader(GL_FRAGMENT_SHADER, kVoxelFrag));

    progUnlit = linkProgram(
        compileShader(GL_VERTEX_SHADER,   kUnlitVert),
        compileShader(GL_FRAGMENT_SHADER, kUnlitFrag));

    if (progVoxels)
    {
        uVP_vox         = glGetUniformLocation(progVoxels, "uVP");
        uLight          = glGetUniformLocation(progVoxels, "uLightDir");
        uColor_v        = glGetUniformLocation(progVoxels, "uBaseColor");
        uModelOffset_vox = glGetUniformLocation(progVoxels, "uModelOffset");
    }

    if (progUnlit)
    {
        uVP_unlit    = glGetUniformLocation(progUnlit, "uVP");
        uColor_unlit = glGetUniformLocation(progUnlit, "uColor");
        uOffset      = glGetUniformLocation(progUnlit, "uOffset");
    }

    // ── Allocate VAO/VBO for voxels (dynamic, rebuilt on demand) ─────────────

    glGenVertexArrays(1, &vaoVoxels);
    glGenBuffers     (1, &vboVoxels);

    glBindVertexArray(vaoVoxels);
    glBindBuffer(GL_ARRAY_BUFFER, vboVoxels);
    // Vertex layout: position (3f) + normal (3f) = 24 bytes stride
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ── Build static meshes ───────────────────────────────────────────────────

    buildGridMesh(40);
    buildWireframeCube();
    buildOriginCube();
}

void Renderer::shutdown()
{
    auto del = [](GLuint& h) { if (h) { glDeleteBuffers(1, &h); h = 0; } };
    auto delV = [](GLuint& h) { if (h) { glDeleteVertexArrays(1, &h); h = 0; } };
    auto delP = [](GLuint& h) { if (h) { glDeleteProgram(h); h = 0; } };

    delV(vaoVoxels); del(vboVoxels);
    delV(vaoGrid);   del(vboGrid);
    delV(vaoWire);   del(vboWire);
    delV(vaoCube);   del(vboCube);
    delP(progVoxels);
    delP(progUnlit);
}

// ─────────────────────────────────────────────────────────────────────────────
// buildGridMesh  –  horizontal line grid at y = 0
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::buildGridMesh(int halfSize)
{
    // X-parallel lines and Z-parallel lines from -halfSize to +halfSize
    std::vector<float> verts;
    verts.reserve((halfSize * 2 + 1) * 4 * 3);

    for (int i = -halfSize; i <= halfSize; ++i)
    {
        float fi = static_cast<float>(i);
        float fH = static_cast<float>(halfSize);

        // X-parallel line
        verts.insert(verts.end(), { -fH, 0.f, fi });
        verts.insert(verts.end(), {  fH, 0.f, fi });

        // Z-parallel line
        verts.insert(verts.end(), { fi, 0.f, -fH });
        verts.insert(verts.end(), { fi, 0.f,  fH });
    }

    gridVertCount = static_cast<int>(verts.size() / 3);

    glGenVertexArrays(1, &vaoGrid);
    glGenBuffers     (1, &vboGrid);

    glBindVertexArray(vaoGrid);
    glBindBuffer(GL_ARRAY_BUFFER, vboGrid);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// buildWireframeCube  –  12 edges of a unit cube at origin
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::buildWireframeCube()
{
    wireVertCount = 24;   // 12 edges × 2 endpoints

    glGenVertexArrays(1, &vaoWire);
    glGenBuffers     (1, &vboWire);

    glBindVertexArray(vaoWire);
    glBindBuffer(GL_ARRAY_BUFFER, vboWire);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sizeof(kWireVerts)),
                 kWireVerts, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuildVoxelMesh  –  collapse all voxels into a single VBO
//
// Vertex format : position (3f) + normal (3f) = 6 floats
// Per voxel     : 6 faces × 6 verts = 36 vertices × 6 floats = 216 floats
//
// Future optimisation: skip faces shared with adjacent voxels (face culling).
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::rebuildVoxelMesh(const VoxelGrid& grid)
{
    std::vector<float> verts;
    verts.reserve(grid.size() * 36 * 6);

    for (const Vec3i& vox : grid.getVoxels())
    {
        const float ox = static_cast<float>(vox.x);
        const float oy = static_cast<float>(vox.y);
        const float oz = static_cast<float>(vox.z);

        for (int face = 0; face < 6; ++face)
        {
            const FaceVert* fv = kFaces[face];
            for (int tri = 0; tri < 6; ++tri)
            {
                const FaceVert& v = fv[kQuadIdx[tri]];
                verts.push_back(v.px + ox);
                verts.push_back(v.py + oy);
                verts.push_back(v.pz + oz);
                verts.push_back(v.nx);
                verts.push_back(v.ny);
                verts.push_back(v.nz);
            }
        }
    }

    voxelVertCount = static_cast<int>(verts.size() / 6);

    glBindVertexArray(vaoVoxels);
    glBindBuffer(GL_ARRAY_BUFFER, vboVoxels);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.empty() ? nullptr : verts.data(),
                 GL_DYNAMIC_DRAW);

    // Re-assert attrib pointers after glBufferData (driver may re-map)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    meshDirty = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildOriginCube  –  single solid cube at (0,0,0) for the origin marker
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::buildOriginCube()
{
    std::vector<float> verts;
    verts.reserve(36 * 6);

    for (int face = 0; face < 6; ++face)
    {
        const FaceVert* fv = kFaces[face];
        for (int tri = 0; tri < 6; ++tri)
        {
            const FaceVert& v = fv[kQuadIdx[tri]];
            verts.push_back(v.px);
            verts.push_back(v.py);
            verts.push_back(v.pz);
            verts.push_back(v.nx);
            verts.push_back(v.ny);
            verts.push_back(v.nz);
        }
    }

    cubeVertCount = static_cast<int>(verts.size() / 6);

    glGenVertexArrays(1, &vaoCube);
    glGenBuffers     (1, &vboCube);

    glBindVertexArray(vaoCube);
    glBindBuffer(GL_ARRAY_BUFFER, vboCube);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderOriginMarker  –  permanent red cube at (0,0,0)
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::renderOriginMarker(const Mat4& vp, const Vec3f& lightDir)
{
    if (!progVoxels || cubeVertCount == 0) return;

    glUseProgram(progVoxels);
    glUniformMatrix4fv(uVP_vox, 1, GL_FALSE, vp.m);
    glUniform3f(uLight,   lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(uColor_v, 1.00f, 0.55f, 0.10f);   // orange
    glUniform3f(uModelOffset_vox, 0.f, 0.f, 0.f);

    glBindVertexArray(vaoCube);
    glDrawArrays(GL_TRIANGLES, 0, cubeVertCount);
    glBindVertexArray(0);
}


// ─────────────────────────────────────────────────────────────────────────────

void Renderer::render(const Mat4& vp, const Vec3f& lightDir)
{
    if (!progVoxels || voxelVertCount == 0) return;

    glUseProgram(progVoxels);
    glUniformMatrix4fv(uVP_vox, 1, GL_FALSE, vp.m);
    glUniform3f(uLight,   lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(uColor_v, 0.38f, 0.62f, 0.92f);
    glUniform3f(uModelOffset_vox, 0.f, 0.f, 0.f);

    glBindVertexArray(vaoVoxels);
    glDrawArrays(GL_TRIANGLES, 0, voxelVertCount);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderSolidBlock — one colored cube at an arbitrary grid position
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::renderSolidBlock(const Mat4& vp, const Vec3f& lightDir,
                                const Vec3i& pos, const Vec3f& color)
{
    if (!progVoxels || cubeVertCount == 0) return;

    glUseProgram(progVoxels);
    glUniformMatrix4fv(uVP_vox, 1, GL_FALSE, vp.m);
    glUniform3f(uLight,   lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(uColor_v, color.x, color.y, color.z);
    glUniform3f(uModelOffset_vox,
                static_cast<float>(pos.x),
                static_cast<float>(pos.y),
                static_cast<float>(pos.z));

    glBindVertexArray(vaoCube);
    glDrawArrays(GL_TRIANGLES, 0, cubeVertCount);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderGrid  –  horizontal reference grid at y = 0
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::renderGrid(const Mat4& vp)
{
    if (!progUnlit || gridVertCount == 0) return;

    glUseProgram(progUnlit);
    glUniformMatrix4fv(uVP_unlit, 1, GL_FALSE, vp.m);
    glUniform3f(uColor_unlit, 0.30f, 0.30f, 0.35f);   // subtle grey
    glUniform3f(uOffset, 0.f, 0.f, 0.f);

    glBindVertexArray(vaoGrid);
    glDrawArrays(GL_LINES, 0, gridVertCount);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderHighlight  –  wireframe outline at a given grid position
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::renderHighlight(const Mat4& vp, const Vec3i& pos,
                                const Vec3f& color)
{
    if (!progUnlit || wireVertCount == 0) return;

    glUseProgram(progUnlit);
    glUniformMatrix4fv(uVP_unlit, 1, GL_FALSE, vp.m);
    glUniform3f(uColor_unlit, color.x, color.y, color.z);
    glUniform3f(uOffset,
                static_cast<float>(pos.x),
                static_cast<float>(pos.y),
                static_cast<float>(pos.z));

    // Draw slightly larger to avoid Z-fighting with solid faces
    glLineWidth(1.5f);
    glBindVertexArray(vaoWire);
    glDrawArrays(GL_LINES, 0, wireVertCount);
    glBindVertexArray(0);
    glLineWidth(1.f);
}
